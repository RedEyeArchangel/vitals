#include "widgets.h"
#include "theme.h"
#include "implot.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Widgets {

static ImU32 WithAlpha(ImVec4 c, float a) {
    c.w = a;
    return ImGui::ColorConvertFloat4ToU32(c);
}

void LedBarVertical(const char* id, float value01, ImVec4 color, ImVec2 size, int segments) {
    value01 = std::clamp(value01, 0.0f, 1.0f);
    ImGui::PushID(id);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float gap = 2.0f;
    float segH = (size.y - gap * (segments - 1)) / segments;
    int litCount = (int)(value01 * segments + 0.5f);

    for (int i = 0; i < segments; ++i) {
        // index 0 = bottom
        float y1 = p0.y + size.y - (i + 1) * (segH + gap) + gap;
        float y0 = y1 + segH;
        ImVec2 a(p0.x, y1);
        ImVec2 b(p0.x + size.x, y0);
        bool lit = i < litCount;

        if (lit) {
            // soft glow behind the topmost lit segment
            if (i == litCount - 1) {
                ImVec2 ga(a.x - 3, a.y - 3);
                ImVec2 gb(b.x + 3, b.y + 3);
                dl->AddRectFilled(ga, gb, WithAlpha(color, 0.35f), 3.0f);
            }
            dl->AddRectFilled(a, b, WithAlpha(color, 0.95f), 2.0f);
        } else {
            dl->AddRectFilled(a, b, WithAlpha(color, 0.10f), 2.0f);
        }
    }

    ImGui::Dummy(size);
    ImGui::PopID();
}

void LedBarHorizontal(const char* id, float value01, ImVec4 color, ImVec2 size, int segments) {
    value01 = std::clamp(value01, 0.0f, 1.0f);
    ImGui::PushID(id);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float gap = 2.0f;
    float segW = (size.x - gap * (segments - 1)) / segments;
    int litCount = (int)(value01 * segments + 0.5f);

    for (int i = 0; i < segments; ++i) {
        float x0 = p0.x + i * (segW + gap);
        float x1 = x0 + segW;
        ImVec2 a(x0, p0.y);
        ImVec2 b(x1, p0.y + size.y);
        bool lit = i < litCount;

        if (lit) {
            if (i == litCount - 1) {
                ImVec2 ga(a.x - 3, a.y - 3);
                ImVec2 gb(b.x + 3, b.y + 3);
                dl->AddRectFilled(ga, gb, WithAlpha(color, 0.35f), 3.0f);
            }
            dl->AddRectFilled(a, b, WithAlpha(color, 0.95f), 2.0f);
        } else {
            dl->AddRectFilled(a, b, WithAlpha(color, 0.10f), 2.0f);
        }
    }

    ImGui::Dummy(size);
    ImGui::PopID();
}

void StatusPill(const char* text, ImVec4 dotColor) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 textSize = ImGui::CalcTextSize(text);
    float padX = 10.0f, padY = 4.0f, dotR = 3.5f, dotGap = 8.0f;
    ImVec2 size(dotR * 2 + dotGap + textSize.x + padX * 2, textSize.y + padY * 2);

    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                       ImGui::ColorConvertFloat4ToU32(Theme::kBgPanelAlt), size.y * 0.5f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y),
                ImGui::ColorConvertFloat4ToU32(Theme::kBorder), size.y * 0.5f);

    ImVec2 dotCenter(p.x + padX + dotR, p.y + size.y * 0.5f);
    dl->AddCircleFilled(dotCenter, dotR + 2.0f, WithAlpha(dotColor, 0.35f));
    dl->AddCircleFilled(dotCenter, dotR, ImGui::ColorConvertFloat4ToU32(dotColor));

    ImVec2 textPos(p.x + padX + dotR * 2 + dotGap, p.y + padY);
    dl->AddText(textPos, ImGui::ColorConvertFloat4ToU32(Theme::kTextSecondary), text);

    ImGui::Dummy(size);
}

bool BeginCard(const char* id, ImVec2 size, bool elevated, ImVec4 borderColor) {
    if (elevated && Theme::ShadowAlpha > 0.0f && size.x > 0.0f && size.y > 0.0f) {
        // Must draw into the *parent* drawlist before BeginChild(): a
        // child's own drawlist clips to its bounds, so a shadow can't be
        // drawn from inside it.
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec4 tint = Theme::ShadowUseAccent ? borderColor : ImVec4(0, 0, 0, 1);
        const int kLayers = 4;
        for (int i = kLayers; i >= 1; --i) {
            float t = (float)i / (float)kLayers;
            float grow = t * 8.0f;
            float offsetY = t * 5.0f;
            float alpha = Theme::ShadowAlpha * (1.0f - t * 0.6f) / kLayers * 2.0f;
            ImVec2 a(p0.x - grow, p0.y - grow + offsetY);
            ImVec2 b(p0.x + size.x + grow, p0.y + size.y + grow + offsetY);
            dl->AddRectFilled(a, b, WithAlpha(tint, alpha), Theme::kPanelRounding + grow * 0.5f);
        }
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kBgPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Theme::kPanelRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Theme::kPanelPadding, Theme::kPanelPadding));
    bool open = ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar);
    return open;
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void DetailHeader(const char* title, const char* subtitle, float value01,
                   const char* valueText, ImVec4 color, float width) {
    width = std::min(width, ImGui::GetContentRegionAvail().x); // never extend under the scrollbar
    ImGui::PushFont(Theme::LoadedFonts.heading);
    ImGui::TextColored(Theme::kTextPrimary, "%s", title);
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 4));
    // Reserve the value's real rendered width (bold, 1.3x, at the current UI
    // scale) — a fixed reservation clipped it under the scrollbar at 4K scale.
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::SetWindowFontScale(1.3f);
    float valueW = ImGui::CalcTextSize(valueText).x;
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    LedBarHorizontal("detail_meter", value01, color,
                     ImVec2(std::max(Theme::Scale(60.0f), width - valueW - spacing - Theme::Scale(12.0f)), Theme::Scale(16.0f)));
    ImGui::SameLine();
    ImGui::PushFont(Theme::LoadedFonts.bodyBold);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextColored(color, "%s", valueText);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();

    if (subtitle && subtitle[0]) {
        ImGui::TextColored(Theme::kTextMuted, "%s", subtitle);
    }
}

void StatCell(const char* label, const char* value) {
    ImGui::TextColored(Theme::kTextMuted, "%s", label);
    ImGui::PushFont(Theme::LoadedFonts.mono);
    ImGui::TextColored(Theme::kTextPrimary, "%s", value);
    ImGui::PopFont();
}

void InfoPill(const char* label, const char* value, ImVec2 size) {
    ImGui::BeginChild(label, size, true);
    ImGui::TextColored(Theme::kTextMuted, "%s", label);
    ImGui::TextColored(Theme::kTextPrimary, "%s", value);
    ImGui::EndChild();
}

void Sparkline(const RingBuffer& history, ImVec4 color, ImVec2 size) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Right-align the last Theme::HistoryWindowSamples against the box width
    // (same fixed 30s window as the big graphs) instead of stretching
    // whatever history exists so far to fill the box.
    int windowSamples = std::max(1, Theme::HistoryWindowSamples);
    int n = (int)std::min<size_t>(history.data.size(), (size_t)windowSamples);

    if (n >= 2) {
        size_t start = history.data.size() - (size_t)n;
        float vmin = history.data[start], vmax = history.data[start];
        for (size_t i = start; i < history.data.size(); ++i) { vmin = std::min(vmin, history.data[i]); vmax = std::max(vmax, history.data[i]); }
        float range = std::max(0.001f, vmax - vmin);
        // pad range a bit so the line doesn't hug the top/bottom edge
        vmin -= range * 0.15f;
        range *= 1.3f;

        int xOffset = windowSamples - n;
        std::vector<ImVec2> pts(n);
        for (int i = 0; i < n; ++i) {
            float x = p0.x + size.x * ((float)(xOffset + i) / (float)(windowSamples - 1));
            float norm = (history.data[start + i] - vmin) / range;
            float y = p0.y + size.y * (1.0f - norm);
            pts[i] = ImVec2(x, y);
        }

        // filled area under the line
        std::vector<ImVec2> fillPts = pts;
        fillPts.push_back(ImVec2(pts.back().x, p0.y + size.y));
        fillPts.push_back(ImVec2(pts.front().x, p0.y + size.y));
        dl->AddConvexPolyFilled(fillPts.data(), (int)fillPts.size(), WithAlpha(color, 0.20f));

        dl->AddPolyline(pts.data(), (int)pts.size(), WithAlpha(color, 0.95f), 0, 1.6f);
    }

    ImGui::Dummy(size);
}

void ValueBar(float value01, ImVec4 color, float width, float height) {
    value01 = std::clamp(value01, 0.0f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = height * 0.3f;
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), WithAlpha(Theme::kTextMuted, 0.35f), r);
    if (value01 > 0.0f)
        dl->AddRectFilled(p, ImVec2(p.x + std::max(height * 0.6f, width * value01), p.y + height), WithAlpha(color, 0.95f), r);
    ImGui::Dummy(ImVec2(width, height));
}

bool ToggleSwitch(const char* id, bool* value) {
    float h = ImGui::GetFrameHeight(), w = h * 1.9f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    if (clicked) *value = !*value;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 track = *value ? Theme::kAccentGpu : Theme::kBorder;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), WithAlpha(track, *value ? 0.55f : 1.0f), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), WithAlpha(*value ? Theme::kAccentGpu : Theme::kTextMuted, 1.0f), h * 0.5f);
    float knobX = *value ? p.x + w - h * 0.5f : p.x + h * 0.5f;
    dl->AddCircleFilled(ImVec2(knobX, p.y + h * 0.5f), h * 0.36f, WithAlpha(Theme::kTextPrimary, 1.0f));
    return clicked;
}

bool SegmentedTabs(const char* id, const char* const* labels, int count, int* selected, float width) {
    ImGui::PushID(id);
    bool changed = false;
    float w = width / (float)count;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    for (int i = 0; i < count; ++i) {
        if (i > 0) ImGui::SameLine();
        bool sel = *selected == i;
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? WithAlpha(Theme::kAccentGpu, 0.30f) : WithAlpha(Theme::kBgPanelAlt, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, sel ? Theme::kAccentGpu : Theme::kBorder);
        if (ImGui::Button(labels[i], ImVec2(w, ImGui::GetFrameHeight() * 1.3f)) && !sel) { *selected = i; changed = true; }
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    return changed;
}

} // namespace Widgets
