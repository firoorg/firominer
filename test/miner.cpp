#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

#include <libcrypto/progpow.hpp>
#include <libethcore/Miner.h>

bool g_exitOnError = false;

namespace
{
std::atomic<unsigned> activeInitializers{0};
std::atomic<bool> initializersOverlapped{false};

class TestMiner : public dev::eth::Miner
{
public:
    TestMiner(unsigned index, bool failFirst, bool detectOverlap = false)
      : Miner("test-", index), m_failFirst(failFirst), m_detectOverlap(detectOverlap)
    {}
    ~TestMiner() override { stopWorking(); }

    bool initialize(dev::eth::WorkPackage const& work) { return initEpoch(work); }
    dev::eth::WorkPackage currentWork() const { return work(); }

    void kick_miner() override {}

private:
    bool initDevice() override { return true; }
    bool initEpoch_internal(dev::eth::WorkPackage const&) override
    {
        if (m_detectOverlap)
        {
            if (activeInitializers.fetch_add(1) != 0)
                initializersOverlapped = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            activeInitializers.fetch_sub(1);
        }
        return !m_failFirst || m_attempts++ != 0;
    }
    void workLoop() override
    {
        while (!shouldStop())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool m_failFirst;
    bool m_detectOverlap;
    unsigned m_attempts = 0;
};
}  // namespace

int main()
{
    using namespace dev::eth;

    TestMiner first{0, false};
    TestMiner retry{1, true};
    first.startWorking();
    retry.startWorking();
    Miner::setDagLoadInfo(DAG_LOAD_MODE_SEQUENTIAL);

    WorkPackage work;
    work.header = dev::h256{1u};
    work.startNonce = UINT64_MAX - 0xffff;
    work.nonceRange = 0x10000;
    if (!nonceInRange(work, work.startNonce + 0xffff) || nonceInRange(work, 0) ||
        wrapNonce(work, 0) != work.startNonce)
    {
        std::cerr << "assigned nonce range wrapping failed\n";
        return 1;
    }
    work.startNonce = 0;
    work.nonceRange = 0;
    if (!first.initialize(work) || retry.initialize(work) || !retry.initialize(work))
    {
        std::cerr << "sequential epoch retry failed\n";
        return 1;
    }

    {
        TestMiner serialFirst{2, false, true};
        TestMiner serialSecond{3, false, true};
        serialFirst.startWorking();
        serialSecond.startWorking();
        std::atomic<unsigned> ready{0};
        std::atomic<bool> start{false};
        bool firstResult = false;
        bool secondResult = false;
        auto initialize = [&](TestMiner& miner, bool& result) {
            ready.fetch_add(1);
            while (!start.load())
                std::this_thread::yield();
            result = miner.initialize(work);
        };
        std::thread firstThread{initialize, std::ref(serialFirst), std::ref(firstResult)};
        std::thread secondThread{initialize, std::ref(serialSecond), std::ref(secondResult)};
        while (ready.load() != 2)
            std::this_thread::yield();
        start = true;
        firstThread.join();
        secondThread.join();
        if (!firstResult || !secondResult || initializersOverlapped)
        {
            std::cerr << "sequential epoch initialization overlapped\n";
            return 1;
        }
    }

    retry.setWork(work, false);
    const auto staleWork = retry.currentWork();
    work.header = dev::h256{2u};
    retry.setWork(work, false);
    const auto currentWork = retry.currentWork();

    retry.pause(PauseDueToInitEpochError, staleWork);
    if (retry.paused())
    {
        std::cerr << "stale work paused the miner\n";
        return 1;
    }

    retry.pause(PauseDueToInitEpochError, currentWork);
    if (!retry.pauseTest(PauseDueToInitEpochError))
    {
        std::cerr << "current work did not pause the miner\n";
        return 1;
    }

    retry.setWork(work, false);
    if (retry.paused())
    {
        std::cerr << "new work did not clear the initialization error\n";
        return 1;
    }

    retry.pause(PauseDueToAPIRequest);
    work.header = dev::h256{3u};
    retry.setWork(work, false);
    if (retry.currentWork())
    {
        std::cerr << "paused miner exposed work\n";
        return 1;
    }
    retry.resume(PauseDueToAPIRequest);
    if (retry.currentWork().header != work.header)
    {
        std::cerr << "resumed miner did not retain latest work\n";
        return 1;
    }

    Solution solution{};
    solution.nonce = 1;
    solution.work.block = 189800;
    solution.work.epochContext = ethash::get_epoch_context(100, false);
    solution.work.header = dev::h256{3u};
    solution.work.boundary = ~dev::h256{};
    const auto hash = progpow::hash(*solution.work.epochContext,
        *solution.work.block / progpow::kPeriodLength,
        ethash::from_bytes(solution.work.header.data()), solution.nonce);
    solution.mixHash = dev::h256{hash.mix_hash.bytes, dev::h256::ConstructFromPointer};
    if (verifyProgpow(solution) != ethash::VerificationResult::kOk)
    {
        std::cerr << "non-mainnet work verification failed\n";
        return 1;
    }
}
