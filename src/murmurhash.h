#pragma once 

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define mmix(h,k) { k *= m; k ^= k >> r; k *= m; h *= m; h ^= k; }

uint32_t MurmurHash2A(const uint8_t* data, size_t len, uint32_t seed)
{
    const uint32_t m = 0x5bd1e995;
    const uint32_t r = 24;

    uint32_t l = len;
    uint32_t h = seed;
 
    while(len >= 4)
    {
        uint32_t k;
        memcpy(&k, data, sizeof(uint32_t));

        mmix(h,k);

        data += 4;
        len -= 4;
    }

    uint32_t t = 0;

    switch(len)
    {
    case 3: t ^= data[2] << 16;
    case 2: t ^= data[1] << 8;
    case 1: t ^= data[0];
    };

    mmix(h,t);
    mmix(h,l);

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}
