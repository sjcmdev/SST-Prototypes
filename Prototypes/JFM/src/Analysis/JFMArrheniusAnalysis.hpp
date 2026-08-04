#pragma once

#include "Analysis/JFMFitEngine.hpp"
#include "Domain/JFMTypes.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace SST::Prototypes::JFM
{
	enum class JFMArrheniusQuantity
	{
		A,
		InvA,
		ALnI0,
		LnI0,
		Rs,
		Rsh,
		Alpha,
		Rsh2
	};

	enum class JFMArrheniusParameterSource
	{
		Prefit,
		Fit,
		Tuned
	};

	struct JFMArrheniusPoint
	{
		std::string name;
		double temperatureK = 0.0;
		double x = 0.0;
		double y = 0.0;
		std::optional<double> uncertainty;
		std::string source;
		std::optional<double> a;
		std::optional<double> aStd;
		std::optional<double> i0;
		std::optional<double> i0Std;
	};

	inline constexpr const char *JFMArrheniusQuantityName(JFMArrheniusQuantity quantity)
	{
		switch (quantity)
		{
		case JFMArrheniusQuantity::A: return "A(T)";
		case JFMArrheniusQuantity::InvA: return "1/A(T)";
		case JFMArrheniusQuantity::ALnI0: return "A*ln(I0)";
		case JFMArrheniusQuantity::LnI0: return "ln(I0)";
		case JFMArrheniusQuantity::Rs: return "Rs(T)";
		case JFMArrheniusQuantity::Rsh: return "Rsh(T)";
		case JFMArrheniusQuantity::Alpha: return "alpha(T)";
		case JFMArrheniusQuantity::Rsh2: return "Rsh2(T)";
		}
		return "";
	}

	inline bool JFMArrheniusUsesInvTemperature(JFMArrheniusQuantity quantity)
	{
		return quantity == JFMArrheniusQuantity::ALnI0 || quantity == JFMArrheniusQuantity::LnI0;
	}

	inline const JFMParameterEstimate *JFMFindParameter(const JFMCharacteristicInfo &info, const std::string &name, bool preferFit, std::string *source = nullptr)
	{
		const auto fromState = [&](const auto &state, const char *stage) -> const JFMParameterEstimate * {
			if (!state || state->status != "completed")
				return nullptr;
			const auto found = state->parameters.find(name);
			if (found == state->parameters.end())
				return nullptr;
			if (source != nullptr)
				*source = stage;
			return &found->second;
		};
		if (preferFit)
		{
			if (const auto *value = fromState(info.fit, "fit"))
				return value;
			return fromState(info.prefit, "prefit");
		}
		if (const auto *value = fromState(info.prefit, "prefit"))
			return value;
		return fromState(info.fit, "fit");
	}

	inline std::optional<double> JFMParameterValue(const JFMCharacteristicInfo &info, const std::string &name, bool preferFit = true)
	{
		const JFMParameterEstimate *estimate = JFMFindParameter(info, name, preferFit);
		return estimate == nullptr ? std::nullopt : std::optional<double>{estimate->value};
	}

	inline std::optional<double> JFMTunedParameterValue(const JFMCharacteristicInfo &info, const std::string &name)
	{
		const auto found = info.metadata.find("tuned." + name);
		if (found != info.metadata.end())
		{
			try
			{
				const double value = std::stod(found->second);
				if (std::isfinite(value))
					return value;
			}
			catch (const std::exception &)
			{
			}
		}
		return JFMParameterValue(info, name, true);
	}

	inline std::optional<double> JFMParameterValue(const JFMCharacteristicInfo &info, const std::string &name, JFMArrheniusParameterSource source)
	{
		if (source == JFMArrheniusParameterSource::Tuned)
			return JFMTunedParameterValue(info, name);
		return JFMParameterValue(info, name, source == JFMArrheniusParameterSource::Fit);
	}

	inline std::optional<double> JFMArrheniusUncertainty(
		JFMArrheniusQuantity quantity,
		const JFMCharacteristicInfo &info,
		bool preferFit)
	{
		std::string source;
		const JFMParameterEstimate *a = JFMFindParameter(info, "A", preferFit, &source);
		const JFMParameterEstimate *i0 = JFMFindParameter(info, "I0", preferFit);
		auto direct = [&](const char *name) -> std::optional<double> {
			if (const JFMParameterEstimate *estimate = JFMFindParameter(info, name, preferFit))
				return estimate->standardError;
			return std::nullopt;
		};
		switch (quantity)
		{
		case JFMArrheniusQuantity::A: return direct("A");
		case JFMArrheniusQuantity::Rs: return direct("Rs");
		case JFMArrheniusQuantity::Rsh: return direct("Rsh");
		case JFMArrheniusQuantity::Alpha: return direct("alpha");
		case JFMArrheniusQuantity::Rsh2: return direct("Rsh2");
		case JFMArrheniusQuantity::InvA:
			if (a != nullptr && a->standardError && a->value != 0.0)
				return *a->standardError / (a->value * a->value);
			break;
		case JFMArrheniusQuantity::LnI0:
			if (i0 != nullptr && i0->standardError && i0->value > 0.0)
				return *i0->standardError / i0->value;
			break;
		case JFMArrheniusQuantity::ALnI0:
			if (a != nullptr && i0 != nullptr && i0->value > 0.0 && (a->standardError || i0->standardError))
			{
				const double lnI0 = std::log(i0->value);
				const double da = a->standardError.value_or(0.0);
				const double di0 = i0->standardError.value_or(0.0);
				return std::sqrt((lnI0 * da) * (lnI0 * da) + (a->value * di0 / i0->value) * (a->value * di0 / i0->value));
			}
			break;
		}
		return std::nullopt;
	}

	inline std::optional<double> JFMArrheniusMonteCarloUncertainty(
		JFMArrheniusQuantity quantity,
		const JFMMonteCarloResult &result)
	{
		const auto parameterIndex = [&](std::string_view name) -> std::optional<std::size_t> {
			const auto found = std::find(result.parameterNames.begin(), result.parameterNames.end(), name);
			if (found == result.parameterNames.end())
				return std::nullopt;
			return static_cast<std::size_t>(std::distance(result.parameterNames.begin(), found));
		};
		const auto aIndex = parameterIndex("A");
		const auto i0Index = parameterIndex("I0");
		const char *directName = nullptr;
		switch (quantity)
		{
		case JFMArrheniusQuantity::A: directName = "A"; break;
		case JFMArrheniusQuantity::Rs: directName = "Rs"; break;
		case JFMArrheniusQuantity::Rsh: directName = "Rsh"; break;
		case JFMArrheniusQuantity::Alpha: directName = "alpha"; break;
		case JFMArrheniusQuantity::Rsh2: directName = "Rsh2"; break;
		default: break;
		}
		const std::optional<std::size_t> directIndex = directName == nullptr ? std::nullopt : parameterIndex(directName);

		std::vector<double> values;
		values.reserve(result.samples.size());
		for (const JFMMonteCarloSample &sample : result.samples)
		{
			if (!sample.converged)
				continue;
			const auto sampleParameter = [&](std::optional<std::size_t> index) -> std::optional<double> {
				if (!index || *index >= sample.parameters.size() || !std::isfinite(sample.parameters[*index]))
					return std::nullopt;
				return sample.parameters[*index];
			};
			const auto a = sampleParameter(aIndex);
			const auto i0 = sampleParameter(i0Index);
			std::optional<double> value;
			switch (quantity)
			{
			case JFMArrheniusQuantity::A:
			case JFMArrheniusQuantity::Rs:
			case JFMArrheniusQuantity::Rsh:
			case JFMArrheniusQuantity::Alpha:
			case JFMArrheniusQuantity::Rsh2:
				value = sampleParameter(directIndex);
				break;
			case JFMArrheniusQuantity::InvA:
				if (a && *a != 0.0)
					value = 1.0 / *a;
				break;
			case JFMArrheniusQuantity::LnI0:
				if (i0 && *i0 > 0.0)
					value = std::log(*i0);
				break;
			case JFMArrheniusQuantity::ALnI0:
				if (a && i0 && *i0 > 0.0)
					value = *a * std::log(*i0);
				break;
			}
			if (value && std::isfinite(*value))
				values.push_back(*value);
		}
		if (values.size() < 2)
			return std::nullopt;
		double mean = 0.0;
		for (double value : values)
			mean += value;
		mean /= static_cast<double>(values.size());
		double variance = 0.0;
		for (double value : values)
		{
			const double delta = value - mean;
			variance += delta * delta;
		}
		return std::sqrt(variance / static_cast<double>(values.size() - 1));
	}

	inline std::optional<double> JFMArrheniusValue(JFMArrheniusQuantity quantity, const JFMCharacteristicInfo &info, bool preferFit = true)
	{
		const auto a = JFMParameterValue(info, "A", preferFit);
		const auto i0 = JFMParameterValue(info, "I0", preferFit);
		switch (quantity)
		{
		case JFMArrheniusQuantity::A: return a;
		case JFMArrheniusQuantity::InvA:
			if (a && *a != 0.0)
				return 1.0 / *a;
			break;
		case JFMArrheniusQuantity::ALnI0:
			if (a && i0 && *i0 > 0.0)
				return *a * std::log(*i0);
			break;
		case JFMArrheniusQuantity::LnI0:
			if (i0 && *i0 > 0.0)
				return std::log(*i0);
			break;
		case JFMArrheniusQuantity::Rs: return JFMParameterValue(info, "Rs", preferFit);
		case JFMArrheniusQuantity::Rsh: return JFMParameterValue(info, "Rsh", preferFit);
		case JFMArrheniusQuantity::Alpha: return JFMParameterValue(info, "alpha", preferFit);
		case JFMArrheniusQuantity::Rsh2: return JFMParameterValue(info, "Rsh2", preferFit);
		}
		return std::nullopt;
	}

	inline std::optional<double> JFMArrheniusValue(JFMArrheniusQuantity quantity, const JFMCharacteristicInfo &info, JFMArrheniusParameterSource source)
	{
		const auto a = JFMParameterValue(info, "A", source);
		const auto i0 = JFMParameterValue(info, "I0", source);
		switch (quantity)
		{
		case JFMArrheniusQuantity::A: return a;
		case JFMArrheniusQuantity::InvA:
			if (a && *a != 0.0)
				return 1.0 / *a;
			break;
		case JFMArrheniusQuantity::ALnI0:
			if (a && i0 && *i0 > 0.0)
				return *a * std::log(*i0);
			break;
		case JFMArrheniusQuantity::LnI0:
			if (i0 && *i0 > 0.0)
				return std::log(*i0);
			break;
		case JFMArrheniusQuantity::Rs: return JFMParameterValue(info, "Rs", source);
		case JFMArrheniusQuantity::Rsh: return JFMParameterValue(info, "Rsh", source);
		case JFMArrheniusQuantity::Alpha: return JFMParameterValue(info, "alpha", source);
		case JFMArrheniusQuantity::Rsh2: return JFMParameterValue(info, "Rsh2", source);
		}
		return std::nullopt;
	}

	inline std::vector<JFMArrheniusPoint> BuildJFMArrheniusPoints(
		const std::vector<JFMCharacteristicRecord> &records,
		JFMArrheniusQuantity quantity,
		bool preferFit = true)
	{
		std::vector<JFMArrheniusPoint> points;
		for (const JFMCharacteristicRecord &record : records)
		{
			if (!record.info.temperatureK || *record.info.temperatureK <= 0.0)
				continue;
			const auto value = JFMArrheniusValue(quantity, record.info, preferFit);
			if (!value || !std::isfinite(*value))
				continue;
			std::string source;
			(void)JFMFindParameter(record.info, quantity == JFMArrheniusQuantity::LnI0 ? "I0" : "A", preferFit, &source);
			const double temperature = *record.info.temperatureK;
			points.push_back(JFMArrheniusPoint{
				record.info.name,
				temperature,
				JFMArrheniusUsesInvTemperature(quantity) ? 1000.0 / temperature : temperature,
				*value,
				JFMArrheniusUncertainty(quantity, record.info, preferFit),
				source});
		}
		std::sort(points.begin(), points.end(), [](const JFMArrheniusPoint &lhs, const JFMArrheniusPoint &rhs) {
			return lhs.x < rhs.x;
		});
		return points;
	}

	inline bool BuildJFMArrheniusTsv(const std::vector<JFMCharacteristicRecord> &records, std::string &text, std::string &error)
	{
		std::ostringstream out;
		out << "name\tstage\tT_K\tinvT_1000_per_K\tA\tA_std\tinvA\tinvA_std\tA_ln_I0\tA_ln_I0_std\tln_I0\tln_I0_std\tRs\tRs_std\tRsh\tRsh_std\talpha\talpha_std\tRsh2\tRsh2_std\n";
		std::vector<JFMCharacteristicRecord> sorted = records;
		std::sort(sorted.begin(), sorted.end(), [](const JFMCharacteristicRecord &lhs, const JFMCharacteristicRecord &rhs) {
			return lhs.info.temperatureK.value_or(0.0) < rhs.info.temperatureK.value_or(0.0);
		});
		std::size_t rows = 0;
		for (const JFMCharacteristicRecord &record : sorted)
		{
			if (!record.info.temperatureK || *record.info.temperatureK <= 0.0)
				continue;
			std::string stage;
			(void)JFMFindParameter(record.info, "A", true, &stage);
			out << record.info.name << '\t' << stage << '\t' << *record.info.temperatureK << '\t' << 1000.0 / *record.info.temperatureK;
			for (JFMArrheniusQuantity quantity : {JFMArrheniusQuantity::A, JFMArrheniusQuantity::InvA, JFMArrheniusQuantity::ALnI0, JFMArrheniusQuantity::LnI0, JFMArrheniusQuantity::Rs, JFMArrheniusQuantity::Rsh, JFMArrheniusQuantity::Alpha, JFMArrheniusQuantity::Rsh2})
			{
				const auto value = JFMArrheniusValue(quantity, record.info, true);
				const auto uncertainty = JFMArrheniusUncertainty(quantity, record.info, true);
				out << '\t';
				if (value)
					out << *value;
				out << '\t';
				if (uncertainty)
					out << *uncertainty;
			}
			out << '\n';
			++rows;
		}
		if (rows == 0)
		{
			error = "No fitted/prefitted characteristics with temperature found.";
			return false;
		}
		text = out.str();
		return true;
	}
} // namespace SST::Prototypes::JFM
