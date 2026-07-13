#pragma once

#include "Core/Utils/Memory.hpp"
#include "Domain/JFMTypes.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace SST::Prototypes::JFM
{
	struct JFMPlotRequest
	{
		std::vector<CharacteristicId> ids;
		std::string title;
	};

	class JFMPlotRequestState
	{
	public:
		void QueuePlot(std::vector<CharacteristicId> ids, std::string title = {})
		{
			if (ids.empty())
				return;
			std::scoped_lock lock(m_Mutex);
			m_Requests.push_back(JFMPlotRequest{std::move(ids), std::move(title)});
		}

		[[nodiscard]] std::vector<JFMPlotRequest> ConsumeRequests()
		{
			std::scoped_lock lock(m_Mutex);
			std::vector<JFMPlotRequest> requests = std::move(m_Requests);
			m_Requests.clear();
			return requests;
		}

	private:
		std::mutex m_Mutex;
		std::vector<JFMPlotRequest> m_Requests;
	};
} // namespace SST::Prototypes::JFM
