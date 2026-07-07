#pragma once

#include "App/Application.hpp"

namespace SST
{
	Application CreateApplication(int argc, char **argv);
}

int main(int argc, char **argv)
{
	auto app = SST::CreateApplication(argc, argv);
	return app.Run();
}
