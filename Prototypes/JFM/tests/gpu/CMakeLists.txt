cmake_minimum_required(VERSION 3.24)
project(JFMGpuOrganization LANGUAGES CXX CUDA)
if(NOT MSVC)
    message(FATAL_ERROR "JFM GPU checks require MSVC")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CUDA_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(SOLVER "${REPO}/Prototypes/JFM/src/Analysis/Solver")
set(LIB "${REPO}/../SST-Libraries")
set(PRE_PORT_KERNEL "${CMAKE_CURRENT_SOURCE_DIR}/pre-port-kernel.cuh" CACHE FILEPATH "Captured pre-port kernel")
if(NOT EXISTS "${PRE_PORT_KERNEL}")
    message(FATAL_ERROR "Set PRE_PORT_KERNEL to the kernel snapshot captured before editing")
endif()
file(TO_CMAKE_PATH "${PRE_PORT_KERNEL}" PRE_PORT_KERNEL)
add_executable(jfm_gpu_organization organization.cu
    "${SOLVER}/solver/diode_objective.cpp" "${SOLVER}/solver/objective.cpp"
    "${LIB}/SST-Math/src/nelder_mead.cpp" "${LIB}/SST-Math/src/param_utils.cpp"
    "${LIB}/SST-Math/src/lambert_w.cpp" "${LIB}/SST-Physics/src/iv.cpp")
target_include_directories(jfm_gpu_organization PRIVATE "${SOLVER}" "${SOLVER}/gpu" "${SOLVER}/lambertw-gpu"
    "${REPO}/Vendor/json/include" "${LIB}/SST-Math/include" "${LIB}/SST-Physics/include"
    "${LIB}/Vendor/boost-math/include")
target_compile_definitions(jfm_gpu_organization PRIVATE PRE_PORT_KERNEL="${PRE_PORT_KERNEL}")
target_compile_options(jfm_gpu_organization PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--fmad=false;-allow-unsupported-compiler;-Xcompiler=/fp:strict>
    $<$<COMPILE_LANGUAGE:CXX>:/fp:strict;/utf-8>)
