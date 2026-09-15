#pragma once

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

namespace dev
{
inline std::string pciId(unsigned bus, unsigned device, unsigned function, unsigned domain = 0)
{
    std::ostringstream id;
    id << std::hex << std::setfill('0');
    // Preserve existing IDs on domain zero, but distinguish other PCI domains.
    if (domain)
        id << std::setw(4) << domain << ':';
    id << std::setw(2) << bus << ':' << std::setw(2) << device << '.' << function;
    return id.str();
}

// CUDA returns a full hexadecimal domain:bus:device.function address.
inline std::string pciId(const char* address)
{
    unsigned domain, bus, device, function;
    char trailing;
    if (std::sscanf(address, "%8x:%2x:%2x.%1x%c", &domain, &bus, &device, &function, &trailing) != 4 ||
        device > 0x1f || function > 7)
        return {};
    return pciId(bus, device, function, domain);
}
}
