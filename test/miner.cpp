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
    void recordHashes(uint32_t size, uint32_t count) { updateHashRate(size, count); }

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

    TelemetryType telemetry;
    telemetry.farm.hashrate = 2e12f;
    if (telemetry.str().find("2000.00 Gh") == std::string::npos)
    {
        std::cerr << "telemetry exceeded its largest hashrate suffix\n";
        return 1;
    }

    DeviceDescriptor fallbackNvidia{};
    fallbackNvidia.clDetected = true;
    fallbackNvidia.clVendorId = 0x10de;
    fallbackNvidia.uniqueId = "CL:0:0";
    DeviceDescriptor fallbackAmd = fallbackNvidia;
    fallbackAmd.clVendorId = 0x1002;
    DeviceDescriptor unmatchedPciNvidia = fallbackNvidia;
    unmatchedPciNvidia.uniqueId = "81:02.0";
    if (shouldAutoSubscribeOpenCL(fallbackNvidia, MinerType::Mixed, true) ||
        !shouldAutoSubscribeOpenCL(fallbackNvidia, MinerType::CL, false) ||
        !shouldAutoSubscribeOpenCL(fallbackNvidia, MinerType::Mixed, false) ||
        !shouldAutoSubscribeOpenCL(fallbackAmd, MinerType::Mixed, true) ||
        shouldAutoSubscribeOpenCL(unmatchedPciNvidia, MinerType::Mixed, true))
    {
        std::cerr << "automatic OpenCL selection did not preserve the CUDA preference\n";
        return 1;
    }

    // Batch sizes must remain usable at devnet/regtest difficulty, and every
    // stream's launch must fit wholly inside its assigned nonce segment.
    for (uint32_t group : {32u, 64u, 128u, 256u, 512u})
    {
        for (uint64_t target : {uint64_t{0}, UINT64_MAX / 2048,
                 uint64_t{0x00ffff0000000000}, uint64_t{0x7fffffffffffffff}, UINT64_MAX})
        {
            if (gpuBatchSize(131072 * group, group, target) < group ||
                gpuBatchSize(131072 * group, group, target, group / 2) != 0)
            {
                std::cerr << "GPU batch rejected low difficulty or exceeded a small nonce range\n";
                return 1;
            }
            for (uint64_t range : {uint64_t{group}, uint64_t{32} * group})
            {
                for (uint32_t streams : {1u, 2u, 3u, 8u})
                {
                    const uint64_t active = std::min<uint64_t>(streams, range / group);
                    const auto batch = gpuBatchSize(3 * group, group, target, range / active);
                    WorkPackage bounded;
                    bounded.startNonce = UINT64_MAX - range + 1;
                    bounded.nonceRange = range;
                    if (!batch || batch % group || range % batch || active * batch > range)
                    {
                        std::cerr << "GPU batch does not divide the nonce range\n";
                        return 1;
                    }
                    auto nonce = bounded.startNonce;
                    for (uint64_t scheduled = 0; scheduled < range; scheduled += batch)
                    {
                        if (!nonceInRange(bounded, nonce) || !nonceInRange(bounded, nonce + batch - 1))
                        {
                            std::cerr << "GPU launch straddled the nonce range\n";
                            return 1;
                        }
                        nonce += batch;
                    }
                    if (nonce - bounded.startNonce != range || nonceInRange(bounded, nonce))
                    {
                        std::cerr << "GPU launch sequence did not cover the nonce range\n";
                        return 1;
                    }
                }
            }
        }
    }
    if (gpuBatchSize(1536, 512, 0, 4096) != 1024 || gpuBatchSize(1536, 512, 0) != 1536 ||
        gpuBatchSize(1024, 0, 0) != 0)
    {
        std::cerr << "GPU batch rounding failed\n";
        return 1;
    }

    // A retargeted resend of the same job continues its nonce range; any other
    // header, range or empty predecessor starts again from the assigned nonce.
    WorkPackage sent;
    sent.header = dev::h256(1u);
    sent.epoch = 7;
    sent.block = 9100;
    sent.startNonce = UINT64_MAX - 4095;
    sent.nonceRange = 4096;
    sent.boundary = dev::h256(2u);
    WorkPackage resent = sent;
    resent.job = "retarget";
    resent.boundary = dev::h256(3u);
    resent.workGeneration = sent.workGeneration + 1;
    WorkPackage nextJob = resent;
    nextJob.header = dev::h256(4u);
    WorkPackage otherRange = resent;
    otherRange.startNonce = 0;
    if (!continuesNonceRange(sent, resent) || continuesNonceRange(WorkPackage{}, resent) ||
        continuesNonceRange(sent, nextJob) || continuesNonceRange(sent, otherRange) ||
        remainingNonces(sent, sent.startNonce) != 4096 ||
        remainingNonces(sent, sent.startNonce + 3584) != 512 ||
        remainingNonces(sent, sent.startNonce + 4096) != 0 ||
        remainingNonces(sent, sent.startNonce + 5000) != 0)
    {
        std::cerr << "resent work did not keep its nonce progress\n";
        return 1;
    }
    // Resumed ranges need not divide into the new batch: CUDA launches whole
    // batches that fit, then searches any remaining blocks with smaller launches.
    for (uint64_t used : {uint64_t{512}, uint64_t{1536}, uint64_t{3584}})
    {
        for (uint32_t streams : {1u, 2u, 3u})
        {
            auto nonce = sent.startNonce + used;
            uint64_t unscheduled = remainingNonces(sent, nonce);
            while (unscheduled >= 512)
            {
                const uint64_t active = std::min<uint64_t>(streams, unscheduled / 512);
                const auto batch = gpuBatchSize(4 * 512, 512, 0, unscheduled / active);
                for (; unscheduled >= batch; unscheduled -= batch)
                {
                    if (!batch || !nonceInRange(sent, nonce) || !nonceInRange(sent, nonce + batch - 1))
                    {
                        std::cerr << "resumed GPU launch left its nonce range\n";
                        return 1;
                    }
                    nonce += batch;
                }
            }
            if (nonce - sent.startNonce != sent.nonceRange)
            {
                std::cerr << "resumed GPU launches did not finish the nonce range\n";
                return 1;
            }
        }
    }

    const auto beforeConstruction = std::chrono::steady_clock::now();
    TestMiner accounting{0, false};
    const auto afterConstruction = std::chrono::steady_clock::now();
    accounting.recordHashes(1024, 2);
    accounting.recordHashes(64, 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    accounting.TriggerHashRateUpdate();
    const auto beforeUpdate = std::chrono::steady_clock::now();
    accounting.recordHashes(32, 1);
    const auto afterUpdate = std::chrono::steady_clock::now();
    const auto minUs = std::chrono::duration_cast<std::chrono::microseconds>(beforeUpdate - afterConstruction).count();
    const auto maxUs = std::chrono::duration_cast<std::chrono::microseconds>(afterUpdate - beforeConstruction).count();
    const float rate = accounting.RetrieveHashRate();
    if (rate < 2336.0e6f / (maxUs + 1) * 0.999f || rate > 2336.0e6f / minUs * 1.001f)
    {
        std::cerr << "hashrate did not accumulate hashes across different batch sizes\n";
        return 1;
    }

    TestMiner first{0, false};
    TestMiner retry{1, true};
    first.startWorking();
    retry.startWorking();
    Miner::setDagLoadInfo(DAG_LOAD_MODE_SEQUENTIAL);

    WorkPackage work;
    work.header = dev::h256{1u};
    work.startNonce = UINT64_MAX - 0xffff;
    work.nonceRange = 0x10000;
    if (!nonceInRange(work, work.startNonce + 0xffff) || nonceInRange(work, 0))
    {
        std::cerr << "assigned nonce range bounds check failed\n";
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

    retry.pause(PauseDueToFarmPaused);
    retry.resume(PauseDueToFarmPaused);
    if (retry.currentWork())
    {
        std::cerr << "farm reconnect resumed the previous session's work\n";
        return 1;
    }
    retry.setWork(work, false);
    retry.pause(PauseDueToInsufficientMemory, retry.currentWork());
    retry.setWork(work, false);
    if (!retry.pauseTest(PauseDueToInsufficientMemory) || retry.pausedString() != "Insufficient memory")
    {
        std::cerr << "memory failure did not retain its pause reason\n";
        return 1;
    }
    retry.setWork(work, true);
    if (retry.paused() || !retry.currentWork())
    {
        std::cerr << "epoch change did not retry a memory failure\n";
        return 1;
    }

    TestMiner thermal{2, false};
    thermal.pause(PauseDueToOverHeating);
    thermal.updateTemperaturePause(false, 0, 40, 80);
    if (!thermal.pauseTest(PauseDueToOverHeating))
    {
        std::cerr << "failed temperature read resumed an overheated miner\n";
        return 1;
    }
    thermal.updateTemperaturePause(true, 40, 40, 80);
    if (thermal.paused())
    {
        std::cerr << "valid cool temperature did not resume an overheated miner\n";
        return 1;
    }
    thermal.updateTemperaturePause(true, 80, 40, 80);
    if (!thermal.pauseTest(PauseDueToOverHeating))
    {
        std::cerr << "valid hot temperature did not pause a miner\n";
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
