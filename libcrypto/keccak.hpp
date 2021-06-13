// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021
#pragma once
#ifndef CRYPTO_KECCAK_HPP_
#define CRYPTO_KECCAK_HPP_

#include <stddef.h>

#include "hash_types.hpp"

#if defined(_MSC_VER)
#include <string.h>
#define __builtin_memcpy memcpy
#endif

namespace ethash
{

void keccakf1600(uint64_t state[25]);
void keccakf800(uint32_t state[25]);

hash256 keccak256(const hash256& input);
hash256 keccak256(const uint8_t* input, size_t input_size);
hash512 keccak512(const hash512& input);
hash512 keccak512(const uint8_t* input, size_t input_size);

}  // namespace ethash

#endif  // !CRYPTO_KECCAK_HPP_
