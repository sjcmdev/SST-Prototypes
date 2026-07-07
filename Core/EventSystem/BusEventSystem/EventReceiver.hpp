#pragma once

#include <vector>

#include "Core/EventSystem/BusEventSystem/SubscriptionHandle.hpp"

namespace SST
{
	class EventReceiver
	{
	public:
		virtual ~EventReceiver() = default;

		void AddSubscription(SubscriptionHandle handle);
		void ClearSubscriptions();
		void SetEnabled(bool enabled);
		[[nodiscard]] bool IsEnabled() const;

	protected:
		std::vector<SubscriptionHandle> m_Subscriptions;
		bool m_Enabled = true;
	};
} // namespace SST
