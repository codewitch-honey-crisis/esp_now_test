#include <WiFi.h>
#include <esp_now.h>

#include <comms.hpp>
#define CHANNEL 1
// Init ESP Now with fallback
static void initialize_esp_now() {
    WiFi.disconnect();
    if (esp_now_init() == ESP_OK) {
        return;
    } else {
        Serial.println("ESP-NOW: Initialization failed");
        ESP.restart();
    }
}
#ifdef CONTROL
static void send_response(const uint8_t* mac_addr) {
    uint8_t data = 1;
    esp_err_t result = esp_now_send(mac_addr, &data, sizeof(data));
    if (result == ESP_OK) {
        return;
    } else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
        // How did we get so far!!
        Serial.println("ESP-NOW: Not initialized");
    } else if (result == ESP_ERR_ESPNOW_ARG) {
        Serial.println("ESP-NOW: Invalid argument");
    } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
        Serial.println("ESP-NOW: Internal error");
    } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
        Serial.println("ESP-NOW: Out of memory");
    } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
        Serial.println("ESP-NOW: Peer not found");
    } else {
        Serial.print("ESP-NOW: Unknown error ");
        Serial.println(result);
    }
}
// config AP SSID
static void configure_device_AP() {
    const char *SSID = "ZambaControlUnit";
    bool result = WiFi.softAP(SSID, "UnusedPassword", CHANNEL, 0);
    if (!result) {
        Serial.println("ESP-NOW: AP Config failed.");
    } else {
        Serial.println("Broadcasting with AP: " + String(SSID));
    }
}

// callback when data is sent from Master to Slave
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.print("Last Packet Sent to: ");
    Serial.println(macStr);
    Serial.print("Last Packet Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
    
}

// callback when data is recv from Master
static void on_data_received(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.print("Last Packet Recv from: ");
    Serial.println(macStr);
    Serial.print("Last Packet Recv Data: ");
    Serial.println(*data);
    Serial.println("");
     // check if the peer exists
    bool exists = esp_now_is_peer_exist(mac_addr);
    if(!exists) {
        esp_now_peer_info_t peer;
        memset(&peer,0,sizeof(peer));
        memcpy(peer.peer_addr,mac_addr,6);
        peer.channel = CHANNEL;
        peer.encrypt = false;
        // Slave not paired, attempt pair
        esp_err_t addStatus = esp_now_add_peer(&peer);
        
        if (addStatus == ESP_ERR_ESPNOW_NOT_INIT) {
            // How did we get so far!!
            Serial.println("ESP-NOW: Not initialized");
            return;
        } else if (addStatus == ESP_ERR_ESPNOW_ARG) {
            Serial.println("ESP-NOW: Invalid argument");
            return;
        } else if (addStatus == ESP_ERR_ESPNOW_FULL) {
            Serial.println("ESP-NOW: Peer list full");
            return;
        } else if (addStatus == ESP_ERR_ESPNOW_NO_MEM) {
            Serial.println("ESP-NOW: Out of memory");
            return;
        } else if (addStatus == ESP_ERR_ESPNOW_EXIST) {
            return;
        } else if(ESP_OK!=addStatus) {
            Serial.print("ESP-NOW: Unknown error: ");
            Serial.println(addStatus);
            return;
        }
    }
    send_response(mac_addr);
}
static void initialize_impl() {
    // Set device in AP mode to begin with
    WiFi.mode(WIFI_STA);
    // configure device AP mode
    configure_device_AP();
    // This is the mac address of the Slave in AP Mode
    Serial.print("AP MAC: ");
    Serial.println(WiFi.softAPmacAddress());
    // Init ESPNow with a fallback logic
    initialize_esp_now();
    // Once ESPNow is successfully Init, we will register for recv CB to
    // get recv packer info.
    esp_now_register_send_cb(on_data_sent);
    esp_now_register_recv_cb(on_data_received);
}

#endif
#ifdef REMOTE
// Global copy of slave
static esp_now_peer_info_t control_info;

#define PRINTSCANRESULTS 1
#define DELETEBEFOREPAIR 0

// Scan for slaves in AP mode
static void scan_for_control() {
    int8_t scanResults = WiFi.scanNetworks();
    memset(&control_info, 0, sizeof(control_info));

    Serial.println("Scanning");
    if (scanResults == 0) {
        Serial.println("No WiFi devices in AP Mode found");
    } else {
        for (int i = 0; i < scanResults; ++i) {
            // Print SSID and RSSI for each device found
            String SSID = WiFi.SSID(i);
            int32_t RSSI = WiFi.RSSI(i);
            String BSSIDstr = WiFi.BSSIDstr(i);

            if (PRINTSCANRESULTS) {
                Serial.print(i + 1);
                Serial.print(": ");
                Serial.print(SSID);
                Serial.print(" (");
                Serial.print(RSSI);
                Serial.print(")");
                Serial.println("");
            }
            delay(10);
            // Check if the current device starts with `Slave`
            if (SSID.indexOf("ZambaControlUnit") == 0) {
                // SSID of interest
                // Get BSSID => Mac Address of the Slave
                int mac[6];
                if (6 == sscanf(BSSIDstr.c_str(), "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5])) {
                    for (int ii = 0; ii < 6; ++ii) {
                        control_info.peer_addr[ii] = (uint8_t)mac[ii];
                    }
                }

                control_info.channel = CHANNEL;  // pick a channel
                control_info.encrypt = 0;        // no encryption

                // we are planning to have only one slave in this example;
                // Hence, break after we find one, to be a bit efficient
                break;
            }
        }
    }

    // clean up ram
    WiFi.scanDelete();
}

static void delete_control() {
    esp_err_t delStatus = esp_now_del_peer(control_info.peer_addr);
    if (delStatus == ESP_OK) {
        return;
    } else if (delStatus == ESP_ERR_ESPNOW_NOT_INIT) {
        // How did we get so far!!
        Serial.println("ESP-NOW: Not initialized");
    } else if (delStatus == ESP_ERR_ESPNOW_ARG) {
        Serial.println("ESP-NOW: Invalid argument");
    } else if (delStatus == ESP_ERR_ESPNOW_NOT_FOUND) {
        Serial.println("ESP-NOW: Peer not found");
    } else {
        Serial.println("ESP-NOW: Unknown error");
    }
}

// Check if the slave is already paired with the master.
// If not, pair the slave with master
static bool connect_control() {
    if (control_info.channel == CHANNEL) {
        if (DELETEBEFOREPAIR) {
            delete_control();
        }

        // check if the peer exists
        bool exists = esp_now_is_peer_exist(control_info.peer_addr);
        if (exists) {
            return true;
        } else {
            // Slave not paired, attempt pair
            esp_err_t addStatus = esp_now_add_peer(&control_info);
            if (addStatus == ESP_OK) {
                return true;
            } else if (addStatus == ESP_ERR_ESPNOW_NOT_INIT) {
                // How did we get so far!!
                Serial.println("ESP-NOW: Not initialized");
                return false;
            } else if (addStatus == ESP_ERR_ESPNOW_ARG) {
                Serial.println("ESP-NOW: Invalid argument");
                return false;
            } else if (addStatus == ESP_ERR_ESPNOW_FULL) {
                Serial.println("ESP-NOW: Peer list full");
                return false;
            } else if (addStatus == ESP_ERR_ESPNOW_NO_MEM) {
                Serial.println("ESP-NOW: Out of memory");
                return false;
            } else if (addStatus == ESP_ERR_ESPNOW_EXIST) {
                return true;
            } else {
                Serial.println("ESP-NOW: Unknown error");
                return false;
            }
        }
    } else {
        return false;
    }
}

static uint8_t data = 0;
// send data
static void send_request() {
    data++;
    const uint8_t *peer_addr = control_info.peer_addr;
    esp_err_t result = esp_now_send(peer_addr, &data, sizeof(data));
    if (result == ESP_OK) {
        return;
    } else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
        // How did we get so far!!
        Serial.println("ESP-NOW: Not initialized");
    } else if (result == ESP_ERR_ESPNOW_ARG) {
        Serial.println("ESP-NOW: Invalid argument");
    } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
        Serial.println("ESP-NOW: Internal error");
    } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
        Serial.println("ESP-NOW: Out of memory");
    } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
        Serial.println("ESP-NOW: Peer not found");
    } else {
        Serial.println("ESP-NOW: Unknown error");
    }
}

// callback when data is sent from Master to Slave
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.print("Last Packet Sent to: ");
    Serial.println(macStr);
    Serial.print("Last Packet Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}
// callback when data is recv from Master
static void on_data_received(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.print("Last Packet Recv from: ");
    Serial.println(macStr);
    Serial.print("Last Packet Recv Data: ");
    Serial.println(*data);
    Serial.println("");

    ESP.deepSleep(1000000 * 5);
}

static void initialize_impl() {
    // Set device in STA mode to begin with
    WiFi.mode(WIFI_STA);
    // This is the mac address of the Master in Station Mode
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    // Init ESPNow with a fallback logic
    initialize_esp_now();
    // Once ESPNow is successfully Init, we will register for Send CB to
    // get the status of Trasnmitted packet
    esp_now_register_send_cb(on_data_sent);
    esp_now_register_recv_cb(on_data_received);
}

static void remote_connect_control() {
    // In the loop we scan for slave
    scan_for_control();
    // If Slave is found, it would be populate in `slave` variable
    // We will check if `slave` is defined and then we proceed further
    if (control_info.channel == CHANNEL) {  // check if slave channel is defined
        // `slave` is defined
        // Add slave as peer if it has not been added already
        bool isPaired = connect_control();
        if (isPaired) {
            // pair success or already paired
            // Send data to device
            send_request();
        } else {
            // slave pair failed
            Serial.println("Slave pair failed!");
        }
    }
}
#endif

bool comms::initialize() {
    initialize_impl();
    return true;
}
void comms::update() const {
#ifdef REMOTE
    remote_connect_control();
#endif
#ifdef CONTROL

#endif
}