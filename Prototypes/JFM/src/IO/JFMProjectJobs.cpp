#include "Core/dspch.hpp"
#include "IO/JFMMonteCarloStorage.hpp"

#include "IO/JFMProjectJobs.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/IO/ProjectRecovery.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMPythonImportBridge.hpp"
#include "IO/JFMProjectBundleSerializer.hpp"
#include "IO/JFMProjectYamlSerializer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <unordered_map>

namespace SST::Prototypes::JFM
{
    void PrepareJFMRecovery(const Ref<JFMWorkspaceStore>& workspace, ProjectRecoveryPlan& recovery)
    {
        for (const auto& info:workspace->GetProjects()) if (info.dirty) {
            recovery.Save(info.handle,workspace->GetProjectRevision(info.handle),info.path.value_or(Path{}),".jfmb",
                [workspace,handle=info.handle] {
                    auto snapshot=workspace->CaptureProjectBundle(handle);
                    if (!snapshot) throw std::runtime_error("Cannot capture the JFM document.");
                    return [snapshot=std::move(*snapshot)](const Path& target) {
                        const auto saved=JFMProjectBundleSerializer::Save(target,snapshot);
                        if (!saved) throw std::runtime_error(saved.Error().userMessage+" "+saved.Error().technicalDetails);
                    };
                });
        }
    }

	namespace Detail
	{
		struct MaterializedSource
		{
			std::vector<JFMCharacteristicRecord> records;
            std::string failure;
		};

		void AppendCacheField(std::string &key, const std::string &value)
		{
			key += std::to_string(value.size()) + ':' + value;
		}

		std::string MaterializationCacheKey(const JFMCharacteristicInfo &info)
		{
			std::string key;
			AppendCacheField(key, info.sourcePath.Utf8());
			AppendCacheField(key, info.importOptions.parser);
			AppendCacheField(key, info.importOptions.namePattern);
			AppendCacheField(key, info.importOptions.shortNamePattern);
			AppendCacheField(key, info.importOptions.variableDelimiter);
			AppendCacheField(key, info.importOptions.delimiter);
			AppendCacheField(key, info.importOptions.decimalSeparator);
			AppendCacheField(key, info.importOptions.temperatureExtraction);
			AppendCacheField(key, info.importOptions.customScriptPath);
			for (const JFMNameVariableRule &rule : info.importOptions.customVariables)
			{
				AppendCacheField(key, rule.name);
				AppendCacheField(key, rule.source);
				AppendCacheField(key, std::to_string(rule.tokenIndex));
				AppendCacheField(key, std::to_string(rule.substringStart));
				AppendCacheField(key, std::to_string(rule.substringLength));
			}
			return key;
		}

		const JFMCharacteristicRecord *FindMaterializedMatch(
			const std::vector<JFMCharacteristicRecord> &candidates,
			const JFMCharacteristicInfo &info)
		{
			if (!info.sourceRecordKey.empty())
			{
				const auto byKey = std::find_if(
					candidates.begin(),
					candidates.end(),
					[&info](const JFMCharacteristicRecord &candidate) {
						return candidate.info.sourceRecordKey == info.sourceRecordKey;
					});
				return byKey == candidates.end() ? nullptr : &*byKey;
			}

			const auto byName = std::find_if(
				candidates.begin(),
				candidates.end(),
				[&info](const JFMCharacteristicRecord &candidate) {
					return candidate.info.name == info.name;
				});
			if (byName != candidates.end())
				return &*byName;

			return candidates.size() == 1 ? &candidates.front() : nullptr;
		}
	}


	SaveProjectJob::SaveProjectJob(
		Ref<EventBus> eventBus,
		Path path,
		JFMProject snapshot,
		ProjectHandle project,
		JFMOperationId operation,
		std::uint64_t revision, std::vector<JFMMonteCarloBundleRecord> samples)
		: m_Samples(std::move(samples)), m_EventBus(std::move(eventBus)),
		  m_Path(std::move(path)),
		  m_Snapshot(std::move(snapshot)),
		  m_Project(project),
		  m_Operation(operation),
		  m_Revision(revision)
	{
	}

	std::string SaveProjectJob::GetName() const
	{
		return "Save JFM Project";
	}

	std::string SaveProjectJob::GetType() const
	{
		return "jfm.project.save";
	}

	void SaveProjectJob::Execute(JobContext &context)
	{
        context.SetStage("Save samples");
        static std::atomic_uint64_t sequence{0};
        const auto run="save-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence);
        for (const auto& record:m_Samples) {
            context.ThrowIfCancellationRequested();
            const auto recordInfo=std::find_if(m_Snapshot.characteristics.begin(),m_Snapshot.characteristics.end(),[&](const auto& info) { return info.id==record.characteristic; });
            const auto storageId=recordInfo!=m_Snapshot.characteristics.end() && recordInfo->monteCarlo ? recordInfo->monteCarlo->sampleStorageId : std::string{};
            if (!storageId.empty() && storageId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
                throw std::invalid_argument("Invalid Monte Carlo storage ID.");
            // A run is immutable; repeated project saves reuse its completed sidecar.
            const auto directory=m_Path.parent_path()/Path(m_Path.filename().Utf8()+".data")/Path(storageId.empty() ? run : storageId)/Path(std::to_string(record.characteristic));
            if (!record.result) throw std::runtime_error("Project MC snapshot is missing.");
            auto saved=std::filesystem::is_regular_file((directory/Path("samples.h5")).Native()) ? Result<void>{}
                : SaveJFMMonteCarloHdf5(directory/Path("samples.h5"),*record.result,[&] { context.ThrowIfCancellationRequested(); });
            if (!saved) {
                if (m_EventBus) m_EventBus->Queue(ProjectSaveFailed{m_Path,saved.Error().technicalDetails,m_Project,m_Operation});
                throw std::runtime_error(saved.Error().technicalDetails);
            }
            for (auto& info:m_Snapshot.characteristics) if (info.id==record.characteristic && info.monteCarlo) {
                info.monteCarlo->outputPath=Path::FromResolved(directory.Native().lexically_relative(m_Path.parent_path().Native()));
                info.monteCarlo->sampleStorageId=storageId.empty() ? run : storageId;
                info.monteCarlo->sampleSaveStatus="saved"; info.monteCarlo->sampleSaveError.clear();
            }
        }
		context.SetStage("Serialize");
		context.SetProgress(0.0f, 2.0f);
		auto serialized = JFMProjectYamlSerializer::Serialize(m_Snapshot);
		if (!serialized)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectSaveFailed{m_Path, serialized.Error().technicalDetails, m_Project, m_Operation});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetStage("Write");
		context.SetProgress(1.0f, 2.0f);
		std::string error;
		if (!TextFileIO::Save(m_Path, serialized.Value(), error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectSaveFailed{m_Path, error, m_Project, m_Operation});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectSaveCompleted{m_Path, m_Project, m_Operation, m_Revision});
	}

	LoadProjectJob::LoadProjectJob(Ref<EventBus> eventBus, Path path, JFMOperationId operation)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Operation(operation)
	{
	}

	std::string LoadProjectJob::GetName() const
	{
		return "Load JFM Project";
	}

	std::string LoadProjectJob::GetType() const
	{
		return "jfm.project.load";
	}

	void LoadProjectJob::Execute(JobContext &context)
	{
		context.SetStage("Read");
		context.SetProgress(0.0f, 2.0f);
		std::string text;
		std::string error;
		if (!TextFileIO::Load(m_Path, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectLoadFailed{m_Path, error, m_Operation});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetStage("Deserialize");
		context.SetProgress(1.0f, 2.0f);
		auto project = JFMProjectYamlSerializer::Deserialize(text);
		if (!project)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectLoadFailed{m_Path, project.Error().technicalDetails, m_Operation});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
        std::vector<JFMMonteCarloBundleRecord> samples;
        for (auto& info:project->characteristics) if (info.monteCarlo && !info.monteCarlo->outputPath.Empty()) {
            context.ThrowIfCancellationRequested();
            auto directory=info.monteCarlo->outputPath;
            if (directory.Native().is_relative()) directory=m_Path.parent_path()/directory;
            const auto file=directory/Path("samples.h5");
            const bool binary=FileSystem::Exists(file.Native());
            if (binary || FileSystem::Exists((directory/Path("parameters.tsv")).Native())) {
                auto loaded=binary ? ReadJFMMonteCarloHdf5(file) : ReadJFMMonteCarloTsv(directory);
                if (loaded) { samples.push_back({info.id,CreateRef<const JFMMonteCarloResult>(std::move(*loaded))}); info.monteCarlo->sampleSaveStatus="saved"; }
                else { info.monteCarlo->sampleSaveStatus="failed"; info.monteCarlo->sampleSaveError=loaded.Error().technicalDetails; }
                info.monteCarlo->outputPath=directory;
            } else if (info.monteCarlo->sampleSaveStatus=="saved" || info.monteCarlo->sampleSaveStatus=="pending") {
                info.monteCarlo->sampleSaveStatus="failed";
                info.monteCarlo->sampleSaveError="MC sample file is missing. Restore the sample directory or rerun MC.";
            }
        }
        if (m_EventBus) {
            ProjectLoadCompleted event{m_Path,std::move(*project),m_Operation}; event.monteCarloResults=std::move(samples);
            m_EventBus->Queue(std::move(event));
        }
	}

	SaveProjectBundleJob::SaveProjectBundleJob(
		Ref<EventBus> eventBus,
		Path path,
		JFMProjectBundleSnapshot snapshot,
		ProjectHandle project,
		JFMOperationId operation,
		std::uint64_t revision)
		: m_EventBus(std::move(eventBus)),
		  m_Path(std::move(path)),
		  m_Snapshot(std::move(snapshot)),
		  m_Project(project),
		  m_Operation(operation),
		  m_Revision(revision)
	{
	}

	std::string SaveProjectBundleJob::GetName() const
	{
		return "Save JFM Project Bundle";
	}

	std::string SaveProjectBundleJob::GetType() const
	{
		return "jfm.project.bundle.save";
	}

	void SaveProjectBundleJob::Execute(JobContext &context)
	{
		context.SetStage("Save bundle");
		context.SetProgress(0.0f, 1.0f);
		auto saved = JFMProjectBundleSerializer::Save(m_Path, m_Snapshot);
		if (!saved)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectBundleSaveFailed{m_Path, saved.Error().technicalDetails, m_Project, m_Operation});
			return;
		}
		context.SetProgress(1.0f, 1.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectBundleSaveCompleted{m_Path, m_Project, m_Operation, m_Revision, std::move(saved).Value().warnings});
	}

	LoadProjectBundleJob::LoadProjectBundleJob(Ref<EventBus> eventBus, Path path, JFMOperationId operation)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Operation(operation)
	{
	}

	std::string LoadProjectBundleJob::GetName() const
	{
		return "Load JFM Project Bundle";
	}

	std::string LoadProjectBundleJob::GetType() const
	{
		return "jfm.project.bundle.load";
	}

	void LoadProjectBundleJob::Execute(JobContext &context)
	{
		context.SetStage("Load bundle");
		context.SetProgress(0.0f, 1.0f);
		auto loaded = JFMProjectBundleSerializer::Load(m_Path);
		if (!loaded)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectBundleLoadFailed{m_Path, loaded.Error().technicalDetails, m_Operation});
			return;
		}
		context.SetProgress(1.0f, 1.0f);
		if (m_EventBus != nullptr)
		{
			JFMProjectBundleLoadResult result = std::move(loaded).Value();
			m_EventBus->Queue(ProjectBundleLoadCompleted{m_Path, std::move(result.snapshot), std::move(result.warnings), m_Operation});
		}
	}

	MaterializeProjectDataJob::MaterializeProjectDataJob(
		Ref<EventBus> eventBus,
		JFMProject project,
		Path pythonRoot,
		ProjectHandle projectHandle,
		JFMOperationId operation)
		: m_EventBus(std::move(eventBus)),
		  m_Project(std::move(project)),
		  m_PythonRoot(std::move(pythonRoot)),
		  m_ProjectHandle(projectHandle),
		  m_Operation(operation)
	{
	}

	std::string MaterializeProjectDataJob::GetName() const
	{
		return "Materialize JFM Project Data";
	}

	std::string MaterializeProjectDataJob::GetType() const
	{
		return "jfm.project.materialize_data";
	}

	bool MaterializeProjectDataJob::UsesPythonRuntime() const
	{
		return false; // the Python bridge acquires the GIL only around its calls
	}

	void MaterializeProjectDataJob::Execute(JobContext &context)
	{
		const std::size_t total = m_Project.characteristics.size();
		context.SetStage("Materialize raw data");
		context.SetProgress(0.0f, static_cast<float>(total));

		JFMPythonImportBridge bridge(m_PythonRoot);
		std::unordered_map<std::string, Detail::MaterializedSource> parsedByRequest;
		std::vector<JFMCharacteristicRecord> materialized;
		std::vector<std::string> warnings;
		std::size_t completed = 0;
		parsedByRequest.reserve(total);
		materialized.reserve(total);
		warnings.reserve(total);

		const auto advanceProgress = [&context, &completed, total]() {
			++completed;
			context.SetProgress(static_cast<float>(completed), static_cast<float>(total));
		};

		for (const JFMCharacteristicInfo &info : m_Project.characteristics)
		{
			context.ThrowIfCancellationRequested();
			context.SetMessage("Materializing " + info.name);

			if (info.sourcePath.Empty())
			{
				warnings.emplace_back("Curve " + info.name + ": no source_path. Restore its source file path or reimport the curve.");
				advanceProgress();
				continue;
			}

			const std::string sourcePath = info.sourcePath.Utf8();
			const std::string cacheKey = Detail::MaterializationCacheKey(info);
			auto parsed = parsedByRequest.find(cacheKey);
			if (parsed == parsedByRequest.end())
			{
				Detail::MaterializedSource source;
				auto result = bridge.ImportFiles({info.sourcePath}, info.importOptions, {});
                if (!result) {
                    source.failure = result.Error().userMessage + " " + result.Error().technicalDetails + " Fix: " + result.Error().suggestion;
                } else {
                    JFMImportBatchResult batch = std::move(result).Value();
                    warnings.insert(warnings.end(), batch.warnings.begin(), batch.warnings.end());
                    source.records.reserve(batch.characteristics.size());
                    for (JFMCharacteristic &characteristic : batch.characteristics) {
                        source.records.emplace_back(JFMCharacteristicRecord{
                            std::move(characteristic.info), CreateRef<const JFMCharacteristicData>(std::move(characteristic.data))});
                    }
                }
                parsed = parsedByRequest.emplace(cacheKey, std::move(source)).first;
			}

            if (!parsed->second.failure.empty()) {
                warnings.emplace_back("Curve " + info.name + " [" + std::to_string(info.id) + "]: " + parsed->second.failure);
                advanceProgress(); continue;
            }
			const JFMCharacteristicRecord *match = Detail::FindMaterializedMatch(parsed->second.records, info);
			if (match == nullptr)
			{
				if (FileSystem::Exists(info.sourcePath.Native()))
					warnings.emplace_back("Curve " + info.name + ": no matching data in " + sourcePath + ". Restore the original source file or reimport this curve.");
				advanceProgress();
				continue;
			}

			materialized.emplace_back(JFMCharacteristicRecord{info, match->data});
			advanceProgress();
		}

		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectDataMaterializationCompleted{
				std::move(materialized),
				std::move(warnings),
				m_ProjectHandle,
				m_Operation});
	}
} // namespace SST::Prototypes::JFM
