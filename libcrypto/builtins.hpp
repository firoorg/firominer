// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021

#pragma once
#ifndef CRYPTO_BUILTINS_HPP_
#define CRYPTO_BUILTINS_HPP_

#ifdef _MSC_VER
#include <intrin.h>

/**
 * Returns the number of leading 0-bits in `x`, starting at the most significant bit position.
 * If `x` is 0, the result is undefined.
 */
static inline int __builtin_clz(unsigned int x)
{
    unsigned long most_significant_bit;
    _BitScanReverse(&most_significant_bit, x);
    return 31 - (int)most_significant_bit;
}

/**
 * Returns the number of 1-bits in `x`.
 *
 * @param x The number to scan for 1-bits
 */

static inline int __builtin_popcount(unsigned int x)
{
    return (int)__popcnt(x);
}

#endif  // !_MSC_VER
#endif  // !CRYPTO_BUILTINS_HPP_