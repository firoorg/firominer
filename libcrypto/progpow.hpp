// progpow: C/C++ implementation of ProgPow.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021

#pragma once
#ifndef CRYPTO_PROGPOW_HPP_
#define CRYPTO_PROGPOW_HPP_

#include <stdint.h>
#include <string>

//// blocks before changing the random program
//#define PROGPOW_PERIOD 3
//// lanes that work together calculating a hash
//#define PROGPOW_LANES 16
//// uint32 registers per lane
//#define PROGPOW_REGS 32
//// uint32 loads from the DAG per lane
//#define PROGPOW_DAG_LOADS 4
//// size of the cached portion of the DAG
//#define PROGPOW_CACHE_BYTES (16 * 1024)
//// DAG accesses, also the number of loops executed
//#define PROGPOW_CNT_DAG 64
//// random cache accesses per loop
//#define PROGPOW_CNT_CACHE 11
//// random math instructions per loop
//#define PROGPOW_CNT_MATH 18
//
//#define EPOCH_LENGTH 7500

namespace progpow
{
constexpr static uint32_t kPeriod{3};               // Number of blocks before cha
constexpr static uint32_t kLanes{16};               // lanes that work together calculating a hash
constexpr static uint32_t kRegs{32};                // uint32 registers per lane
constexpr static uint32_t kDag_loads{4};            // uint32 loads from the DAG per lane
constexpr static uint32_t kCache_bytes{16 * 1024};  // size of the cached portion of the DAG
constexpr static uint32_t kDag_count{64};    // DAG accesses, also the number of loops executed
constexpr static uint32_t kCache_count{11};  // random cache accesses per loop
constexpr static uint32_t kMath_count{18};   // random math instructions per loop


enum class kernel_type
{
    Cuda,
    OpenCL
};

// KISS99 is simple, fast, and passes the TestU01 suite
// https://en.wikipedia.org/wiki/KISS_(algorithm)
// http://www.cse.yorku.ca/~oz/marsaglia-rng.html
typedef struct
{
    uint32_t z, w, jsr, jcong;
} kiss99_t;


std::string getKern(uint64_t seed, kernel_type kern);


}  // namespace progpow

#endif  // !CRYPTO_PROGPOW_HPP_
