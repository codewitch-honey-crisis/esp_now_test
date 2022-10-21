// remote.hpp
// main file for remote firmware
#pragma once
#include <Arduino.h>
#include <SPIFFS.h>
#include <comms.hpp>


comms comm_link;

void setup() {
    Serial.begin(115200);
    SPIFFS.begin();
    if(!comm_link.initialize()) {
        Serial.println("comm link initialization failure.");
        while(1);
    }
    uint8_t mac[6];
    comm_link.mac_address(mac);
    Serial.printf("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    comm_link.control_address(mac);
    Serial.printf("Control MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    comm_link.update();
    
}
void loop() {
}