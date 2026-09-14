#include <chrono>
#include <iostream>
#include <map>
#include <string>

#include <boost/asio.hpp>

#include <libcrypto/ethash.hpp>
#include <libpoolprotocols/testing/SimulateClient.h>

boost::asio::io_service g_io_service;
bool g_exitOnError = false;

int main(int argc, char* argv[])
{
    const bool noEval = argc == 2 && std::string{argv[1]} == "--noeval";
    if (argc > 2 || (argc == 2 && !noEval))
    {
        std::cerr << "usage: simulation-verification-test [--noeval]\n";
        return 2;
    }

    std::map<std::string, dev::eth::DeviceDescriptor> devices;
    dev::eth::FarmSettings settings;
    settings.noEval = noEval;
    dev::eth::Farm farm(devices, settings, {}, {}, {});
    SimulateClient client(0, 1.0f, "mainnet");

    dev::eth::WorkPackage work;
    work.algo = "ethash";
    work.block.emplace(0);
    dev::eth::Solution solution{0, {}, work, std::chrono::steady_clock::now(), 0};

    while (ethash::verify_light(ethash::from_bytes(solution.work.header.data()),
        ethash::from_bytes(solution.mixHash.data()), solution.nonce,
        ethash::from_bytes(solution.work.get_boundary().data())))
    {
        ++solution.nonce;
    }

    bool accepted = false;
    bool rejected = false;
    std::chrono::milliseconds responseDelay{-1};
    client.onSolutionAccepted([&](std::chrono::milliseconds const& delay, unsigned const&, bool) {
        accepted = true;
        responseDelay = delay;
    });
    client.onSolutionRejected([&](std::chrono::milliseconds const& delay, unsigned const&) {
        rejected = true;
        responseDelay = delay;
    });
    client.submitSolution(solution);

    const bool expectedAccepted = !noEval;
    if (accepted != expectedAccepted || rejected == expectedAccepted ||
        (!noEval && responseDelay != std::chrono::milliseconds{0}))
    {
        std::cerr << "simulation verification did not follow the Farm evaluation setting\n";
        return 1;
    }
}
