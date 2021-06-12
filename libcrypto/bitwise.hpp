// progpow: C/C++ implementation of ProgPow.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021

#pragma once
#ifndef CRYPTO_BITWISE_HPP_
#define CRYPTO_BITWISE_HPP_

#include "attributes.hpp"
#include "builtins.hpp"
#include "endianess.hpp"
#include <cstring>

namespace crypto
{

/** FNV 32-bit prime. */
constexpr uint32_t kFNV_PRIME = 0x01000193;

/** FNV 32-bit offset basis. */
constexpr uint32_t kFNV_OFFSET_BASIS = 0x811c9dc5;


// Performs left direction circular bit rotation
static INLINE uint32_t rotl32(uint32_t n, uint32_t s)
{
    const uint32_t mask{31};
    s &= mask;
    uint32_t neg_s{(uint32_t)(-(int)s)};
    return (n << s) | (n >> (neg_s & mask));
}

// Performs right direction circular bit rotation
static INLINE uint32_t rotr32(uint32_t n, uint32_t s)
{
    const uint32_t mask{31};
    s &= mask;
    uint32_t neg_s{(uint32_t)(-(int)s)};
    return (n >> s) | (n << (neg_s & mask));
}

// Returns the number of leading 0-bits in x, starting at the most significant bit position.
static INLINE uint32_t clz32(uint32_t v)
{
    return v ? (uint32_t)__builtin_clz(v) : 32u;
}

// Returns the number of 1-bits in x.
static INLINE uint32_t popcnt32(uint32_t v)
{
    return (uint32_t)__builtin_popcount(v);
}

// Computes x * y and returns the high half of the product of x and y.
static INLINE uint32_t mul_hi32(uint32_t x, uint32_t y)
{
    return (uint32_t)(((uint64_t)x * (uint64_t)y) >> 32);
}

    /**
 * The implementation of FNV-1 hash.
 *
 * See https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1_hash.
 */
NO_SANITIZE("unsigned-integer-overflow")
static inline uint32_t fnv1(uint32_t u, uint32_t v) noexcept
{
    return static_cast<uint32_t>(static_cast<uint64_t>(u) * kFNV_PRIME) ^ v;
}

/**
 * The implementation of FNV-1a hash.
 *
 * See https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash.
 */
NO_SANITIZE("unsigned-integer-overflow")
static inline uint32_t fnv1a(uint32_t u, uint32_t v) noexcept
{
    return static_cast<uint32_t>(static_cast<uint64_t>(u ^ v) * kFNV_PRIME);
}

}  // namespace crypto

#endif  // !CRYPTO_BITWISE_HPP_
