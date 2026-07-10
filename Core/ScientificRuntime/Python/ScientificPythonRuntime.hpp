#pragma once

#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/ASEBridge.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"
#include "ScientificRuntime/Python/PythonInterpreter.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace SST
{
	class ScientificPythonRuntime final
	{
	public:
		[[nodiscard]] Result<void> Initialize();
		void Shutdown();

		[[nodiscard]] bool IsReady() const;
		[[nodiscard]] Result<ScriptRunResult> RunBridgeRoundtripDemo() const;

	private:
		PythonInterpreter m_Interpreter;
		ScriptRunner m_ScriptRunner;
		bool m_IsReady = false;
	};
} // namespace SST
