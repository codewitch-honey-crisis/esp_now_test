#include <comms.hpp>
#include <Arduino.h>
#include <SPIFFS.h>
#include <string.h>
#include "esp_mac.h"
#include <esp_now.h>
#include <WiFi.h>
#include <htcw_data.hpp>
using namespace data;

// max size is 250 bytes!
typedef struct remote_request {
    int data;
} remote_request_t;

static_assert(sizeof(remote_request_t)<=250,"remote request is too large");

// max size is 250 bytes!
typedef struct control_response {
    float data1;
    int data2;
    char data3[32];
} control_response_t;

static_assert(sizeof(control_response_t)<=250,"control_response is too large");
#ifdef REMOTE
static void send_request(const uint8_t* address) {
    Serial.println("Sending...");
    remote_request_t req;
    req.data = 1;
    esp_now_send(address,(const uint8_t*)&req,sizeof(remote_request_t));
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
    strcpy(resp.data3,"hello");
    if(ESP_OK!=esp_now_send(address,(const uint8_t*)&resp,sizeof(resp))) {
        Serial.println("ESP-NOW failed to send response");
    }
}
#endif

#ifdef CONTROL
typedef struct mac_key {
    uint8_t address[6];
    bool operator==(const mac_key& rhs) const {
        return 0==memcmp((void*)address,(void*)rhs.address,6);
    }
} mac_key_t;

static int address_hash(const mac_key& key) {
    int result = 0;
    for(int i = 0;i<6;++i) {
        result <<= 8;
        result |= key.address[i];
    }
    return result;
}
static int next_channel=0;
static simple_fixed_map<mac_key_t,uint8_t,4> addresses(address_hash);
#endif

static void on_data_receive(const uint8_t * mac, const uint8_t *incomingData, int len) {
#ifdef CONTROL
    if(len!=sizeof(remote_request_t)) {
        Serial.println("ESP-NOW request unrecognized");
        return;
    }
    mac_key_t key;
    memcpy(key.address,mac,6);
    const uint8_t* ch=addresses.find(key);
    if(NULL==ch) {
        if(next_channel==ESP_NOW_MAX_TOTAL_PEER_NUM) {
            Serial.println("ESP-NOW Max peers connected");
            return;
        }
        esp_now_peer_info_t peer;
        memset(&peer,0,sizeof(esp_now_peer_info_t));
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = next_channel++;
        peer.encrypt = false;
        if(ESP_OK!=esp_now_add_peer(&peer)) {
            Serial.println("ESP-NOW peer unable to be registered");
            return;
        }
        using p_t = simple_pair<const mac_key_t,uint8_t>;
        if(!addresses.insert(p_t(key,peer.channel))) {
            Serial.println("ESP-NOW out of memory in peer table");
            return;
        }
    }
    remote_request_t req;
    memcpy(&req,incomingData,sizeof(remote_request_t));
    received_request(mac,req);
#endif
#ifdef REMOTE
    control_response_t resp;
    if(len==sizeof(resp)) {
        memcpy(&resp,incomingData,sizeof(resp));
        received_response(resp);
    } else {
        Serial.println("ESP-NOW response not recognized");
    }

#endif
}
static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}
bool comms::initialize() {
    WiFi.mode(WIFI_AP);
    if(ESP_OK!=esp_now_init()) {
        Serial.println("ESP-NOW initialization failure.");
        return false;
    }
    if(ESP_OK!=esp_now_register_send_cb(on_data_sent)) {
        Serial.println("ESP-NOW send callback registration failure.");
        return false;
    }
    if(ESP_OK!=esp_now_register_recv_cb(on_data_receive)) {
        Serial.println("ESP-NOW receive callback registration failure.");
        return false;
    }
#ifdef REMOTE
    char sz[18];
    File file = SPIFFS.open("/control.mac","r");
    if(!file || file.readBytes(sz,18)<17) {
        Serial.println("Could not read control.mac");
        return false;
    }
    sz[17]='\0';
    int mac[6];
    sscanf(sz,"%x:%x:%x:%x:%x:%x%*c",&mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]);
    for(int i = 0;i < 6; ++i) {
        m_control_mac[i]=(uint8_t)mac[i];
    }
    esp_now_peer_info_t peer;
    memset(&peer,0,sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, m_control_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (ESP_OK!=esp_now_add_peer(&peer)){
        Serial.println("ESP-NOW failed to add control as peer");
        return false;
    }
#endif
    return true;
}
#ifdef REMOTE
void comms::control_address(uint8_t* mac) const {
    memcpy(mac,m_control_mac,6);
}

void comms::update() const {
    send_request(m_control_mac);
}
#endif
void comms::mac_address(uint8_t* mac) const {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}
