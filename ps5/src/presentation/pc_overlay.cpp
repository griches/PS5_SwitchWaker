// PS5 presentation metrics. Frame intervals are CPU submissions, not GPU timestamps.
#include "pc_internal.h"
#include "pc/pc_profiler.h"
#include "pc/pc_menu_language.h"
#include "pc/pc_ui_style.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "m_Do/m_Do_ext.h"
#include <imgui.h>
#include <imgui_internal.h> // display order only; the counter never takes focus
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {
enum View { Compact, Expanded, Graph, Profiler } view = Expanded;
int corner;
float textScale = 1.f;
bool behindMenu = false;
constexpr size_t capacity = 180;
struct HeapSample { double used{}, total{}; bool valid{}; };
struct State {
    uint64_t last{}, window{}, busy{}, memoryAt{};
    unsigned presents{}, games{};
    double fps{}, gameMs{}, average{}, p99{}, maximum{};
    bool measured{};
    std::array<float, capacity> history{};
    size_t count{}, next{};
    std::array<HeapSample, 3> heaps{};
} state;
void summarize() {
    if (!state.count) return;
    auto sorted=state.history;
    std::sort(sorted.begin(),sorted.begin()+state.count);
    double sum=0;
    for(size_t i=0;i<state.count;++i) {
        sum+=sorted[i];
    }
    state.average=sum/state.count;
    state.p99=sorted[size_t(std::ceil(state.count*.99))-1];
    state.maximum=sorted[state.count-1];
}
void sample_memory(uint64_t now) {
    if(view!=Profiler || (state.memoryAt && now-state.memoryAt<1000000000ull)) return;
    state.memoryAt=now;
    JKRHeap* heaps[]={mDoExt_getZeldaHeap(),mDoExt_getGameHeap(),mDoExt_getArchiveHeap()};
    for(size_t i=0;i<state.heaps.size();++i) {
        auto* heap=heaps[i];
        auto& sample=state.heaps[i];
        sample={};
        if(!heap) continue;
        const auto total=heap->getHeapSize();
        const auto free=heap->getTotalFreeSize();
        if(free<0 || uint32_t(free)>total) continue;
        constexpr double mib=1024.0*1024.0;
        sample={(total-uint32_t(free))/mib,total/mib,true};
    }
}
const char* tr(const char* key) { return pc_menu_text(key); }

void metric(const char* label, const char* value, float width) {
    const float f=ImGui::GetFontSize();
    const ImVec2 p=ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width,f*1.35f));
    auto* draw=ImGui::GetWindowDrawList();
    const float valueX=p.x+width-ImGui::CalcTextSize(value).x;
    const ImVec4 clip(p.x,p.y,std::max(p.x,valueX-f*.5f),p.y+f);
    draw->AddText(ImGui::GetFont(),f,p,ImGui::GetColorU32(pc::ui::muted),label,nullptr,0.f,&clip);
    draw->AddText(ImVec2(valueX,p.y),ImGui::GetColorU32(pc::ui::text),value);
}

void headline(float width) {
    const float f=ImGui::GetFontSize();
    const ImVec2 p=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();
    char fps[32], time[32];
    if (state.measured) {
        std::snprintf(fps,sizeof(fps),"%.1f",state.fps);
        std::snprintf(time,sizeof(time),"%.2f ms",state.average);
    } else { std::strcpy(fps,"--"); std::strcpy(time,"-- ms"); }
    if (view==Compact) {
        ImGui::Dummy(ImVec2(width,f*1.25f));
        char text[48];
        std::snprintf(text,sizeof(text),"%s FPS",fps);
        draw->AddText(p,ImGui::GetColorU32(pc::ui::accent),text);
        draw->AddText(ImVec2(p.x+width-ImGui::CalcTextSize(time).x,p.y),ImGui::GetColorU32(pc::ui::text),time);
        return;
    }
    ImGui::Dummy(ImVec2(width,f*2.75f));
    draw->AddText(ImGui::GetFont(),f*.8f,p,ImGui::GetColorU32(pc::ui::muted),"FPS");
    draw->AddText(ImGui::GetFont(),f*1.8f,ImVec2(p.x,p.y+f*.7f),ImGui::GetColorU32(pc::ui::accent),fps);
    const char* label=tr("Frame time");
    const float labelWidth=ImGui::GetFont()->CalcTextSizeA(f*.8f,1e6f,0.f,label).x;
    draw->AddText(ImGui::GetFont(),f*.8f,ImVec2(p.x+width-labelWidth,p.y),ImGui::GetColorU32(pc::ui::muted),label);
    const float numberWidth=ImGui::GetFont()->CalcTextSizeA(f*1.2f,1e6f,0.f,time).x;
    draw->AddText(ImGui::GetFont(),f*1.2f,ImVec2(p.x+width-numberWidth,p.y+f*1.15f),ImGui::GetColorU32(pc::ui::text),time);
}

void graph(float width) {
    const float f=ImGui::GetFontSize(), height=f*5.5f;
    float peak=50.f;
    for(size_t i=0;i<state.count;++i) peak=std::max(peak,state.history[i]);
    const float ceiling=std::ceil(peak/10.f)*10.f;
    char range[32];
    std::snprintf(range,sizeof(range),"%.0f ms",ceiling);
    metric(tr("Frame history"),range,width);
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float plotWidth=std::max(f,width-f*3.f);
    const ImVec2 end(p.x+plotWidth,p.y+height);
    ImGui::Dummy(ImVec2(width,height+f*.25f));
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,end,ImGui::GetColorU32(pc::ui::background),f*.2f);
    for(int i=1;i<4;++i) {
        const float x=p.x+plotWidth*i/4.f;
        draw->AddLine(ImVec2(x,p.y),ImVec2(x,end.y),IM_COL32(35,55,63,130));
    }
    for(float budget:{16.667f,33.333f}) {
        const float y=end.y-budget/ceiling*height;
        draw->AddLine(ImVec2(p.x,y),ImVec2(end.x,y),ImGui::GetColorU32(pc::ui::border));
        draw->AddText(ImGui::GetFont(),f*.65f,ImVec2(end.x+f*.3f,y-f*.35f),
            ImGui::GetColorU32(pc::ui::muted),budget<20.f ? "60 fps" : "30 fps");
    }
    draw->PushClipRect(p,end,true);
    const size_t start=state.count==capacity ? state.next : 0;
    for(size_t i=1;i<state.count;++i) {
        const float a=state.history[(start+i-1)%capacity], b=state.history[(start+i)%capacity];
        // Newest sample stays at the right edge while history fills.
        const float offset=float(capacity-state.count+i);
        const ImVec2 from(p.x+(offset-1)/(capacity-1)*plotWidth,end.y-std::min(a/ceiling,1.f)*height);
        const ImVec2 to(p.x+offset/(capacity-1)*plotWidth,end.y-std::min(b/ceiling,1.f)*height);
        draw->AddQuadFilled(from,to,ImVec2(to.x,end.y),ImVec2(from.x,end.y),IM_COL32(100,210,220,30));
        draw->AddLine(from,to,ImGui::GetColorU32(b>33.333f ? pc::ui::warm : pc::ui::accent),std::max(1.f,f*.1f));
    }
    draw->PopClipRect();
}

void memory(float width) {
    if (!std::any_of(state.heaps.begin(),state.heaps.end(),[](const HeapSample& h){return h.valid;})) return;
    const float f=ImGui::GetFontSize();
    ImGui::Spacing();
    ImGui::TextColored(pc::ui::muted,"%s",tr("Game heaps (MiB)"));
    const char* names[]={tr("Main"),tr("Game"),tr("Archive")};
    for(size_t i=0;i<state.heaps.size();++i) {
        const auto& h=state.heaps[i];
        if (!h.valid) continue;
        char value[64];
        std::snprintf(value,sizeof(value),"%.1f / %.1f",h.used,h.total);
        metric(names[i],value,width);
        const ImVec2 p=ImGui::GetCursorScreenPos();
        const float height=f*.18f;
        ImGui::Dummy(ImVec2(width,height+f*.2f));
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(p,ImVec2(p.x+width,p.y+height),ImGui::GetColorU32(pc::ui::border),height*.5f);
        const float fraction=h.total>0 ? float(std::clamp(h.used/h.total,0.0,1.0)) : 0.f;
        if (fraction>0.f) draw->AddRectFilled(p,ImVec2(p.x+width*fraction,p.y+height),ImGui::GetColorU32(pc::ui::accent),height*.5f);
    }
}

}
void pc_profiler_reset() { state={}; }
void pc_profiler_mode(const char* value) {
    view=std::strcmp(value,"compact")==0?Compact:std::strcmp(value,"graph")==0?Graph:
        std::strcmp(value,"profiler")==0?Profiler:Expanded;
}
void pc_profiler_position(const char* value) {
    corner=std::strcmp(value,"tr")==0?1:std::strcmp(value,"bl")==0?2:std::strcmp(value,"br")==0?3:0;
}
void pc_profiler_scale(const char* value) {
    textScale=std::strcmp(value,"large")==0?1.25f:std::strcmp(value,"small")==0?.8f:1.f;
}
void pc_profiler_behind_menu(const char* value) {
    behindMenu = std::strcmp(value,"1") == 0;
}
float pc_profiler_menu_inset(bool* left) {
    *left = corner % 2 == 0;
    if (!pc::gConfig.fpsOverlay || view == Compact || behindMenu) return 0.f;
    const float scale=std::max(1.f,ImGui::GetIO().DisplaySize.y/720.f)*textScale;
    // Same 24-em body, two 0.7-em paddings and 12-pixel margin as the overlay.
    // Add one more margin between the live preview and the options menu.
    return ImGui::GetFontSize()*scale*25.4f + 24.f*scale;
}

namespace pc {
void overlayFrame(uint64_t busyNs, bool gameFrame) {
    if(!gConfig.fpsOverlay || !ImGui::GetCurrentContext()) return;
    const auto now=monotonicNs();
    if(!state.window) state.window=now;
    if(state.last && now>=state.last) {
        state.history[state.next]=float((now-state.last)/1e6);
        state.next=(state.next+1)%capacity;
        state.count=std::min(capacity,state.count+1);
        ++state.presents;
    }
    state.last=now;
    if(gameFrame) { ++state.games; state.busy+=busyNs; }
    if(now-state.window>=500000000ull) {
        const double seconds=(now-state.window)/1e9;
        state.fps=state.presents/seconds;
        state.gameMs=state.games?state.busy/1e6/state.games:0;
        summarize();
        state.measured=true;
        state.window=now; state.presents=state.games=0; state.busy=0;
    }
    sample_memory(now);
    const auto screen=ImGui::GetIO().DisplaySize;
    if (screen.x<=0 || screen.y<=0) return;
    const float scale=std::max(1.f,screen.y/720.f)*textScale;
    const float f=ImGui::GetFontSize()*scale, padding=f*.7f;
    const float width=std::min(f*(view==Compact ? 15.f : 24.f),screen.x-padding*4.f);
    const bool right=corner%2, bottom=corner>=2;
    const float margin=12.f*scale;
    ImGui::SetNextWindowPos(ImVec2(right?screen.x-margin:margin,bottom?screen.y-margin:margin),
        ImGuiCond_Always,ImVec2(right?1.f:0.f,bottom?1.f:0.f));
    ImGui::SetNextWindowSize(ImVec2(width+padding*2.f,0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(padding,padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,f*.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(f*.4f,f*.15f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ui::surface);
    ImGui::PushStyleColor(ImGuiCol_Border,ui::border);
    ImGui::PushStyleColor(ImGuiCol_Text,ui::text);
    ImGui::SetNextWindowBgAlpha(.94f);
    constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoInputs;
    if(ImGui::Begin("##cos_fps_overlay",nullptr,flags)) {
        // Change drawing order without taking focus or input from the options menu.
        if (behindMenu) ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
        else ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::SetWindowFontScale(scale);
        headline(width);
        if(view!=Compact) {
            char value[64];
            if(state.measured) std::snprintf(value,sizeof(value),"%.2f ms",state.gameMs);
            else std::strcpy(value,"-- ms");
            metric(tr("Game CPU"),value,width);
            if(view==Graph || view==Profiler) graph(width);
            if(view==Profiler) {
                if(state.measured) std::snprintf(value,sizeof(value),"%.2f ms",state.p99);
                else std::strcpy(value,"-- ms");
                metric("P99",value,width);
                if(state.measured) std::snprintf(value,sizeof(value),"%.2f ms",state.maximum);
                else std::strcpy(value,"-- ms");
                metric(tr("Peak"),value,width);
                memory(width);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
}
}
