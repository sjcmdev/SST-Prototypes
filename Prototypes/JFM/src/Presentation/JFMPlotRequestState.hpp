#pragma once

#include "Core/Utils/Memory.hpp"
#include "Domain/JFMTypes.hpp"

#include <mutex>
#include <vector>

namespace SST::Prototypes::JFM
{
	class JFMPlotRequestState
	{
	public:
		void QueuePlot(std::vector<CharacteristicId> ids)
		{
			if (ids.empty())
				return;
			std::scoped_lock lock(m_Mutex);
			m_Requests.push_back(std::move(ids));
		}

		[[nodiscard]] std::vector<std::vector<CharacteristicId>> ConsumeRequests()
		{
			std::scoped_lock lock(m_Mutex);
			std::vector<std::vector<CharacteristicId>> requests = std::move(m_Requests);
			m_Requests.clear();
			return requests;
		}

	private:
		std::mutex m_Mutex;
		std::vector<std::vector<CharacteristicId>> m_Requests;
	};
} // namespace SST::Prototypes::JFM
