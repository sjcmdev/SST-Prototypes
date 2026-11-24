cmake_minimum_required(VERSION 3.25)
project(JFMGpuTests LANGUAGES CXX CUDA)
if(NOT MSVC)
    message(FATAL_ERROR "JFM GPU tests require MSVC and CUDA")
endif()
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(SOLVER "${ROOT}/Prototypes/JFM/src/Analysis/Solver")
set(LIBRARIES "${ROOT}/../SST-Libraries")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/Vendor/GoogleTest" googletest EXCLUDE_FROM_ALL)
add_executable(JFMGpuTests ../JFMGpuBatchTests.cpp
    "${ROOT}/Prototypes/JFM/src/Analysis/JFMFitEngine.cpp"
    "${ROOT}/Core/JobSystem/JobContext.cpp"
    "${SOLVER}/gpu/gpu_backend.cu" "${SOLVER}/solver/objective.cpp" "${SOLVER}/solver/diode_objective.cpp")
target_include_directories(JFMGpuTests PRIVATE "${ROOT}" "${ROOT}/Core" "${ROOT}/Prototypes/JFM/src"
    "${SOLVER}" "${SOLVER}/gpu" "${SOLVER}/lambertw-gpu"
    "${LIBRARIES}/SST-Math/include" "${LIBRARIES}/SST-Physics/include")
target_compile_definitions(JFMGpuTests PRIVATE JFM_CUDA_AVAILABLE=1 NOMINMAX)
target_compile_options(JFMGpuTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/fp:strict;/utf-8;/MP6>"
    "$<$<COMPILE_LANGUAGE:CUDA>:--fmad=false;-allow-unsupported-compiler;-Xcompiler=/fp:strict;-Xcompiler=/utf-8>")
target_link_libraries(JFMGpuTests PRIVATE gtest_main
    "${LIBRARIES}/build/bin/$<CONFIG>-windows-x86_64/SST-Math/SST-Math.lib")
enable_testing()
add_test(NAME JFMGpuBatch COMMAND JFMGpuTests)
add_test(NAME JFMGpuInvalidDevice COMMAND JFMGpuTests --gtest_filter=JFMGpuBatch.ExplicitDeviceSelection)
set_tests_properties(JFMGpuInvalidDevice PROPERTIES ENVIRONMENT "JFM_CUDA_DEVICE=2147483647;JFM_EXPECT_NO_GPU=1")
add_test(NAME JFMGpuMalformedDevice COMMAND JFMGpuTests --gtest_filter=JFMGpuBatch.ExplicitDeviceSelection)
set_tests_properties(JFMGpuMalformedDevice PROPERTIES ENVIRONMENT "JFM_CUDA_DEVICE=-1;JFM_EXPECT_NO_GPU=1")
