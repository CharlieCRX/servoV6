#ifndef REGISTER_BLOCK_H
#define REGISTER_BLOCK_H

#include <vector>
#include <cstdint>

struct RegisterBlock {
    std::vector<uint16_t> data;
};

#endif // REGISTER_BLOCK_H
