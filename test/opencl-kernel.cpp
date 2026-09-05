#include <libcrypto/progpow.hpp>

#include <fstream>
#include <iostream>
#include <string>

// Emit the complete generated kernel for an offline OpenCL compiler check.
int main(int argc, char** argv)
{
    if (argc != 6)
        return 1;
    const auto period = std::stoull(argv[1]);
    const auto epoch = std::stoi(argv[2]);
    std::ifstream kernel(argv[5]);
    if (!kernel)
        return 1;
    std::cout << "#define GROUP_SIZE " << argv[3] << '\n'
              << "#define ACCESSES 64\n#define MAX_OUTPUTS 15\n#define PLATFORM 0\n#define COMPUTE 0\n"
              << "#define LIGHT_WORDS " << ethash::calculate_light_cache_num_items(epoch) << '\n'
              << "#define DAG_NODES " << ethash::calculate_full_dataset_num_items(epoch) * 2 << '\n'
              << "#define PROGPOW_DAG_ELEMENTS " << ethash::calculate_full_dataset_num_items(epoch) / 2
              << '\n';
    if (std::string(argv[4]) == "inline")
        std::cout << "#define FIROPOW_CL_INLINE_MIX 1\n";
    std::cout << progpow::getKern(period, progpow::kernel_type::OpenCL) << kernel.rdbuf();
    return std::cout ? 0 : 1;
}
