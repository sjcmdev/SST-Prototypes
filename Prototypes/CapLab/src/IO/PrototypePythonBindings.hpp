#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Core/JobSystem/JobContext.hpp"
namespace SST::Prototypes::CapLab {
void RegisterPrototypePythonModule();
Json ReadLabAS(const std::string &text, const Path &source, char decimal, JobContext &context, const std::string &options = "");
}
