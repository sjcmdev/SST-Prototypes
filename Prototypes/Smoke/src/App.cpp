#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/SmokeDomainLayer.hpp"
#include "IO/SmokeIOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/SmokePrototypePanel.hpp"
#include "Storage/SmokeStorageLayer.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST
{
	Application CreateApplication(int argc, char **argv)
	{
		using namespace Prototypes::Smoke;
		return Application::Create(argc, argv, [](Application &application) {
			auto resultStore = CreateRef<PrototypeResultStore>();
			application.GetEditorLayer().RegisterPanel<SmokePrototypePanel>(resultStore);
			application.PushLayer(CreateUnique<SmokeStorageLayer>(resultStore));
			application.PushLayer(CreateUnique<SmokeIOLayer>(resultStore));
			application.PushLayer(CreateUnique<SmokeDomainLayer>(std::move(resultStore)));
		});
	}
} // namespace SST

