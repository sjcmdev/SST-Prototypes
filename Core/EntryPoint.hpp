#pragma once

#include "App/Application.hpp"

namespace SST
{
	void SetupApplication(Application &application);
}

int main(int argc, char **argv)
{
	auto app = SST::Application::Create(argc, argv, SST::SetupApplication);
	return app.Run();
}
