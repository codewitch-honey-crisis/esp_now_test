// comms.hpp
// controls network communication
#pragma once
#include <stdint.h>
class comms {
#ifdef REMOTE    
    uint8_t m_control_mac[6];
#endif
public:
    bool initialize(
#ifdef REMOTE
        bool rescan = true
#endif
        );
#ifdef REMOTE
    void control_address(uint8_t* mac) const;
    void update() const;
#endif
    void mac_address(uint8_t* mac) const;
};