#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/SmokeDomainLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/SmokePrototypePanel.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST
{
	void SetupApplication(Application &application)
	{
		using namespace Prototypes::Smoke;
		auto resultStore = CreateRef<PrototypeResultStore>();
		application.GetEditorLayer().RegisterPanel<SmokePrototypePanel>(resultStore);
		application.InsertLayerBefore(LayerId::ImGui, CreateUnique<SmokeDomainLayer>(std::move(resultStore)));
	}
} // namespace SST
