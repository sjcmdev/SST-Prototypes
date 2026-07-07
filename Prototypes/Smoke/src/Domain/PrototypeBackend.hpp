#pragma once

namespace SST::Prototypes::Smoke
{
	struct PrototypeInput
	{
		double voltage = 1.2;
		double temperatureKelvin = 300.0;
	};

	struct PrototypeResult
	{
		double carrierDensity = 0.0;
		double mobility = 0.0;
		double conductivity = 0.0;
	};

	class PrototypeBackend final
	{
	public:
		[[nodiscard]] static PrototypeResult Evaluate(const PrototypeInput &input)
		{
			const double temperatureScale = 300.0 / input.temperatureKelvin;
			const double carrierDensity = 1.0e16 * input.voltage;
			const double mobility = 1400.0 * temperatureScale;
			return PrototypeResult{
				carrierDensity,
				mobility,
				carrierDensity * mobility * 1.602176634e-19};
		}
	};
} // namespace SST::Prototypes::Smoke

