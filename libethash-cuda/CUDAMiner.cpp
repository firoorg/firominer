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

#include <cstring>
#include <fstream>
#include <iostream>

#include <nvrtc.h>

#include <libethcore/Farm.h>
#include <libcrypto/ethash.hpp>
#include <libcrypto/progpow.hpp>

#include "CUDAMiner.h"
#include "CUDAMiner_kernel.h"

using namespace std;
using namespace dev;
using namespace eth;

struct CUDAChannel : public LogChannel
{
    static const char* name() { return EthOrange "cu"; }
    static const int verbosity = 2;
};
#define cudalog clog(CUDAChannel)

CUDAMiner::CUDAMiner(unsigned _index, CUSettings _settings, DeviceDescriptor& _device)
  : Miner("cuda-", _index),
    m_settings(_settings)
{
    m_deviceDescriptor = _device;
}

CUDAMiner::~CUDAMiner()
{
    stopWorking();
    kick_miner();
    cleanup();
}

void CUDAMiner::cleanup() noexcept
{
    if (m_compileThread && m_compileThread->joinable())
        m_compileThread->join();
    m_compileThread.reset();

    if (m_context)
    {
        cuCtxSetCurrent(m_context);
        cudaDeviceSynchronize();
    }
    for (auto& stream : m_streams)
    {
        if (stream)
            cudaStreamDestroy(stream);
        stream = nullptr;
    }
    for (auto& module : m_module)
    {
        if (module)
            cuModuleUnload(module);
        module = nullptr;
    }
    for (auto& buffer : m_search_buf)
    {
        if (buffer)
            cudaFreeHost(const_cast<Search_results*>(buffer));
        buffer = nullptr;
    }
    if (m_device_dag)
        cudaFree(m_device_dag);
    if (m_device_light)
        cudaFree(m_device_light);
    m_device_dag = nullptr;
    m_device_light = nullptr;
    m_allocated_memory_dag = 0;
    m_allocated_memory_light_cache = 0;
    m_kernel[0] = nullptr;
    m_kernel[1] = nullptr;
    m_kernelReady = false;
    m_hasNextProgpowKernel = false;

    if (m_context)
    {
        cuCtxSetCurrent(nullptr);
        cuDevicePrimaryCtxRelease(m_device);
        m_context = nullptr;
    }
}

bool CUDAMiner::initDevice()
{
    cudalog << "Using Pci Id : " << m_deviceDescriptor.uniqueId << " " << m_deviceDescriptor.cuName
            << " (Compute " + m_deviceDescriptor.cuCompute + ") Memory : "
            << dev::getFormattedMemory((double)m_deviceDescriptor.totalMemory);

    // Set Hardware Monitor Info
    m_hwmoninfo.deviceType = HwMonitorInfoType::NVIDIA;
    m_hwmoninfo.devicePciId = m_deviceDescriptor.uniqueId;
    m_hwmoninfo.deviceIndex = -1;  // Will be later on mapped by nvml (see Farm() constructor)

    try
    {
        CU_SAFE_CALL(cuDeviceGet(&m_device, m_deviceDescriptor.cuDeviceIndex));

        try
        {
            CU_SAFE_CALL(cuDevicePrimaryCtxRelease(m_device));
        }
        catch (const cuda_runtime_error& ec)
        {
            (void)ec;
            cudalog << "Releasing a primary context that has not been previously retained will "
                       "fail with CUDA_ERROR_INVALID_CONTEXT, this is normal";
            //            cudalog << " Error : " << ec.what();
        }
        CU_SAFE_CALL(cuDevicePrimaryCtxSetFlags(m_device, m_settings.schedule));
        CU_SAFE_CALL(cuDevicePrimaryCtxRetain(&m_context, m_device));
        CU_SAFE_CALL(cuCtxSetCurrent(m_context));

        // Create mining buffers
        for (unsigned i = 0; i != m_settings.streams; ++i)
        {
            CUDA_SAFE_CALL(cudaMallocHost(&m_search_buf[i], sizeof(Search_results)));
            CUDA_SAFE_CALL(cudaStreamCreateWithFlags(&m_streams[i], cudaStreamNonBlocking));
        }
    }
    catch (const cuda_runtime_error& ec)
    {
        cudalog << "Could not set CUDA device on Pci Id " << m_deviceDescriptor.uniqueId << " Error : " << ec.what();
        cudalog << "Mining aborted on this device.";
        cleanup();
        return false;
    }
    return true;
}

bool CUDAMiner::initEpoch_internal(WorkPackage const& _work)
{
    // If we get here it means epoch has changed so it's not necessary
    // to check again dag sizes. They're changed for sure
    bool retVar = false;
    auto startInit = std::chrono::steady_clock::now();
    size_t RequiredMemory = (m_epochContext->full_dataset_size + m_epochContext->light_cache_size);

    size_t FreeMemory = m_deviceDescriptor.freeMemory;
    FreeMemory += m_allocated_memory_dag;
    FreeMemory += m_allocated_memory_light_cache;

    if (FreeMemory < RequiredMemory)
    {
        cudalog << "Epoch " << m_epochContext->epoch_number << " requires "
                << dev::getFormattedMemory((double)RequiredMemory) << " memory.";
        cudalog << "Only " << dev::getFormattedMemory((double)FreeMemory)
                << " available. Mining suspended on device ...";
        pause(MinerPauseEnum::PauseDueToInsufficientMemory, _work);
        return false;
    }

    try
    {
        // If we have already enough memory allocated, we just have to
        // copy light_cache and regenerate the DAG
        if (m_allocated_memory_dag != m_epochContext->full_dataset_size ||
            m_allocated_memory_light_cache != m_epochContext->light_cache_size)
        {
            // Release previously allocated memory for dag and light
            if (m_device_light)
            {
                auto* light = m_device_light;
                m_device_light = nullptr;
                m_allocated_memory_light_cache = 0;
                CUDA_SAFE_CALL(cudaFree(reinterpret_cast<void*>(light)));
            }
            if (m_device_dag)
            {
                auto* dag = m_device_dag;
                m_device_dag = nullptr;
                m_allocated_memory_dag = 0;
                CUDA_SAFE_CALL(cudaFree(reinterpret_cast<void*>(dag)));
            }

            cudalog << "Generating DAG + Light : " << dev::getFormattedMemory((double)RequiredMemory);

            // create buffer for cache
            CUDA_SAFE_CALL(cudaMalloc(reinterpret_cast<void**>(&m_device_light), m_epochContext->light_cache_size));
            m_allocated_memory_light_cache = m_epochContext->light_cache_size;
            CUDA_SAFE_CALL(cudaMalloc(reinterpret_cast<void**>(&m_device_dag), m_epochContext->full_dataset_size));
            m_allocated_memory_dag = m_epochContext->full_dataset_size;
        }
        else
        {
            cudalog << "Generating DAG + Light (reusing buffers): " << dev::getFormattedMemory((double)RequiredMemory);
        }

        CUDA_SAFE_CALL(cudaMemcpyAsync(reinterpret_cast<void*>(m_device_light), m_epochContext->light_cache,
            m_epochContext->light_cache_size, cudaMemcpyHostToDevice, m_streams[0]));

        ethash_generate_dag(m_device_dag, m_epochContext->full_dataset_size, m_device_light,
            m_epochContext->light_cache_num_items, m_settings.gridSize, m_settings.blockSize, m_streams[0]);

        cudalog << "Generated DAG + Light in "
                << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startInit)
                       .count()
                << " ms. " << dev::getFormattedMemory((double)(m_deviceDescriptor.totalMemory - RequiredMemory))
                << " left.";

        retVar = true;
    }
    catch (const cuda_runtime_error& ec)
    {
        cudalog << "Unexpected error " << ec.what() << " on CUDA device " << m_deviceDescriptor.uniqueId;
        cudalog << "Mining suspended ...";
        pause(MinerPauseEnum::PauseDueToInitEpochError, _work);
    }
    catch (std::runtime_error const& _e)
    {
        cwarn << "Fatal GPU error: " << _e.what();
        cwarn << "Terminating.";
        exit(-1);
    }

    return retVar;
}

void CUDAMiner::workLoop()
{
    WorkPackage current;
    current.header = h256();
    bool nonceRangeExhausted = false;
    uint64_t old_period_seed = -1;
    uint64_t old_dag_elements = 0;
    int old_epoch = -1;

    m_search_buf.resize(m_settings.streams);
    m_streams.resize(m_settings.streams);

    if (!initDevice())
        return;

    try
    {
        auto startCompile = [this](uint64_t period, uint64_t dag_elements) {
            m_kernelReady = false;
            m_compileThread.reset(new std::thread([this, period, dag_elements] {
                try
                {
                    asyncCompile(period, dag_elements);
                }
                catch (const std::exception& ex)
                {
                    cudalog << "Failed to compile ProgPoW kernel : " << ex.what();
                }
            }));
        };

        while (!shouldStop())
        {
            // Wait for work
            bool new_work_expected{true};
            if (!m_new_work.compare_exchange_strong(new_work_expected, false))
            {
                std::unique_lock l(x_work);
                m_new_work_signal.wait_for(l, std::chrono::milliseconds(50));
                continue;
            }

            const WorkPackage w = work();
            if (!w)
            {
                continue;
            }
            if (!w.epochContext || !w.epoch || !w.block)
                continue;
            if (nonceRangeExhausted && current.workGeneration == w.workGeneration)
                continue;
            m_epochContext = w.epochContext;
            if (w.epoch.has_value() && old_epoch != static_cast<int>(w.epoch.value()))
            {
                if (!initEpoch(w))
                    continue;
                old_epoch = static_cast<int>(w.epoch.value());
                if (m_new_work.load())
                {
                    continue;
                }
            }
            uint64_t period_seed = w.block.value() / progpow::kPeriodLength;
            uint64_t dag_elements = m_epochContext->full_dataset_num_items / 2;
            if (!m_hasNextProgpowKernel)
            {
                m_nextProgpowPeriod = period_seed;
                m_nextProgpowDagElements = dag_elements;
                if (m_compileThread)
                {
                    m_compileThread->join();
                }

                startCompile(period_seed, dag_elements);
                m_hasNextProgpowKernel = true;
            }

            if (old_period_seed != period_seed || old_dag_elements != dag_elements)
            {
                if (m_compileThread)
                {
                    m_compileThread->join();
                }

                // sanity check the next kernel
                if (!m_kernelReady || period_seed != m_nextProgpowPeriod ||
                    dag_elements != m_nextProgpowDagElements)
                {
                    // This shouldn't happen!!! Try to recover
                    m_nextProgpowPeriod = period_seed;
                    m_nextProgpowDagElements = dag_elements;
                    startCompile(period_seed, dag_elements);
                    m_compileThread->join();
                }
                if (!m_kernelReady)
                {
                    m_compileThread.reset();
                    m_hasNextProgpowKernel = false;
                    pause(MinerPauseEnum::PauseDueToInitEpochError, w);
                    continue;
                }
                old_period_seed = period_seed;
                old_dag_elements = dag_elements;
                m_kernelExecIx = m_kernelCompIx ^ 1;
                cudalog << "Launching period " << period_seed << " ProgPow kernel";
                m_nextProgpowPeriod = period_seed + 1;
                m_nextProgpowDagElements = dag_elements;
                startCompile(period_seed + 1, dag_elements);
            }

            if (m_new_work.load(std::memory_order_relaxed) ||
                work().workGeneration != w.workGeneration)
                continue;

            // Persist most recent job.
            // Job's differences should be handled at higher level
            current = w;
            uint64_t upper64OfBoundary = (uint64_t)(u64)((u256)w.get_boundary() >> 192);

            // Eventually start searching
            nonceRangeExhausted = search(current.header.data(), upper64OfBoundary, current.startNonce, w);
        }

        cleanup();
    }
    catch (cuda_runtime_error const& _e)
    {
        cleanup();
        string _what = "GPU error: ";
        _what.append(_e.what());
        throw std::runtime_error(_what);
    }
    catch (...)
    {
        cleanup();
        throw;
    }
}

void CUDAMiner::kick_miner()
{
    m_new_work.store(true, std::memory_order_relaxed);
    m_new_work_signal.notify_one();
}

int CUDAMiner::getNumDevices()
{
    int deviceCount;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    if (err == cudaSuccess)
        return deviceCount;

    if (err == cudaErrorInsufficientDriver)
    {
        int driverVersion = 0;
        cudaDriverGetVersion(&driverVersion);
        if (driverVersion == 0)
            std::cerr << "CUDA Error : No CUDA driver found" << std::endl;
        else
            std::cerr << "CUDA Error : Insufficient CUDA driver " << std::to_string(driverVersion) << std::endl;
    }
    else
    {
        std::cerr << "CUDA Error : " << cudaGetErrorString(err) << std::endl;
    }

    return 0;
}

void CUDAMiner::enumDevices(std::map<string, DeviceDescriptor>& _DevicesCollection)
{
    int numDevices = getNumDevices();

    for (int i = 0; i < numDevices; i++)
    {
        string uniqueId;
        ostringstream s;
        DeviceDescriptor deviceDescriptor;
        cudaDeviceProp props;

        try
        {
            CUDA_SAFE_CALL(cudaGetDeviceProperties(&props, i));
            CUDA_SAFE_CALL(cudaSetDevice(i));
            s << setw(2) << setfill('0') << hex << props.pciBusID << ":" << setw(2) << props.pciDeviceID << ".0";
            uniqueId = s.str();

            if (_DevicesCollection.find(uniqueId) != _DevicesCollection.end())
                deviceDescriptor = _DevicesCollection[uniqueId];
            else
                deviceDescriptor = DeviceDescriptor();

            deviceDescriptor.name = string(props.name);
            deviceDescriptor.cuDetected = true;
            deviceDescriptor.uniqueId = uniqueId;
            deviceDescriptor.type = DeviceTypeEnum::Gpu;
            deviceDescriptor.cuDeviceIndex = i;
            deviceDescriptor.cuDeviceOrdinal = i;
            deviceDescriptor.cuName = string(props.name);
            deviceDescriptor.totalMemory = props.totalGlobalMem;
            deviceDescriptor.cuCompute = (to_string(props.major) + "." + to_string(props.minor));
            deviceDescriptor.cuComputeMajor = props.major;
            deviceDescriptor.cuComputeMinor = props.minor;
            CUDA_SAFE_CALL(cudaMemGetInfo(&deviceDescriptor.freeMemory, &deviceDescriptor.totalMemory));
            _DevicesCollection[uniqueId] = deviceDescriptor;
        }
        catch (const cuda_runtime_error& _e)
        {
            std::cerr << _e.what() << std::endl;
        }
    }
}

void CUDAMiner::asyncCompile(uint64_t period, uint64_t dag_elements)
{
    auto saveName = getThreadName();
    setThreadName(name().c_str());

    if (!dropThreadPriority())
        cudalog << "Unable to lower compiler priority.";

    cuCtxSetCurrent(m_context);

    compileKernel(period, dag_elements, m_module[m_kernelCompIx], m_kernel[m_kernelCompIx]);

    setThreadName(saveName.c_str());

    m_kernelCompIx ^= 1;
    m_kernelReady = true;
}

void CUDAMiner::compileKernel(uint64_t period_seed, uint64_t dag_elms, CUmodule& module, CUfunction& kernel)
{
    const char* name = "progpow_search";

    std::string text = progpow::getKern(period_seed, progpow::kernel_type::Cuda);
    text += std::string(CUDAMiner_kernel);

    std::string tmpDir;
#ifdef _WIN32
    if (const char* temp = getenv("TEMP"))
        tmpDir = temp;
    else
        tmpDir = ".";
#else
    tmpDir = "/tmp";
#endif
    tmpDir.append("/kernel.");
    tmpDir.append(std::to_string(Index()));
    tmpDir.append(".cu");
#ifdef DEV_BUILD
    cudalog << "Dumping " << tmpDir;
    ofstream write;
    write.open(tmpDir);
    write << text;
    write.close();
#endif

    nvrtcProgram prog = nullptr;
    CUmodule newModule = nullptr;
    const int deviceArch = m_deviceDescriptor.cuComputeMajor * 10 + m_deviceDescriptor.cuComputeMinor;
    int compileArch = deviceArch;
    try
    {
        NVRTC_SAFE_CALL(nvrtcCreateProgram(&prog,  // prog
            text.c_str(),                          // buffer
            tmpDir.c_str(),                        // name
            0,                                     // numHeaders
            NULL,                                  // headers
            NULL));                                // includeNames

        NVRTC_SAFE_CALL(nvrtcAddNameExpression(prog, name));
#if CUDA_VERSION >= 11020
        int numArchs = 0;
        NVRTC_SAFE_CALL(nvrtcGetNumSupportedArchs(&numArchs));
        std::vector<int> supportedArchs(numArchs);
        NVRTC_SAFE_CALL(nvrtcGetSupportedArchs(supportedArchs.data()));
        compileArch = 0;
        for (int arch : supportedArchs)
            if (arch <= deviceArch)
                compileArch = std::max(compileArch, arch);
        if (!compileArch)
            throw cuda_runtime_error("NVRTC does not support this GPU's compute capability");
#else
#if CUDA_VERSION >= 11010
        constexpr int maxSupportedArch = 86;
#elif CUDA_VERSION >= 11000
        constexpr int maxSupportedArch = 80;
#elif CUDA_VERSION >= 10000
        constexpr int maxSupportedArch = 75;
#elif CUDA_VERSION >= 9020
        constexpr int maxSupportedArch = 72;
#elif CUDA_VERSION >= 9000
        constexpr int maxSupportedArch = 70;
#elif CUDA_VERSION >= 8000
        constexpr int maxSupportedArch = 62;
#else
        constexpr int maxSupportedArch = 53;
#endif
        compileArch = std::min(deviceArch, maxSupportedArch);
#endif
        std::string op_arch = "--gpu-architecture=compute_" + to_string(compileArch);
        std::string op_dag = "-DPROGPOW_DAG_ELEMENTS=" + to_string(dag_elms);

        const char* opts[] = {op_arch.c_str(), op_dag.c_str(), "-lineinfo"};
        nvrtcResult compileResult = nvrtcCompileProgram(prog,  // prog
            sizeof(opts) / sizeof(opts[0]),                    // numOptions
            opts);                                             // options
#ifdef DEV_BUILD
        if (g_logOptions & LOG_COMPILE)
        {
            // Obtain compilation log from the program.
            size_t logSize;
            NVRTC_SAFE_CALL(nvrtcGetProgramLogSize(prog, &logSize));
            std::vector<char> log(logSize);
            NVRTC_SAFE_CALL(nvrtcGetProgramLog(prog, log.data()));
            cudalog << "Compile log: " << log.data();
        }
#endif
        NVRTC_SAFE_CALL(compileResult);
        // Obtain PTX from the program.
        size_t ptxSize;
        NVRTC_SAFE_CALL(nvrtcGetPTXSize(prog, &ptxSize));
        std::vector<char> ptx(ptxSize);
        NVRTC_SAFE_CALL(nvrtcGetPTX(prog, ptx.data()));
        // Load the generated PTX and get a handle to the kernel.
#ifdef DEV_BUILD
        if (g_logOptions & LOG_COMPILE)
        {
            std::vector<char> jitInfo(32 * 1024);
            std::vector<char> jitErr(32 * 1024);
            CUjit_option jitOpt[] = {CU_JIT_INFO_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER, CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES,
                CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES, CU_JIT_LOG_VERBOSE, CU_JIT_GENERATE_LINE_INFO};
            void* jitOptVal[] =
                {jitInfo.data(), jitErr.data(), (void*)(32 * 1024), (void*)(32 * 1024), (void*)(1), (void*)(1)};
            CU_SAFE_CALL(cuModuleLoadDataEx(&newModule, ptx.data(), 6, jitOpt, jitOptVal));
            cudalog << "JIT info: \n" << jitInfo.data();
            cudalog << "JIT err: \n" << jitErr.data();
        }
        else
#endif
        {
            CUjit_option jitOpt[] = {CU_JIT_GENERATE_LINE_INFO};
            void* jitOptVal[] = {(void*)(1)};
            CU_SAFE_CALL(cuModuleLoadDataEx(&newModule, ptx.data(), 1, jitOpt, jitOptVal));
        }
        // Find the mangled name
        const char* mangledName;
        NVRTC_SAFE_CALL(nvrtcGetLoweredName(prog, name, &mangledName));
#ifdef DEV_BUILD
        if (g_logOptions & LOG_COMPILE)
        {
            cudalog << "Mangled name: " << mangledName;
        }
#endif
        CUfunction newKernel;
        CU_SAFE_CALL(cuModuleGetFunction(&newKernel, newModule, mangledName));
        if (module)
            CU_SAFE_CALL(cuModuleUnload(module));
        module = newModule;
        newModule = nullptr;
        kernel = newKernel;

        NVRTC_SAFE_CALL(nvrtcDestroyProgram(&prog));
    }
    catch (...)
    {
        if (newModule)
            cuModuleUnload(newModule);
        if (prog)
            nvrtcDestroyProgram(&prog);
        throw;
    }

    cudalog << "Pre-compiled period " << period_seed << " CUDA ProgPow kernel for compute_" << compileArch;
}

bool CUDAMiner::search(uint8_t const* header, uint64_t target, uint64_t start_nonce, const dev::eth::WorkPackage& w)
{
    hash32_t current_header;
    memcpy(&current_header, header, sizeof(current_header));
    hash64_t* dag = m_device_dag;

    uint32_t active_streams = m_settings.streams;
    if (w.nonceRange)
    {
        if (w.nonceRange < m_settings.blockSize)
        {
            cudalog << "Nonce range exhausted (smaller than a CUDA block), waiting for new work";
            return true;
        }

        active_streams = static_cast<uint32_t>(
            std::min<uint64_t>(active_streams, w.nonceRange / m_settings.blockSize));
    }
    const uint32_t launch_batch_size = gpuBatchSize(m_settings.gridSize * m_settings.blockSize,
        m_settings.blockSize, target, w.nonceRange ? w.nonceRange / active_streams : 0);
    const uint32_t launch_grid_size = launch_batch_size / m_settings.blockSize;
    std::vector<uint64_t> launched_nonce(active_streams);
    std::vector<bool> stream_active(active_streams, true);
    uint64_t next_nonce = start_nonce;
    uint64_t scheduled_hashes = 0;

    auto search_start = std::chrono::steady_clock::now();

    // prime each stream, clear search result buffers and start the search
    uint32_t current_index;
    for (current_index = 0; current_index < active_streams; current_index++)
    {
        cudaStream_t stream = m_streams[current_index];
        volatile Search_results& buffer(*m_search_buf[current_index]);
        buffer.count = 0;
        launched_nonce[current_index] = next_nonce;

        // Run the batch for this stream
        volatile Search_results* Buffer = &buffer;
        bool hack_false = false;
        void* args[] = {&launched_nonce[current_index], &current_header, &target, &dag, &Buffer, &hack_false};
        CU_SAFE_CALL(cuLaunchKernel(m_kernel[m_kernelExecIx],  //
            launch_grid_size, 1, 1,                            // grid dim
            m_settings.blockSize, 1, 1,                        // block dim
            0,                                                 // shared mem
            stream,                                            // stream
            args, 0));                                         // arguments
        next_nonce = wrapNonce(w, next_nonce + launch_batch_size);
        if (w.nonceRange)
            scheduled_hashes += launch_batch_size;
    }

    // Process stream batches until new work arrives or a bounded Stratum
    // nonce range has been exhausted and every in-flight launch has drained.
    uint32_t in_flight_streams = active_streams;
    bool stop_relaunch = false;

    uint32_t gids[MAX_SEARCH_RESULTS];
    h256 mixHashes[MAX_SEARCH_RESULTS];


    while (in_flight_streams)
    {
        stop_relaunch = stop_relaunch || m_new_work.load() || paused();
        uint32_t completed_batches = 0;

        // This inner loop will process each cuda stream individually
        for (current_index = 0; current_index < active_streams; current_index++)
        {
            if (!stream_active[current_index])
                continue;

            // Each pass of this loop will wait for a stream to exit,
            // save any found solutions, then restart the stream
            // on the next group of nonces.
            cudaStream_t stream = m_streams[current_index];
            const uint64_t completed_nonce_base = launched_nonce[current_index];

            // Wait for the stream complete
            CUDA_SAFE_CALL(cudaStreamSynchronize(stream));
            ++completed_batches;

            if (shouldStop())
            {
                m_new_work.store(false, std::memory_order_relaxed);
                stop_relaunch = true;
            }
            stop_relaunch = stop_relaunch || m_new_work.load() || paused();

            // Detect solutions in current stream's solution buffer
            volatile Search_results& buffer(*m_search_buf[current_index]);
            uint32_t found_count = std::min((unsigned)buffer.count, MAX_SEARCH_RESULTS);

            if (found_count)
            {
                buffer.count = 0;

                // Extract solution and pass to higer level
                // using io_service as dispatcher

                for (uint32_t i = 0; i < found_count; i++)
                {
                    gids[i] = buffer.result[i].gid;
                    memcpy(mixHashes[i].data(), (void*)&buffer.result[i].mix, sizeof(buffer.result[i].mix));
                }
            }

            // restart the stream on the next batch of nonces
            // unless we are done for this round.
            if (!stop_relaunch && (!w.nonceRange || scheduled_hashes < w.nonceRange))
            {
                launched_nonce[current_index] = next_nonce;
                volatile Search_results* Buffer = &buffer;
                bool hack_false = false;
                void* args[] =
                    {&launched_nonce[current_index], &current_header, &target, &dag, &Buffer, &hack_false};
                CU_SAFE_CALL(cuLaunchKernel(m_kernel[m_kernelExecIx],  //
                    launch_grid_size, 1, 1,                            // grid dim
                    m_settings.blockSize, 1, 1,                        // block dim
                    0,                                                 // shared mem
                    stream,                                            // stream
                    args, 0));                                         // arguments
                next_nonce = wrapNonce(w, next_nonce + launch_batch_size);
                if (w.nonceRange)
                    scheduled_hashes += launch_batch_size;
            }
            else
            {
                stream_active[current_index] = false;
                --in_flight_streams;
            }
            if (found_count)
            {
                for (uint32_t i = 0; i < found_count; i++)
                {
                    uint64_t nonce = completed_nonce_base + gids[i];
                    Farm::f().submitProof(Solution{nonce, mixHashes[i], w, std::chrono::steady_clock::now(), m_index});

                    double d = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - search_start)
                                   .count();

                    cudalog << EthWhite << "Job: " << w.header.abridged() << " Sol: 0x" << toHex(nonce)
                            << EthLime " found in " << dev::getFormattedElapsed(d) << EthReset;
                }
            }
        }

        // Update the hash rate
        updateHashRate(launch_batch_size, completed_batches);

        // Bail out if it's shutdown time
        if (shouldStop())
        {
            m_new_work.store(false, std::memory_order_relaxed);
            break;
        }
    }

    const bool exhausted = !stop_relaunch && !shouldStop() && w.nonceRange && scheduled_hashes >= w.nonceRange;
    if (exhausted)
        cudalog << "Nonce range exhausted, waiting for new work";

#ifdef DEV_BUILD
    // Optionally log job switch time
    if (!shouldStop() && (g_logOptions & LOG_SWITCH))
        cudalog << "Switch time: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - m_workSwitchStart)
                       .count()
                << " ms.";
#endif
    return exhausted;
}
