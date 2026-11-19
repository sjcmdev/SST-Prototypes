#include "Core/dspch.hpp"

#include "Presentation/Panels/PanelRegistry.hpp"

#include <algorithm>
#include <limits>

namespace SST
{
	PanelId PanelRegistry::AddExisting(Ref<IPanel> panel, PanelId id)
	{
		if (!panel || id == std::numeric_limits<PanelId>::max() || (id != 0 && Contains(id)) ||
            std::any_of(m_Entries.begin(), m_Entries.end(), [&](const Entry &entry) { return entry.panel == panel; })) return 0;
		if (id == 0) id = m_NextId++;
		m_NextId = std::max(m_NextId, id + 1);
		if (auto window = std::dynamic_pointer_cast<EditorWindow>(panel))
        {
            if (window->m_Id == 0) window->m_DefaultState = window->GetState();
            window->m_Id = id;
        }
		m_Entries.push_back({id, std::move(panel)});
		return id;
	}
	WeakRef<IPanel> PanelRegistry::Get(PanelId id)
	{
		const auto it = findEntry(id);
		return it == m_Entries.end() ? WeakRef<IPanel>{} : CreateWeakRef(it->panel);
	}

	WeakRef<const IPanel> PanelRegistry::Get(PanelId id) const
	{
		const auto it = findEntry(id);
		return it == m_Entries.end() ? WeakRef<const IPanel>{} : WeakRef<const IPanel>(it->panel);
	}

	bool PanelRegistry::Contains(PanelId id) const
	{
		return findEntry(id) != m_Entries.end();
	}

	bool PanelRegistry::Remove(PanelId id)
	{
		const auto it = findEntry(id);
		if (it == m_Entries.end())
			return false;

		m_Entries.erase(it);
		return true;
	}

	void PanelRegistry::Clear()
	{
		m_Entries.clear();
	}

	PanelRegistry::PanelId PanelRegistry::Clone(PanelId sourceId)
	{
		const auto it = findEntry(sourceId);
		if (it == m_Entries.end() || !it->panel)
			return 0;

		auto panel = it->panel->Clone();
		if (!panel)
			return 0;

		if (panel->GetTitle() == it->panel->GetTitle())
			panel->SetTitle(it->panel->GetTitle() + " copy");

		return AddExisting(std::move(panel));
	}

	std::vector<PanelRegistry::PanelId> PanelRegistry::GetIds() const
	{
		std::vector<PanelId> ids;
		ids.reserve(m_Entries.size());
		for (const auto &entry : m_Entries)
			ids.push_back(entry.id);
		return ids;
	}

	std::vector<Entry> &PanelRegistry::Entries()
	{
		return m_Entries;
	}

	const std::vector<Entry> &PanelRegistry::Entries() const
	{
		return m_Entries;
	}

	PanelRegistry::Iterator PanelRegistry::findEntry(PanelId id)
	{
		return std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) {
			return entry.id == id;
		});
	}

	PanelRegistry::ConstIterator PanelRegistry::findEntry(PanelId id) const
	{
		return std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry &entry) {
			return entry.id == id;
		});
	}
} // namespace SST
