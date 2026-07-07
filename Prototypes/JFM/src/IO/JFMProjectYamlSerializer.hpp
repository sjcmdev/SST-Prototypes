#pragma once

#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	class JFMProjectYamlSerializer
	{
	public:
		[[nodiscard]] static Result<std::string> Serialize(const JFMProject &project);
		[[nodiscard]] static Result<JFMProject> Deserialize(const std::string &text);
	};
} // namespace SST::Prototypes::JFM
