// ethash: C/C++ implementation of Ethash, the Ethereum Proof of Work algorithm.
// Copyright 2018-2019 Pawel Bylica.
// Licensed under the Apache License, Version 2.0.

// Modified by Firominer's authors 2021

#include "keccak.hpp"
#include "attributes.hpp"
#include "bitwise.hpp"
#include <string_view>

namespace ethash
{
/// Loads 64-bit integer from given memory location as little-endian number.
static inline ALWAYS_INLINE uint64_t load_le(const uint8_t* data)
{
    /* memcpy is the best way of expressing the intention. Every compiler will
       optimize is to single load instruction if the target architecture
       supports unaligned memory access (GCC and clang even in O0).
       This is great trick because we are violating C/C++ memory alignment
       restrictions with no performance penalty. */
    uint64_t word;
    __builtin_memcpy(&word, data, sizeof(word));
    return le::uint64(word);
}

constexpr uint64_t round_constants_64[24] = {  //
    0x0000000000000001, 0x0000000000008082, 0x800000000000808a, 0x8000000080008000,
    0x000000000000808b, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
    0x000000000000008a, 0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
    0x000000008000808b, 0x800000000000008b, 0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080, 0x000000000000800a, 0x800000008000000a,
    0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008};

constexpr uint32_t round_constants_32[24] = {0x00000001, 0x00008082, 0x0000808A, 0x80008000,
    0x0000808B, 0x80000001, 0x80008081, 0x00008009, 0x0000008A, 0x00000088, 0x80008009, 0x8000000A,
    0x8000808B, 0x0000008B, 0x00008089, 0x00008003, 0x00008002, 0x00000080, 0x0000800A, 0x8000000A,
    0x80008081, 0x00008080, 0x80000001, 0x80008008};


/// The Keccak-f[1600] function.
///
/// The implementation of the Keccak-f function with 1600-bit width of the permutation (b).
/// The size of the state is also 1600 bit what gives 25 64-bit words.
///
/// @param state  The state of 25 64-bit words on which the permutation is to be performed.
///
/// The implementation based on:
/// - "simple" implementation by Ronny Van Keer, included in "Reference and optimized code in C",
///   https://keccak.team/archives.html, CC0-1.0 / Public Domain.
static inline ALWAYS_INLINE void keccakf1600_implementation(uint64_t state[25])
{
    uint64_t Aba, Abe, Abi, Abo, Abu;
    uint64_t Aga, Age, Agi, Ago, Agu;
    uint64_t Aka, Ake, Aki, Ako, Aku;
    uint64_t Ama, Ame, Ami, Amo, Amu;
    uint64_t Asa, Ase, Asi, Aso, Asu;

    uint64_t Eba, Ebe, Ebi, Ebo, Ebu;
    uint64_t Ega, Ege, Egi, Ego, Egu;
    uint64_t Eka, Eke, Eki, Eko, Eku;
    uint64_t Ema, Eme, Emi, Emo, Emu;
    uint64_t Esa, Ese, Esi, Eso, Esu;

    uint64_t Ba, Be, Bi, Bo, Bu;

    uint64_t Da, De, Di, Do, Du;

    Aba = state[0];
    Abe = state[1];
    Abi = state[2];
    Abo = state[3];
    Abu = state[4];
    Aga = state[5];
    Age = state[6];
    Agi = state[7];
    Ago = state[8];
    Agu = state[9];
    Aka = state[10];
    Ake = state[11];
    Aki = state[12];
    Ako = state[13];
    Aku = state[14];
    Ama = state[15];
    Ame = state[16];
    Ami = state[17];
    Amo = state[18];
    Amu = state[19];
    Asa = state[20];
    Ase = state[21];
    Asi = state[22];
    Aso = state[23];
    Asu = state[24];

    for (size_t n = 0; n < 24; n += 2)
    {
        // Round (n + 0): Axx -> Exx

        Ba = Aba ^ Aga ^ Aka ^ Ama ^ Asa;
        Be = Abe ^ Age ^ Ake ^ Ame ^ Ase;
        Bi = Abi ^ Agi ^ Aki ^ Ami ^ Asi;
        Bo = Abo ^ Ago ^ Ako ^ Amo ^ Aso;
        Bu = Abu ^ Agu ^ Aku ^ Amu ^ Asu;

        Da = Bu ^ crypto::rotl64(Be, 1);
        De = Ba ^ crypto::rotl64(Bi, 1);
        Di = Be ^ crypto::rotl64(Bo, 1);
        Do = Bi ^ crypto::rotl64(Bu, 1);
        Du = Bo ^ crypto::rotl64(Ba, 1);

        Ba = Aba ^ Da;
        Be = crypto::rotl64(Age ^ De, 44);
        Bi = crypto::rotl64(Aki ^ Di, 43);
        Bo = crypto::rotl64(Amo ^ Do, 21);
        Bu = crypto::rotl64(Asu ^ Du, 14);
        Eba = Ba ^ (~Be & Bi) ^ round_constants_64[n];
        Ebe = Be ^ (~Bi & Bo);
        Ebi = Bi ^ (~Bo & Bu);
        Ebo = Bo ^ (~Bu & Ba);
        Ebu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Abo ^ Do, 28);
        Be = crypto::rotl64(Agu ^ Du, 20);
        Bi = crypto::rotl64(Aka ^ Da, 3);
        Bo = crypto::rotl64(Ame ^ De, 45);
        Bu = crypto::rotl64(Asi ^ Di, 61);
        Ega = Ba ^ (~Be & Bi);
        Ege = Be ^ (~Bi & Bo);
        Egi = Bi ^ (~Bo & Bu);
        Ego = Bo ^ (~Bu & Ba);
        Egu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Abe ^ De, 1);
        Be = crypto::rotl64(Agi ^ Di, 6);
        Bi = crypto::rotl64(Ako ^ Do, 25);
        Bo = crypto::rotl64(Amu ^ Du, 8);
        Bu = crypto::rotl64(Asa ^ Da, 18);
        Eka = Ba ^ (~Be & Bi);
        Eke = Be ^ (~Bi & Bo);
        Eki = Bi ^ (~Bo & Bu);
        Eko = Bo ^ (~Bu & Ba);
        Eku = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Abu ^ Du, 27);
        Be = crypto::rotl64(Aga ^ Da, 36);
        Bi = crypto::rotl64(Ake ^ De, 10);
        Bo = crypto::rotl64(Ami ^ Di, 15);
        Bu = crypto::rotl64(Aso ^ Do, 56);
        Ema = Ba ^ (~Be & Bi);
        Eme = Be ^ (~Bi & Bo);
        Emi = Bi ^ (~Bo & Bu);
        Emo = Bo ^ (~Bu & Ba);
        Emu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Abi ^ Di, 62);
        Be = crypto::rotl64(Ago ^ Do, 55);
        Bi = crypto::rotl64(Aku ^ Du, 39);
        Bo = crypto::rotl64(Ama ^ Da, 41);
        Bu = crypto::rotl64(Ase ^ De, 2);
        Esa = Ba ^ (~Be & Bi);
        Ese = Be ^ (~Bi & Bo);
        Esi = Bi ^ (~Bo & Bu);
        Eso = Bo ^ (~Bu & Ba);
        Esu = Bu ^ (~Ba & Be);

        // Round (n + 1): Exx -> Axx

        Ba = Eba ^ Ega ^ Eka ^ Ema ^ Esa;
        Be = Ebe ^ Ege ^ Eke ^ Eme ^ Ese;
        Bi = Ebi ^ Egi ^ Eki ^ Emi ^ Esi;
        Bo = Ebo ^ Ego ^ Eko ^ Emo ^ Eso;
        Bu = Ebu ^ Egu ^ Eku ^ Emu ^ Esu;

        Da = Bu ^ crypto::rotl64(Be, 1);
        De = Ba ^ crypto::rotl64(Bi, 1);
        Di = Be ^ crypto::rotl64(Bo, 1);
        Do = Bi ^ crypto::rotl64(Bu, 1);
        Du = Bo ^ crypto::rotl64(Ba, 1);

        Ba = Eba ^ Da;
        Be = crypto::rotl64(Ege ^ De, 44);
        Bi = crypto::rotl64(Eki ^ Di, 43);
        Bo = crypto::rotl64(Emo ^ Do, 21);
        Bu = crypto::rotl64(Esu ^ Du, 14);
        Aba = Ba ^ (~Be & Bi) ^ round_constants_64[n + 1];
        Abe = Be ^ (~Bi & Bo);
        Abi = Bi ^ (~Bo & Bu);
        Abo = Bo ^ (~Bu & Ba);
        Abu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Ebo ^ Do, 28);
        Be = crypto::rotl64(Egu ^ Du, 20);
        Bi = crypto::rotl64(Eka ^ Da, 3);
        Bo = crypto::rotl64(Eme ^ De, 45);
        Bu = crypto::rotl64(Esi ^ Di, 61);
        Aga = Ba ^ (~Be & Bi);
        Age = Be ^ (~Bi & Bo);
        Agi = Bi ^ (~Bo & Bu);
        Ago = Bo ^ (~Bu & Ba);
        Agu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Ebe ^ De, 1);
        Be = crypto::rotl64(Egi ^ Di, 6);
        Bi = crypto::rotl64(Eko ^ Do, 25);
        Bo = crypto::rotl64(Emu ^ Du, 8);
        Bu = crypto::rotl64(Esa ^ Da, 18);
        Aka = Ba ^ (~Be & Bi);
        Ake = Be ^ (~Bi & Bo);
        Aki = Bi ^ (~Bo & Bu);
        Ako = Bo ^ (~Bu & Ba);
        Aku = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Ebu ^ Du, 27);
        Be = crypto::rotl64(Ega ^ Da, 36);
        Bi = crypto::rotl64(Eke ^ De, 10);
        Bo = crypto::rotl64(Emi ^ Di, 15);
        Bu = crypto::rotl64(Eso ^ Do, 56);
        Ama = Ba ^ (~Be & Bi);
        Ame = Be ^ (~Bi & Bo);
        Ami = Bi ^ (~Bo & Bu);
        Amo = Bo ^ (~Bu & Ba);
        Amu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl64(Ebi ^ Di, 62);
        Be = crypto::rotl64(Ego ^ Do, 55);
        Bi = crypto::rotl64(Eku ^ Du, 39);
        Bo = crypto::rotl64(Ema ^ Da, 41);
        Bu = crypto::rotl64(Ese ^ De, 2);
        Asa = Ba ^ (~Be & Bi);
        Ase = Be ^ (~Bi & Bo);
        Asi = Bi ^ (~Bo & Bu);
        Aso = Bo ^ (~Bu & Ba);
        Asu = Bu ^ (~Ba & Be);
    }

    state[0] = Aba;
    state[1] = Abe;
    state[2] = Abi;
    state[3] = Abo;
    state[4] = Abu;
    state[5] = Aga;
    state[6] = Age;
    state[7] = Agi;
    state[8] = Ago;
    state[9] = Agu;
    state[10] = Aka;
    state[11] = Ake;
    state[12] = Aki;
    state[13] = Ako;
    state[14] = Aku;
    state[15] = Ama;
    state[16] = Ame;
    state[17] = Ami;
    state[18] = Amo;
    state[19] = Amu;
    state[20] = Asa;
    state[21] = Ase;
    state[22] = Asi;
    state[23] = Aso;
    state[24] = Asu;
}

static inline ALWAYS_INLINE void keccakf800_implementation(uint32_t state[25])
{
    int round;

    uint32_t Aba, Abe, Abi, Abo, Abu;
    uint32_t Aga, Age, Agi, Ago, Agu;
    uint32_t Aka, Ake, Aki, Ako, Aku;
    uint32_t Ama, Ame, Ami, Amo, Amu;
    uint32_t Asa, Ase, Asi, Aso, Asu;

    uint32_t Eba, Ebe, Ebi, Ebo, Ebu;
    uint32_t Ega, Ege, Egi, Ego, Egu;
    uint32_t Eka, Eke, Eki, Eko, Eku;
    uint32_t Ema, Eme, Emi, Emo, Emu;
    uint32_t Esa, Ese, Esi, Eso, Esu;

    uint32_t Ba, Be, Bi, Bo, Bu;

    uint32_t Da, De, Di, Do, Du;

    Aba = state[0];
    Abe = state[1];
    Abi = state[2];
    Abo = state[3];
    Abu = state[4];
    Aga = state[5];
    Age = state[6];
    Agi = state[7];
    Ago = state[8];
    Agu = state[9];
    Aka = state[10];
    Ake = state[11];
    Aki = state[12];
    Ako = state[13];
    Aku = state[14];
    Ama = state[15];
    Ame = state[16];
    Ami = state[17];
    Amo = state[18];
    Amu = state[19];
    Asa = state[20];
    Ase = state[21];
    Asi = state[22];
    Aso = state[23];
    Asu = state[24];

    for (round = 0; round < 22; round += 2)
    {
        /* Round (round + 0): Axx -> Exx */

        Ba = Aba ^ Aga ^ Aka ^ Ama ^ Asa;
        Be = Abe ^ Age ^ Ake ^ Ame ^ Ase;
        Bi = Abi ^ Agi ^ Aki ^ Ami ^ Asi;
        Bo = Abo ^ Ago ^ Ako ^ Amo ^ Aso;
        Bu = Abu ^ Agu ^ Aku ^ Amu ^ Asu;

        Da = Bu ^ crypto::rotl32(Be, 1);
        De = Ba ^ crypto::rotl32(Bi, 1);
        Di = Be ^ crypto::rotl32(Bo, 1);
        Do = Bi ^ crypto::rotl32(Bu, 1);
        Du = Bo ^ crypto::rotl32(Ba, 1);

        Ba = Aba ^ Da;
        Be = crypto::rotl32(Age ^ De, 12);
        Bi = crypto::rotl32(Aki ^ Di, 11);
        Bo = crypto::rotl32(Amo ^ Do, 21);
        Bu = crypto::rotl32(Asu ^ Du, 14);
        Eba = Ba ^ (~Be & Bi) ^ round_constants_32[round];
        Ebe = Be ^ (~Bi & Bo);
        Ebi = Bi ^ (~Bo & Bu);
        Ebo = Bo ^ (~Bu & Ba);
        Ebu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Abo ^ Do, 28);
        Be = crypto::rotl32(Agu ^ Du, 20);
        Bi = crypto::rotl32(Aka ^ Da, 3);
        Bo = crypto::rotl32(Ame ^ De, 13);
        Bu = crypto::rotl32(Asi ^ Di, 29);
        Ega = Ba ^ (~Be & Bi);
        Ege = Be ^ (~Bi & Bo);
        Egi = Bi ^ (~Bo & Bu);
        Ego = Bo ^ (~Bu & Ba);
        Egu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Abe ^ De, 1);
        Be = crypto::rotl32(Agi ^ Di, 6);
        Bi = crypto::rotl32(Ako ^ Do, 25);
        Bo = crypto::rotl32(Amu ^ Du, 8);
        Bu = crypto::rotl32(Asa ^ Da, 18);
        Eka = Ba ^ (~Be & Bi);
        Eke = Be ^ (~Bi & Bo);
        Eki = Bi ^ (~Bo & Bu);
        Eko = Bo ^ (~Bu & Ba);
        Eku = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Abu ^ Du, 27);
        Be = crypto::rotl32(Aga ^ Da, 4);
        Bi = crypto::rotl32(Ake ^ De, 10);
        Bo = crypto::rotl32(Ami ^ Di, 15);
        Bu = crypto::rotl32(Aso ^ Do, 24);
        Ema = Ba ^ (~Be & Bi);
        Eme = Be ^ (~Bi & Bo);
        Emi = Bi ^ (~Bo & Bu);
        Emo = Bo ^ (~Bu & Ba);
        Emu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Abi ^ Di, 30);
        Be = crypto::rotl32(Ago ^ Do, 23);
        Bi = crypto::rotl32(Aku ^ Du, 7);
        Bo = crypto::rotl32(Ama ^ Da, 9);
        Bu = crypto::rotl32(Ase ^ De, 2);
        Esa = Ba ^ (~Be & Bi);
        Ese = Be ^ (~Bi & Bo);
        Esi = Bi ^ (~Bo & Bu);
        Eso = Bo ^ (~Bu & Ba);
        Esu = Bu ^ (~Ba & Be);


        /* Round (round + 1): Exx -> Axx */

        Ba = Eba ^ Ega ^ Eka ^ Ema ^ Esa;
        Be = Ebe ^ Ege ^ Eke ^ Eme ^ Ese;
        Bi = Ebi ^ Egi ^ Eki ^ Emi ^ Esi;
        Bo = Ebo ^ Ego ^ Eko ^ Emo ^ Eso;
        Bu = Ebu ^ Egu ^ Eku ^ Emu ^ Esu;

        Da = Bu ^ crypto::rotl32(Be, 1);
        De = Ba ^ crypto::rotl32(Bi, 1);
        Di = Be ^ crypto::rotl32(Bo, 1);
        Do = Bi ^ crypto::rotl32(Bu, 1);
        Du = Bo ^ crypto::rotl32(Ba, 1);

        Ba = Eba ^ Da;
        Be = crypto::rotl32(Ege ^ De, 12);
        Bi = crypto::rotl32(Eki ^ Di, 11);
        Bo = crypto::rotl32(Emo ^ Do, 21);
        Bu = crypto::rotl32(Esu ^ Du, 14);
        Aba = Ba ^ (~Be & Bi) ^ round_constants_32[round + 1];
        Abe = Be ^ (~Bi & Bo);
        Abi = Bi ^ (~Bo & Bu);
        Abo = Bo ^ (~Bu & Ba);
        Abu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Ebo ^ Do, 28);
        Be = crypto::rotl32(Egu ^ Du, 20);
        Bi = crypto::rotl32(Eka ^ Da, 3);
        Bo = crypto::rotl32(Eme ^ De, 13);
        Bu = crypto::rotl32(Esi ^ Di, 29);
        Aga = Ba ^ (~Be & Bi);
        Age = Be ^ (~Bi & Bo);
        Agi = Bi ^ (~Bo & Bu);
        Ago = Bo ^ (~Bu & Ba);
        Agu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Ebe ^ De, 1);
        Be = crypto::rotl32(Egi ^ Di, 6);
        Bi = crypto::rotl32(Eko ^ Do, 25);
        Bo = crypto::rotl32(Emu ^ Du, 8);
        Bu = crypto::rotl32(Esa ^ Da, 18);
        Aka = Ba ^ (~Be & Bi);
        Ake = Be ^ (~Bi & Bo);
        Aki = Bi ^ (~Bo & Bu);
        Ako = Bo ^ (~Bu & Ba);
        Aku = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Ebu ^ Du, 27);
        Be = crypto::rotl32(Ega ^ Da, 4);
        Bi = crypto::rotl32(Eke ^ De, 10);
        Bo = crypto::rotl32(Emi ^ Di, 15);
        Bu = crypto::rotl32(Eso ^ Do, 24);
        Ama = Ba ^ (~Be & Bi);
        Ame = Be ^ (~Bi & Bo);
        Ami = Bi ^ (~Bo & Bu);
        Amo = Bo ^ (~Bu & Ba);
        Amu = Bu ^ (~Ba & Be);

        Ba = crypto::rotl32(Ebi ^ Di, 30);
        Be = crypto::rotl32(Ego ^ Do, 23);
        Bi = crypto::rotl32(Eku ^ Du, 7);
        Bo = crypto::rotl32(Ema ^ Da, 9);
        Bu = crypto::rotl32(Ese ^ De, 2);
        Asa = Ba ^ (~Be & Bi);
        Ase = Be ^ (~Bi & Bo);
        Asi = Bi ^ (~Bo & Bu);
        Aso = Bo ^ (~Bu & Ba);
        Asu = Bu ^ (~Ba & Be);
    }

    state[0] = Aba;
    state[1] = Abe;
    state[2] = Abi;
    state[3] = Abo;
    state[4] = Abu;
    state[5] = Aga;
    state[6] = Age;
    state[7] = Agi;
    state[8] = Ago;
    state[9] = Agu;
    state[10] = Aka;
    state[11] = Ake;
    state[12] = Aki;
    state[13] = Ako;
    state[14] = Aku;
    state[15] = Ama;
    state[16] = Ame;
    state[17] = Ami;
    state[18] = Amo;
    state[19] = Amu;
    state[20] = Asa;
    state[21] = Ase;
    state[22] = Asi;
    state[23] = Aso;
    state[24] = Asu;
}

static void keccakf1600_generic(uint64_t state[25])
{
    keccakf1600_implementation(state);
}

/// The pointer to the best Keccak-f[1600] function implementation,
/// selected during runtime initialization.
static void (*keccakf1600_best)(uint64_t[25]) = keccakf1600_generic;

#if defined(__x86_64__) && __has_attribute(target)
__attribute__((target("bmi,bmi2"))) static void keccakf1600_bmi(uint64_t state[25])
{
    keccakf1600_implementation(state);
}

__attribute__((constructor)) static void select_keccakf1600_implementation()
{
    // Init CPU information.
    // This is needed on macOS because of the bug: https://bugs.llvm.org/show_bug.cgi?id=48459.
    __builtin_cpu_init();

    // Check if both BMI and BMI2 are supported. Some CPUs like Intel E5-2697 v2 incorrectly
    // report BMI2 but not BMI being available.
    if (__builtin_cpu_supports("bmi") && __builtin_cpu_supports("bmi2"))
        keccakf1600_best = keccakf1600_bmi;
}
#endif

void keccakf1600(uint64_t state[25])
{
    keccakf1600_best(state);
}

static void keccakf800_generic(uint32_t state[25])
{
    keccakf800_implementation(state);
}

/// The pointer to the best Keccak-f[1600] function implementation,
/// selected during runtime initialization.
static void (*keccakf800_best)(uint32_t[25]) = keccakf800_generic;

#if defined(__x86_64__) && __has_attribute(target)
__attribute__((target("bmi,bmi2"))) static void keccakf800_bmi(uint32_t state[25])
{
    keccakf800_implementation(state);
}

__attribute__((constructor)) static void select_keccakf800_implementation()
{
    // Init CPU information.
    // This is needed on macOS because of the bug: https://bugs.llvm.org/show_bug.cgi?id=48459.
    __builtin_cpu_init();

    // Check if both BMI and BMI2 are supported. Some CPUs like Intel E5-2697 v2 incorrectly
    // report BMI2 but not BMI being available.
    if (__builtin_cpu_supports("bmi") && __builtin_cpu_supports("bmi2"))
        keccakf800_best = keccakf800_bmi;
}
#endif

void keccakf800(uint32_t state[25])
{
    keccakf800_best(state);
}

static inline ALWAYS_INLINE void keccak(
    uint64_t* out, size_t bits, const uint8_t* input, size_t input_size)
{
    static const size_t word64_size{sizeof(uint64_t)};
    const size_t hash_size{bits / 8};
    const size_t block_size{(1600 - bits * 2) / 8};
    const size_t block_word64s{block_size / word64_size};

    std::basic_string_view<uint8_t> input_view{input, input_size};

    uint64_t state[25]{0};
    uint64_t* state_iterator{state};
    uint64_t last_word{0};
    uint8_t* last_word_iter = reinterpret_cast<uint8_t*>(&last_word);

    // Consume all input data
    while (input_view.size())
    {
        if (input_view.size() >= block_size)
        {
            for (size_t i{0}; i < block_word64s; i++)
            {
                state[i] ^= load_le(input_view.data());
                input_view.remove_prefix(word64_size);
            }
            keccakf1600_best(state);
            continue;
        }
        if (input_view.size() >= word64_size)
        {
            *state_iterator ^= load_le(input_view.data());
            ++state_iterator;
            input_view.remove_prefix(word64_size);
            continue;
        }
        __builtin_memcpy(last_word_iter, input_view.data(), input_view.size());
        last_word_iter += input_view.size();
        break;
    }

    *last_word_iter = 0x01;
    *state_iterator ^= le::uint64(last_word);
    state[block_word64s - 1] ^= 0x8000000000000000;

    keccakf1600_best(state);

    for (size_t i{0}; i < (hash_size / word64_size); ++i)
    {
        out[i] = le::uint64(state[i]);
    }
}

hash256 keccak256(const uint8_t* input, size_t input_size)
{
    hash256 out{};
    keccak(out.word64s, 256, input, input_size);
    return out;
}

hash256 keccak256(const hash256& input)
{
    return keccak256(input.bytes, sizeof(input));
}

hash512 keccak512(const uint8_t* input, size_t input_size)
{
    hash512 out{};
    keccak(out.word64s, 512, input, input_size);
    return out;
}

hash512 keccak512(const hash512& input)
{
    return keccak512(input.bytes, sizeof(input));
}

}  // namespace ethash
