/*
This file is part of firominer.

firominer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

firominer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with firominer.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <libdevcore/Worker.h>
#include <libethcore/Miner.h>
#include <cuda.h>
#include "CUDAMiner_cuda.h"

#include <functional>

namespace dev
{
namespace eth
{
class CUDAMiner : public Miner
{
public:
    CUDAMiner(unsigned _index, CUSettings _settings, DeviceDescriptor& _device);
    ~CUDAMiner() override;

    static int getNumDevices();
    static void enumDevices(std::map<std::string, DeviceDescriptor>& _DevicesCollection);

    void search(
        uint8_t const* header, uint64_t target, uint64_t _startN, const dev::eth::WorkPackage& w);

protected:
    bool initDevice() override;

    bool initEpoch_internal(WorkPackage const& _work) override;

    void kick_miner() override;

private:
    std::atomic<bool> m_new_work = {false};

    void workLoop() override;

    uint8_t m_kernelCompIx = 0;
    uint8_t m_kernelExecIx = 1;
    CUmodule m_module[2] = {};
    CUfunction m_kernel[2] = {};
    std::vector<volatile Search_results*> m_search_buf;
    std::vector<cudaStream_t> m_streams;
    bool m_hasNextProgpowKernel = false;
    bool m_kernelReady = false;
    uint64_t m_nextProgpowDagElements = 0;

    CUSettings m_settings;

    uint64_t m_allocated_memory_dag = 0; // dag_size is a uint64_t in EpochContext struct
    size_t m_allocated_memory_light_cache = 0;

    void compileKernel(uint64_t prog_seed, uint64_t dag_words, CUmodule& module, CUfunction& kernel);
    void asyncCompile(uint64_t period, uint64_t dag_elements);
    void cleanup() noexcept;

    CUcontext m_context = nullptr;
    CUdevice m_device = 0;

    hash64_t* m_device_dag = nullptr;
    hash64_t* m_device_light = nullptr;
};


}  // namespace eth
}  // namespace dev
