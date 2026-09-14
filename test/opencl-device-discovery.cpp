#define CL_TARGET_OPENCL_VERSION 120
#include <libethash-cl/CLDevice.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

struct _cl_device_id
{
    bool nvidia = false;
    bool amd = false;
    bool standard = false;
    bool failSlot = false;
    cl_uint topologyType = 1;
    cl_uint bus = 0x81, slot = 2, function = 0, domain = 0;
};

// Test the production query/fallback logic without an installed OpenCL driver.
extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetDeviceInfo(cl_device_id device,
    cl_device_info param, size_t size, void* value, size_t* actualSize)
{
    const auto copy = [=](const void* data, size_t bytes) {
        if (actualSize)
            *actualSize = bytes;
        if (!value || size < bytes)
            return CL_INVALID_VALUE;
        std::memcpy(value, data, bytes);
        return CL_SUCCESS;
    };
    if (device->nvidia && param == 0x4008)
        return copy(&device->bus, sizeof(cl_uint));
    if (device->nvidia && param == 0x4009 && !device->failSlot)
    {
        const cl_uint slot = (device->slot << 3) | device->function;
        return copy(&slot, sizeof(slot));
    }
    if (device->amd && param == 0x4037)
    {
        cl_uchar topology[24]{};
        std::memcpy(topology, &device->topologyType, sizeof(cl_uint));
        topology[21] = static_cast<cl_uchar>(device->bus);
        topology[22] = static_cast<cl_uchar>(device->slot);
        topology[23] = static_cast<cl_uchar>(device->function);
        return copy(topology, sizeof(topology));
    }
#ifdef CL_DEVICE_PCI_BUS_INFO_KHR
    if (device->standard && param == CL_DEVICE_PCI_BUS_INFO_KHR)
    {
        const cl_device_pci_bus_info_khr pci{
            device->domain, device->bus, device->slot, device->function};
        return copy(&pci, sizeof(pci));
    }
#endif
    return CL_INVALID_VALUE;
}

int main()
{
    using namespace dev::eth;
    const auto check = [](bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    check(isSupportedOpenCLGpu(CL_DEVICE_TYPE_GPU, 0x1002), "AMD GPU rejected");
    check(isSupportedOpenCLGpu(CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_DEFAULT, 0x10de),
        "NVIDIA GPU bitmask rejected");
    check(!isSupportedOpenCLGpu(CL_DEVICE_TYPE_CPU, 0x1002), "CPU accepted as GPU");
    check(!isSupportedOpenCLGpu(CL_DEVICE_TYPE_GPU, 0x8086), "Intel support enabled unintentionally");

    _cl_device_id device;
    device.nvidia = true;
    device.function = 3;
    check(openclDeviceId(&device, 0x10de, 0, 0) == "81:02.3", "NVIDIA PCI identity changed");
    device.failSlot = true;
    check(openclDeviceId(&device, 0x10de, 0, 0) == "CL:0:0", "Partial PCI query accepted");
    device.nvidia = false;
    device.amd = true;
    check(openclDeviceId(&device, 0x1002, 0, 0) == "81:02.3", "AMD high PCI bus sign extended");
    device.topologyType = 0;
    check(openclDeviceId(&device, 0x1002, 0, 0) == "CL:0:0", "Non-PCI topology accepted");
    device.amd = false;
#ifdef CL_DEVICE_PCI_BUS_INFO_KHR
    device.standard = true;
    check(openclDeviceId(&device, 0x1002, 0, 0) == "81:02.3", "Standard AMD PCI query failed");
    check(openclDeviceId(&device, 0x10de, 0, 0) == "81:02.3", "Standard NVIDIA PCI query failed");
    device.domain = 1;
    check(openclDeviceId(&device, 0x1002, 0, 0) == "0001:81:02.3", "PCI domains collapsed");
    device.standard = false;
#endif
    check(openclDeviceId(&device, 0x1002, 1, 0) == "CL:1:0", "First fallback identity incorrect");
    check(openclDeviceId(&device, 0x1002, 1, 1) == "CL:1:1", "Devices without PCI info collapsed");
    check(openclDeviceId(&device, 0x10de, 2, 0) == "CL:2:0", "Platforms without PCI info collapsed");
    std::cout << "OpenCL vendor and PCI discovery checks passed\n";
}
