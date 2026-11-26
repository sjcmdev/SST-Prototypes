#pragma once

#include "App/Application.hpp"

namespace SST
{
	void SetupApplication(Application &application);
}

int main(int argc, char **argv)
{
	return SST::RunApplication(argc, argv, SST::SetupApplication);
}
