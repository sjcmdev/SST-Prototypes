#pragma once

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

namespace SST
{
	class ScientificPythonRuntime final
	{
	public:
		[[nodiscard]] Result<void> Initialize();
		void Shutdown();

		[[nodiscard]] bool IsReady() const;

	private:
		PythonInterpreter m_Interpreter;
		bool m_IsReady = false;
	};
} // namespace SST
