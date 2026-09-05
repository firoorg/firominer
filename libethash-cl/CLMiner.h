/// OpenCL miner implementation.
///
/// @file
/// @copyright GNU General Public License

#pragma once

#include <fstream>

#include <libdevcore/Worker.h>
#include <libethcore/Miner.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>

#pragma GCC diagnostic push
#if __GNUC__ >= 6
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif
#pragma GCC diagnostic ignored "-Wmissing-braces"
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS true
#define CL_HPP_ENABLE_EXCEPTIONS true
#define CL_HPP_CL_1_2_DEFAULT_BUILD true
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#include "CL/cl2.hpp"
#pragma GCC diagnostic pop

// macOS OpenCL fix:
#ifndef CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV
#define CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV 0x4000
#endif

#ifndef CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV
#define CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV 0x4001
#endif

namespace dev
{
namespace eth
{
class CLMiner : public Miner
{
public:

    CLMiner(unsigned _index, CLSettings _settings, DeviceDescriptor& _device);
    ~CLMiner() override;

    static void enumDevices(std::map<std::string, DeviceDescriptor>& _DevicesCollection);

protected:
    bool initDevice() override;

    bool initEpoch_internal(WorkPackage const& _work) override;

    void kick_miner() noexcept override;

private:
    
    void workLoop() override;
    bool compileKernel(uint64_t prog_seed, std::shared_ptr<ethash::epoch_context> const& epochContext,
        cl::Program& program, cl::Kernel& searchKernel);
    void asyncCompile(uint64_t period, std::shared_ptr<ethash::epoch_context> epochContext);
    void cleanup() noexcept;

    cl::Context m_context;
    cl::CommandQueue m_queue;
    cl::CommandQueue m_abortqueue;
    cl::Kernel m_searchKernel;
    cl::Kernel m_nextSearchKernel;
    cl::Kernel m_dagKernel;
    cl::Device m_device;
    cl::Buffer m_header;
    cl::Buffer m_searchBuffer;

    cl::Buffer* m_dag = nullptr;
    cl::Buffer* m_light = nullptr;

    CLSettings m_settings;
    std::mutex m_abortMutex;
    bool m_hasNextProgpowKernel = false;
    bool m_kernelReady = false;
    uint32_t m_nextProgpowEpoch = 0;

    unsigned m_dagItems = 0;

    cl::Program m_program;
    cl::Program m_nextProgram;
    char m_options[256] = {0};
    int m_computeCapability = 0;

    std::atomic<bool> m_kickEnabled = {false};

};

}  // namespace eth
}  // namespace dev
