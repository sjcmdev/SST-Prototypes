#include "Core/dspch.hpp"

#include "Presentation/Panels/MonteCarloScatterPanel.hpp"

#include "Analysis/JFMMonteCarloStatistics.hpp"
#include "App/NativeFileDialog.hpp"
#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Core/Platform/ProcessRunner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>
#include <sstream>

namespace SST::Prototypes::JFM
{
	namespace
	{
		constexpr NativeFileDialogFilter PngFilter{"PNG image", "png"};
		constexpr NativeFileDialogFilter TsvFilter{"TSV data", "tsv"};

		std::string FileToken(std::string value)
		{
			for (char &character : value)
			{
				if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_')
					character = '_';
			}
			return value;
		}

		Path EnsureExtension(Path path, const char *extension)
		{
			const std::string text = path.String();
			const std::string suffix = extension;
			if (text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix)
				return path;
			return Path::FromResolved(path.Native().string() + suffix);
		}

		std::string ColorHex(const std::array<float, 4> &color)
		{
			auto channel = [](float value) {
				return std::clamp(static_cast<int>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f)), 0, 255);
			};
			std::ostringstream text;
			text << '#'
				<< std::uppercase << std::hex << std::setfill('0') << std::setw(2) << channel(color[0])
				<< std::setw(2) << channel(color[1])
				<< std::setw(2) << channel(color[2])
				<< std::setw(2) << channel(color[3]);
			return text.str();
		}

		std::vector<JFMMonteCarloParameterPair> DefaultPairsForResult(
			const JFMFitMonteCarloSettings &settings,
			const JFMMonteCarloResult &result)
		{
			std::vector<JFMMonteCarloParameterPair> pairs;
			for (const JFMMonteCarloParameterPair &pair : settings.defaultParameterPairs)
			{
				const auto x = std::find(result.parameterNames.begin(), result.parameterNames.end(), pair.x);
				const auto y = std::find(result.parameterNames.begin(), result.parameterNames.end(), pair.y);
				if (x != result.parameterNames.end() && y != result.parameterNames.end() && x != y)
					pairs.push_back(pair);
			}
			return pairs;
		}

		std::vector<JFMMonteCarloParameterPair> AllPairsForResult(const JFMMonteCarloResult &result)
		{
			std::vector<JFMMonteCarloParameterPair> pairs;
			for (std::size_t x = 0; x < result.parameterNames.size(); ++x)
				for (std::size_t y = x + 1; y < result.parameterNames.size(); ++y)
					pairs.push_back({result.parameterNames[x], result.parameterNames[y]});
			return pairs;
		}

		std::optional<int> ParameterIndex(const JFMMonteCarloResult &result, const std::string &name)
		{
			const auto found = std::find(result.parameterNames.begin(), result.parameterNames.end(), name);
			if (found == result.parameterNames.end())
				return std::nullopt;
			return static_cast<int>(std::distance(result.parameterNames.begin(), found));
		}

		std::string CompactProcessError(std::string text)
		{
			text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
			for (char &character : text)
				if (character == '\n' || character == '\t')
					character = ' ';
			constexpr std::size_t maxLength = 220;
			if (text.size() > maxLength)
				text = text.substr(0, maxLength) + "...";
			return text;
		}

		Path ResolveScatterPlotScript()
		{
			const std::array<Path, 4> candidates = {
				Path("python") / "plot_mc_scatter.py",
				Path("Prototypes") / "JFM" / "python" / "plot_mc_scatter.py",
				Path("install") / "app" / "python" / "plot_mc_scatter.py",
				[] {
					const auto executable = Platform::CurrentExecutablePath();
					if (!executable)
						return Path{};
					return executable->parent_path() / "python" / "plot_mc_scatter.py";
				}()};
			for (const Path &candidate : candidates)
				if (!candidate.Empty() && FileSystem::Exists(candidate.Native()))
					return candidate;
			return {};
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

	void MonteCarloScatterPanel::AddGroup(std::string title)
	{
		PlotGroup group;
		group.id = m_NextGroupId++;
		group.title = title.empty() ? "MC group " + std::to_string(group.id) : std::move(title);
		m_Groups.push_back(std::move(group));
		m_ActiveGroup = m_Groups.size() - 1;
	}

	void MonteCarloScatterPanel::AddPlot(JFMCharacteristicRef ref)
	{
		AddPlot(ref, -1, -1);
	}

	void MonteCarloScatterPanel::AddPlot(JFMCharacteristicRef ref, int xParameter, int yParameter, int initialPlacement, int dockSlot, bool open)
	{
		if (ref.project == InvalidProjectHandle || ref.characteristic == InvalidCharacteristicId)
			return;
		if (m_Groups.empty())
			AddGroup();
		PlotGroup &group = m_Groups[std::min(m_ActiveGroup, m_Groups.size() - 1)];
		const auto existing = std::find_if(group.plots.begin(), group.plots.end(), [ref, xParameter, yParameter](const PlotState &plot) {
			return plot.ref == ref
				&& (xParameter < 0 || plot.options.xParameter == xParameter)
				&& (yParameter < 0 || plot.options.yParameter == yParameter);
		});
		if (existing != group.plots.end())
		{
			m_ActivePlot = existing->id;
			existing->open = existing->open || open;
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
		if (xParameter >= 0 && yParameter >= 0 && xParameter != yParameter)
		{
			plot.options.xParameter = xParameter;
			plot.options.yParameter = yParameter;
			plot.axesInitialized = true;
		}
		plot.needsInitialPlacement = initialPlacement >= 0;
		plot.initialPlacement = initialPlacement;
		plot.needsDock = dockSlot >= 0;
		plot.dockSlot = dockSlot;
		plot.open = open;
		m_ActivePlot = plot.id;
		group.plots.push_back(std::move(plot));
	}

	void MonteCarloScatterPanel::AddPlots(ProjectHandle project, const std::vector<CharacteristicId> &ids)
	{
		for (CharacteristicId id : ids)
			AddPlot({project, id});
	}

	void MonteCarloScatterPanel::AddDefaultPlotsForCharacteristic(ProjectHandle project, const JFMCharacteristicInfo &info, int &placement)
	{
		const Ref<const JFMMonteCarloResult> result = m_Store != nullptr ? m_Store->GetMonteCarloResult({project, info.id}) : nullptr;
		if (result == nullptr)
			return;
		AddGroup(info.name);
		const JFMFitMonteCarloSettings settings = m_Settings != nullptr ? m_Settings->GetSnapshot().fitMonteCarlo : JFMFitMonteCarloSettings{};
		std::vector<JFMMonteCarloParameterPair> pairs = DefaultPairsForResult(settings, *result);
		if (pairs.empty())
		{
			const std::vector<JFMMonteCarloParameterPair> allPairs = AllPairsForResult(*result);
			if (!allPairs.empty())
				pairs.push_back(allPairs[static_cast<std::size_t>(std::clamp(m_AddPlotPair, 0, static_cast<int>(allPairs.size()) - 1))]);
		}
		int pairIndex = 0;
		for (const JFMMonteCarloParameterPair &pair : pairs)
		{
			const std::optional<int> x = ParameterIndex(*result, pair.x);
			const std::optional<int> y = ParameterIndex(*result, pair.y);
			if (x && y)
			{
				const bool primary = pair.x == "A" && pair.y == "I0";
				const int dockSlot = primary || pairIndex == 0 ? 0 : 1;
				AddPlot({project, info.id}, *x, *y, placement++, dockSlot, true);
				++pairIndex;
			}
		}
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

	std::string MonteCarloScatterPanel::PlotWindowTitle(const PlotState &plot, const JFMMonteCarloResult &result) const
	{
		const auto record = m_Store != nullptr ? m_Store->FindCharacteristicRecord(plot.ref) : std::nullopt;
		const std::string name = record ? record->info.name : "Characteristic " + std::to_string(plot.ref.characteristic);
		const int xParameter = std::clamp(plot.options.xParameter, 0, static_cast<int>(result.parameterNames.size()) - 1);
		const int yParameter = std::clamp(plot.options.yParameter, 0, static_cast<int>(result.parameterNames.size()) - 1);
		return name + " - " + result.parameterNames[xParameter] + " vs " + result.parameterNames[yParameter];
	}

	std::string MonteCarloScatterPanel::PlotDataFileName(const PlotState &plot, const JFMMonteCarloResult &result, bool uncertaintyOnly) const
	{
		const auto record = m_Store != nullptr ? m_Store->FindCharacteristicRecord(plot.ref) : std::nullopt;
		const std::string characteristicName = FileToken(record ? record->info.name : "characteristic-" + std::to_string(plot.ref.characteristic));
		const int xParameter = std::clamp(plot.options.xParameter, 0, static_cast<int>(result.parameterNames.size()) - 1);
		const int yParameter = std::clamp(plot.options.yParameter, 0, static_cast<int>(result.parameterNames.size()) - 1);
		const std::string pair = FileToken(result.parameterNames[xParameter]) + "-vs-" + FileToken(result.parameterNames[yParameter]);
		return (uncertaintyOnly ? "uncertainty-" : "") + characteristicName + "__" + pair + ".tsv";
	}

	bool MonteCarloScatterPanel::ExportPlot(const PlotState &plot, bool uncertaintyOnly, std::optional<Path> directory) const
	{
		if (m_Store == nullptr)
			return false;
		const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult(plot.ref);
		if (result == nullptr)
			return false;
		if (!directory)
			directory = OutputDirectory(plot.ref);
		std::error_code error;
		(void)FileSystem::CreateDirectories(directory->Native(), error);
		if (error)
			return false;
		return ExportPlotToFile(plot, uncertaintyOnly, directory->Join(PlotDataFileName(plot, *result, uncertaintyOnly)));
	}

	bool MonteCarloScatterPanel::ExportPlotToFile(const PlotState &plot, bool uncertaintyOnly, const Path &path) const
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
		std::error_code error;
		(void)FileSystem::CreateDirectories(path.parent_path().Native(), error);
		if (error)
			return false;
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
			std::array<std::vector<const JFMMonteCarloScatterPoint *>, 3> classes;
			for (const JFMMonteCarloScatterPoint &point : statistics->points)
				if (point.sigmaClass < classes.size())
					classes[point.sigmaClass].push_back(&point);
			static constexpr const char *labels[] = {"le_1_sigma", "1_2_sigma", "2_3_sigma"};
			for (std::size_t sigma = 0; sigma < classes.size(); ++sigma)
			{
				if (sigma > 0)
					output << '\t';
				output << labels[sigma] << '_' << result->parameterNames[xParameter]
					<< '\t' << labels[sigma] << '_' << result->parameterNames[yParameter]
					<< '\t' << labels[sigma] << "_delta_chi2";
			}
			output << '\n';
			std::size_t rows = 0;
			for (const auto &points : classes)
				rows = std::max(rows, points.size());
			for (std::size_t row = 0; row < rows; ++row)
			{
				for (std::size_t sigma = 0; sigma < classes.size(); ++sigma)
				{
					if (sigma > 0)
						output << '\t';
					if (row < classes[sigma].size())
					{
						const JFMMonteCarloScatterPoint &point = *classes[sigma][row];
						output << point.x << '\t' << point.y << '\t' << point.deltaChiSquared;
					}
					else
					{
						output << "\t\t";
					}
				}
				output << '\n';
			}
		}
		return static_cast<bool>(output);
	}

	bool MonteCarloScatterPanel::SavePlotImage(const PlotState &plot, std::optional<Path> outputPath) const
	{
		m_LastSavePlotError.clear();
		if (m_Store == nullptr)
		{
			m_LastSavePlotError = "workspace unavailable";
			return false;
		}
		const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult(plot.ref);
		if (result == nullptr || result->parameterNames.size() < 2)
		{
			m_LastSavePlotError = "MC result unavailable";
			return false;
		}
		const auto record = m_Store->FindCharacteristicRecord(plot.ref);
		const std::string name = FileToken(record ? record->info.name : "characteristic-" + std::to_string(plot.ref.characteristic));
		const int xParameter = std::clamp(plot.options.xParameter, 0, static_cast<int>(result->parameterNames.size()) - 1);
		const int yParameter = std::clamp(plot.options.yParameter, 0, static_cast<int>(result->parameterNames.size()) - 1);
		const std::string pair = FileToken(result->parameterNames[xParameter]) + "-vs-" + FileToken(result->parameterNames[yParameter]);
		if (!outputPath)
		{
			outputPath = SaveFileDialog(PngFilter, (name + "__" + pair + ".png").c_str(), OutputDirectory(plot.ref));
			if (!outputPath)
				return false;
			outputPath = EnsureExtension(*outputPath, ".png");
		}
		std::error_code error;
		const Path dataDirectory = Path::FromResolved(FileSystem::TempDirectoryPath(error) / "jfm-mc-scatter");
		if (error)
		{
			m_LastSavePlotError = "temporary directory failed";
			return false;
		}
		const Path dataPath = dataDirectory.Join(name + "__" + pair + ".tsv");
		if (!ExportPlotToFile(plot, false, dataPath))
		{
			m_LastSavePlotError = "temporary TSV export failed";
			return false;
		}
		const Path script = ResolveScatterPlotScript();
		if (script.Empty())
		{
			m_LastSavePlotError = "plot_mc_scatter.py not found";
			return false;
		}
		std::vector<std::string> arguments = {
			script.String(),
			"--input", dataPath.String(),
			"--output", outputPath->String(),
			"--title", PlotWindowTitle(plot, *result),
			"--xlabel", result->parameterNames[xParameter],
			"--ylabel", result->parameterNames[yParameter],
			"--color1", ColorHex(plot.options.sigmaColors[0]),
			"--color2", ColorHex(plot.options.sigmaColors[1]),
			"--color3", ColorHex(plot.options.sigmaColors[2])};
		if (plot.options.hasAxisLimits)
		{
			arguments.push_back("--xlim");
			arguments.push_back(std::to_string(plot.options.xMinimum));
			arguments.push_back(std::to_string(plot.options.xMaximum));
			arguments.push_back("--ylim");
			arguments.push_back(std::to_string(plot.options.yMinimum));
			arguments.push_back(std::to_string(plot.options.yMaximum));
		}
		struct PythonCandidate
		{
			Path executable;
			std::vector<std::string> prefixArguments;
		};
		const std::array<PythonCandidate, 3> pythonCandidates = {{
			{Path("python"), {}},
			{Path("py"), {"-3"}},
			{Platform::ResolvePreferredPythonExecutable(), {}}}};
		std::optional<Platform::ProcessRunResult> process;
		for (const PythonCandidate &python : pythonCandidates)
		{
			std::vector<std::string> candidateArguments = python.prefixArguments;
			candidateArguments.insert(candidateArguments.end(), arguments.begin(), arguments.end());
			auto candidate = Platform::RunProcess(Platform::ProcessRunOptions{
				python.executable,
				candidateArguments,
				Path::FromResolved(FileSystem::CurrentPath()),
				std::chrono::seconds(30)});
			if (candidate && candidate->exitCode == 0)
			{
				process = *candidate;
				break;
			}
			if (!candidate)
				m_LastSavePlotError = "python process failed: " + candidate.error().technicalDetails;
			else
			{
				const std::string details = candidate->standardError.empty() ? candidate->standardOutput : candidate->standardError;
				m_LastSavePlotError = "python exit " + std::to_string(candidate->exitCode) + ": " + CompactProcessError(details);
			}
		}
		if (!process)
			return false;
		if (!FileSystem::Exists(outputPath->Native()))
		{
			m_LastSavePlotError = "PNG was not created";
			return false;
		}
		return true;
	}

	std::string MonteCarloScatterPanel::LastSavePlotError() const
	{
		return m_LastSavePlotError;
	}

	bool MonteCarloScatterPanel::SaveAllPlotImages(const Path &directory)
	{
		int saved = 0;
		int total = 0;
		for (const PlotGroup &group : m_Groups)
			for (const PlotState &plot : group.plots)
			{
				const Ref<const JFMMonteCarloResult> result = m_Store != nullptr ? m_Store->GetMonteCarloResult(plot.ref) : nullptr;
				if (result == nullptr)
					continue;
				const auto record = m_Store->FindCharacteristicRecord(plot.ref);
				const std::string name = FileToken(record ? record->info.name : "characteristic-" + std::to_string(plot.ref.characteristic));
				const int xParameter = std::clamp(plot.options.xParameter, 0, static_cast<int>(result->parameterNames.size()) - 1);
				const int yParameter = std::clamp(plot.options.yParameter, 0, static_cast<int>(result->parameterNames.size()) - 1);
				const std::string pair = FileToken(result->parameterNames[xParameter]) + "-vs-" + FileToken(result->parameterNames[yParameter]);
				++total;
				saved += SavePlotImage(plot, directory.Join(name + "__" + pair + ".png")) ? 1 : 0;
			}
		SetStatus("MC plots saved", saved, total);
		return saved == total && total > 0;
	}

	void MonteCarloScatterPanel::RenderAddPlotControls(ProjectHandle project)
	{
		const JFMProject snapshot = m_Store->GetProjectSnapshot(project);
		if (m_AddPlotCharacteristic == InvalidCharacteristicId)
		{
			const std::vector<CharacteristicId> selected = m_Store->GetSelectedCharacteristicIds();
			for (CharacteristicId id : selected)
				if (m_Store->GetMonteCarloResult({project, id}) != nullptr)
				{
					m_AddPlotCharacteristic = id;
					break;
				}
			if (m_AddPlotCharacteristic == InvalidCharacteristicId)
				for (const JFMCharacteristicInfo &info : snapshot.characteristics)
					if (m_Store->GetMonteCarloResult({project, info.id}) != nullptr)
					{
						m_AddPlotCharacteristic = info.id;
						break;
					}
		}

		const auto current = std::find_if(snapshot.characteristics.begin(), snapshot.characteristics.end(), [this](const JFMCharacteristicInfo &info) {
			return info.id == m_AddPlotCharacteristic;
		});
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("##add_mc_characteristic", current != snapshot.characteristics.end() ? current->name.c_str() : "Characteristic"))
		{
			for (const JFMCharacteristicInfo &info : snapshot.characteristics)
			{
				if (m_Store->GetMonteCarloResult({project, info.id}) == nullptr)
					continue;
				if (ImGui::Selectable(info.name.c_str(), info.id == m_AddPlotCharacteristic))
				{
					m_AddPlotCharacteristic = info.id;
					m_AddPlotPair = 0;
				}
			}
			ImGui::EndCombo();
		}

		const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult({project, m_AddPlotCharacteristic});
		std::vector<JFMMonteCarloParameterPair> pairs;
		if (result != nullptr)
		{
			const JFMFitMonteCarloSettings settings = m_Settings != nullptr ? m_Settings->GetSnapshot().fitMonteCarlo : JFMFitMonteCarloSettings{};
			pairs = DefaultPairsForResult(settings, *result);
			if (pairs.empty())
				pairs = AllPairsForResult(*result);
		}
		m_AddPlotPair = std::clamp(m_AddPlotPair, 0, std::max(0, static_cast<int>(pairs.size()) - 1));
		std::string pairLabel = "Pair";
		if (!pairs.empty())
			pairLabel = pairs[static_cast<std::size_t>(m_AddPlotPair)].x + " vs " + pairs[static_cast<std::size_t>(m_AddPlotPair)].y;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::BeginCombo("##add_mc_pair", pairLabel.c_str()))
		{
			for (int index = 0; index < static_cast<int>(pairs.size()); ++index)
			{
				const std::string label = pairs[static_cast<std::size_t>(index)].x + " vs " + pairs[static_cast<std::size_t>(index)].y;
				if (ImGui::Selectable(label.c_str(), index == m_AddPlotPair))
					m_AddPlotPair = index;
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("Add plot") && result != nullptr && !pairs.empty())
		{
			const JFMMonteCarloParameterPair &pair = pairs[static_cast<std::size_t>(m_AddPlotPair)];
			const std::optional<int> x = ParameterIndex(*result, pair.x);
			const std::optional<int> y = ParameterIndex(*result, pair.y);
			if (x && y)
				AddPlot({project, m_AddPlotCharacteristic}, *x, *y, -1, 0);
		}
	}

	void MonteCarloScatterPanel::RenderDockspace()
	{
		ImVec2 available = ImGui::GetContentRegionAvail();
		available.y = std::max(420.0f, available.y);
		m_DockspaceId = ImGui::GetID("##mc_scatter_dockspace");
		if (m_RebuildDockLayout)
		{
			ImGui::DockBuilderRemoveNode(m_DockspaceId);
			ImGui::DockBuilderAddNode(m_DockspaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(m_DockspaceId, available);
			ImGuiID left = m_DockspaceId;
			ImGuiID right = 0;
			ImGui::DockBuilderSplitNode(left, ImGuiDir_Right, 0.5f, &right, &left);
			m_LeftDockId = left;
			m_RightDockId = right;
			ImGui::DockBuilderFinish(m_DockspaceId);
			m_RebuildDockLayout = false;
		}
		if (m_LeftDockId == 0)
			m_LeftDockId = m_DockspaceId;
		if (m_RightDockId == 0)
			m_RightDockId = m_DockspaceId;
		ImGui::DockSpace(m_DockspaceId, available, ImGuiDockNodeFlags_None);
	}

	void MonteCarloScatterPanel::SetStatus(std::string action, int saved, int total)
	{
		m_Status = std::move(action) + ": " + std::to_string(saved) + "/" + std::to_string(total);
	}

	void MonteCarloScatterPanel::RenderPlotWindow(PlotState &plot)
	{
		const Ref<const JFMMonteCarloResult> result = m_Store != nullptr ? m_Store->GetMonteCarloResult(plot.ref) : nullptr;
		if (result == nullptr)
			return;
		if (!plot.axesInitialized)
			InitializeAxes(plot, *result);
		const std::string title = PlotWindowTitle(plot, *result) + "###mc_scatter_plot_" + std::to_string(plot.id);
		if (plot.needsDock && m_DockspaceId != 0)
		{
			const ImGuiID target = plot.dockSlot == 1 ? m_RightDockId : m_LeftDockId;
			ImGui::SetNextWindowDockID(target != 0 ? target : m_DockspaceId, ImGuiCond_Always);
			plot.needsDock = false;
			plot.needsInitialPlacement = false;
		}
		else if (plot.needsInitialPlacement)
		{
			const ImGuiViewport *viewport = ImGui::GetMainViewport();
			const float width = std::max(560.0f, viewport->WorkSize.x * 0.48f);
			const float height = std::max(360.0f, viewport->WorkSize.y * 0.42f);
			const int column = plot.initialPlacement % 2;
			const int row = plot.initialPlacement / 2;
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + column * (width + 12.0f), viewport->WorkPos.y + row * 38.0f), ImGuiCond_Once);
			ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Once);
			plot.needsInitialPlacement = false;
		}
		if (!ImGui::Begin(title.c_str(), &plot.open, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::End();
			return;
		}
		if (ImGui::Button("Save"))
		{
			const Path defaultPath = OutputDirectory(plot.ref);
			const Path defaultFile = defaultPath.Join(PlotDataFileName(plot, *result, false));
			const std::optional<Path> path = SaveFileDialog(TsvFilter, defaultFile.filename().String().c_str(), defaultPath);
			SetStatus("Plot data saved", path && ExportPlotToFile(plot, false, EnsureExtension(*path, ".tsv")) ? 1 : 0, 1);
		}
		ImGui::SameLine();
		if (ImGui::Button("Save Plot"))
		{
			const bool saved = SavePlotImage(plot);
			SetStatus("Plot image saved", saved ? 1 : 0, 1);
			if (!saved && !LastSavePlotError().empty())
				m_Status += " - " + LastSavePlotError();
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove"))
		{
			plot.open = false;
			plot.remove = true;
		}
		const float height = std::max(360.0f, ImGui::GetContentRegionAvail().y - 8.0f);
		(void)RenderJFMCovariancePlot(*result, plot.options, "plot", height);
		ImGui::End();
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
		if (ImGui::BeginChild("##mc_scatter_controls", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() * 1.8f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			if (ImGui::Button("Add group"))
				AddGroup();
			ImGui::SameLine();
			if (ImGui::Button("Plot all MC"))
			{
				m_Groups.clear();
				m_ActiveGroup = 0;
				m_LeftDockId = 0;
				m_RightDockId = 0;
				m_RebuildDockLayout = true;
				int placement = 0;
				const JFMProject snapshot = m_Store->GetProjectSnapshot(project);
				for (const JFMCharacteristicInfo &info : snapshot.characteristics)
					if (m_Store->GetMonteCarloResult({project, info.id}) != nullptr)
						AddDefaultPlotsForCharacteristic(project, info, placement);
				if (m_Groups.empty())
					AddGroup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear all plots"))
			{
				for (PlotGroup &group : m_Groups)
					group.plots.clear();
				m_ActivePlot = 0;
			}
			ImGui::SameLine();
			RenderAddPlotControls(project);

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
				if (std::vector<Path> paths = PickFoldersDialog(false, m_Settings != nullptr ? m_Settings->GetDefaultProjectsDirectory() : Path{}); !paths.empty())
				{
					const JFMProject snapshot = m_Store->GetProjectSnapshot(project);
					int saved = 0;
					int total = 0;
					const JFMFitMonteCarloSettings settings = m_Settings != nullptr ? m_Settings->GetSnapshot().fitMonteCarlo : JFMFitMonteCarloSettings{};
					for (const JFMCharacteristicInfo &info : snapshot.characteristics)
					{
						const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult({project, info.id});
						if (result == nullptr)
							continue;
						std::vector<JFMMonteCarloParameterPair> pairs = DefaultPairsForResult(settings, *result);
						if (pairs.empty())
							pairs = AllPairsForResult(*result);
						for (const JFMMonteCarloParameterPair &pair : pairs)
						{
							const std::optional<int> x = ParameterIndex(*result, pair.x);
							const std::optional<int> y = ParameterIndex(*result, pair.y);
							if (!x || !y)
								continue;
							PlotState plot;
							plot.ref = {project, info.id};
							plot.options.xParameter = *x;
							plot.options.yParameter = *y;
							plot.axesInitialized = true;
							++total;
							saved += ExportPlot(plot, false, paths.front()) ? 1 : 0;
						}
					}
					SetStatus("MC data saved", saved, total);
				}
			ImGui::SameLine();
			if (ImGui::Button("Save all MC plots"))
				if (std::vector<Path> paths = PickFoldersDialog(false, m_Settings != nullptr ? m_Settings->GetDefaultProjectsDirectory() : Path{}); !paths.empty())
				{
					std::vector<PlotGroup> previousGroups = m_Groups;
					std::size_t previousActiveGroup = m_ActiveGroup;
					const JFMProject snapshot = m_Store->GetProjectSnapshot(project);
					m_Groups.clear();
					int placement = 0;
					for (const JFMCharacteristicInfo &info : snapshot.characteristics)
						if (m_Store->GetMonteCarloResult({project, info.id}) != nullptr)
							AddDefaultPlotsForCharacteristic(project, info, placement);
					(void)SaveAllPlotImages(paths.front());
					m_Groups = std::move(previousGroups);
					m_ActiveGroup = std::min(previousActiveGroup, m_Groups.empty() ? 0 : m_Groups.size() - 1);
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
		}
		ImGui::EndChild();

		if (ImGui::BeginChild("##mc_scatter_plot_list", ImVec2(0.0f, 150.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)
			&& ImGui::BeginTable("##mc_scatter_list", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Group");
			ImGui::TableSetupColumn("Plot");
			ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableHeadersRow();
			for (PlotGroup &group : m_Groups)
			{
				for (PlotState &plot : group.plots)
				{
					const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult(plot.ref);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(group.title.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(result != nullptr ? PlotWindowTitle(plot, *result).c_str() : "Monte Carlo result pending");
					ImGui::TableSetColumnIndex(2);
					ImGui::PushID(static_cast<int>(plot.id));
					if (ImGui::SmallButton(plot.open ? "Hide" : "Show"))
						plot.open = !plot.open;
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
					{
						plot.open = false;
						plot.remove = true;
					}
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}
		ImGui::EndChild();
		RenderDockspace();

		ImGui::End();
		SetVisible(visible);

		for (PlotGroup &group : m_Groups)
			for (PlotState &plot : group.plots)
				if (plot.open)
					RenderPlotWindow(plot);
		for (PlotGroup &group : m_Groups)
			group.plots.erase(std::remove_if(group.plots.begin(), group.plots.end(), [](const PlotState &plot) {
				return plot.remove;
			}), group.plots.end());
	}
} // namespace SST::Prototypes::JFM
