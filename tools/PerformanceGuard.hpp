#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>
#define NOMINMAX
#include <Windows.h>
#undef GetJob
#undef interface
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace perfguard {
using Clock = std::chrono::steady_clock;
inline void BoundaryCheck(const char* source) {
    auto root=std::filesystem::path(source).parent_path();
    while(!root.empty() && !std::filesystem::exists(root/"tools/boundary_check.py")) {
        const auto parent=root.parent_path(); if(parent==root) break; root=parent;
    }
    ASSERT_TRUE(std::filesystem::exists(root/"tools/boundary_check.py"));
    const auto command="python \""+(root/"tools/boundary_check.py").string()+"\"";
    ASSERT_EQ(std::system(command.c_str()),0);
}
inline std::size_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX p{};
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)))
        throw std::runtime_error("GetProcessMemoryInfo failed");
    return p.PrivateUsage;
}
template<class Frame> void Frames(const char* name,Frame frame,double ceiling_ms) {
    for(int i=0;i<5;++i) frame();
    std::vector<double> samples;
    for(int i=0;i<30;++i) {
        const auto start=Clock::now(); frame();
        samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
    }
    std::sort(samples.begin(),samples.end());
    const double median=samples[samples.size()/2];
    std::cout<<"PERFGUARD "<<name<<" median_ms="<<median<<" p95_ms="<<samples[28]<<" ceiling_ms="<<ceiling_ms<<'\n';
    EXPECT_LE(median,ceiling_ms)<<name;
}
template<class Operation> void Plateau(const char* name,Operation operation,std::size_t ceiling_bytes) {
    operation(); operation(); // Warm caches, allocator and numerical runtime before measuring retained commit.
    const auto baseline=PrivateBytes(); std::size_t peak=baseline;
    for(int i=0;i<10;++i) {
        operation(); const auto bytes=PrivateBytes(); peak=std::max(peak,bytes);
        std::cout<<"PERFGUARD "<<name<<" repeat="<<i+1<<" private_bytes="<<bytes<<'\n';
        ASSERT_LE(peak-baseline,ceiling_bytes)<<name<<" repeat="<<i+1<<" baseline_bytes="<<baseline<<"; expected to tighten after perfret1";
    }
    const auto growth=peak-baseline;
    std::cout<<"PERFGUARD "<<name<<" baseline_bytes="<<baseline<<" growth_bytes="<<growth<<" ceiling_bytes="<<ceiling_bytes<<'\n';
}
}
