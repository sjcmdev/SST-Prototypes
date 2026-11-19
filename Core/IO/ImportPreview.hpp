#pragma once

#include "Core/IO/ImportSources.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <cstdint>

namespace SST
{
    class PreviewGeneration
    {
    public:
        std::uint64_t Next() { return ++current; }
        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const { return generation == current; }
    private:
        std::uint64_t current = 0;
    };

    template<class T> struct ImportPreviewItem
    {
        ImportSource source;
        std::optional<T> value;
        std::optional<StructuredError> error;
    };

    // The caller owns the job and publishes only a current generation on its UI/command thread.
    template<class T, class Parser>
    std::vector<ImportPreviewItem<T>> PreviewSources(
        JobContext &context, std::span<const ImportSourceEntry> sources, Parser parse)
    {
        std::vector<ImportPreviewItem<T>> result;
        result.reserve(sources.size());
        context.SetStage("Import preview");
        context.SetProgress(0, static_cast<float>(sources.size()));
        for (const auto &entry : sources) {
            context.ThrowIfCancellationRequested();
            context.SetMessage(entry.source.path.Utf8());
            ImportPreviewItem<T> item{entry.source, {}, entry.error};
            if (!item.error) try {
                Result<T> parsed = parse(entry.source);
                if (parsed) item.value = std::move(parsed).Value();
                else item.error = parsed.Error();
            } catch (const JobCancelledException &) { throw; }
            catch (const std::exception &error) {
                item.error = StructuredError{ErrorCategory::IO, Severity::Warning, "Import preview failed",
                    error.what(), "Check the file and parser settings.", "ImportPreview", "import.preview_failed"};
            }
            context.ThrowIfCancellationRequested();
            result.push_back(std::move(item));
            context.SetProgress(static_cast<float>(result.size()), static_cast<float>(sources.size()));
        }
        context.ThrowIfCancellationRequested();
        return result;
    }
}
