#pragma once

#include <optional>

#include "Domain/PrototypeBackend.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
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
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

