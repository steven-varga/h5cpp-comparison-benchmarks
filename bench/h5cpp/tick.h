#pragma once

#include <cstdint>

struct tick_t {
    uint64_t time;
    float    price;
    uint32_t size;
    uint16_t contract_id;
    uint16_t flags;
};
