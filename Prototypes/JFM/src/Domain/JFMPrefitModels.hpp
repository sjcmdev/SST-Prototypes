#pragma once

#include <array>

namespace SST::Prototypes::JFM
{
	inline constexpr std::array<const char *, 10> PrefitModels = {
		"SD-0",
		"SD-Rs",
		"SD-Rsh",
		"SD-RsRsh",
		"SD-0-I_ph",
		"SD-Rs-I_ph",
		"SD-Rsh-I_ph",
		"SD-RsRsh-I_ph",
		"SD-RsRsh-alpha-Rsh2",
		"SD-RsRsh-alpha-Rsh2-I_ph"};
} // namespace SST::Prototypes::JFM
