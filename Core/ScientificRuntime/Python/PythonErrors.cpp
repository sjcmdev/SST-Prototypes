#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonErrors.hpp"

namespace SST
{
	StructuredError MakePythonUnavailableError(
		const std::string &userMessage,
		const std::string &technicalDetails,
		const std::string &suggestion,
		const std::string &code)
	{
		return StructuredError{
			ErrorCategory::Python,
			Severity::Error,
			userMessage,
			technicalDetails,
			suggestion,
			"ScientificRuntime/Python",
			code};
	}

	StructuredError MakePythonExecutionError(
		const std::string &userMessage,
		const std::string &technicalDetails,
		const std::string &suggestion,
		const std::string &code)
	{
		return StructuredError{
			ErrorCategory::Python,
			Severity::Error,
			userMessage,
			technicalDetails,
			suggestion,
			"ScientificRuntime/Python",
			code};
	}
} // namespace SST
