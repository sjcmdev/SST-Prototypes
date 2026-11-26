#pragma once
#include "Library/Session.hpp"
#include <nlohmann/json.hpp>
namespace SST::ScapsLab::Library::Python {
Request ReadRequest(const nlohmann::json&);
nlohmann::json Reply(const Session&,bool includeData=false);
// Also used at the project file edge; never called by a panel, solver or job to exchange payloads.
nlohmann::json StateDocument(const State&);
std::shared_ptr<const State> ReadState(const nlohmann::json&);
nlohmann::json BrowserDocument(const Browser&);
Browser ReadBrowser(const nlohmann::json&);
nlohmann::json DraftsDocument(const DraftMap&);
std::shared_ptr<const DraftMap> ReadDrafts(const nlohmann::json&,const State&);
}
