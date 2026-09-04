#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <libcrypto/ethash.hpp>
#include <libcrypto/progpow.hpp>

namespace
{
ethash::hash256 fromHex(const std::string& hex)
{
    if (hex.size() != 64)
        throw std::invalid_argument("expected a 256-bit hex string");

    ethash::hash256 hash{};
    for (size_t i = 0; i < sizeof(hash); ++i)
        hash.bytes[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    return hash;
}

bool concurrentLazyDagLookupMatchesReference()
{
    std::vector<ethash::hash512> light(3);
    std::vector<uint32_t> l1(ethash::kL1_cache_words);
    std::vector<ethash::hash2048> full(65);
    ethash::epoch_context context{0, static_cast<uint32_t>(light.size()), light.size() * sizeof(light[0]),
        static_cast<uint32_t>(full.size() * 2), full.size() * sizeof(full[0]), light.data(), l1.data(),
        reinterpret_cast<ethash::hash1024*>(full.data())};
    const auto expected = ethash::detail::calculate_dataset_item_2048(context, 64);

    for (unsigned trial = 0; trial != 4; ++trial)
    {
        full[64] = {};
        std::atomic<unsigned> ready{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> threads;
        for (unsigned i = 0; i != 8; ++i)
        {
            threads.emplace_back([&] {
                ready.fetch_add(1, std::memory_order_relaxed);
                while (!go.load(std::memory_order_acquire))
                    std::this_thread::yield();
                (void)ethash::detail::lazy_lookup_2048(context, 64);
            });
        }
        while (ready.load(std::memory_order_relaxed) != threads.size())
            std::this_thread::yield();
        go.store(true, std::memory_order_release);
        for (auto& thread : threads)
            thread.join();
        if (std::memcmp(&full[64], &expected, sizeof(expected)) != 0)
            return false;
    }
    return true;
}
}  // namespace

int main()
{
    if (!concurrentLazyDagLookupMatchesReference())
    {
        std::cerr << "concurrent lazy DAG lookup mismatch\n";
        return 1;
    }

    if (ethash::calculate_epoch_from_block_num(1205099) != 926 ||
        ethash::calculate_epoch_from_block_num(1205100) != 650 ||
        ethash::calculate_epoch_from_block_num(189799, "testnet") != 145 ||
        ethash::calculate_epoch_from_block_num(189800, "testnet") != 100 ||
        ethash::calculate_epoch_from_block_num(3899, "devnet") != 2 ||
        ethash::calculate_epoch_from_block_num(3900, "devnet") != 1 ||
        ethash::calculate_epoch_from_block_num(3900, "regtest") != 1 ||
        ethash::calculate_epoch_from_block_num(1205100, "testnet") != 100 ||
        ethash::calculate_epoch_from_block_num(1205100, "devnet") != 1 ||
        ethash::calculate_epoch_from_block_num(2000000, "mainnet") != 650)
    {
        std::cerr << "Firo epoch schedule mismatch\n";
        return 1;
    }

    const auto epoch = ethash::calculate_epoch_from_seed(ethash::calculate_seed_from_epoch(650));
    if (!epoch || *epoch != 650)
    {
        std::cerr << "failed to resolve Firo's terminal DAG epoch\n";
        return 1;
    }

    const auto context = ethash::get_epoch_context(0, false);
    const auto header = fromHex("2d794e900dcad779e658de9078d9a88eee87d75f7b09a8fdd270d3a8e76650c7");
    const auto boundary = fromHex("0001869e7a058e2aaf266cd2f166fb85c98d651e60eadbbe72bb0a36f8802805");
    const auto expectedMix = fromHex("cfab3766331d6c4e6913e6688a71e4c26b7f36c1581cdbec0f5b19db8956eb50");
    const auto nonce = std::stoull("85f22c9b3cd2f123", nullptr, 16);
    const auto result = progpow::hash(*context, 1, header, nonce);
    const auto widePeriodResult = progpow::hash(*context, (uint64_t{1} << 32) + 1, header, nonce);

    if (!ethash::is_equal(result.mix_hash, expectedMix) ||
        ethash::to_hex(result.final_hash) != "00017c7de1fa499314f9e3dd3537546982073624f7d478592cf28a6d13929f2d" ||
        progpow::verify_full(*context, 1, header, expectedMix, nonce, boundary) !=
            ethash::VerificationResult::kOk)
    {
        std::cerr << "FiroPoW reference vector mismatch\n";
        return 1;
    }
    if (ethash::is_equal(widePeriodResult.mix_hash, result.mix_hash))
    {
        std::cerr << "FiroPoW program seed was truncated to 32 bits\n";
        return 1;
    }

    // Firo master at 4f0c771462b2f327e3a7ff7bf2532bc33c727713.
    const auto terminalContext = ethash::get_epoch_context(650, false);
    const auto terminalMix = fromHex("ea028f6c32723037aedcbf54112ca795bc93f658f55898d8116c9aed6302f83a");
    const auto terminalResult = progpow::hash(*terminalContext, 1205100, header, nonce);
    if (!ethash::is_equal(terminalResult.mix_hash, terminalMix) ||
        ethash::to_hex(terminalResult.final_hash) !=
            "68df2e10b36ae1b75db2eb746c46b249a4237f3e1681717ac50bad5bd216e173" ||
        progpow::verify_full(*terminalContext, 1205100, header, terminalMix, nonce,
            fromHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")) !=
            ethash::VerificationResult::kOk ||
        progpow::verify_full(1205100, header, terminalMix, nonce,
            fromHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")) !=
            ethash::VerificationResult::kOk)
    {
        std::cerr << "Firo terminal-epoch vector mismatch\n";
        return 1;
    }
}
