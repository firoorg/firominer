// progpow: C/C++ implementation of ProgPow.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021

#pragma once
#ifndef CRYPTO_PROGPOW_HPP_
#define CRYPTO_PROGPOW_HPP_

#include "ethash.hpp"
#include "kiss99.hpp"
#include <stdint.h>
#include <string>

namespace progpow
{
constexpr static uint32_t kPeriodLength{1};         // Number of blocks before change of the random program
constexpr static uint32_t kLanes{16};               // lanes that work together calculating a hash
constexpr static uint32_t kRegs{32};                // uint32 registers per lane
constexpr static uint32_t kDag_loads{4};            // uint32 loads from the DAG per lane
constexpr static uint32_t kCache_bytes{16 * 1024};  // size of the cached portion of the DAG
constexpr static uint32_t kDag_count{64};           // DAG accesses, also the number of loops executed
constexpr static uint32_t kCache_count{11};         // random cache accesses per loop
constexpr static uint32_t kMath_count{18};          // random math instructions per loop

constexpr static uint32_t kWords_per_lane{sizeof(ethash::hash2048) / (sizeof(uint32_t) * kLanes)};

enum class kernel_type
{
    Cuda,
    OpenCL
};

// ProgPoW mix RNG state.
//
// Encapsulates the state of the random number generator used in computing ProgPoW mix.
// This includes the state of the KISS99 RNG and the precomputed random permutation of the
// sequence of mix item indexes.
class mix_rng_state
{
public:
    explicit mix_rng_state(uint64_t seed) noexcept;

    uint32_t next_dst() noexcept { return dst_seq_[(dst_counter_++) % kRegs]; }
    uint32_t next_src() noexcept { return src_seq_[(src_counter_++) % kRegs]; }

    crypto::kiss99 rng;

private:
    size_t dst_counter_{0};
    size_t src_counter_{0};
    std::array<uint32_t, kRegs> dst_seq_;
    std::array<uint32_t, kRegs> src_seq_;
};


std::string getKern(uint64_t seed, kernel_type kern);

ethash::hash256 hash_seed(const ethash::hash256& header_hash, uint64_t nonce) noexcept;
ethash::hash256 hash_mix(const ethash::epoch_context& context, const uint32_t period, uint64_t seed);
ethash::hash256 hash_final(const ethash::hash256& input_hash, const ethash::hash256& mix_hash) noexcept;

ethash::result hash(
    const ethash::epoch_context& context, const uint32_t period, const ethash::hash256& header_hash, uint64_t nonce);

ethash::VerificationResult verify_full(const ethash::epoch_context& context, const uint32_t period,
    const ethash::hash256& header_hash, const ethash::hash256& mix_hash, uint64_t nonce,
    const ethash::hash256& boundary) noexcept;

ethash::VerificationResult verify_full(const uint64_t block_number, const ethash::hash256& header_hash,
    const ethash::hash256& mix_hash, uint64_t nonce, const ethash::hash256& boundary) noexcept;

}  // namespace progpow

#endif  // !CRYPTO_PROGPOW_HPP_
