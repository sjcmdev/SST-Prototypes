#pragma once
#include "PlotEditing.hpp"
#include "Core/Undo/UndoStack.hpp"
#include <sst/plotlab/GraphSnapshot.hpp>
#include <sst/ui/Plot/AxisJson.hpp>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nlohmann/json.hpp>
#include <algorithm>

namespace SST::PlotEditingPython {
using Json = nlohmann::json;
namespace plot = sst::ui::plot;
namespace pl = sst::plotlab;
namespace nb = nanobind;
// Called only at nanobind edges; GUI callers construct PlotEdit directly.
inline plot::PlotEdit Decode(const Json& request, const pl::Project& project, pl::Id layer) {
    plot::PlotEdit edit;
    const char* actions[]={"axis","legend","style","group_style","visibility","rename","move","remove","rescale","group_colors"};
    const auto action=request.at("action").get<std::string>();
    const auto found=std::find(std::begin(actions),std::end(actions),action);
    if(found==std::end(actions)) throw std::invalid_argument("plot.edit.action: unknown action");
    edit.kind=static_cast<plot::PlotEditKind>(found-std::begin(actions));
    if(request.contains("series_ids")) for(const auto& id:request.at("series_ids"))
        edit.series.push_back(id.is_string()?id.get<std::string>():std::to_string(id.get<pl::Id>()));
    const auto& spec=std::get<pl::Layer>(project.object(layer).data).spec;
    if(edit.kind==plot::PlotEditKind::Axis) {
        const char* names[]={"bottom","left","top","right"};
        const auto name=request.value("side",std::string{"bottom"});
        const auto side=std::find(std::begin(names),std::end(names),name);
        if(side==std::end(names)) throw std::invalid_argument("plot.edit.side: expected bottom/left/top/right");
        edit.side=static_cast<plot::AxisSide>(side-std::begin(names));
        if(!request.at("axis").is_null()) {
            const auto base=edit.side==plot::AxisSide::Bottom?spec.x:edit.side==plot::AxisSide::Left?spec.y:edit.side==plot::AxisSide::Top?spec.top.value_or(spec.x):spec.right.value_or(spec.y);
            auto value=plot::EncodeAxis(base);value.merge_patch(request.at("axis"));edit.axis=plot::DecodeAxis(value);
        }
    }
    edit.legend=spec.legend;
    if(edit.kind==plot::PlotEditKind::Legend) {
        const auto& value=request.at("legend");auto& legend=edit.legend;
        legend.visible=value.value("visible",legend.visible);legend.outside=value.value("outside",legend.outside);
        legend.interactive=value.value("interactive",legend.interactive);legend.frame=value.value("frame",legend.frame);
        legend.columns=value.value("columns",legend.columns);legend.fontSizePt=value.value("font_pt",legend.fontSizePt);
        legend.position=static_cast<plot::LegendPosition>(value.value("position",static_cast<int>(legend.position)));
    }
    if(edit.kind==plot::PlotEditKind::Style || edit.kind==plot::PlotEditKind::GroupStyle) {
        for(auto id:project.children(layer)) if(!edit.series.empty() && std::to_string(id)==plot::SeriesKey(edit.series.front())) edit.style=project.series(id).style;
        const auto& value=request.at("style");auto& style=edit.style;
        style.color=value.value("color",style.color);style.markerFill=value.value("fill",style.markerFill);style.markerOutline=value.value("outline",style.markerOutline);
        if(value.contains("marker")) style.markerAuto=false;
        style.marker=static_cast<plot::Marker>(value.value("marker",static_cast<int>(style.marker)));
        style.lineStyle=static_cast<plot::LineStyle>(value.value("line_style",static_cast<int>(style.lineStyle)));
        style.lineWidthPt=value.value("line_pt",style.lineWidthPt);style.markerSizePt=value.value("marker_pt",style.markerSizePt);
    }
    edit.visible=request.value("visible",true);edit.text=request.value("text",std::string{});edit.offset=request.value("offset",0);
    return edit;
}
inline Json Request(nb::dict request) {
    return Json::parse(nb::cast<std::string>(nb::module_::import_("json").attr("dumps")(request)));
}
inline nb::object Response(const Json& value) { return nb::module_::import_("json").attr("loads")(nb::str(value.dump().c_str())); }

template<class Host> struct Session {
    Ref<pl::Project> project;
    Ref<CommandRegistry> commands=CreateRef<CommandRegistry>();
    Ref<UndoStack> history=CreateRef<UndoStack>();
    explicit Session(const std::string& text) : project(CreateRef<pl::Project>(pl::Project::fromJson(text))) { commands->SetUndoStack(history); }
    nb::object Edit(pl::Id layer, nb::dict request) {
        try {
            const auto result=ExecutePlotEdit(*commands,project,layer,Decode(Request(request),*project,layer));
            if(!result) return Response({{"ok",false},{"error",{{"code",result.Error().code},{"message",result.Error().technicalDetails}}}});
            return Response({{"ok",true},{"revision",project->revision()},{"error",nullptr}});
        } catch(const std::exception& e) {return Response({{"ok",false},{"error",{{"code","plot.edit.invalid"},{"message",e.what()}}}});}
    }
};
template<class Host> void Bind(nb::module_& module) {
    using S=Session<Host>;
    nb::class_<S>(module,"PlotSession")
        .def(nb::init<const std::string&>(),nb::arg("project_json"))
        .def("edit",&S::Edit,nb::arg("layer_id"),nb::arg("request"))
        .def("undo",[](S& s) {auto r=s.history->Undo();return bool(r);})
        .def("redo",[](S& s) {auto r=s.history->Redo();return bool(r);})
        .def("project_json",[](S& s) {return s.project->toJson();})
        .def("save",[](S& s,const std::string& path) {s.project->save(std::filesystem::u8path(path));})
        .def("snapshot",[](S& s,pl::Id graph,nb::list ranges) {
            if(nb::len(ranges)!=4) throw std::invalid_argument("Expected four axis limits");
            const auto layers=s.project->children(graph);
            if(layers.size()!=1) throw std::invalid_argument("Expected one plot layer");
            auto view=std::get<pl::Layer>(s.project->object(layers.front()).data).view;
            view.viewport=plot::Bounds{nb::cast<double>(ranges[0]),nb::cast<double>(ranges[1]),nb::cast<double>(ranges[2]),nb::cast<double>(ranges[3])};
            return nb::module_::import_("json").attr("loads")(nb::str(pl::snapshotJson(*pl::snapshotGraph(*s.project,graph,view)).c_str()));
        },nb::arg("graph_id"),nb::arg("ranges"));
}
}
