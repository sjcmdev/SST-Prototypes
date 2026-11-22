#pragma once
#include "Workflow.hpp"
namespace SST::ScapsLab {
template<class F> void EditFixture(DeviceState& state,F edit) {auto document=state.Document();edit(document);state.SetDocument(document);}
}
