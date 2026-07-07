#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/JFMDomainLayer.hpp"
#include "IO/JFMIOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/JFMPrototypePanel.hpp"
#include "Storage/JFMStorageLayer.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST
{
	Application CreateApplication(int argc, char **argv)
	{
		return Application::Create(argc, argv, [](Application &application) {
			auto resultStore = CreateRef<Prototypes::JFM::PrototypeResultStore>();
			application.GetEditorLayer().RegisterPanel<Prototypes::JFM::JFMPrototypePanel>(resultStore);
			application.PushLayer(CreateUnique<Prototypes::JFM::JFMStorageLayer>(resultStore));
			application.PushLayer(CreateUnique<Prototypes::JFM::JFMIOLayer>(resultStore));
			application.PushLayer(CreateUnique<Prototypes::JFM::JFMDomainLayer>(std::move(resultStore)));
		});
	}
} // namespace SST

