#pragma once

#include <libdevcore/Worker.h>
#include <libethcore/Farm.h>
#include <libethcore/Miner.h>

#include <filesystem>
#include <fstream>

#include <libcrypto/progpow.hpp>

#include "../PoolClient.h"

using namespace std;
using namespace dev;
using namespace eth;

class SimulateClient : public PoolClient, Worker
{
public:
    SimulateClient(unsigned const& block, float const& difficulty);
    ~SimulateClient() override;

    void connect() override;
    void disconnect() override;

    bool isPendingState() override { return false; }
    string ActiveEndPoint() override { return ""; };

    void submitHashrate(uint64_t const& rate, string const& id) override;
    void submitSolution(const Solution& solution) override;

private:
    void workLoop() override;
    unsigned m_block;
    float m_difficulty;
    std::chrono::steady_clock::time_point m_start_time;

    float hr_alpha = 0.45f;
    float hr_max = 0.0f;
    float hr_mean = 0.0f;

    std::atomic<bool> solution_arrived{false};
    std::filesystem::path out_file_path_;
    std::fstream out_file_;
};
