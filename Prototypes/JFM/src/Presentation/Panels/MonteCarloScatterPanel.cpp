#include "Core/dspch.hpp"

#include "Presentation/Panels/MonteCarloScatterPanel.hpp"

#include "Analysis/JFMMonteCarloStatistics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <imgui.h>
#include <optional>

namespace SST::Prototypes::JFM
{
	namespace
	{
		std::string FileToken(std::string value)
		{
			for (char &character : value)
			{
				if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_')
					character = '_';
			}
			return value;
		}
	}

	MonteCarloScatterPanel::MonteCarloScatterPanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_EventBus(std::move(eventBus))
	{
		AddGroup();
		if (m_EventBus != nullptr)
		{
			AddSubscription(m_EventBus->Subscribe<MonteCarloRequested>([this](const MonteCarloRequested &event) {
				SetVisible(true);
				AddPlots(event.project, event.ids);
			}));
			AddSubscription(m_EventBus->Subscribe<MonteCarloCompleted>([this](const MonteCarloCompleted &event) {
				SetVisible(true);
				AddPlot(event.ref);
			}));
		}
	}

	Ref<IPanel> MonteCarloScatterPanel::Clone() const
	{
		return CreateRef<MonteCarloScatterPanel>(m_Store, m_Settings, m_EventBus, GetTitle(), IsVisible());
	}

	void MonteCarloScatterPanel::AddGroup()
	{
		PlotGroup group;
		group.id = m_NextGroupId++;
		group.title = "MC group " + std::to_string(group.id);
		m_Groups.push_back(std::move(group));
		m_ActiveGroup = m_Groups.size() - 1;
	}

	void MonteCarloScatterPanel::AddPlot(JFMCharacteristicRef ref)
	{
		if (ref.project == InvalidProjectHandle || ref.characteristic == InvalidCharacteristicId)
			return;
		if (m_Groups.empty())
			AddGroup();
		PlotGroup &group = m_Groups[std::min(m_ActiveGroup, m_Groups.size() - 1)];
		const auto existing = std::find_if(group.plots.begin(), group.plots.end(), [ref](const PlotState &plot) { return plot.ref == ref; });
		if (existing != group.plots.end())
		{
			m_ActivePlot = existing->id;
			return;
		}
		PlotState plot;
		plot.id = m_NextPlotId++;
		plot.ref = ref;
		if (m_Settings != nullptr)
		{
			const JFMFitMonteCarloSettings settings = m_Settings->GetSnapshot().fitMonteCarlo;
			plot.options.zoomOut = settings.scatterZoomOut;
			plot.options.showSigmaClasses = settings.showSigmaClasses;
			plot.options.showSigmaEllipses = settings.showSigmaEllipses;
			plot.options.sigmaColors = settings.sigmaColors;
		}
		m_ActivePlot = plot.id;
		group.plots.push_back(std::move(plot));
	}

	void MonteCarloScatterPanel::AddPlots(ProjectHandle project, const std::vector<CharacteristicId> &ids)
	{
		for (CharacteristicId id : ids)
			AddPlot({project, id});
	}

	void MonteCarloScatterPanel::InitializeAxes(PlotState &plot, const JFMMonteCarloResult &result) const
	{
		plot.axesInitialized = true;
		if (result.parameterNames.size() < 2)
			return;
		if (m_Settings != nullptr)
		{
			const auto &pairs = m_Settings->GetSnapshot().fitMonteCarlo.defaultParameterPairs;
			for (const JFMMonteCarloParameterPair &pair : pairs)
			{
				const auto x = std::find(result.parameterNames.begin(), result.parameterNames.end(), pair.x);
				const auto y = std::find(result.parameterNames.begin(), result.parameterNames.end(), pair.y);
				if (x != result.parameterNames.end() && y != result.parameterNames.end() && x != y)
				{
					plot.options.xParameter = static_cast<int>(std::distance(result.parameterNames.begin(), x));
					plot.options.yParameter = static_cast<int>(std::distance(result.parameterNames.begin(), y));
					return;
				}
			}
		}
		plot.options.xParameter = 0;
		plot.options.yParameter = 1;
	}

	Path MonteCarloScatterPanel::OutputDirectory(JFMCharacteristicRef ref) const
	{
		if (m_Store != nullptr)
		{
			const auto state = m_Store->GetMonteCarloState(ref);
			if (state && !state->outputPath.Empty())
				return state->outputPath;
		}
		Path root = m_Settings != nullptr ? m_Settings->GetDefaultProjectsDirectory() : Path::FromResolved(FileSystem::CurrentPath());
		return root / std::filesystem::path("monte-carlo")
			/ std::filesystem::path("project-" + std::to_string(ref.project))
			/ std::filesystem::path("characteristic-" + std::to_string(ref.characteristic));
	}

	bool MonteCarloScatterPanel::ExportPlot(const PlotState &plot, bool uncertaintyOnly) const
	{
		if (m_Store == nullptr)
			return false;
		const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult(plot.ref);
		if (result == nullptr || result->parameterNames.size() < 2)
			return false;
		const int xParameter = std::clamp(plot.options.xParameter, 0, static_cast<int>(result->parameterNames.size()) - 1);
		const int yParameter = std::clamp(plot.options.yParameter, 0, static_cast<int>(result->parameterNames.size()) - 1);
		const auto statistics = CalculateJFMMonteCarloScatterStatistics(*result, xParameter, yParameter);
		if (!statistics)
			return false;
		const Path directory = OutputDirectory(plot.ref);
		std::error_code error;
		(void)FileSystem::CreateDirectories(directory.Native(), error);
		if (error)
			return false;
		const std::string pair = FileToken(result->parameterNames[xParameter]) + "-vs-" + FileToken(result->parameterNames[yParameter]);
		const Path path = directory.Join((uncertaintyOnly ? "uncertainty-" : "scatter-") + pair + ".tsv");
		std::ofstream output(path.Native());
		if (!output)
			return false;
		output << std::setprecision(17);
		if (uncertaintyOnly)
		{
			output << "x\ty\tmean_x\tmean_y\tstd_x\tstd_y\tcovariance_xy\n";
			output << result->parameterNames[xParameter] << '\t' << result->parameterNames[yParameter] << '\t'
				<< statistics->meanX << '\t' << statistics->meanY << '\t'
				<< std::sqrt(std::max(0.0, statistics->covarianceXX)) << '\t'
				<< std::sqrt(std::max(0.0, statistics->covarianceYY)) << '\t'
				<< statistics->covarianceXY << '\n';
		}
		else
		{
			output << result->parameterNames[xParameter] << '\t' << result->parameterNames[yParameter]
				<< "\tmahalanobis_squared\tdelta_chi2\tsigma_class\n";
			for (const JFMMonteCarloScatterPoint &point : statistics->points)
				output << point.x << '\t' << point.y << '\t' << point.mahalanobisSquared << '\t' << point.deltaChiSquared << '\t' << static_cast<int>(point.sigmaClass + 1) << '\n';
		}
		return static_cast<bool>(output);
	}

	void MonteCarloScatterPanel::SetStatus(std::string action, int saved, int total)
	{
		m_Status = std::move(action) + ": " + std::to_string(saved) + "/" + std::to_string(total);
	}

	void MonteCarloScatterPanel::Render()
	{
		if (!IsVisible())
			return;
		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::End();
			SetVisible(visible);
			return;
		}
		if (m_Store == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		const ProjectHandle project = m_Store->GetActiveProjectHandle();
		if (ImGui::Button("Add group"))
			AddGroup();
		ImGui::SameLine();
		if (ImGui::Button("Add plot"))
			AddPlots(project, m_Store->GetSelectedCharacteristicIds());
		ImGui::SameLine();
		if (ImGui::Button("Plot all MC"))
		{
			const JFMProject snapshot = m_Store->GetProjectSnapshot(project);
			for (const JFMCharacteristicInfo &info : snapshot.characteristics)
				if (m_Store->GetMonteCarloResult({project, info.id}) != nullptr)
					AddPlot({project, info.id});
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear all plots"))
		{
			for (PlotGroup &group : m_Groups)
				group.plots.clear();
			m_ActivePlot = 0;
		}

		auto exportAll = [this](bool uncertaintyOnly) {
			int saved = 0;
			int total = 0;
			for (const PlotGroup &group : m_Groups)
				for (const PlotState &plot : group.plots)
				{
					++total;
					saved += ExportPlot(plot, uncertaintyOnly) ? 1 : 0;
				}
			SetStatus(uncertaintyOnly ? "Uncertainty saved" : "MC data saved", saved, total);
		};
		ImGui::SameLine();
		if (ImGui::Button("Save all MC data"))
			exportAll(false);
		ImGui::SameLine();
		if (ImGui::Button("Save all MC"))
		{
			exportAll(false);
			exportAll(true);
		}
		ImGui::SameLine();
		if (ImGui::Button("Save uncertainty"))
			exportAll(true);
		ImGui::SameLine();
		if (ImGui::Button("Clear simulations"))
		{
			const JFMProject snapshot = m_Store->GetProjectSnapshot(project);
			std::vector<CharacteristicId> ids;
			for (const JFMCharacteristicInfo &info : snapshot.characteristics)
				if (info.monteCarlo)
					ids.push_back(info.id);
			(void)m_Store->ClearMonteCarloResults(project, ids);
			for (PlotGroup &group : m_Groups)
				group.plots.clear();
		}
		if (!m_Status.empty())
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(m_Status.c_str());
		}

		std::optional<std::size_t> removeGroup;
		if (ImGui::BeginTabBar("##mc_groups", ImGuiTabBarFlags_Reorderable))
		{
			for (std::size_t groupIndex = 0; groupIndex < m_Groups.size(); ++groupIndex)
			{
				PlotGroup &group = m_Groups[groupIndex];
				bool open = true;
				ImGui::PushID(static_cast<int>(group.id));
				if (ImGui::BeginTabItem(group.title.c_str(), m_Groups.size() > 1 ? &open : nullptr))
				{
					m_ActiveGroup = groupIndex;
					std::optional<std::uint64_t> removePlot;
					const float height = group.plots.size() <= 1
						? std::max(360.0f, ImGui::GetContentRegionAvail().y - 40.0f)
						: std::max(360.0f, ImGui::GetContentRegionAvail().y * 0.5f);
					for (PlotState &plot : group.plots)
					{
						ImGui::PushID(static_cast<int>(plot.id));
						const auto record = m_Store->FindCharacteristicRecord(plot.ref);
						const std::string name = record ? record->info.name : "Characteristic " + std::to_string(plot.ref.characteristic);
						if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
						{
							m_ActivePlot = plot.id;
							const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult(plot.ref);
							if (result == nullptr)
								ImGui::TextDisabled("Monte Carlo result pending");
							else
							{
								if (!plot.axesInitialized)
									InitializeAxes(plot, *result);
								if (ImGui::Button("Save"))
									SetStatus("Plot saved", ExportPlot(plot, false) ? 1 : 0, 1);
								ImGui::SameLine();
								if (ImGui::Button("Remove"))
									removePlot = plot.id;
								(void)RenderJFMCovariancePlot(*result, plot.options, "plot", height);
							}
						}
						ImGui::PopID();
					}
					if (removePlot)
						group.plots.erase(std::remove_if(group.plots.begin(), group.plots.end(), [removePlot](const PlotState &plot) {
							return plot.id == *removePlot;
						}), group.plots.end());
					if (group.plots.empty())
						ImGui::TextDisabled("No MC plots");
					ImGui::EndTabItem();
				}
				if (!open)
					removeGroup = groupIndex;
				ImGui::PopID();
			}
			ImGui::EndTabBar();
		}
		if (removeGroup)
		{
			m_Groups.erase(m_Groups.begin() + static_cast<std::ptrdiff_t>(*removeGroup));
			m_ActiveGroup = std::min(m_ActiveGroup, m_Groups.size() - 1);
		}

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
