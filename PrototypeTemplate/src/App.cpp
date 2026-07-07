#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/__PROTOTYPE_NAME__DomainLayer.hpp"
#include "IO/__PROTOTYPE_NAME__IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/__PROTOTYPE_NAME__PrototypePanel.hpp"
#include "Storage/__PROTOTYPE_NAME__StorageLayer.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST
{
	Application CreateApplication(int argc, char **argv)
	{
		return Application::Create(argc, argv, [](Application &application) {
			auto resultStore = CreateRef<Prototypes::__PROTOTYPE_NAME__::PrototypeResultStore>();
			application.GetEditorLayer().RegisterPanel<Prototypes::__PROTOTYPE_NAME__::__PROTOTYPE_NAME__PrototypePanel>(resultStore);
			application.PushLayer(CreateUnique<Prototypes::__PROTOTYPE_NAME__::__PROTOTYPE_NAME__StorageLayer>(resultStore));
			application.PushLayer(CreateUnique<Prototypes::__PROTOTYPE_NAME__::__PROTOTYPE_NAME__IOLayer>(resultStore));
			application.PushLayer(CreateUnique<Prototypes::__PROTOTYPE_NAME__::__PROTOTYPE_NAME__DomainLayer>(std::move(resultStore)));
		});
	}
} // namespace SST

