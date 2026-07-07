#pragma once

#include <optional>

#include "Domain/PrototypeBackend.hpp"

namespace SST::Prototypes::JFM
{
	class PrototypeResultStore final
	{
	public:
		void Save(PrototypeResult result)
		{
			m_Result = result;
		}

		[[nodiscard]] const std::optional<PrototypeResult> &LastResult() const
		{
			return m_Result;
		}

	private:
		std::optional<PrototypeResult> m_Result;
	};
} // namespace SST::Prototypes::JFM

