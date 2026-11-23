#pragma once
#include "Core/Parameters/ParameterBounds.hpp"
#include <memory>

namespace SST::ScapsLab {
enum class BoundsFileAction { Inspect, Reload, Reset };
std::shared_ptr<const ParameterBounds> BoundsTable();
const ParameterBounds& BuiltinBounds();
std::string InitializeParameterBounds(std::string defaultsFile,std::string userFile);
std::string ReloadParameterBounds();
std::string ResetParameterBounds();
std::string EnsureUserBoundsFile();
std::string UserBoundsFile();
std::string ParameterBoundsKey(std::string_view path);
double ParameterToSI(std::string_view key);
void RequireParameter(std::string_view path,double nativeValue);
}
