#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <windows.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

static int platformNames(decltype(&clGetPlatformIDs) getIDs,
    decltype(&clGetPlatformInfo) getInfo, const char* loader, std::vector<std::string>& names)
{
    const auto check = [loader](cl_int error, const char* operation) {
        if (error == CL_SUCCESS)
            return true;
        std::cerr << loader << ": " << operation << " failed: " << error << '\n';
        return false;
    };
    cl_uint count = 0;
    const auto error = getIDs(0, nullptr, &count);
    if (error == CL_PLATFORM_NOT_FOUND_KHR || (error == CL_SUCCESS && count == 0))
    {
        std::cout << loader << ": no OpenCL platforms\n";
        return 77;
    }
    if (!check(error, "clGetPlatformIDs(count)"))
        return 1;
    std::vector<cl_platform_id> platforms(count);
    if (!check(getIDs(count, platforms.data(), nullptr), "clGetPlatformIDs(list)"))
        return 1;
    for (const auto platform : platforms)
    {
        size_t size = 0;
        if (!check(getInfo(platform, CL_PLATFORM_NAME, 0, nullptr, &size), "platform name size"))
            return 1;
        if (size == 0)
        {
            std::cerr << loader << ": empty platform name buffer\n";
            return 1;
        }
        std::string name(size, '\0');
        if (!check(getInfo(platform, CL_PLATFORM_NAME, size, &name[0], nullptr), "platform name"))
            return 1;
        name.pop_back(); // CL_PLATFORM_NAME includes its terminating null character.
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return 0;
}

int main()
{
    // Keep the system loader loaded until process exit while comparing both loaders.
    const auto system = LoadLibraryExW(L"OpenCL.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system)
    {
        const auto error = GetLastError();
        std::cerr << "Cannot load System32 OpenCL.dll: " << error << '\n';
        return error == ERROR_MOD_NOT_FOUND ? 77 : 1;
    }
    const auto getIDs = reinterpret_cast<decltype(&clGetPlatformIDs)>(
        GetProcAddress(system, "clGetPlatformIDs"));
    const auto getInfo = reinterpret_cast<decltype(&clGetPlatformInfo)>(
        GetProcAddress(system, "clGetPlatformInfo"));
    if (!getIDs || !getInfo)
    {
        std::cerr << "System32 OpenCL.dll is missing platform entry points\n";
        return 1;
    }

    std::vector<std::string> expected, actual;
    const auto result = platformNames(getIDs, getInfo, "System loader", expected);
    if (result != 0)
        return result;
    if (platformNames(&clGetPlatformIDs, &clGetPlatformInfo, "Linked loader", actual) != 0)
        return 1;
    if (actual != expected)
    {
        std::cerr << "OpenCL platform discovery differs:\n";
        for (const auto& name : expected)
            std::cerr << "  System: " << name << '\n';
        for (const auto& name : actual)
            std::cerr << "  Linked: " << name << '\n';
        return 1;
    }
    std::cout << "OpenCL loaders agree on " << actual.size() << " platform(s)\n";
    return 0;
}
