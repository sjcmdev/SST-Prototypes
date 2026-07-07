#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/__PROTOTYPE_NAME__DomainLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/__PROTOTYPE_NAME__PrototypePanel.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST
{
	void SetupApplication(Application &application)
	{
		auto resultStore = CreateRef<Prototypes::__PROTOTYPE_NAME__::PrototypeResultStore>();
		application.GetEditorLayer().RegisterPanel<Prototypes::__PROTOTYPE_NAME__::__PROTOTYPE_NAME__PrototypePanel>(resultStore);
		application.InsertLayerBefore(
			LayerId::ImGui,
			CreateUnique<Prototypes::__PROTOTYPE_NAME__::__PROTOTYPE_NAME__DomainLayer>(std::move(resultStore)));
	}
} // namespace SST
