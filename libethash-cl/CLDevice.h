#pragma once

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#include <CL/cl_ext.h>
#endif

#include <libdevcore/PciId.h>

namespace dev::eth
{
inline bool isSupportedOpenCLGpu(cl_device_type type, cl_uint vendor)
{
    return (type & CL_DEVICE_TYPE_GPU) && (vendor == 0x1002 || vendor == 0x10de);
}

inline std::string openclDeviceId(cl_device_id device, cl_uint vendor,
    unsigned platformIndex, unsigned deviceIndex)
{
#ifdef CL_DEVICE_PCI_BUS_INFO_KHR
    // Prefer the complete address: legacy vendor queries do not report the domain.
    cl_device_pci_bus_info_khr pci{};
    if (clGetDeviceInfo(device, CL_DEVICE_PCI_BUS_INFO_KHR, sizeof(pci), &pci, nullptr) == CL_SUCCESS)
        return pciId(pci.pci_bus, pci.pci_device, pci.pci_function, pci.pci_domain);
#endif

    // Retain the PCI IDs shared with CUDA and hardware monitoring.
    if (vendor == 0x10de)
    {
        cl_uint bus = 0, slot = 0;
        if (clGetDeviceInfo(device, 0x4008, sizeof(bus), &bus, nullptr) == CL_SUCCESS &&
            clGetDeviceInfo(device, 0x4009, sizeof(slot), &slot, nullptr) == CL_SUCCESS)
            return pciId(bus, slot >> 3, slot & 7);
    }
    else if (vendor == 0x1002)
    {
        // cl_device_topology_amd has a 32-bit type followed by PCI topology bytes.
        union
        {
            cl_uint type;
            cl_uchar bytes[24];
        } topology{};
        if (clGetDeviceInfo(device, 0x4037, sizeof(topology), &topology, nullptr) == CL_SUCCESS &&
            topology.type == 1) // CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD
            return pciId(topology.bytes[21], topology.bytes[22], topology.bytes[23]);
    }

    // Optional PCI queries must not collapse several devices into an empty map key.
    return "CL:" + std::to_string(platformIndex) + ':' + std::to_string(deviceIndex);
}
}
