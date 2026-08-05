#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMPrefitBridgeResult
	{
		JFMPrefitState state;
		JFMPrefitCurve curve;
	};

	class JFMPythonPrefitBridge
	{
	public:
		explicit JFMPythonPrefitBridge(Path pythonRoot);

		[[nodiscard]] Result<JFMPrefitBridgeResult> Run(const JFMCharacteristicRecord &characteristic) const;

		// Hierarchical branch/model/range search (docs/model-detection.md):
		// the model_id in `characteristic` is ignored, the Python search picks
		// its own and reports it back in the result's modelId.
		[[nodiscard]] Result<JFMPrefitBridgeResult> RunAuto(const JFMCharacteristicRecord &characteristic) const;

	private:
		Path m_PythonRoot;
	};
}
