#pragma once

#include <string>
#include <yaml-cpp/node/node.h>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	class JFMProjectYamlSerializer
	{
	public:
		[[nodiscard]] static YAML::Node ToNode(const JFMProject &project);
		[[nodiscard]] static Result<JFMProject> FromNode(const YAML::Node &node);
		[[nodiscard]] static Result<std::string> Serialize(const JFMProject &project);
		[[nodiscard]] static Result<JFMProject> Deserialize(const std::string &text);
	};
} // namespace SST::Prototypes::JFM
