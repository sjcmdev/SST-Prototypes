#include <gtest/gtest.h>

#include "Domain/PrototypeBackend.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	TEST(__PROTOTYPE_NAME__BackendSmoke, EvaluatesDeterministicConductivity)
	{
		const PrototypeResult result = PrototypeBackend::Evaluate(PrototypeInput{2.0, 300.0});

		EXPECT_DOUBLE_EQ(result.carrierDensity, 2.0e16);
		EXPECT_DOUBLE_EQ(result.mobility, 1400.0);
		EXPECT_NEAR(result.conductivity, 4.4860945752, 1.0e-12);
	}
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

