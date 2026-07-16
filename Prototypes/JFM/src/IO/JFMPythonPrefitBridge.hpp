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

	private:
		Path m_PythonRoot;
	};
}
