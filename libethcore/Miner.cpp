/*
 This file is part of ethereum.

 firominer is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ethereum is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with firominer.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Miner.h"

#include <libcrypto/progpow.hpp>

namespace dev::eth
{
unsigned Miner::s_dagLoadMode = 0;
std::mutex Miner::s_dagLoadMutex;

FarmFace* FarmFace::m_this = nullptr;

ethash::VerificationResult verifyProgpow(Solution const& solution)
{
    auto context = solution.work.epochContext;
    if (!context)
        context = ethash::get_epoch_context(solution.work.epoch.value(), false);
    return progpow::verify_full(*context, solution.work.block.value() / progpow::kPeriodLength,
        ethash::from_bytes(solution.work.header.data()), ethash::from_bytes(solution.mixHash.data()), solution.nonce,
        ethash::from_bytes(solution.work.get_boundary().data()));
}

DeviceDescriptor Miner::getDescriptor()
{
    return m_deviceDescriptor;
}

void Miner::setWork(WorkPackage const& _work, bool _retryInsufficientMemory)
{
    {
        std::scoped_lock l(x_work, x_pause);

        m_pauseFlags.reset(MinerPauseEnum::PauseDueToInitEpochError);
        if (_retryInsufficientMemory)
            m_pauseFlags.reset(MinerPauseEnum::PauseDueToInsufficientMemory);
        ++m_workGeneration;

        // Retain the newest job while paused so it can be resumed immediately.
        m_work = _work;
        m_work.workGeneration = m_workGeneration;

#ifdef DEV_BUILD
        m_workSwitchStart = std::chrono::steady_clock::now();
#endif
    }

    kick_miner();
}

void Miner::pause(MinerPauseEnum what)
{
    {
        std::scoped_lock l(x_work, x_pause);
        m_pauseFlags.set(what);
        if (what == PauseDueToFarmPaused)
        {
            m_work = {};
            m_work.workGeneration = ++m_workGeneration;
        }
    }
    kick_miner();
}

void Miner::pause(MinerPauseEnum what, WorkPackage const& _work)
{
    {
        std::scoped_lock l(x_work, x_pause);
        if (m_work.workGeneration != _work.workGeneration)
            return;
        m_pauseFlags.set(what);
        if (what == PauseDueToFarmPaused)
        {
            m_work = {};
            m_work.workGeneration = ++m_workGeneration;
        }
    }
    kick_miner();
}

bool Miner::paused()
{
    std::scoped_lock l(x_pause);
    return m_pauseFlags.any();
}

bool Miner::pauseTest(MinerPauseEnum what)
{
    std::scoped_lock l(x_pause);
    return m_pauseFlags.test(what);
}

std::string Miner::pausedString()
{
    std::scoped_lock l(x_pause);
    std::string retVar;
    if (m_pauseFlags.any())
    {
        for (int i = 0; i < MinerPauseEnum::Pause_MAX; i++)
        {
            if (m_pauseFlags[(MinerPauseEnum)i])
            {
                if (!retVar.empty())
                    retVar.append("; ");

                if (i == MinerPauseEnum::PauseDueToOverHeating)
                    retVar.append("Overheating");
                else if (i == MinerPauseEnum::PauseDueToAPIRequest)
                    retVar.append("Api request");
                else if (i == MinerPauseEnum::PauseDueToFarmPaused)
                    retVar.append("Farm suspended");
                else if (i == MinerPauseEnum::PauseDueToInsufficientMemory)
                    retVar.append("Insufficient memory");
                else if (i == MinerPauseEnum::PauseDueToInitEpochError)
                    retVar.append("Epoch initialization error");
            }
        }
    }
    return retVar;
}

void Miner::resume(MinerPauseEnum fromwhat)
{
    bool resumed;
    {
        std::scoped_lock l(x_pause);
        m_pauseFlags.reset(fromwhat);
        resumed = !m_pauseFlags.any();
    }
    if (resumed)
        kick_miner();
}

float Miner::RetrieveHashRate() noexcept
{
    return m_hashRate.load(std::memory_order_relaxed);
}

void Miner::TriggerHashRateUpdate() noexcept
{
    bool b = false;
    if (m_hashRateUpdate.compare_exchange_strong(b, true, std::memory_order_relaxed))
        return;
    // GPU didn't respond to last trigger, assume it's dead.
    // This can happen on CUDA if:
    //   runtime of --cuda-grid-size * --cuda-streams exceeds time of m_collectInterval
    m_hashRate = 0.0;
}

bool Miner::initEpoch(WorkPackage const& _work)
{
    auto initialize = [&] {
        return !shouldStop() && work().workGeneration == _work.workGeneration && initEpoch_internal(_work);
    };
    if (s_dagLoadMode != DAG_LOAD_MODE_SEQUENTIAL)
        return initialize();

    std::scoped_lock lock(s_dagLoadMutex);
    return initialize();
}

WorkPackage Miner::work() const
{
    std::scoped_lock l(x_work, x_pause);
    return m_pauseFlags.any() ? WorkPackage{} : m_work;
}

void Miner::updateHashRate(uint32_t _groupSize, uint32_t _increment) noexcept
{
    m_hashCount += uint64_t{_groupSize} * _increment;
    bool b = true;
    if (!m_hashRateUpdate.compare_exchange_strong(b, false, std::memory_order_relaxed))
        return;
    using namespace std::chrono;
    auto t = steady_clock::now();
    auto us = duration_cast<microseconds>(t - m_hashTime).count();
    m_hashTime = t;

    m_hashRate.store(
        us ? (float(m_hashCount) * 1.0e6f) / us : 0.0f, std::memory_order_relaxed);
    m_hashCount = 0;
}

bool Miner::dropThreadPriority()
{
#if defined(__linux__)
    // Non Posix hack to lower compile thread's priority. Under POSIX
    // the nice value is a process attribute, under Linux it's a thread
    // attribute
    return nice(5) != -1;
#elif defined(WIN32)
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#else
    return false;
#endif
}

}  // namespace dev::eth
