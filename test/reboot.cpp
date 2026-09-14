#include <libethcore/Farm.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#endif

boost::asio::io_context g_io_service;
bool g_exitOnError = false;

int main()
{
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;
    const auto directory = fs::path(boost::dll::program_location().string()).parent_path();
#ifdef _WIN32
    const auto script = directory / "reboot.bat";
#else
    const auto script = directory / "reboot.sh";
#endif
    const auto marker = directory / "reboot-marker";
    const auto pidFile = directory / "reboot-pid";
    try
    {
        if (fs::exists(script) || fs::exists(marker) || fs::exists(pidFile))
            throw std::runtime_error("Reboot fixture directory is not empty");
        {
            std::ofstream output(script);
#ifdef _WIN32
            output << "@echo off\n"
                      "ping.exe 127.0.0.1 -n 2 >nul\n"
                      ">\"%~dp0reboot-marker\" echo %~1\n"
                      ">>\"%~dp0reboot-marker\" echo %~2\n";
#else
            output << "#!/bin/sh\n"
                      "printf '%s\\n' \"$$\" > \"$(dirname \"$0\")/reboot-pid\"\n"
                      "sleep 0.2\n"
                      "printf '%s\\n' \"$1\" \"$2\" > \"$(dirname \"$0\")/reboot-marker\"\n";
#endif
            if (!output)
                throw std::runtime_error("Could not write reboot script");
        }
#ifndef _WIN32
        fs::permissions(script, fs::perms::owner_exec, fs::perm_options::add);
#endif
        std::map<std::string, dev::eth::DeviceDescriptor> devices;
        dev::eth::Farm farm(devices, {}, {}, {}, {});
        bool responsive = false;
        boost::asio::post(g_io_service, [&]() { responsive = !fs::exists(marker); });
        if (!farm.reboot({"api_miner_reboot", "argument with spaces"}))
            throw std::runtime_error("Reboot script did not launch");
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        bool finished = false;
        while (std::chrono::steady_clock::now() < deadline)
        {
            g_io_service.poll();
            if (fs::exists(marker))
            {
#ifdef _WIN32
                std::ifstream input(marker);
                std::string first, second;
                std::getline(input, first);
                std::getline(input, second);
                finished = first == "api_miner_reboot" && second == "argument with spaces";
#else
                pid_t pid = 0;
                std::ifstream(pidFile) >> pid;
                if (pid > 0 && kill(pid, 0) == -1 && errno == ESRCH)
                {
                    int status = 0;
                    finished = waitpid(pid, &status, WNOHANG) == -1 && errno == ECHILD;
                }
#endif
                if (finished)
                    break;
            }
            std::this_thread::sleep_for(1ms);
        }
        std::ifstream input(marker);
        std::string first, second;
        std::getline(input, first);
        std::getline(input, second);
        if (!responsive || !finished || first != "api_miner_reboot" || second != "argument with spaces")
            throw std::runtime_error("Reboot script blocked, lost arguments, or was not reaped");
        input.close();
        fs::remove(script);
        fs::remove(marker);
        fs::remove(pidFile);
        std::cout << "Reboot script completed without blocking the event loop\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
