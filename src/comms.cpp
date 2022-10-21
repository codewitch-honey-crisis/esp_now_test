#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_now.h>
#include <string.h>

#include <comms.hpp>
#include <htcw_data.hpp>

#include "esp_mac.h"
using namespace data;

// max size is 250 bytes!
typedef struct remote_request {
    int data;
} remote_request_t;

static_assert(sizeof(remote_request_t) <= 250, "remote request is too large");

// max size is 250 bytes!
typedef struct control_response {
    float data1;
    int data2;
    char data3[32];
} control_response_t;

static_assert(sizeof(control_response_t) <= 250, "control_response is too large");
#ifdef REMOTE
static void send_request(const uint8_t* address) {
    Serial.println("Sending...");
    remote_request_t req;
    req.data = 1;
    esp_now_send(address, (const uint8_t*)&req, sizeof(remote_request_t));
}

static void received_response(const control_response_t& resp) {
    Serial.println("Response received:");
    Serial.print("\tdata1: ");
    Serial.println(resp.data1);
    Serial.print("\tdata2: ");
    Serial.println(resp.data2);
    Serial.print("\tdata3: ");
    Serial.println(resp.data3);
    Serial.println();

    ESP.deepSleep(1 * 1000000);
}
#endif
#ifdef CONTROL
static void received_request(const uint8_t* address, const remote_request_t& req) {
    Serial.println("Received request: ");
    Serial.print(req.data);
    Serial.println();
    control_response_t resp;
    resp.data1 = 1;
    resp.data2 = 2.3;
    strcpy(resp.data3, "hello");
    if (ESP_OK != esp_now_send(address, (const uint8_t*)&resp, sizeof(resp))) {
        Serial.println("ESP-NOW failed to send response");
    }
}
#endif

#ifdef CONTROL
typedef struct mac_key {
    uint8_t address[6];
    bool operator==(const mac_key& rhs) const {
        return 0 == memcmp((void*)address, (void*)rhs.address, 6);
    }
} mac_key_t;

static int address_hash(const mac_key& key) {
    int result = 0;
    for (int i = 0; i < 6; ++i) {
        result <<= 8;
        result |= key.address[i];
    }
    return result;
}
static int next_channel = 0;
static simple_fixed_map<mac_key_t, uint8_t, 4> addresses(address_hash);
#endif


// Check if the slave is already paired with the master.
// If not, pair the slave with master
bool manageSlave() {
  if (slave.channel == 0) {
    Serial.print("Slave Status: ");
    // check if the peer exists
    bool exists = esp_now_is_peer_exist(slave.peer_addr);
    if ( exists) {
      // Slave already paired.
      Serial.println("Already Paired");
      return true;
    } else {
      // Slave not paired, attempt pair
      esp_err_t addStatus = esp_now_add_peer(&slave);
      if (addStatus == ESP_OK) {
        // Pair success
        Serial.println("Pair success");
        return true;
      } else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT) {
        // How did we get so far!!
        Serial.println("ESPNOW Not Init");
        return false;
      } else if (addStatus == ESP_ERR_ESPNOW_ARG) {
        Serial.println("Invalid Argument");
        return false;
      } else if (addStatus == ESP_ERR_ESPNOW_FULL) {
        Serial.println("Peer list full");
        return false;
      } else if (addStatus == ESP_ERR_ESPNOW_NO_MEM) {
        Serial.println("Out of memory");
        return false;
      } else if (addStatus == ESP_ERR_ESPNOW_EXIST) {
        Serial.println("Peer Exists");
        return true;
      } else {
        Serial.println("Not sure what happened");
        return false;
      }
    }
  } else {
    // No slave found to process
    Serial.println("No Slave found to process");
    return false;
  }
}

void deletePeer() {
  esp_err_t delStatus = esp_now_del_peer(slave.peer_addr);
  Serial.print("Slave Delete Status: ");
  if (delStatus == ESP_OK) {
    // Delete success
    Serial.println("Success");
  } else if (delStatus == ESP_ERR_ESPNOW_NOT_INIT) {
    // How did we get so far!!
    Serial.println("ESPNOW Not Init");
  } else if (delStatus == ESP_ERR_ESPNOW_ARG) {
    Serial.println("Invalid Argument");
  } else if (delStatus == ESP_ERR_ESPNOW_NOT_FOUND) {
    Serial.println("Peer not found.");
  } else {
    Serial.println("Not sure what happened");
  }
}
static void on_data_receive(const uint8_t* mac, const uint8_t* incomingData, int len) {
#ifdef CONTROL
    if (len != sizeof(remote_request_t)) {
        Serial.println("ESP-NOW request unrecognized");
        return;
    }
    mac_key_t key;
    memcpy(key.address, mac, 6);
    const uint8_t* ch = addresses.find(key);
    if (NULL == ch) {
        if (next_channel == ESP_NOW_MAX_TOTAL_PEER_NUM) {
            Serial.println("ESP-NOW Max peers connected");
            return;
        }
        esp_now_peer_info_t peer;
        memset(&peer, 0, sizeof(esp_now_peer_info_t));
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = next_channel++;
        peer.encrypt = false;
        if (ESP_OK != esp_now_add_peer(&peer)) {
            Serial.println("ESP-NOW peer unable to be registered");
            return;
        }
        using p_t = simple_pair<const mac_key_t, uint8_t>;
        if (!addresses.insert(p_t(key, peer.channel))) {
            Serial.println("ESP-NOW out of memory in peer table");
            return;
        }
    }
    remote_request_t req;
    memcpy(&req, incomingData, sizeof(remote_request_t));
    received_request(mac, req);
#endif
#ifdef REMOTE
    control_response_t resp;
    if (len == sizeof(resp)) {
        memcpy(&resp, incomingData, sizeof(resp));
        received_response(resp);
    } else {
        Serial.println("ESP-NOW response not recognized");
    }

#endif
}
static void on_data_sent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if(status != ESP_NOW_SEND_SUCCESS) {
        ESP.restart();
    }
}

// Scan for slaves in AP mode
static bool scan_for_control(uint8_t* address) {
    int8_t scanResults = WiFi.scanNetworks();
    if (scanResults == 0) {
        Serial.println("ESP-NOW: No control units found in range.");
        // clean up ram
        WiFi.scanDelete();
        return false;
    } else {
        String BSSIDstr;
        //int32_t max_rssi = -(2 ^ 32 - 1);
        //int max_index = -1;
        int units = 0;
        int i;
        for (i = 0; i < scanResults; ++i) {
            // Print SSID and RSSI for each device found
            String SSID = WiFi.SSID(i);
            //int32_t RSSI = WiFi.RSSI(i);
            if (SSID.equals("ZambaControlUnit")) {
                Serial.println("Zamba Control Unit found");
                if (++units == 2) {
                    Serial.println("ESP-NOW: Warning: Multiple control units found in range. Choosing the first found.");
                }
                /*if (RSSI >= max_rssi) {
                    max_rssi = RSSI;
                    max_index = i;
                }*/
                BSSIDstr = WiFi.BSSIDstr(i);
                break;
            } else {
                Serial.print("Non-Zamba unit ");
                Serial.print(SSID);
                Serial.println(" found. Skipping.");
            }
        }
        
        int mac[6];
        sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x%*c", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
        uint8_t* pa = address;
        for (int ii = 0; ii < 6; ++ii) {
            *pa++ = (uint8_t)mac[ii];
        }
        Serial.printf("Retrieved BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n", address[0], address[1], address[2], address[3], address[4], address[5]);
        // clean up ram
        WiFi.scanDelete();
        return true;
    }
    // clean up ram
    WiFi.scanDelete();
    return false;

}


bool comms::initialize(
#ifdef REMOTE
    bool rescan
#endif
) {
    WiFi.mode(WIFI_AP);
#ifdef CONTROL
    const char* SSID = "ZambaControlUnit";
    // the password doesn't secure anything so it's not important
    bool result = WiFi.softAP(SSID, "ZambaControlUnit01", 1, 0);
    if (!result) {
        Serial.println("ESP-NOW: AP Config failed.");
        return false;
    } else {
        Serial.print("AP Config Success. Broadcasting with: ");
        Serial.println(SSID);
        WiFi.disconnect();
    }
#endif
    if (ESP_OK != esp_now_init()) {
        Serial.println("ESP-NOW initialization failure.");
        return false;
    }
    if (ESP_OK != esp_now_register_send_cb(on_data_sent)) {
        Serial.println("ESP-NOW send callback registration failure.");
        return false;
    }
    if (ESP_OK != esp_now_register_recv_cb(on_data_receive)) {
        Serial.println("ESP-NOW receive callback registration failure.");
        return false;
    }
#ifdef REMOTE
    if (!rescan) {
        char sz[18];
        File file = SPIFFS.open("/control.mac", "r");
        if (!file || file.readBytes(sz, 18) < 17) {
            rescan = true;
        } else {
            sz[17] = '\0';
            int mac[6];
            sscanf(sz, "%x:%x:%x:%x:%x:%x%*c", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
            for (int i = 0; i < 6; ++i) {
                m_control_mac[i] = (uint8_t)mac[i];
            }
        }
    }
    if (rescan) {
        Serial.println("Scanning local area for control unit(s)");
        uint8_t mac[6];
        while (!scan_for_control(mac)) {
            delay(100);
        }
        Serial.println();
        if (SPIFFS.exists("/control.mac")) {
            SPIFFS.remove("/control.mac");
        }
        File file = SPIFFS.open("/control.mac", "w");
        file.printf("%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        file.close();
        memcpy(m_control_mac, mac, 6);
    }
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, m_control_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (ESP_OK != esp_now_add_peer(&peer)) {
        Serial.println("ESP-NOW failed to add control as peer");
        return false;
    }
#endif
    return true;
}
#ifdef REMOTE
void comms::control_address(uint8_t* mac) const {
    memcpy(mac, m_control_mac, 6);
}

void comms::update() const {
    send_request(m_control_mac);
}
#endif
void comms::mac_address(uint8_t* mac) const {
    int a[6];
    sscanf(WiFi.macAddress().c_str(), "%x:%x:%x:%x:%x:%x%*c", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
    for (int i = 0; i < 6; ++i) {
        *mac++ = (uint8_t)a[i];
    }

    //    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
}
