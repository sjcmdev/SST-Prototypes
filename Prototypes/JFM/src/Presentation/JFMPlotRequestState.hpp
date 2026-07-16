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
		struct CurveGroup
		{
			std::string key;
			std::string title;
			std::vector<CharacteristicId> ids;
			std::string groupingId;
			std::string bucketKey;
		};

		ProjectHandle project = InvalidProjectHandle;
		std::string title;
		std::vector<CurveGroup> groups;
	};

	class JFMPlotRequestState
	{
	public:
		void QueuePlot(std::vector<CharacteristicId> ids, std::string title = {}, ProjectHandle project = InvalidProjectHandle)
		{
			if (ids.empty())
				return;
			std::scoped_lock lock(m_Mutex);
			JFMPlotRequest request;
			request.project = project;
			request.title = title;
			request.groups.push_back({"selection", std::move(title), std::move(ids), {}, {}});
			m_Requests.push_back(std::move(request));
		}

		void QueueGroups(ProjectHandle project, std::string title, std::vector<JFMPlotRequest::CurveGroup> groups)
		{
			if (groups.empty())
				return;
			std::scoped_lock lock(m_Mutex);
			m_Requests.push_back(JFMPlotRequest{project, std::move(title), std::move(groups)});
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
