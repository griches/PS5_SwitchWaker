#include "ps5_build_number.h"
#include "pc/pc_menu_widgets.h"
#include "pc/pc_menu_language.h"
#include "pc/pc_disc_region.h"
#include "pc/pc_ui_style.h"
#include <imgui.h>
#include <aurora/imgui.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {
float menuSurfaceOpacity = 1.f;
ImU32 menuSurfaceColor(bool selected) {
    ImVec4 value = selected ? pc::ui::selected : pc::ui::surface;
    value.w *= menuSurfaceOpacity;
    return ImGui::GetColorU32(value);
}

// One small immutable atlas shared by the footer and the startup hint.
ImTextureID promptAtlas() {
    static const ImTextureID texture = []() -> ImTextureID {
        FILE* file = std::fopen("/app0/res/menu-prompts.rgba", "rb");
        if (!file) return 0;
        std::vector<unsigned char> rgba(600 * 100 * 4);
        const bool valid = std::fread(rgba.data(), 1, rgba.size(), file) == rgba.size() &&
            std::fgetc(file) == EOF && !std::ferror(file);
        std::fclose(file);
        return valid ? aurora_imgui_add_texture(600, 100, rgba.data()) : 0;
    }();
    return texture;
}
bool promptIcon(ImDrawList* draw, int icon, ImVec2 p, float size, float alpha = 1.f) {
    const ImTextureID texture = promptAtlas();
    if (!texture) return false;
    draw->AddImage(texture, p, ImVec2(p.x + size, p.y + size),
        ImVec2(icon / 6.f, 0.f), ImVec2((icon + 1) / 6.f, 1.f),
        IM_COL32(255, 255, 255, int(255 * alpha * ImGui::GetStyle().Alpha)));
    return true;
}
// Only controller names are substituted; translations retain normal prose.
int buttonWord(std::string_view word) {
    if (word == "L1") return 2;
    if (word == "R1") return 3;
    if (word == "L3") return 5;
    if (word == "Square" || word == "Quadrat" || word == "Carré" ||
        word == "Cuadrado" || word == "Quadrato") return 1;
    return -1;
}
ImVec2 buttonText(ImDrawList* draw, ImVec2 origin, float size, float wrap,
                  const char* text, ImU32 ink, float iconOpacity = 1.f) {
    const std::string_view input(text);
    auto* font = ImGui::GetFont();
    const auto measure = [font, size](std::string_view value) {
        return value.empty() ? 0.f : font->CalcTextSizeA(size, 1e6f, 0.f,
            value.data(), value.data() + value.size()).x;
    };
    const float space = measure(" ");
    float x = 0.f, y = 0.f, width = 0.f;
    for (size_t at = 0; at < input.size();) {
        if (input[at] == '\n') { x = 0.f; y += size * 1.25f; ++at; continue; }
        if (input[at] == ' ') { x += space; ++at; continue; }
        size_t end = input.find_first_of(" \n", at);
        if (end == std::string_view::npos) end = input.size();
        const auto word = input.substr(at, end - at);
        size_t first = 0, last = word.size();
        while (first < last && word[first] == '(') ++first;
        while (last > first && std::string_view("):.,;").find(word[last - 1]) != std::string_view::npos) --last;
        const int icon = buttonWord(word.substr(first, last - first));
        const bool image = icon >= 0 && promptAtlas();
        const float prefix = measure(word.substr(0, first));
        const float w = image ? prefix + size * 1.5f + measure(word.substr(last)) : measure(word);
        if (wrap > 0.f && x > 0.f && x + w > wrap) { x = 0.f; y += size * 1.25f; }
        if (draw) {
            const ImVec2 p(origin.x + x, origin.y + y);
            if (image) {
                if (first) draw->AddText(font, size, p, ink, word.data(), word.data() + first);
                promptIcon(draw, icon, ImVec2(p.x + prefix, p.y - size * .25f), size * 1.5f, iconOpacity);
                if (last < word.size()) draw->AddText(font, size, ImVec2(p.x + prefix + size * 1.5f, p.y),
                    ink, word.data() + last, word.data() + word.size());
            } else draw->AddText(font, size, p, ink, word.data(), word.data() + word.size());
        }
        x += w; width = std::max(width, x); at = end;
    }
    return ImVec2(width, input.empty() ? 0.f : y + size * 1.25f);
}
// Shorten at UTF-8 character boundaries; full setting names remain in the help panel.
std::string fit_text(const char* text, float width, float size = 0.f) {
    if (size == 0.f) size = ImGui::GetFontSize();
    const auto measure = [size](const char* value) { return buttonText(nullptr, ImVec2(), size, 0.f, value, 0).x; };
    std::string result(text);
    if (measure(text) <= width) return result;
    while (!result.empty()) {
        size_t end = result.size() - 1;
        while (end && (static_cast<unsigned char>(result[end]) & 0xc0) == 0x80) --end;
        result.resize(end);
        if (measure((result + "…").c_str()) <= width) return result + "…";
    }
    return "";
}
}

namespace {
void chevron(ImDrawList* draw, ImVec2 center, float f, bool right, ImU32 ink) {
    const float direction = right ? 1.f : -1.f;
    const ImVec2 tip(center.x + direction*f*.10f, center.y);
    draw->AddLine(ImVec2(center.x-direction*f*.10f, center.y-f*.19f), tip, ink, std::max(1.f,f*.065f));
    draw->AddLine(tip, ImVec2(center.x-direction*f*.10f, center.y+f*.19f), ink, std::max(1.f,f*.065f));
}
}

bool pc_ps5_gameplay_option_row(const char* label, const char* value, bool category,
    bool selected, bool locked, const ImVec4& dim, const ImVec4& accent, bool adjustable) {
    const float fontSize = ImGui::GetFontSize();
    const float padding = fontSize * 0.3f;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = fontSize + padding * 2;
    // Fixed columns and reserved chevrons keep values still when selection moves.
    const float valueLeft = width * .60f, valueRight = width - fontSize;
    const std::string shownLabel = fit_text(label, std::max(0.f, valueLeft-fontSize*1.15f));
    const std::string shownValue = fit_text(value, std::max(0.f, valueRight-valueLeft-fontSize*.5f));
    const bool clicked = ImGui::InvisibleButton("##gameplay_option", ImVec2(width, height));
    const ImVec2 p = ImGui::GetItemRectMin(), end(p.x+width,p.y+height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, end, menuSurfaceColor(selected), fontSize*.18f);
    if (selected || ImGui::IsItemHovered()) {
        draw->AddRect(p, end, ImGui::GetColorU32(selected ? accent : pc::ui::border), fontSize*.18f);
    }
    if (selected) {
        draw->AddRectFilled(ImVec2(p.x, p.y+padding), ImVec2(p.x+fontSize*.12f,end.y-padding),
            ImGui::GetColorU32(accent),fontSize*.06f);
    }
    const float y = p.y+(height-fontSize)*.5f;
    buttonText(draw, ImVec2(p.x+fontSize*.5f,y), fontSize, 0.f, shownLabel.c_str(),
        ImGui::GetColorU32(locked ? dim : ImGui::GetStyleColorVec4(ImGuiCol_Text)), locked ? .45f : 1.f);
    const ImU32 ink=ImGui::GetColorU32(selected && !locked ? accent : dim);
    const float textX=p.x+valueRight-ImGui::CalcTextSize(shownValue.c_str()).x;
    draw->AddText(ImVec2(textX,y),ink,shownValue.c_str());
    if (category || (selected && adjustable && !locked))
        chevron(draw,ImVec2(end.x-fontSize*.45f,p.y+height*.5f),fontSize,true,ink);
    if (selected && adjustable && !locked)
        chevron(draw,ImVec2(textX-fontSize*.6f,p.y+height*.5f),fontSize,false,ink);
    return clicked;
}

namespace {
void tab_icon(ImDrawList* d, ImVec2 p, float s, int icon, ImU32 c) {
    const float t = std::max(1.5f, s * 0.075f);
    if (icon == 0) { // display
        d->AddRect(p, ImVec2(p.x+s, p.y+s*.7f), c, s*.08f, 0, t);
        d->AddLine(ImVec2(p.x+s*.5f,p.y+s*.7f),ImVec2(p.x+s*.5f,p.y+s*.95f),c,t);
        d->AddLine(ImVec2(p.x+s*.25f,p.y+s*.95f),ImVec2(p.x+s*.75f,p.y+s*.95f),c,t);
    } else if (icon == 1) { // effects: wand with a star at its tip
        d->AddLine(ImVec2(p.x+s*.12f,p.y+s*.88f),ImVec2(p.x+s*.57f,p.y+s*.43f),c,t);
        const ImVec2 star[] = {
            ImVec2(p.x+s*.72f,p.y+s*.04f), ImVec2(p.x+s*.78f,p.y+s*.22f),
            ImVec2(p.x+s*.96f,p.y+s*.28f), ImVec2(p.x+s*.78f,p.y+s*.34f),
            ImVec2(p.x+s*.72f,p.y+s*.52f), ImVec2(p.x+s*.66f,p.y+s*.34f),
            ImVec2(p.x+s*.48f,p.y+s*.28f), ImVec2(p.x+s*.66f,p.y+s*.22f),
        };
        d->AddPolyline(star,8,c,ImDrawFlags_Closed,t);
    } else if (icon == 2) { // exploration compass
        d->AddCircle(ImVec2(p.x+s*.5f,p.y+s*.5f),s*.47f,c,24,t);
        d->AddTriangle(ImVec2(p.x+s*.65f,p.y+s*.2f),ImVec2(p.x+s*.6f,p.y+s*.6f),
            ImVec2(p.x+s*.2f,p.y+s*.8f),c,t);
    } else if (icon == 3) { // controller
        d->AddRect(ImVec2(p.x,p.y+s*.2f),ImVec2(p.x+s,p.y+s*.85f),c,s*.2f,0,t);
        d->AddLine(ImVec2(p.x+s*.15f,p.y+s*.5f),ImVec2(p.x+s*.4f,p.y+s*.5f),c,t);
        d->AddLine(ImVec2(p.x+s*.275f,p.y+s*.375f),ImVec2(p.x+s*.275f,p.y+s*.625f),c,t);
        d->AddCircleFilled(ImVec2(p.x+s*.72f,p.y+s*.4f),s*.07f,c);
        d->AddCircleFilled(ImVec2(p.x+s*.85f,p.y+s*.6f),s*.07f,c);
    } else { // system sliders
        for (int i=0;i<3;++i) {
            const float y=p.y+s*(.2f+.3f*i), x=p.x+s*(i==1?.7f:.3f);
            d->AddLine(ImVec2(p.x,y),ImVec2(p.x+s,y),c,t);
            d->AddCircleFilled(ImVec2(x,y),s*.1f,c);
        }
    }
}
}

void pc_ps5_logo(ImDrawList* draw, ImVec2 p, float f) {
    const ImU32 accent=ImGui::GetColorU32(pc::ui::accent);
    const ImU32 muted=ImGui::GetColorU32(pc::ui::muted);
    // A sail above a wake: vector artwork stays sharp at every menu scale.
    draw->AddTriangleFilled(ImVec2(p.x+f*.8f,p.y),ImVec2(p.x+f*.8f,p.y+f*.85f),
        ImVec2(p.x+f*.1f,p.y+f*.85f),accent);
    draw->AddTriangleFilled(ImVec2(p.x+f*.95f,p.y+f*.25f),ImVec2(p.x+f*.95f,p.y+f*.85f),
        ImVec2(p.x+f*1.4f,p.y+f*.85f),muted);
    draw->AddBezierCubic(ImVec2(p.x,p.y+f*1.1f),ImVec2(p.x+f*.5f,p.y+f*.8f),
        ImVec2(p.x+f*.85f,p.y+f*1.45f),ImVec2(p.x+f*1.5f,p.y+f*1.1f),
        accent,std::max(1.f,f*.08f));
}

void pc_ps5_menu_heading(const char* title) {
    const float f=ImGui::GetFontSize(), width=ImGui::GetContentRegionAvail().x;
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float nameX=f*1.95f, nameSize=f*1.2f, versionSize=f*.7f;
    const char* name="ProsperoWake";
    const float nameEnd=nameX+ImGui::GetFont()->CalcTextSizeA(nameSize,1e6f,0.f,name).x;
    const float labelWidth=ImGui::CalcTextSize(title).x;
    char buildLabel[64];
    std::snprintf(buildLabel,sizeof(buildLabel),pc_menu_text("Build %s"),PS5_BUILD_NUMBER);
    const auto region = pc_disc_region();
    const char* regionLabel = region == PcDiscRegion::USA ? "USA" :
                              region == PcDiscRegion::PAL ? "PAL" : nullptr;
    if (regionLabel) {
        const size_t length = std::strlen(buildLabel);
        std::snprintf(buildLabel + length, sizeof(buildLabel) - length, " - %s", regionLabel);
    }
    const float versionWidth=ImGui::GetFont()->CalcTextSizeA(versionSize,1e6f,0.f,buildLabel).x;
    const float versionX=(width-versionWidth)*.5f;
    // Keep the build centered, using a second line when larger text needs the space.
    const bool secondLine=versionX<nameEnd+f*.6f || versionX+versionWidth>width-labelWidth-f*.6f;
    const float dividerY=f*(secondLine ? 2.4f : 1.6f);
    ImGui::Dummy(ImVec2(width,dividerY+f*.3f));
    auto* draw=ImGui::GetWindowDrawList();
    const ImU32 muted=ImGui::GetColorU32(pc::ui::muted);
    draw->PushClipRect(p,ImVec2(p.x+width,p.y+dividerY+f*.3f),true);
    pc_ps5_logo(draw, p, f);
    draw->AddText(ImGui::GetFont(),nameSize,ImVec2(p.x+nameX,p.y),
        ImGui::GetColorU32(pc::ui::text),name);
    const float versionY=p.y+(secondLine ? f*1.4f : (nameSize-versionSize)*.5f);
    draw->AddText(ImGui::GetFont(),versionSize,ImVec2(p.x+versionX,versionY),muted,buildLabel);
    if (nameEnd+f*.6f+labelWidth<=width)
        draw->AddText(ImVec2(p.x+width-labelWidth,p.y+f*.1f),muted,title);
    draw->AddLine(ImVec2(p.x,p.y+dividerY),ImVec2(p.x+width,p.y+dividerY),
        ImGui::GetColorU32(pc::ui::border));
    draw->PopClipRect();
}

void pc_ps5_menu_section(const char* title) {
    const float f=ImGui::GetFontSize(), width=ImGui::GetContentRegionAvail().x;
    const ImVec2 p=ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width,f*1.45f));
    auto* draw=ImGui::GetWindowDrawList();
    const float size=f*.85f;
    draw->AddText(ImGui::GetFont(),size,ImVec2(p.x+f*.5f,p.y+f*.35f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),title);
    const float start=f+ImGui::GetFont()->CalcTextSizeA(size,1e6f,0.f,title).x;
    if (start<width) draw->AddLine(ImVec2(p.x+start,p.y+f*.8f),ImVec2(p.x+width,p.y+f*.8f),
        ImGui::GetColorU32(pc::ui::border));
}

bool pc_ps5_menu_tab(const char* label, int icon, bool selected, float width) {
    const float f=ImGui::GetFontSize(), height=f*1.9f;
    ImGui::PushID(icon);
    const bool clicked=ImGui::InvisibleButton("##console_tab",ImVec2(width,height));
    const ImVec2 p=ImGui::GetItemRectMin();
    auto* draw=ImGui::GetWindowDrawList();
    const ImU32 ink=ImGui::GetColorU32(selected ? pc::ui::accent : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    draw->AddRectFilled(p,ImVec2(p.x+width,p.y+height),
        menuSurfaceColor(selected),f*.25f);
    if (selected) draw->AddRectFilled(ImVec2(p.x+f*.4f,p.y+height-f*.1f),
        ImVec2(p.x+width-f*.4f,p.y+height),ink,f*.05f);
    // Drop decorative icons uniformly on narrow tabs before truncating translated names.
    const bool showIcon = width >= f * 7.5f;
    const float iconSize=f*.85f, iconSpace=showIcon ? iconSize+f*.45f : 0.f;
    const std::string shown=fit_text(label,std::max(0.f,width-f-iconSpace));
    // Center the complete icon/label group, including shortened translations.
    const float groupWidth=iconSpace+ImGui::CalcTextSize(shown.c_str()).x;
    const float left=p.x+(width-groupWidth)*.5f;
    const float textX=left+iconSpace;
    if (showIcon) tab_icon(draw,ImVec2(left,p.y+(height-iconSize)*.5f),iconSize,icon,ink);
    draw->AddText(ImVec2(textX,p.y+(height-f)*.5f),ink,shown.c_str());
    ImGui::PopID();
    return clicked;
}

void pc_ps5_menu_footer(const char* confirm, const char* back, const char* tabs, const char* close) {
    const float f=ImGui::GetFontSize(), cell=ImGui::GetContentRegionAvail().x*.25f;
    const char* labels[]={confirm,back,tabs,close};
    for (int i=0;i<4;++i) {
        if (i) ImGui::SameLine(0,0);
        ImVec2 p=ImGui::GetCursorScreenPos();
        const float iconWidth=i<2 ? f*1.65f : f*3.3f, gap=f*.35f;
        const float wrap=std::max(0.f,cell-iconWidth-gap-f*.3f);
        const std::string label=fit_text(labels[i],wrap,f*.82f);
        const float labelWidth=ImGui::GetFont()->CalcTextSizeA(f*.82f,1e6f,0.f,label.c_str()).x;
        const float groupWidth=iconWidth+(label.empty() ? 0.f : gap+labelWidth);
        const float height=f*1.8f;
        ImGui::Dummy(ImVec2(cell,height));
        ImDrawList* d=ImGui::GetWindowDrawList();
        p.x+=std::max(0.f,(cell-groupWidth)*.5f);
        p.y+=(height-f)*.5f;
        const ImU32 ink=ImGui::GetColorU32(ImGuiCol_Text);
        const float t=std::max(1.5f,f*.075f);
        const float symbolX=p.x+(iconWidth-f)*.5f;
        const bool artwork = promptAtlas() != 0;
        if (artwork) {
            if (i == 2) {
                promptIcon(d, 2, ImVec2(p.x, p.y - f * .3f), f * 1.6f);
                promptIcon(d, 3, ImVec2(p.x + f * 1.7f, p.y - f * .3f), f * 1.6f);
            } else {
                const float size = i == 3 ? f * 2.5f : f * 1.6f;
                promptIcon(d, i == 3 ? 4 : i,
                    ImVec2(p.x + (iconWidth - size) * .5f, p.y + (f - size) * .5f), size);
            }
        } else if(i==0) {
            d->AddLine(ImVec2(symbolX+f*.15f,p.y+f*.15f),ImVec2(symbolX+f*.85f,p.y+f*.85f),ink,t);
            d->AddLine(ImVec2(symbolX+f*.85f,p.y+f*.15f),ImVec2(symbolX+f*.15f,p.y+f*.85f),ink,t);
        } else if(i==1) d->AddRect(ImVec2(symbolX+f*.15f,p.y+f*.15f),ImVec2(symbolX+f*.85f,p.y+f*.85f),ink,f*.04f,0,t);
        else if(i==2) {
            d->AddRect(p,ImVec2(p.x+iconWidth,p.y+f),ink,f*.15f,0,t);
            const float labelSize=f*.65f;
            const float labelWidth=ImGui::CalcTextSize("L1 / R1").x*.65f;
            d->AddText(ImGui::GetFont(),labelSize,
                ImVec2(p.x+(iconWidth-labelWidth)*.5f,p.y+(f-labelSize)*.5f),ink,"L1 / R1");
        } else {
            d->AddRect(ImVec2(p.x+f*.5f,p.y),ImVec2(p.x+f*2.1f,p.y+f),ink,f*.18f,0,t);
            for(int k=0;k<3;++k) d->AddCircleFilled(ImVec2(p.x+f*(.9f+.4f*k),p.y+f*.5f),f*.035f,ink);
        }
        d->AddText(ImGui::GetFont(),f*.82f,ImVec2(p.x+iconWidth+gap,p.y+f*.09f),ink,label.c_str());
    }
}

void pc_ps5_menu_style_begin(bool highContrast, float scale, float opacity) {
    // These fills sit over the window background. Fade them faster so stacked
    // layers do not make tabs and rows look opaque at high transparency.
    menuSurfaceOpacity = highContrast ? 1.f : opacity * opacity;
    const float f=ImGui::GetFontSize()*scale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,f*.65f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,f*.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,f*.3f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(f*.9f,f*.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(f*.35f,f*.18f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,f*.3f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,pc::ui::background);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Text,highContrast ? ImVec4(1,1,1,1) : pc::ui::text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,highContrast ? pc::ui::text : pc::ui::muted);
    ImGui::PushStyleColor(ImGuiCol_Header,pc::ui::selected);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,pc::ui::selected);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,pc::ui::selected);
    ImGui::PushStyleColor(ImGuiCol_Separator,pc::ui::border);
    ImGui::PushStyleColor(ImGuiCol_Border,pc::ui::border);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,pc::ui::background);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,pc::ui::border);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,pc::ui::muted);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,pc::ui::accent);
}
void pc_ps5_menu_style_end() {
    ImGui::PopStyleColor(13);
    ImGui::PopStyleVar(7);
}


void pc_ps5_menu_welcome(float alpha, bool highContrast, float textScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float f = ImGui::GetFontSize() * std::max(1.f, display.y / 720.f * 1.9f) * textScale;
    const char* message = pc_menu_text("Touchpad opens options");
    auto* font = ImGui::GetFont();
    const char* heading = pc_menu_text("Options");
    const auto region = pc_disc_region();
    const char* badge = region == PcDiscRegion::USA ? "USA" : region == PcDiscRegion::PAL ? "PAL" : nullptr;
    float badgeFont = f * .75f;
    float badgeWidth = badge ? font->CalcTextSizeA(badgeFont, 1e6f, 0.f, badge).x + f * .7f : 0.f;
    const float headingWidth = font->CalcTextSizeA(f * .82f, 1e6f, 0.f, heading).x;
    const float contentWidth = std::max(font->CalcTextSizeA(f, 1e6f, 0.f, message).x,
        headingWidth + (badge ? badgeWidth + f * .8f : 0.f));
    // Keep larger text and longer translations inside the television safe area.
    const float naturalWidth = contentWidth + f * 4.4f;
    const float fit = std::min(1.f, display.x * .9f / naturalWidth);
    f *= fit;
    badgeFont *= fit;
    badgeWidth *= fit;
    const float width = naturalWidth * fit;
    const float height = f * 3.1f;
    const ImVec2 p((display.x - width) * .5f, display.y * .055f - (1.f - alpha) * f * .4f);
    const ImVec2 end(p.x + width, p.y + height);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const auto color = [alpha](ImVec4 c) { c.w *= alpha; return ImGui::ColorConvertFloat4ToU32(c); };
    draw->AddRectFilled(ImVec2(p.x, p.y + f * .2f), ImVec2(end.x, end.y + f * .2f),
                        IM_COL32(0, 0, 0, int(65 * alpha)), f * .6f);
    draw->AddRectFilled(p, end, color(highContrast ? ImVec4(0, 0, 0, 1) : pc::ui::background), f * .6f);
    draw->AddRect(p, end, color(pc::ui::border), f * .6f);
    draw->AddRectFilled(ImVec2(p.x + f * .4f, p.y + f * .7f),
        ImVec2(p.x + f * .5f, end.y - f * .7f), color(pc::ui::accent), f * .05f);
    if (!promptIcon(draw, 4, ImVec2(p.x + f * .8f, p.y + f * .55f), f * 2.f, alpha)) {
        draw->AddRect(ImVec2(p.x + f, p.y + f), ImVec2(p.x + f * 2.6f, p.y + f * 2.f),
                      color(pc::ui::text), f * .2f);
    }
    draw->AddText(ImGui::GetFont(), f * .82f, ImVec2(p.x + f * 3.2f, p.y + f * .6f),
                  color(pc::ui::accent), heading);
    if (badge) {
        const ImVec2 badgeMin(end.x - f * 1.2f - badgeWidth, p.y + f * .48f);
        const ImVec2 badgeMax(badgeMin.x + badgeWidth, badgeMin.y + f * .96f);
        draw->AddRectFilled(badgeMin, badgeMax,
            color(highContrast ? ImVec4(0, 0, 0, 1) : pc::ui::surface), f * .24f);
        draw->AddRect(badgeMin, badgeMax, color(pc::ui::border), f * .24f);
        draw->AddText(font, badgeFont,
            ImVec2(badgeMin.x + f * .35f, badgeMin.y + (f * .96f - badgeFont) * .5f),
            color(highContrast ? pc::ui::text : pc::ui::muted), badge);
    }
    draw->AddText(ImGui::GetFont(), f, ImVec2(p.x + f * 3.2f, p.y + f * 1.5f),
                  color(pc::ui::text), message);
}


float pc_ps5_menu_text_height(const char* text, float size, float width) {
    return buttonText(nullptr, ImVec2(), size, width, text, 0).y;
}
void pc_ps5_menu_text(const char* text, const ImVec4& color) {
    const auto extent = buttonText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
        ImGui::GetFontSize(), ImGui::GetContentRegionAvail().x, text, ImGui::GetColorU32(color));
    ImGui::Dummy(extent);
}
