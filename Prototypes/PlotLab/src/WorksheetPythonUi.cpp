#include "PlotLabWorkspace.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <set>

namespace SST::Prototypes::PlotLab {
void WorkspacePanel::PythonTrustBanner() {
    if(state->document->HasPython() && !state->document->PythonTrusted()) {
        ImGui::TextWrapped("Python formulas and scripts are disabled for this project.");
        if(ImGui::Button("Trust this project"))Attempt([&]{state->document->TrustPython();});
        ImGui::Separator();
    }
}
namespace {
bool Word(char c) {return std::isalnum(static_cast<unsigned char>(c)) || c=='_' || static_cast<unsigned char>(c)>=128;}
}
bool WorkspacePanel::CodeEditor(const char* label,std::string& text,CodeState& editor,pl::Id sheet,float editorHeight) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(120);
    const auto gutter=ImGui::GetCursorScreenPos();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+38);
    if(editor.refocus || editor.jumpLine) {ImGui::SetKeyboardFocusHere();editor.refocus=false;}
    const auto callback=[](ImGuiInputTextCallbackData* data) {
        auto& e=*static_cast<CodeState*>(data->UserData);
        if(e.moveCursor>=0){data->CursorPos=data->SelectionStart=data->SelectionEnd=std::min(e.moveCursor,data->BufTextLen);e.moveCursor=-1;}
        if(e.jumpLine) {
            int line=1,pos=0;while(pos<data->BufTextLen && line<e.jumpLine)if(data->Buf[pos++]=='\n')++line;
            data->CursorPos=data->SelectionStart=data->SelectionEnd=pos;e.jumpLine=0;
        }
        e.cursor=data->CursorPos;e.first=data->SelectionStart;e.last=data->SelectionEnd;return 0;
    };
    const auto inputId=ImGui::GetID("##code");
    const bool changed=ImGui::InputTextMultiline("##code",&text,{-1,editorHeight},ImGuiInputTextFlags_AllowTabInput|ImGuiInputTextFlags_CallbackAlways,callback,&editor);
    const bool active=ImGui::IsItemActive();const auto rect=GImGui->LastItemData.Rect;
    ImVec2 scroll{};if(auto* input=ImGui::GetInputTextState(inputId))scroll=input->Scroll;
    auto* draw=ImGui::GetWindowDrawList();const float lineHeight=ImGui::GetTextLineHeight();
    draw->PushClipRect({gutter.x,rect.Min.y},{rect.Max.x-16,rect.Max.y},true);
    std::size_t first=0;int line=1;
    const std::set<std::string_view> keywords={"import","from","as","def","return","for","in","if","else","elif","while","try","except","with","True","False","None","and","or","not","result","params"};
    while(first<=text.size()) {
        auto end=text.find('\n',first);if(end==std::string::npos)end=text.size();
        const float y=rect.Min.y+ImGui::GetStyle().FramePadding.y+(line-1)*lineHeight-scroll.y;
        if(y+lineHeight>=rect.Min.y && y<rect.Max.y) {
            const auto number=std::to_string(line);draw->AddText({gutter.x,y},ImGui::GetColorU32(ImGuiCol_TextDisabled),number.c_str());
            for(auto pos=first;pos<end;) {
                auto tokenEnd=pos+1;ImU32 color=0;
                if(text[pos]=='#'){tokenEnd=end;color=IM_COL32(106,160,110,255);}
                else if(text[pos]=='\'' || text[pos]=='"') {const char quote=text[pos];while(tokenEnd<end){if(text[tokenEnd++] == quote && text[tokenEnd-2]!='\\')break;}color=IM_COL32(210,165,110,255);}
                else if(Word(text[pos])) {while(tokenEnd<end && Word(text[tokenEnd]))++tokenEnd;if(keywords.contains(std::string_view(text).substr(pos,tokenEnd-pos)))color=IM_COL32(125,170,235,255);}
                if(color) {const float x=rect.Min.x+ImGui::GetStyle().FramePadding.x+ImGui::CalcTextSize(text.data()+first,text.data()+pos).x-scroll.x;draw->AddText({x,y},color,text.data()+pos,text.data()+tokenEnd);}
                pos=tokenEnd;
            }
        }
        if(end==text.size())break;first=end+1;++line;
    }
    draw->PopClipRect();
    auto& doc=*state->document;
    if(active) {
        const auto cursor=std::min(text.size(),static_cast<std::size_t>(std::max(editor.cursor,0)));
        const auto prefix=text.substr(0,cursor);
        if(prefix!=editor.completionSource || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space))) {
            if(editor.completionJob)doc.CancelPython(editor.completionJob);
            editor.completionSource=prefix;PythonRequest request;request.action=PythonAction::Completion;request.source=prefix;request.worksheet=sheet;
            if(&editor==&consoleCode)request.consoleSession=1;
            editor.completionJob=doc.SubmitPython(std::move(request));
        }
    }
    if(editor.completionJob) {
        const auto& result=doc.PollPython(editor.completionJob);
        if(!result.running){editor.completions=result.completions;editor.completionJob=0;}
    }
    bool inserted=false;
    if(!editor.completions.empty() && (active || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))) {
        ImGui::TextDisabled("Completion (Ctrl+Space); hover for signature and help");
        if(ImGui::BeginChild("##completions",{0,72},ImGuiChildFlags_Borders)) {
            for(const auto& item:editor.completions) {
                if(ImGui::Selectable((item.insert+item.signature).c_str())) {
                    auto cursor=std::min(text.size(),static_cast<std::size_t>(std::max(editor.cursor,0)));auto begin=cursor;
                    while(begin && Word(text[begin-1]))--begin;
                    text.replace(begin,cursor-begin,item.insert);editor.moveCursor=static_cast<int>(begin+item.insert.size());editor.refocus=true;inserted=true;
                }
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s%s\n%s",item.insert.c_str(),item.signature.c_str(),item.documentation.c_str());
            }
        }ImGui::EndChild();
    }
    ImGui::PopID();return changed||inserted;
}
bool WorkspacePanel::PythonHeader(pl::Id column) {
    auto& doc=*state->document;const auto& object=doc.project->object(column);const auto& definition=std::get<pl::Column>(object.data).formula;
    auto [it,added]=headerDrafts.try_emplace(column,definition?definition->expression:"");auto& text=it->second;auto& editor=headerCodes[column];
    if(editor.refocus){ImGui::SetKeyboardFocusHere();editor.refocus=false;}
    const auto callback=[](ImGuiInputTextCallbackData* data){
        auto& e=*static_cast<CodeState*>(data->UserData);
        if(data->EventFlag==ImGuiInputTextFlags_CallbackCompletion && !e.completions.empty()) {
            int first=data->CursorPos;while(first && Word(data->Buf[first-1]))--first;
            data->DeleteChars(first,data->CursorPos-first);data->InsertChars(first,e.completions.front().insert.c_str());
        }
        e.cursor=data->CursorPos;return 0;
    };
    bool apply=ImGui::InputText("##header",&text,ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_CallbackAlways|ImGuiInputTextFlags_CallbackCompletion,callback,&editor);
    const bool focused=ImGui::IsItemActive(), hovered=ImGui::IsItemHovered();
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)){EditColumn(column);valuesDialog=true;}
    if(focused) {
        const auto prefix=text.substr(0,std::min(text.size(),static_cast<std::size_t>(std::max(editor.cursor,0))));
        if(prefix!=editor.completionSource){if(editor.completionJob)doc.CancelPython(editor.completionJob);editor.completionSource=prefix;PythonRequest request;request.action=PythonAction::Completion;request.source=prefix;request.worksheet=object.parent;editor.completionJob=doc.SubmitPython(std::move(request));}
        if(lastPreviewSource!=text || headerPreviewColumn!=column){lastPreviewSource=text;headerPreviewColumn=column;previewDue=ImGui::GetTime()+.15;}
        if(previewDue && ImGui::GetTime()>=previewDue && doc.PythonTrusted()){previewDue=0;Attempt([&]{StartPythonFormula(column,text,true);});}
    }
    if(editor.completionJob){const auto& result=doc.PollPython(editor.completionJob);if(!result.running){editor.completions=result.completions;editor.completionJob=0;}}
    if(previewJob && headerPreviewColumn==column){const auto& result=doc.PollPython(previewJob);if(!result.running){formulaPreview=result;previewJob=0;}}
    if((focused || hovered) && (headerPreviewColumn==column || !editor.completions.empty())) {
        ImGui::BeginTooltip();
        if(focused){for(const auto& item:editor.completions)ImGui::Text("%s%s",item.insert.c_str(),item.signature.c_str());ImGui::TextDisabled("Tab: insert first completion; Enter: apply");}
        if(headerPreviewColumn==column){ImGui::TextUnformatted(previewJob?"Preview: calculating":"Preview: first 20 rows");if(!formulaPreview.error.empty())ImGui::TextWrapped("%s",formulaPreview.error.c_str());else for(std::size_t row=0;row<formulaPreview.preview.size();++row)ImGui::Text("%zu: %.6g",row+1,formulaPreview.preview[row]);}
        ImGui::EndTooltip();
    }
    if(apply)Attempt([&]{StartPythonFormula(column,text,false);});
    if(!focused && !apply && definition)text=definition->expression;
    return apply;
}
void WorkspacePanel::StartPythonFormula(pl::Id column,const std::string& source,bool previewOnly) {
    auto& doc=*state->document;
    PythonRequest request;request.action=previewOnly?PythonAction::Preview:PythonAction::Formula;
    request.column=column;request.worksheet=doc.project->object(column).parent;request.source=source;
    if(metadata==column && valuesDialog) {
        request.formula.rowFrom=rowFrom;request.formula.rowTo=rowTo;request.formula.recalculate=static_cast<pl::RecalculateMode>(recalculate);
        request.formula.excludeMasked=true;request.formula.excludeFiltered=excludeFiltered;
    } else request.formula=std::get<pl::Column>(doc.project->object(column).data).formula.value_or(pl::Formula{});
    if(previewOnly) {if(previewJob)doc.CancelPython(previewJob);previewJob=doc.SubmitPython(std::move(request));}
    else {if(pythonRun)doc.CancelPython(pythonRun);pythonRun=doc.SubmitPython(std::move(request));}
}
void WorkspacePanel::PythonFormulaEditor(pl::Id column) {
    auto& doc=*state->document;const auto sheet=doc.project->object(column).parent;
    const bool changed=CodeEditor("Column formula",formula,formulaCode,sheet,110);
    if(changed || lastPreviewSource!=formula) {lastPreviewSource=formula;previewDue=ImGui::GetTime()+.15;}
    if(previewDue && ImGui::GetTime()>=previewDue && doc.PythonTrusted()) {
        previewDue=0;Attempt([&]{StartPythonFormula(column,formula,true);});
    }
    if(previewJob) {
        const auto& result=doc.PollPython(previewJob);if(!result.running){formulaPreview=result;previewJob=0;}
    }
    ImGui::TextUnformatted(previewJob?"Preview: calculating (previous values retained)":"Preview: first 20 rows");
    if(!doc.PythonTrusted())ImGui::TextWrapped("Trust this project to enable Python evaluation.");
    else if(!formulaPreview.error.empty())ImGui::TextWrapped("%s",formulaPreview.error.c_str());
    else if(ImGui::BeginChild("##formula_preview",{0,65},ImGuiChildFlags_Borders)) {
        for(std::size_t i=0;i<formulaPreview.preview.size();++i) {if(i)ImGui::SameLine();ImGui::Text("%zu: %.6g",i+1,formulaPreview.preview[i]);}
        ImGui::EndChild();
    } else ImGui::EndChild();
}
void WorkspacePanel::OpenScript(pl::Id id,const std::string& source) {
    auto& doc=*state->document;
    if(scriptId && doc.project->objects().contains(scriptId))doc.scriptDrafts[scriptId]=scriptSource;
    if(!id)id=doc.SaveScript(doc.NextName(7,"Script"),source);
    scriptId=id;scriptName=doc.project->object(id).name;
    scriptSource=doc.scriptDrafts.contains(id)?doc.scriptDrafts.at(id):std::get<pl::Script>(doc.project->object(id).data).source;
    scriptWindow=true;scriptCode={};scriptParameters.clear();
    PythonRequest request;request.action=PythonAction::Parameters;request.source=scriptSource;parameterJob=doc.SubmitPython(std::move(request));
}
void WorkspacePanel::PythonWindows(bool hosted) {
    auto& doc=*state->document;
    const auto render=[&](const char* title,bool& open,const std::function<void()>& content) {
        if(hosted)windows.Render(title,open,0,content);
        else {if(ImGui::Begin(title,&open))content();ImGui::End();}
    };
    auto sheet=CurrentSheet();if(!sheet)sheet=doc.activeSheet;
    if(pythonRun) {
        const auto& result=doc.PollPython(pythonRun);
        if(!result.running) {pythonOutput=result.output+(result.ok?"Finished.":result.error);if(result.line)scriptCode.jumpLine=result.line;pythonRun=0;state->RefreshWindows();}
    }
    if(scriptWindow) {
        ImGui::SetNextWindowSize({800,650},ImGuiCond_FirstUseEver);
        render("Python Script",scriptWindow,[&] {
            if(!doc.project->objects().contains(scriptId)) {scriptWindow=false;return;}
            if(sst::ui::widgets::Text("Name",&scriptName))doc.Rename(scriptId,scriptName);
            if(CodeEditor("Code",scriptSource,scriptCode,sheet,260)) {
                doc.scriptDrafts[scriptId]=scriptSource;
                if(parameterJob)doc.CancelPython(parameterJob);
                PythonRequest request;request.action=PythonAction::Parameters;request.source=scriptSource;parameterJob=doc.SubmitPython(std::move(request));
            }
            if(parameterJob) {auto result=doc.PollPython(parameterJob);if(!result.running) {
                for(auto& parameter:scriptParameters)for(auto& next:result.parameters)if(parameter.name==next.name && (!next.slider || (parameter.value>=next.minimum && parameter.value<=next.maximum)))next.value=parameter.value;
                const auto& stored=doc.scriptParameterDrafts.contains(scriptId)?doc.scriptParameterDrafts.at(scriptId):std::get<pl::Script>(doc.project->object(scriptId).data).parameters;
                for(auto& next:result.parameters)if(auto it=stored.find(next.name);it!=stored.end() && (!next.slider || (it->second>=next.minimum && it->second<=next.maximum)))next.value=it->second;
                scriptParameters=result.parameters;parameterJob=0;
            }}
            for(auto& p:scriptParameters) {
                ImGui::PushID(p.name.c_str());
                bool changed=false;
                if(p.slider) {if(sst::ui::widgets::BeginLabelledRow(p.name.c_str())){changed=ImGui::SliderScalar("##value",ImGuiDataType_Double,&p.value,&p.minimum,&p.maximum,"%.6g");sst::ui::widgets::EndLabelledRow();}}
                else changed=sst::ui::widgets::Double(p.name.c_str(),&p.value,.01f,"%.6g");
                if(changed)doc.scriptParameterDrafts[scriptId][p.name]=p.value;
                ImGui::PopID();
            }
            const auto run=[&](bool selection) {
                PythonRequest request;request.worksheet=sheet;request.source=scriptSource;
                if(selection) {auto first=std::min(scriptCode.first,scriptCode.last),last=std::max(scriptCode.first,scriptCode.last);if(first==last)throw std::invalid_argument("Select code first");request.source=std::string(std::count(scriptSource.begin(),scriptSource.begin()+first,'\n'),'\n')+scriptSource.substr(first,last-first);}
                for(auto& p:scriptParameters)request.parameters[p.name]=p.value;
                pythonRun=doc.SubmitPython(std::move(request));
            };
            ImGui::BeginDisabled(pythonRun || !doc.PythonTrusted());
            if(ImGui::Button("Run"))Attempt([&]{run(false);});ImGui::SameLine();if(ImGui::Button("Run selection"))Attempt([&]{run(true);});ImGui::EndDisabled();
            if(!doc.PythonTrusted() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Trust this project in Project Explorer to enable Python.");
            ImGui::SameLine();if(ImGui::Button("Stop") && pythonRun)doc.CancelPython(pythonRun);
            ImGui::SameLine();if(ImGui::Button("Save in project"))Attempt([&]{doc.SaveScript(scriptName,scriptSource,scriptId);doc.scriptDrafts.erase(scriptId);});
            ImGui::SameLine();if(ImGui::Button("Open file"))Attempt([&]{if(auto path=OpenFileDialog({"Python script","py"})){std::string error;if(!TextFileIO::Load(*path,scriptSource,error))throw std::runtime_error(error);doc.scriptDrafts[scriptId]=scriptSource;}});
            ImGui::SameLine();if(ImGui::Button("Save file"))Attempt([&]{if(auto path=SaveFileDialog({"Python script","py"},"script.py")){std::string error;if(!TextFileIO::Save(*path,scriptSource,error))throw std::runtime_error(error);}});
            ImGui::Separator();ImGui::TextWrapped("%s",pythonOutput.c_str());
        });
    }
    if(consoleWindow) {
        ImGui::SetNextWindowSize({700,450},ImGuiCond_FirstUseEver);
        render("Python Console",consoleWindow,[&] {
            CodeEditor("Python",consoleSource,consoleCode,sheet,160);
            ImGui::BeginDisabled(pythonRun || !doc.PythonTrusted());
            if(ImGui::Button("Run"))Attempt([&]{PythonRequest request;request.action=PythonAction::Console;request.source=consoleSource;request.worksheet=sheet;pythonRun=doc.SubmitPython(std::move(request));});
            ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Stop") && pythonRun)doc.CancelPython(pythonRun);
            ImGui::TextWrapped("%s",pythonOutput.c_str());
        });
    }
    if(historyWindow) {
        render("History as Python",historyWindow,[&] {
            std::string source;for(const auto& line:doc.pythonHistory)source+=line+"\n";
            if(ImGui::Button("Send to new script"))Attempt([&]{OpenScript(0,source);});
            ImGui::SameLine();if(ImGui::Button("Copy"))ImGui::SetClipboardText(source.c_str());
            ImGui::BeginChild("##history");ImGui::TextUnformatted(source.c_str());ImGui::EndChild();
        });
    }
}
}
