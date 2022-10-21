// comms.hpp
// controls network communication
#pragma once
#include <stdint.h>
class comms final {
public:
    bool initialize();
    void update() const;
};