// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021

#pragma once
#ifndef CRYPTO_HASH_TYPES_HPP_
#define CRYPTO_HASH_TYPES_HPP_

#include <stdint.h>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "endianess.hpp"


namespace ethash
{
union hash256
{
    uint64_t word64s[4];
    uint32_t word32s[8];
    uint8_t bytes[32];
    char str[32];
};

union hash512
{
    uint64_t word64s[8];
    uint32_t word32s[16];
    uint8_t bytes[64];
    char str[64];
};

union hash1024
{
    union hash512 hash512s[2];
    uint64_t word64s[16];
    uint32_t word32s[32];
    uint8_t bytes[128];
    char str[128];
};

union hash2048
{
    union hash512 hash512s[4];
    union hash1024 hash1024s[2];
    uint64_t word64s[32];
    uint32_t word32s[64];
    uint8_t bytes[256];
    char str[256];
};

#if __BYTE_ORDER == __LITTLE_ENDIAN

struct le
{
    static uint32_t uint32(uint32_t x) noexcept { return x; }
    static uint64_t uint64(uint64_t x) noexcept { return x; }

    static const hash1024& uint32s(const hash1024& h) noexcept { return h; }
    static const hash512& uint32s(const hash512& h) noexcept { return h; }
    static const hash256& uint32s(const hash256& h) noexcept { return h; }
};

struct be
{
    static uint32_t uint32(uint32_t x) noexcept { return bswap32(x); }
    static uint64_t uint64(uint64_t x) noexcept { return bswap64(x); }
};

#elif __BYTE_ORDER == __BIG_ENDIAN

struct le
{
    static uint32_t uint32(uint32_t x) noexcept { return bswap32(x); }
    static uint64_t uint64(uint64_t x) noexcept { return bswap64(x); }

    static hash1024 uint32s(hash1024 h) noexcept
    {
        for (auto& w : h.word32s)
            w = uint32(w);
        return h;
    }

    static hash512 uint32s(hash512 h) noexcept
    {
        for (auto& w : h.word32s)
            w = uint32(w);
        return h;
    }

    static hash256 uint32s(hash256 h) noexcept
    {
        for (auto& w : h.word32s)
            w = uint32(w);
        return h;
    }
};

struct be
{
    static uint64_t uint64(uint64_t x) noexcept { return x; }
};

#endif

inline bool is_less_or_equal(const hash256& a, const hash256& b) noexcept
{
    for (size_t i{0}; i < (sizeof(a) / sizeof(a.word64s[0])); ++i)
    {
        if (be::uint64(a.word64s[i]) > be::uint64(b.word64s[i]))
            return false;
        if (be::uint64(a.word64s[i]) < be::uint64(b.word64s[i]))
            return true;
    }
    return true;
}

inline bool is_equal(const hash256& a, const hash256& b)
{
    return std::memcmp(a.bytes, b.bytes, sizeof(a)) == 0;
}

inline std::string to_hex(const hash256& value)
{
    std::stringstream s;
    for (size_t i = 0; i < 32; i++)
    {
        s << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(value.bytes[i]);
    }
    return s.str();
}

inline void shiftLeft(hash256& hash, uint32_t bitsToShift)
{
    // use hash_256.word64s
    static const size_t size_of_element = sizeof(hash.word64s[0]);
    static const size_t bits_of_element = size_of_element * 8;
    static const size_t num_of_elements = sizeof(hash) / size_of_element;


    if (!bitsToShift)
    {
        return;
    }

    hash256 tmp;
    for (size_t i{0}; i < num_of_elements; i++)
    {
        tmp.word64s[i] = be::uint64(hash.word64s[i]);
        hash.word64s[i] = 0;
    }

    int elements_to_jump_over = bitsToShift / bits_of_element;
    bitsToShift = bitsToShift % bits_of_element;

    for (int i = num_of_elements - 1; i - elements_to_jump_over >= 0; i--)
    {
        if (i - elements_to_jump_over - 1 >= 0)
        {
            hash.word64s[i - elements_to_jump_over - 1] = be::uint64(tmp.word64s[i] >> (bits_of_element - bitsToShift));
        }
        if (i - elements_to_jump_over >= 0)
        {
            hash.word64s[i - elements_to_jump_over] |= be::uint64(tmp.word64s[i] << bitsToShift);
        }
    }
}

inline hash256 from_compact(const uint32_t nbits, bool* pfNegative = nullptr, bool* pfOverflow = nullptr)
{
    // see https://doxygen.bitcoincore.org/classarith__uint256.html
    //     https://doxygen.bitcoincore.org/classarith__uint256.html#a06c0f1937edece69b8d33f88e8d35bc8
    //     https://github.com/RavenCommunity/kawpowminer/blob/e5deed9afe8300748b97f1d79cce64a77f3e7713/libpoolprotocols/stratum/arith_uint256.h#L266-L285
    //     https://github.com/RavenCommunity/kawpowminer/blob/e5deed9afe8300748b97f1d79cce64a77f3e7713/libpoolprotocols/stratum/arith_uint256.cpp#L207-L225

    hash256 res{};
    uint32_t nSize = nbits >> 24;
    uint32_t nWord = nbits & 0x007fffff;
    if (nSize <= 3)
    {
        nWord >>= 8 * (3 - nSize);
        res.word32s[7] = be::uint32(nWord);
    }
    else
    {
        res.word32s[7] = be::uint32(nWord);
        shiftLeft(res, 8 * (nSize - 3));
    }

    if (pfNegative)
    {
        *pfNegative = nWord != 0 && (nbits & 0x00800000) != 0;
    }
    if (pfOverflow)
    {
        *pfOverflow = nWord != 0 && ((nSize > 34) || (nWord > 0xff && nSize > 33) || (nWord > 0xffff && nSize > 32));
    }

    return res;
}

}  // namespace ethash

#endif  // !CRYPTO_HASH_TYPES_HPP_
