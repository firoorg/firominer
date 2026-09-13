# Compile the actual generated kernels without requiring an OpenCL runtime or GPU.
# This catches compiler/type errors; it does not verify GPU hashes or performance.
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(period IN ITEMS 1 1205100 4294967297)
    if(period EQUAL 1)
        set(epoch 0)
    else()
        set(epoch 650)
    endif()
    foreach(group IN ITEMS 64 128 256)
        foreach(mode IN ITEMS legacy inline)
            set(source "${OUTPUT_DIR}/kernel-${period}-${group}-${mode}.cl")
            execute_process(COMMAND "${GENERATOR}" "${period}" "${epoch}" "${group}" "${mode}"
                "${KERNEL_SOURCE}" OUTPUT_FILE "${source}" RESULT_VARIABLE result)
            if(NOT result STREQUAL "0")
                message(FATAL_ERROR "OpenCL kernel generation failed: ${source}")
            endif()
            execute_process(COMMAND "${CLANG}" -x cl -cl-std=CL1.2 -fgnu89-inline
                -Werror=incompatible-pointer-types -O2 -S -emit-llvm "${source}" -o "${source}.ll"
                RESULT_VARIABLE result ERROR_VARIABLE errors)
            if(NOT result STREQUAL "0")
                message(FATAL_ERROR "OpenCL kernel compilation failed: ${source}\n${errors}")
            endif()
        endforeach()
    endforeach()
endforeach()
message(STATUS "Compiled 18 OpenCL kernels (legacy and inline mix); GPU execution was not tested")
