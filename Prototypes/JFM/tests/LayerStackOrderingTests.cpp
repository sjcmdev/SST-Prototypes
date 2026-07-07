#include <gtest/gtest.h>

#include <vector>

#include "Core/LayerStack.hpp"
#include "Core/Utils/Memory.hpp"

namespace SST
{
	TEST(LayerStackOrderingTests, InsertsDomainBeforeImGui)
	{
		LayerStack stack;
		stack.PushLayer(CreateUnique<Layer>("CoreLayer"));
		stack.PushLayer(CreateUnique<Layer>("IOLayer"));
		stack.PushLayer(CreateUnique<Layer>("ImGuiLayer"));
		stack.PushLayer(CreateUnique<Layer>("EditorLayer"));

		stack.InsertLayerBefore(LayerId::ImGui, CreateUnique<Layer>("DomainLayer"));

		std::vector<std::string> names;
		for (const auto &layer : stack)
			names.push_back(layer->GetName());

		ASSERT_EQ(names.size(), 5);
		EXPECT_EQ(names[0], "CoreLayer");
		EXPECT_EQ(names[1], "IOLayer");
		EXPECT_EQ(names[2], "DomainLayer");
		EXPECT_EQ(names[3], "ImGuiLayer");
		EXPECT_EQ(names[4], "EditorLayer");
	}
} // namespace SST
