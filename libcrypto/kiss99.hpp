// progpow: C/C++ implementation of ProgPow.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021

#ifndef CRYPTO_KISS99_HPP_
#define CRYPTO_KISS99_HPP_

#include <stdint.h>

namespace crypto
{

/*
KISS PRNG by the spec from 1999.

The implementation of KISS pseudo-random number generator
by the specification published on 21 Jan 1999 in
http://www.cse.yorku.ca/~oz/marsaglia-rng.html.
The KISS is not versioned so here we are using `kiss99` prefix to indicate
the version from 1999.

The specification uses `unsigned long` type with the intention for 32-bit
values. Because in GCC/clang for 64-bit architectures `unsigned long` is
64-bit size type, here the explicit `uint32_t` type is used.

*/

class kiss99
{
public:
    // Creates KISS generator state with default values provided by the specification.
    kiss99() noexcept = default;

    // Creates KISS generator state with provided init values.
    kiss99(uint32_t z, uint32_t w, uint32_t jsr, uint32_t jcong) noexcept
      : z_{z}, w_{w}, jsr_{jsr}, jcong_{jcong}
    {}

    // Generates next number from the KISS generator.
    NO_SANITIZE("unsigned-integer-overflow")
    uint32_t operator()() noexcept
    {
        z_ = 36969u * (z_ & 0xffff) + (z_ >> 16u);
        w_ = 18000u * (w_ & 0xffff) + (w_ >> 16u);

        jcong_ = 69069u * jcong_ + 1234567u;

        jsr_ ^= (jsr_ << 17u);
        jsr_ ^= (jsr_ >> 13u);
        jsr_ ^= (jsr_ << 5u);

        return (((z_ << 16u) + w_) ^ jcong_) + jsr_;
    }

private:
    uint32_t z_{362436069u};
    uint32_t w_{521288629u};
    uint32_t jsr_{123456789u};
    uint32_t jcong_{380116160u};
};

}  // namespace crypto
#endif  // !CRYPTO_KISS99_HPP_
