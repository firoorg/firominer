#include <libdevcore/Log.h>
#include <chrono>

#include "SimulateClient.h"

using namespace std;
using namespace std::chrono;
using namespace dev;
using namespace eth;

SimulateClient::SimulateClient(unsigned const& block, float const& difficulty)
  : PoolClient(), Worker("sim"), m_block(block), m_difficulty(difficulty)
{}

SimulateClient::~SimulateClient() = default;

void SimulateClient::connect()
{
    // Initialize new session
    m_connected.store(true, memory_order_relaxed);
    m_session = unique_ptr<Session>(new Session);
    m_session->subscribed.store(true, memory_order_relaxed);
    m_session->authorized.store(true, memory_order_relaxed);

    if (m_onConnected)
        m_onConnected();

    // No need to worry about starting again.
    // Worker class prevents that
    startWorking();
}

void SimulateClient::disconnect()
{
    cnote << "Simulation results : " << EthWhiteBold << "Max "
          << dev::getFormattedHashes((double)hr_max, ScaleSuffix::Add, 6) << " Mean "
          << dev::getFormattedHashes((double)hr_mean, ScaleSuffix::Add, 6) << EthReset;

    m_conn->addDuration(m_session->duration());
    m_session = nullptr;
    m_connected.store(false, memory_order_relaxed);

    if (m_onDisconnected)
        m_onDisconnected();
}

void SimulateClient::submitHashrate(uint64_t const& rate, string const& id)
{
    (void)rate;
    (void)id;
}

void SimulateClient::submitSolution(const Solution& solution)
{
    // This is a fake submission only evaluated locally
    solution_arrived.store(true);
    std::chrono::steady_clock::time_point submit_start = std::chrono::steady_clock::now();
    ethash::VerificationResult result;
    if (solution.work.algo == "ethash")
    {
        result = ethash::verify_full(solution.work.block.value(), ethash::from_bytes(solution.work.header.data()),
            ethash::from_bytes(solution.mixHash.data()), solution.nonce,
            ethash::from_bytes(solution.work.get_boundary().data()));
    }
    else if (solution.work.algo == "progpow")
    {
        result = progpow::verify_full(solution.work.block.value(), ethash::from_bytes(solution.work.header.data()),
            ethash::from_bytes(solution.mixHash.data()), solution.nonce,
            ethash::from_bytes(solution.work.get_boundary().data()));
    }

    bool accepted = (result == ethash::VerificationResult::kOk);
    std::chrono::milliseconds response_delay_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - submit_start);

    if (accepted)
    {
        if (m_onSolutionAccepted)
            m_onSolutionAccepted(response_delay_ms, solution.midx, false);
    }
    else
    {
        if (m_onSolutionRejected)
            m_onSolutionRejected(response_delay_ms, solution.midx);
    }
}

// Handles all logic here
void SimulateClient::workLoop()
{
    m_start_time = std::chrono::steady_clock::now();

    // apply exponential sliding average
    // ref: https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average

    WorkPackage current;

    current.algo = "progpow";
    current.block.emplace(m_block);
    current.epoch.emplace(m_block / ethash::kEpoch_length);

    ethash::hash256 seed_h256{ethash::calculate_seed_from_epoch(current.epoch.value())};
    current.seed = h256(reinterpret_cast<::byte*>(seed_h256.bytes), h256::ConstructFromPointer);

    current.header = h256::random();
    current.boundary = h256(dev::getTargetFromDiff(m_difficulty));
    current.block_boundary = current.boundary;

    m_onWorkReceived(current);  // submit new fake job
    cnote << "Using block " << m_block << ", difficulty " << m_difficulty;

    while (m_session)
    {
        float hr = Farm::f().HashRate();
        hr_max = std::max(hr_max, hr);
        hr_mean = hr_alpha * hr_mean + (1.0f - hr_alpha) * hr;
        this_thread::sleep_for(chrono::milliseconds(200));
        if (solution_arrived)
        {
            solution_arrived.store(false);
            ++m_block;
            current.block.emplace(m_block);
            current.epoch.emplace(m_block / ethash::kEpoch_length);
            seed_h256 = ethash::calculate_seed_from_epoch(current.epoch.value());
            current.seed = h256(reinterpret_cast<::byte*>(seed_h256.bytes), h256::ConstructFromPointer);
            current.header = h256::random();
            m_onWorkReceived(current);  // submit new fake job
        }


    }

}
