#include <libdevcore/Log.h>
#include <chrono>

#include "SimulateClient.h"

using namespace std;
using namespace std::chrono;
using namespace dev;
using namespace eth;

static std::string random_string(size_t len)
{
    static constexpr char kAlphaNum[]{
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"};

    // don't count the null terminator
    static constexpr size_t kNumberOfCharacters{sizeof(kAlphaNum) - 1};

    std::random_device rd;
    std::default_random_engine engine{rd()};

    // yield random numbers up to and including kNumberOfCharacters - 1
    std::uniform_int_distribution<size_t> uniform_dist{0, kNumberOfCharacters - 1};

    std::string s;
    s.reserve(len);

    for (size_t i{0}; i < len; ++i)
    {
        size_t random_number{uniform_dist(engine)};
        s += kAlphaNum[random_number];
    }

    return s;
}


SimulateClient::SimulateClient(unsigned const& block, float const& difficulty)
  : PoolClient(), Worker("sim"), m_block(block), m_difficulty(difficulty)
{
    filesystem::path tmp_dir{filesystem::temp_directory_path()};
    bool found{false};
    for (size_t i = 0; i < 5000; i++)
    {
        out_file_path_ = filesystem::path{tmp_dir / random_string(8)};
        out_file_path_ += ".txt";
        if (!filesystem::exists(out_file_path_))
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        throw std::runtime_error("Can't find a valid output path");
    }

    out_file_.open(out_file_path_.string(), std::ios_base::out);
    if (!out_file_.is_open())
    {
        throw std::runtime_error(strerror(errno));
    }
    cnote << "Dumping results to " << out_file_path_.string();
}

SimulateClient::~SimulateClient()
{
    if (out_file_.is_open())
    {
        out_file_.close();
    }
};

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
        auto dag_epoch_number{ethash::calculate_epoch_from_block_num(solution.work.block.value())};
        auto dag_epoch_context{ethash::get_epoch_context(dag_epoch_number, false)};
        auto progpow_period{solution.work.block.value() / progpow::kPeriodLength};
        auto target{ethash::from_bytes(solution.work.get_boundary().data())};
        auto header{ethash::from_bytes(solution.work.header.data())};
        auto computed_mix{ethash::from_bytes(solution.mixHash.data())};
        auto expected{progpow::hash(*dag_epoch_context, progpow_period, header, solution.nonce)};
        if (!ethash::is_less_or_equal(expected.final_hash, target))
        {
            result = ethash::VerificationResult::kInvalidNonce;
        }
        else
        {
            if (!ethash::is_equal(expected.mix_hash, computed_mix))
            {
                result = ethash::VerificationResult::kInvalidMixHash;
            }
            else
            {
                result = ethash::VerificationResult::kOk;
            }
        }
        if (result == ethash::VerificationResult::kOk)
        {
            h256 fh{reinterpret_cast<::byte*>(expected.final_hash.bytes), h256::ConstructFromPointer};
            // Save data to out_file
            std::stringstream s;
            s << "{" << std::to_string(solution.work.block.value()) << ", ";  // Block number
            s << "\"" << solution.work.header.hex() << "\", ";                // Block header
            s << "\"" << solution.work.boundary.hex() << "\", ";              // Boundary
            s << "\"" << dev::toHex(solution.nonce) << "\", ";                // Nonce
            s << "\"" << solution.mixHash.hex() << "\", ";                    // Mix hash
            s << "\"" << fh.hex() << "\" },";                                 // Final hash
            out_file_ << s.str() << std::endl;
        }
        // result = progpow::verify_full(solution.work.block.value(), ethash::from_bytes(solution.work.header.data()),
        //    ethash::from_bytes(solution.mixHash.data()), solution.nonce,
        //    ethash::from_bytes(solution.work.get_boundary().data()));
    }

    bool accepted = (result == ethash::VerificationResult::kOk);
    std::chrono::milliseconds response_delay_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - submit_start);

    if (accepted)
    {
        // Save solution and work


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

            // Generate and submit a new random work incrementing block
            current.block.emplace(++m_block);
            current.epoch.emplace(current.block.value() / ethash::kEpoch_length);
            seed_h256 = ethash::calculate_seed_from_epoch(current.epoch.value());
            current.seed = h256(reinterpret_cast<::byte*>(seed_h256.bytes), h256::ConstructFromPointer);
            current.header = h256::random();

            m_onWorkReceived(current);  // submit new fake job
        }
    }
}
