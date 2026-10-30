#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <unordered_map>

namespace nebula {

namespace {

using namespace Gdiplus;

std::unordered_map<std::wstring, Font*> g_fonts;

Font* FontFor(const wchar_t* family, float px, int style) {
    if (px < 1.0f) px = 1.0f;
    wchar_t key[192]{};
    swprintf(key, 192, L"%s|%.1f|%d", family, px, style);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    Font* font = new Font(family, px, style, UnitPixel);
    if (font->GetLastStatus() != Ok) {
        delete font;
        font = new Font(L"Segoe UI", px, style, UnitPixel);
    }
    g_fonts[key] = font;
    return font;
}

void DrawTextIn(Graphics& g, const std::wstring& text, const RectF& rect, float px,
                const Color& color, int style = FontStyleRegular,
                StringAlignment align = StringAlignmentNear,
                StringAlignment line = StringAlignmentCenter, bool ellipsis = true,
                const wchar_t* family = L"Segoe UI") {
    if (text.empty()) return;
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(line);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    if (ellipsis) format.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(text.c_str(), -1, FontFor(family, px, style), rect, &format, &brush);
}

void DrawPathText(Graphics& g, const std::wstring& text, const RectF& rect, float px,
                  const Color& color, StringAlignment align = StringAlignmentNear) {
    if (text.empty()) return;
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetTrimming(StringTrimmingEllipsisPath);
    g.DrawString(text.c_str(), -1, FontFor(L"Segoe UI", px, FontStyleRegular), rect,
                 &format, &brush);
}

void DrawWrapped(Graphics& g, const std::wstring& text, const RectF& rect, float px,
                 const Color& color, StringAlignment align = StringAlignmentNear) {
    if (text.empty()) return;
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingNone);
    format.SetFormatFlags(0);
    g.DrawString(text.c_str(), -1, FontFor(L"Segoe UI", px, FontStyleRegular), rect,
                 &format, &brush);
}

float TextWidth(Graphics& g, const std::wstring& text, float px,
                int style = FontStyleRegular) {
    if (text.empty()) return 0.0f;
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap |
                          StringFormatFlagsMeasureTrailingSpaces);
    RectF bounds;
    RectF layout(0, 0, 10000, 1000);
    g.MeasureString(text.c_str(), -1, FontFor(L"Segoe UI", px, style), layout,
                    &format, &bounds);
    return bounds.Width;
}

void AddRound(GraphicsPath& path, const RectF& r, float radius) {
    if (r.Width <= 0.0f || r.Height <= 0.0f) return;
    const float d = std::min(radius * 2.0f, std::min(r.Width, r.Height));
    if (d <= 1.0f) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRound(Graphics& g, const RectF& r, float radius, const Color& color) {
    if (r.Width <= 0 || r.Height <= 0) return;
    GraphicsPath path;
    AddRound(path, r, radius);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

void StrokeRound(Graphics& g, const RectF& r, float radius, const Color& color,
                 float width = 1.0f) {
    if (r.Width <= 0 || r.Height <= 0) return;
    GraphicsPath path;
    AddRound(path, r, radius);
    Pen pen(color, width);
    g.DrawPath(&pen, &path);
}

void Glyph(Graphics& g, const wchar_t* glyph, const RectF& rect, float px,
           const Color& color) {
    DrawTextIn(g, glyph, rect, px, color, FontStyleRegular, StringAlignmentCenter,
               StringAlignmentCenter, false, L"Segoe MDL2 Assets");
}

void FillCircle(Graphics& g, float cx, float cy, float radius, const Color& color) {
    SolidBrush brush(color);
    g.FillEllipse(&brush, cx - radius, cy - radius, radius * 2, radius * 2);
}

void DrawGlow(Graphics& g, float cx, float cy, float rx, float ry,
              const Color& center) {
    GraphicsPath path;
    path.AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
    PathGradientBrush brush(&path);
    brush.SetCenterColor(center);
    Color surround(0, center.GetRed(), center.GetGreen(), center.GetBlue());
    int count = 1;
    brush.SetSurroundColors(&surround, &count);
    g.FillPath(&brush, &path);
}

const wchar_t* KindGlyph(ItemKind kind) {
    switch (kind) {
        case ItemKind::Folder: return L"\uE8B7";
        case ItemKind::Image: return L"\uEB9F";
        case ItemKind::Video: return L"\uE714";
        case ItemKind::Audio: return L"\uE8D6";
        case ItemKind::Program: return L"\uE756";
        default: return L"\uE7C3";
    }
}

void DrawFolderGlyph(Graphics& g, const RectF& rect, const Color& color) {
    const float w = std::min(rect.Width, rect.Height * 1.28f);
    const float h = w / 1.28f;
    const float x = rect.X + (rect.Width - w) * 0.5f;
    const float y = rect.Y + (rect.Height - h) * 0.5f;
    FillRound(g, RectF(x + w * 0.02f, y + h * 0.04f, w * 0.52f, h * 0.38f),
              h * 0.09f, color);
    FillRound(g, RectF(x, y + h * 0.29f, w, h * 0.69f), h * 0.10f, color);
}

void DrawGridGlyph(Graphics& g, const RectF& rect, const Color& color) {
    const float side = std::min(rect.Width, rect.Height) * 0.78f;
    const float gap = side * 0.16f;
    const float cell = (side - gap) * 0.5f;
    const float x = rect.X + (rect.Width - side) * 0.5f;
    const float y = rect.Y + (rect.Height - side) * 0.5f;
    FillRound(g, RectF(x, y, cell, cell), 1.5f, color);
    FillRound(g, RectF(x + cell + gap, y, cell, cell), 1.5f, color);
    FillRound(g, RectF(x, y + cell + gap, cell, cell), 1.5f, color);
    FillRound(g, RectF(x + cell + gap, y + cell + gap, cell, cell), 1.5f, color);
}

void DrawProgramGlyph(Graphics& g, const RectF& rect, const Color& color) {
    const float w = std::min(rect.Width, rect.Height * 1.24f);
    const float h = w / 1.24f;
    const float x = rect.X + (rect.Width - w) * 0.5f;
    const float y = rect.Y + (rect.Height - h) * 0.5f;
    const RectF window(x, y, w, h);
    StrokeRound(g, window, h * 0.10f, color, 1.3f);
    Pen pen(color, 1.0f);
    g.DrawLine(&pen, x + w * 0.10f, y + h * 0.31f, x + w * 0.90f, y + h * 0.31f);
    const float dot = std::max(1.0f, w * 0.035f);
    FillCircle(g, x + w * 0.17f, y + h * 0.16f, dot, color);
    FillCircle(g, x + w * 0.28f, y + h * 0.16f, dot, color);
    FillCircle(g, x + w * 0.39f, y + h * 0.16f, dot, color);
}

void DrawAudioFileArtwork(Graphics& g, const RectF& thumb) {
    const float cardW = thumb.Height * 0.57f;
    const float cardH = thumb.Height * 0.76f;
    const float x = thumb.X + (thumb.Width - cardW) * 0.5f;
    const float y = thumb.Y + (thumb.Height - cardH) * 0.5f + thumb.Height * 0.01f;

    FillCircle(g, thumb.X + thumb.Width * 0.5f, thumb.Y + thumb.Height * 0.5f,
               thumb.Height * 0.34f, Color(32, 74, 222, 128));

    const RectF card(x, y, cardW, cardH);
    GraphicsPath cardPath;
    AddRound(cardPath, card, cardW * 0.10f);
    LinearGradientBrush fill(
        card, Color(248, 18, 53, 46), Color(250, 7, 12, 17),
        LinearGradientModeVertical);
    g.FillPath(&fill, &cardPath);
    Pen border(Color(ui::WithAlpha(ui::Success, 130)), 1.2f);
    g.DrawPath(&border, &cardPath);

    // A folded corner turns the rounded tile into a musical file.
    const float cut = cardW * 0.27f;
    GraphicsPath fold;
    fold.AddPolygon(std::vector<PointF>{
                        PointF(card.GetRight() - cut, card.Y),
                        PointF(card.GetRight(), card.Y + cut),
                        PointF(card.GetRight() - cut, card.Y + cut)}
                        .data(),
                    3);
    SolidBrush foldBrush(Color(255, 14, 34, 32));
    g.FillPath(&foldBrush, &fold);
    Pen foldLine(Color(ui::WithAlpha(ui::Success, 110)), 1.0f);
    g.DrawLine(&foldLine, card.GetRight() - cut, card.Y + cut, card.GetRight(),
               card.Y + cut);

    Glyph(g, L"\uE8D6",
          RectF(card.X, card.Y + cardH * 0.18f, card.Width, cardH * 0.37f),
          cardW * 0.39f, Color(ui::WithAlpha(ui::Success, 235)));

    const float barW = std::max(1.4f, cardW * 0.065f);
    const float barGap = cardW * 0.055f;
    const float barsW = barW * 5 + barGap * 4;
    const float bx = card.X + (card.Width - barsW) * 0.5f;
    const float baseY = card.Y + cardH * 0.82f;
    const float heights[5] = {0.06f, 0.11f, 0.16f, 0.09f, 0.13f};
    for (int i = 0; i < 5; ++i) {
        const float bh = cardH * heights[i];
        FillRound(g, RectF(bx + i * (barW + barGap), baseY - bh, barW, bh), barW * 0.5f,
                  Color(ui::WithAlpha(ui::Accent2, 210)));
    }
}

std::wstring UpperExtension(const std::wstring& path) {
    std::wstring ext = ExtensionOf(path);
    if (!ext.empty() && ext.front() == L'.') ext.erase(ext.begin());
    for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towupper(c));
    if (ext.empty()) ext = L"ARCHIVO";
    return ext;
}

std::wstring RateText(int rate) {
    wchar_t buffer[64]{};
    double khz = rate / 1000.0;
    if (std::fabs(khz - std::round(khz)) < 0.01) {
        swprintf(buffer, 64, L"%.0f kHz", khz);
    } else {
        swprintf(buffer, 64, L"%.1f kHz", khz);
        for (wchar_t* p = buffer; *p; ++p) {
            if (*p == L'.') *p = L',';
        }
    }
    return buffer;
}

const wchar_t* kFilterNames[5] = {L"Todo", L"Imágenes", L"Vídeos", L"Audio",
                                  L"Programas"};

}  // namespace

void ShutdownPaintResources() {
    for (auto& entry : g_fonts) delete entry.second;
    g_fonts.clear();
}

// ------------------------------------------------------------------ root paint

void App::DrawWindow(Graphics& g, const Layout& layout) {
    DrawBackground(g, layout);
    DrawHeader(g, layout);

    if (editorOpen_) {
        DrawEditor(g, layout);
    } else {
        DrawToolbar(g, layout);
        DrawFilters(g, layout);
        DrawSidebar(g, layout);
        if (layout.showConnect) {
            DrawConnect(g, layout);
        } else {
            DrawGrid(g, layout);
        }
        DrawStatus(g, layout);
    }

    if (settingsOpen_) DrawSettings(g, layout);
}

void App::DrawBackground(Graphics& g, const Layout& layout) {
    const float radius = maximized_ ? 0.0f : S(18);

    if (!maximized_) {
        for (int i = 10; i >= 1; --i) {
            const float expand = S(1.4f * i);
            RectF shadow(layout.panel.X - expand,
                         layout.panel.Y - expand + S(3.5f),
                         layout.panel.Width + expand * 2,
                         layout.panel.Height + expand * 2);
            FillRound(g, shadow, radius + expand, Color(3, 0, 0, 0));
        }
    }

    GraphicsPath panelPath;
    AddRound(panelPath, layout.panel, radius);

    LinearGradientBrush background(
        layout.panel,
        Color(ui::WithAlpha(ui::C(255, 24, 25, 36), static_cast<unsigned>(opacity_))),
        Color(ui::WithAlpha(ui::C(255, 8, 9, 14), static_cast<unsigned>(opacity_))),
        LinearGradientModeVertical);
    g.FillPath(&background, &panelPath);

    {
        GraphicsState state = g.Save();
        g.SetClip(&panelPath, CombineModeReplace);
        DrawGlow(g, layout.panel.X + layout.panel.Width * 0.18f,
                 layout.panel.Y + layout.panel.Height * 0.04f, S(520), S(330),
                 Color(48, 139, 92, 246));
        DrawGlow(g, layout.panel.GetRight() - layout.panel.Width * 0.12f,
                 layout.panel.GetBottom() - layout.panel.Height * 0.01f, S(470), S(300),
                 Color(30, 34, 211, 238));
        g.Restore(state);
    }

    Pen border(Color(ui::WithAlpha(ui::TextPrimary, 34)), 1.0f);
    g.DrawPath(&border, &panelPath);

    // A very subtle inner highlight on the top edge.
    GraphicsState highlight = g.Save();
    g.SetClip(&panelPath, CombineModeReplace);
    Pen light(Color(ui::WithAlpha(ui::TextPrimary, 20)), 1.0f);
    g.DrawLine(&light, layout.panel.X + radius, layout.panel.Y + 0.5f,
               layout.panel.GetRight() - radius, layout.panel.Y + 0.5f);
    g.Restore(highlight);
}

void App::DrawHeader(Graphics& g, const Layout& layout) {
    const RectF logo(layout.panel.X + S(22), layout.panel.Y + S(14), S(34), S(34));
    GraphicsPath logoPath;
    AddRound(logoPath, logo, S(10));
    LinearGradientBrush logoBrush(logo, Color(ui::Accent), Color(ui::Accent2),
                                 LinearGradientModeForwardDiagonal);
    g.FillPath(&logoBrush, &logoPath);
    Pen logoBorder(Color(ui::WithAlpha(ui::TextPrimary, 40)), 1.0f);
    g.DrawPath(&logoBorder, &logoPath);

    GraphicsPath play;
    play.AddPolygon(std::vector<PointF>{PointF(logo.X + logo.Width * 0.36f,
                                               logo.Y + logo.Height * 0.27f),
                                        PointF(logo.X + logo.Width * 0.36f,
                                               logo.Y + logo.Height * 0.73f),
                                        PointF(logo.X + logo.Width * 0.74f,
                                               logo.Y + logo.Height * 0.50f)}
                        .data(),
                    3);
    SolidBrush white{Color(ui::TextPrimary)};
    g.FillPath(&white, &play);


    DrawTextIn(g, L"Nébula", RectF(logo.GetRight() + S(12), logo.Y - S(1), S(210), S(23)),
               S(16.5), ui::TextPrimary, FontStyleBold, StringAlignmentNear,
               StringAlignmentCenter);
    DrawTextIn(g, L"MEDIA MANAGER",
               RectF(logo.GetRight() + S(13), logo.Y + S(20), S(210), S(13)), S(8.5),
               ui::TextMuted, FontStyleRegular, StringAlignmentNear,
               StringAlignmentCenter);

    // Tabs.
    struct TabInfo {
        int id;
        Tab value;
        const wchar_t* glyph;
        const wchar_t* label;
    };
    const TabInfo tabs[2] = {
        {zid::TabLibrary, Tab::Library, L"\uE8B7", L"Biblioteca"},
        {zid::TabDrive, Tab::Drive, L"\uE753", L"Google Drive"},
    };
    for (const auto& tab : tabs) {
        const RectF* rect = ZoneRect(layout, tab.id);
        if (!rect) continue;
        const bool active = tab_ == tab.value;
        const bool hover = hoverId_ == tab.id;
        if (active) {
            FillRound(g, *rect, S(11), Color(ui::WithAlpha(ui::TextPrimary, 13)));
            StrokeRound(g, *rect, S(11), Color(ui::WithAlpha(ui::TextPrimary, 20)));
            FillRound(g, RectF(rect->X + S(9), rect->Y + S(11), S(3), S(14)), S(1.5f),
                      ui::AccentSoft);
        } else if (hover) {
            FillRound(g, *rect, S(11), Color(ui::WithAlpha(ui::TextPrimary, 7)));
        }
        const Color iconColor = active ? ui::AccentSoft : ui::TextMuted;
        const Color textColor = active ? ui::TextPrimary : ui::TextSecondary;
        if (tab.value == Tab::Library) {
            DrawGridGlyph(g, RectF(rect->X + S(17), rect->Y + S(10), S(15), S(16)),
                          iconColor);
        } else {
            Glyph(g, tab.glyph,
                  RectF(rect->X + S(17), rect->Y, S(18), rect->Height), S(12.5),
                  iconColor);
        }
        DrawTextIn(g, tab.label,
                   RectF(rect->X + S(38), rect->Y, rect->Width - S(44), rect->Height),
                   S(12.0), textColor, active ? FontStyleBold : FontStyleRegular);
    }

    // Window controls.
    const int controlIds[5] = {zid::Settings, zid::Pin, zid::Min, zid::Max, zid::Close};
    for (int id : controlIds) {
        const RectF* rect = ZoneRect(layout, id);
        if (!rect) continue;
        const bool hover = hoverId_ == id;
        const bool active = (id == zid::Settings && settingsOpen_) ||
                            (id == zid::Pin && pin_);
        if (id == zid::Close && hover) {
            FillRound(g, *rect, S(9), Color(225, 190, 55, 62));
        } else if (hover) {
            FillRound(g, *rect, S(9), Color(ui::WithAlpha(ui::TextPrimary, 10)));
        } else if (active) {
            FillRound(g, *rect, S(9), Color(ui::WithAlpha(ui::Accent, 22)));
        }
        const wchar_t* glyph = L"";
        ARGB color = hover ? ui::TextPrimary : ui::TextSecondary;
        if (id == zid::Settings) glyph = L"\uE713";
        if (id == zid::Pin) glyph = pin_ ? L"\uE718" : L"\uE77A";
        if (id == zid::Min) glyph = L"\uE921";
        if (id == zid::Max) glyph = maximized_ ? L"\uE923" : L"\uE922";
        if (id == zid::Close) {
            glyph = L"\uE8BB";
            if (hover) color = ui::TextPrimary;
        }
        if (id == zid::Pin && pin_) color = ui::AccentSoft;
        if (id == zid::Settings && settingsOpen_) color = ui::AccentSoft;
        Glyph(g, glyph, *rect, S(12), color);
    }

    Pen divider(Color(ui::WithAlpha(ui::TextPrimary, 8)), 1.0f);
    g.DrawLine(&divider, layout.header.X + S(22), layout.header.GetBottom() - 0.5f,
               layout.header.GetRight() - S(22), layout.header.GetBottom() - 0.5f);
}

void App::DrawToolbar(Graphics& g, const Layout& layout) {
    const ViewState& view = View();

    auto navButton = [&](int id, const wchar_t* glyph, bool enabled) {
        const RectF* rect = ZoneRect(layout, id);
        if (!rect) return;
        const bool hover = enabled && hoverId_ == id;
        if (hover) {
            FillRound(g, *rect, S(9), Color(ui::WithAlpha(ui::TextPrimary, 9)));
        }
        Glyph(g, glyph, *rect, S(13),
              enabled ? (hover ? ui::TextPrimary : ui::TextSecondary)
                      : Color(ui::WithAlpha(ui::TextMuted, 95)));
    };

    navButton(zid::Back, L"\uE72B", view.hist > 0);
    navButton(zid::Up, L"\uE74A", !ParentPath(view.path).empty());
    navButton(zid::Refresh, L"\uE72C", true);

    const RectF* openRect = ZoneRect(layout, zid::OpenFolder);
    if (openRect) {
        const bool hover = hoverId_ == zid::OpenFolder;
        FillRound(g, *openRect, S(9),
                  Color(ui::WithAlpha(ui::TextPrimary, hover ? 11 : 6)));
        StrokeRound(g, *openRect, S(9), Color(ui::WithAlpha(ui::TextPrimary, 12)));
        Glyph(g, L"\uE710", RectF(openRect->X + S(10), openRect->Y, S(16), openRect->Height),
              S(11.5), hover ? ui::TextPrimary : ui::TextSecondary);
        DrawTextIn(g, L"Abrir carpeta",
                   RectF(openRect->X + S(30), openRect->Y,
                         openRect->Width - S(36), openRect->Height),
                   S(11.0), hover ? ui::TextPrimary : ui::TextSecondary);
    }

    FillRound(g, layout.pathPill, S(9), Color(38, 0, 0, 0));
    StrokeRound(g, layout.pathPill, S(9), Color(ui::WithAlpha(ui::TextPrimary, 11)));
    if (tab_ == Tab::Drive) {
        Glyph(g, L"\uE753",
              RectF(layout.pathPill.X + S(11), layout.pathPill.Y, S(15),
                    layout.pathPill.Height),
              S(11), ui::Accent2);
    } else {
        DrawFolderGlyph(g,
                        RectF(layout.pathPill.X + S(12), layout.pathPill.Y, S(14),
                              layout.pathPill.Height),
                        Color(ui::TextMuted));
    }
    std::wstring pathText = view.path;
    if (tab_ == Tab::Drive && driveRoot_.empty()) {
        pathText = L"Google Drive · Sin conectar";
    } else if (pathText.empty()) {
        pathText = L"Sin carpeta";
    }
    DrawPathText(g, pathText,
                 RectF(layout.pathPill.X + S(31), layout.pathPill.Y,
                       layout.pathPill.Width - S(40), layout.pathPill.Height),
                 S(11.2), ui::TextSecondary);

    // Search field.
    const bool searchDisabled = layout.showConnect;
    const bool focused = searchFocused_ && !searchDisabled;
    FillRound(g, layout.search, S(9),
              searchDisabled ? Color(25, 0, 0, 0) : Color(55, 0, 0, 0));
    StrokeRound(g, layout.search, S(9),
                focused ? Color(ui::WithAlpha(ui::AccentSoft, 120))
                        : Color(ui::WithAlpha(ui::TextPrimary, 12)),
                focused ? 1.3f : 1.0f);
    Glyph(g, L"\uE721", RectF(layout.search.X + S(11), layout.search.Y, S(16),
                             layout.search.Height),
          S(11), searchDisabled
                    ? Color(ui::WithAlpha(ui::TextMuted, 90))
                    : (focused ? ui::AccentSoft : ui::TextMuted));

    const float clearSpace = view.search.empty() ? 0.0f : S(28);
    RectF textRect(layout.search.X + S(32), layout.search.Y,
                   layout.search.Width - S(40) - clearSpace, layout.search.Height);
    if (searchDisabled) {
        DrawTextIn(g, L"Conecta Drive para buscar", textRect, S(11.0),
                   Color(ui::WithAlpha(ui::TextMuted, 95)), FontStyleRegular,
                   StringAlignmentNear, StringAlignmentCenter);
    } else if (view.search.empty()) {
        DrawTextIn(g, tab_ == Tab::Drive ? L"Buscar en Drive…" : L"Buscar en esta carpeta…",
                   textRect, S(11.0), ui::TextMuted, FontStyleRegular,
                   StringAlignmentNear, StringAlignmentCenter);
    } else {
        const float width = TextWidth(g, view.search, S(11.2));
        const float overflow = std::max(0.0f, width - textRect.Width);
        GraphicsState state = g.Save();
        g.SetClip(textRect, CombineModeReplace);
        DrawTextIn(g, view.search,
                   RectF(textRect.X - overflow, textRect.Y, width + S(8),
                         textRect.Height),
                   S(11.2), ui::TextPrimary, FontStyleRegular, StringAlignmentNear,
                   StringAlignmentCenter, false);
        g.Restore(state);
        if (focused && caretOn_) {
            float caretX = textRect.X + std::min(width, textRect.Width) + 1.0f;
            caretX = std::min(caretX, textRect.GetRight() - 1.0f);
            Pen caret(Color(ui::Accent2), S(1.2f));
            g.DrawLine(&caret, caretX, layout.search.Y + S(9), caretX,
                       layout.search.GetBottom() - S(9));
        }
    }

    if (!searchDisabled && !view.search.empty()) {
        const RectF* clear = ZoneRect(layout, zid::ClearSearch);
        if (clear) {
            if (hoverId_ == zid::ClearSearch) {
                FillCircle(g, clear->X + clear->Width / 2, clear->Y + clear->Height / 2,
                           S(9), Color(ui::WithAlpha(ui::TextPrimary, 14)));
            }
            Glyph(g, L"\uE8BB", *clear, S(8.5), ui::TextMuted);
        }
    }
}

void App::DrawFilters(Graphics& g, const Layout& layout) {
    ViewState& view = View();
    for (int i = 0; i < 5; ++i) {
        const int id = zid::FilterAll + i;
        const RectF* rect = ZoneRect(layout, id);
        if (!rect) continue;
        const bool active = static_cast<int>(view.filter) == i;
        const bool hover = hoverId_ == id;
        if (active) {
            FillRound(g, *rect, S(9), Color(ui::WithAlpha(ui::Accent, 32)));
            StrokeRound(g, *rect, S(9), Color(ui::WithAlpha(ui::AccentSoft, 80)));
        } else if (hover) {
            FillRound(g, *rect, S(9), Color(ui::WithAlpha(ui::TextPrimary, 8)));
        }
        DrawTextIn(g, kFilterNames[i], *rect, S(10.8),
                   active ? ui::AccentSoft : ui::TextSecondary, FontStyleRegular,
                   StringAlignmentCenter, StringAlignmentCenter);
    }

    const RectF* recursive = ZoneRect(layout, zid::Recursive);
    if (recursive) {
        const bool hover = hoverId_ == zid::Recursive;
        if (view.recursive) {
            FillRound(g, *recursive, S(9), Color(ui::WithAlpha(ui::Accent2, 18)));
            StrokeRound(g, *recursive, S(9), Color(ui::WithAlpha(ui::Accent2, 70)));
        } else if (hover) {
            FillRound(g, *recursive, S(9), Color(ui::WithAlpha(ui::TextPrimary, 8)));
        }
        const RectF box(recursive->X + S(10), recursive->Y + S(7), S(14), S(14));
        if (view.recursive) {
            FillRound(g, box, S(4), Color(ui::Accent2));
            Glyph(g, L"\uE73E", box, S(8), Color(255, 9, 12, 18));
        } else {
            StrokeRound(g, box, S(4), Color(ui::WithAlpha(ui::TextPrimary, 35)));
        }
        DrawTextIn(g, L"Subcarpetas",
                   RectF(recursive->X + S(30), recursive->Y, recursive->Width - S(34),
                         recursive->Height),
                   S(10.3), view.recursive ? ui::Accent2 : ui::TextSecondary);
    }

    const RectF& zoomGroup = layout.zoomGroup;
    FillRound(g, zoomGroup, S(9), Color(ui::WithAlpha(ui::TextPrimary, 4)));
    StrokeRound(g, zoomGroup, S(9), Color(ui::WithAlpha(ui::TextPrimary, 9)));

    auto zoomButton = [&](int id, bool plus) {
        const RectF* rect = ZoneRect(layout, id);
        if (!rect) return;
        const bool hover = hoverId_ == id;
        if (hover) {
            FillRound(g, *rect, S(7), Color(ui::WithAlpha(ui::TextPrimary, 10)));
        }
        const float cx = rect->X + rect->Width * 0.5f;
        const float cy = rect->Y + rect->Height * 0.5f;
        Pen pen(hover ? ui::TextPrimary : ui::TextSecondary, S(1.3f));
        g.DrawLine(&pen, cx - S(4), cy, cx + S(4), cy);
        if (plus) g.DrawLine(&pen, cx, cy - S(4), cx, cy + S(4));
    };
    zoomButton(zid::ZoomOut, false);
    zoomButton(zid::ZoomIn, true);

    const RectF* zoomReset = ZoneRect(layout, zid::ZoomReset);
    if (zoomReset) {
        if (hoverId_ == zid::ZoomReset) {
            FillRound(g, *zoomReset, S(7), Color(ui::WithAlpha(ui::TextPrimary, 10)));
        }
        DrawTextIn(g, std::to_wstring(zoom_) + L"%", *zoomReset, S(9.4),
                   hoverId_ == zid::ZoomReset ? ui::TextPrimary : ui::TextSecondary,
                   FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
    }

    std::wstring countText;
    if (layout.showConnect) {
        countText = L"Sin conexión";
    } else if (pendingDeep_) {
        countText = L"Buscando en subcarpetas…";
    } else {
        wchar_t buffer[128]{};
        const bool searchActive = !view.search.empty();
        swprintf(buffer, 128, searchActive ? L"%zu resultado%s" : L"%zu elemento%s",
                 layout.visible.size(), layout.visible.size() == 1 ? L"" : L"s");
        countText = buffer;
        if (view.deep) countText += L" · subcarpetas";
    }
    const float countRight = layout.zoomGroup.X - S(10);
    DrawTextIn(g, countText,
               RectF(countRight - S(220), layout.filters.Y, S(220),
                     layout.filters.Height),
               S(10.2), pendingDeep_ ? ui::Accent2 : ui::TextMuted, FontStyleRegular,
               StringAlignmentFar, StringAlignmentCenter);
}

void App::DrawSidebar(Graphics& g, const Layout& layout) {
    FillRound(g, layout.sidebar, S(14), Color(ui::WithAlpha(ui::TextPrimary, 5)));
    StrokeRound(g, layout.sidebar, S(14), Color(ui::WithAlpha(ui::TextPrimary, 9)));

    for (const auto& entry : layout.side) {
        if (entry.kind == SideEntry::Kind::Section) {
            DrawTextIn(g, entry.label, entry.rect, S(8.7), ui::TextMuted,
                       FontStyleBold);
            continue;
        }
        const bool hover = hoverId_ == (zid::SidebarBase + static_cast<int>(&entry - layout.side.data()));
        if (entry.active) {
            FillRound(g, entry.rect, S(9), Color(ui::WithAlpha(ui::Accent, 20)));
            FillRound(g, RectF(entry.rect.X + S(1), entry.rect.Y + S(9), S(3),
                               entry.rect.Height - S(18)),
                      S(1.5f), ui::AccentSoft);
        } else if (hover) {
            FillRound(g, entry.rect, S(9), Color(ui::WithAlpha(ui::TextPrimary, 7)));
        }
        Glyph(g, entry.glyph.c_str(),
              RectF(entry.rect.X + S(12), entry.rect.Y, S(18), entry.rect.Height),
              S(12), entry.active ? ui::AccentSoft : ui::TextMuted);
        DrawTextIn(g, entry.label,
                   RectF(entry.rect.X + S(34), entry.rect.Y, entry.rect.Width - S(42),
                         entry.rect.Height),
                   S(11.2), entry.active ? ui::TextPrimary : ui::TextSecondary);
    }

    if (!layout.showSidebarTip) return;

    const RectF& tip = layout.sidebarTip;
    GraphicsPath tipPath;
    AddRound(tipPath, tip, S(12));
    LinearGradientBrush tipBrush(
        tip, Color(ui::WithAlpha(ui::Accent, 25)),
        Color(ui::WithAlpha(ui::TextPrimary, 3)), LinearGradientModeVertical);
    g.FillPath(&tipBrush, &tipPath);
    Pen tipBorder(Color(ui::WithAlpha(ui::AccentSoft, 35)), 1.0f);
    g.DrawPath(&tipBorder, &tipPath);

    if (tab_ == Tab::Library) {
        FillCircle(g, tip.X + S(14), tip.Y + S(15), S(3), ui::AccentSoft);
        DrawTextIn(g, L"ARRASTRA A TU EDITOR",
                   RectF(tip.X + S(22), tip.Y + S(7), tip.Width - S(30), S(16)),
                   S(8.2), ui::AccentSoft, FontStyleBold);
        DrawWrapped(g,
                    L"Suelta tus archivos en Premiere, DaVinci o CapCut.",
                    RectF(tip.X + S(12), tip.Y + S(28), tip.Width - S(24), S(48)), S(9.6),
                    ui::TextSecondary);
    } else if (!driveRoot_.empty() && DirExists(driveRoot_)) {
        FillCircle(g, tip.X + S(14), tip.Y + S(15), S(3), ui::Success);
        DrawTextIn(g, L"GOOGLE DRIVE",
                   RectF(tip.X + S(22), tip.Y + S(7), tip.Width - S(30), S(16)),
                   S(8.2), ui::Success, FontStyleBold);
        DrawWrapped(g,
                    L"Los archivos en la nube se descargan al arrastrarlos.",
                    RectF(tip.X + S(12), tip.Y + S(28), tip.Width - S(24), S(48)), S(9.6),
                    ui::TextSecondary);
    } else {
        FillCircle(g, tip.X + S(14), tip.Y + S(15), S(3), ui::Accent2);
        DrawTextIn(g, L"CONECTA DRIVE",
                   RectF(tip.X + S(22), tip.Y + S(7), tip.Width - S(30), S(16)),
                   S(8.2), ui::Accent2, FontStyleBold);
        DrawWrapped(g, L"Elige Mi unidad o arrastra la carpeta a la ventana.",
                    RectF(tip.X + S(12), tip.Y + S(28), tip.Width - S(24), S(48)), S(9.6),
                    ui::TextSecondary);
    }
}

void App::DrawGrid(Graphics& g, const Layout& layout) {
    const ViewState& view = View();
    if (layout.visible.empty()) {
        const float cx = layout.grid.X + layout.grid.Width * 0.5f;
        const float cy = layout.grid.Y + layout.grid.Height * 0.42f;
        const float ring = S(31);
        FillCircle(g, cx, cy, ring, Color(ui::WithAlpha(ui::TextPrimary, 5)));
        Pen ringBorder(Color(ui::WithAlpha(ui::TextPrimary, 10)), 1.0f);
        g.DrawEllipse(&ringBorder, cx - ring, cy - ring, ring * 2, ring * 2);
        const bool searching = !view.search.empty();
        Glyph(g, searching ? L"\uE721" : (tab_ == Tab::Drive ? L"\uE753" : L"\uE8B7"),
              RectF(cx - S(16), cy - S(16), S(32), S(32)), S(21),
              searching ? ui::AccentSoft : ui::TextMuted);
        std::wstring title;
        std::wstring subtitle;
        if (pendingDeep_) {
            title = L"Buscando en subcarpetas…";
            subtitle = L"Esto puede tardar unos segundos.";
        } else if (searching) {
            title = L"Sin resultados";
            subtitle = view.recursive
                           ? L"No hay coincidencias en esta carpeta ni en sus subcarpetas."
                           : L"Prueba con otro término o activa Subcarpetas.";
        } else if (layout.showConnect) {
            title = L"Google Drive";
            subtitle = L"Conecta tu unidad para ver tus archivos aquí.";
        } else {
            title = L"Esta carpeta está vacía";
            subtitle = L"Arrastra archivos aquí o navega a otra carpeta.";
        }
        DrawTextIn(g, title, RectF(cx - S(260), cy + S(44), S(520), S(24)), S(14),
                   ui::TextPrimary, FontStyleBold, StringAlignmentCenter);
        DrawWrapped(g, subtitle, RectF(cx - S(250), cy + S(72), S(500), S(44)), S(10.2),
                    ui::TextMuted, StringAlignmentCenter);

        if (!layout.showScrollThumb) return;
    }

    const GraphicsState state = g.Save();
    g.SetClip(layout.grid, CombineModeReplace);

    for (const auto& card : layout.cards) {
        if (card.rect.GetBottom() < layout.grid.Y ||
            card.rect.Y > layout.grid.GetBottom() ||
            card.rect.GetRight() < layout.grid.X ||
            card.rect.X > layout.grid.GetRight()) {
            continue;
        }
        if (card.index < 0 || card.index >= static_cast<int>(view.items.size())) continue;
        const MediaItem& item = view.items[card.index];
        const bool selected = IsSelected(item.path);
        const bool hover = hoverCard_ == card.index;
        DrawCard(g, card, selected, hover, item);
    }
    g.Restore(state);

    if (layout.showScrollThumb) {
        FillRound(g, layout.scrollThumb, S(2), Color(ui::WithAlpha(ui::TextPrimary, 46)));
    }
}

void App::DrawCard(Graphics& g, const CardHit& card, bool selected, bool hover,
                   const MediaItem& item) {
    const RectF& r = card.rect;
    const float scale = std::clamp(r.Width / std::max(1.0f, S(176.0f)), 0.75f, 1.7f);
    const float fontScale = std::clamp(scale, 0.9f, 1.3f);
    const float radius = S(12) * scale;
    Color fill = ui::CardFill;
    if (selected) fill = Color(ui::WithAlpha(ui::Accent, 26));
    else if (hover) fill = ui::CardFillHover;
    FillRound(g, r, radius, fill);
    if (selected) {
        StrokeRound(g, r, radius, Color(ui::WithAlpha(ui::AccentSoft, 135)), 1.4f);
    } else {
        StrokeRound(g, r, radius,
                    hover ? Color(ui::WithAlpha(ui::AccentSoft, 55))
                          : Color(ui::WithAlpha(ui::TextPrimary, 10)));
    }

    const float pad = S(10) * scale;
    const RectF thumb(r.X + pad, r.Y + pad, r.Width - pad * 2, S(102) * scale);
    const float thumbRadius = S(9) * scale;
    GraphicsPath thumbPath;
    AddRound(thumbPath, thumb, thumbRadius);

    const ARGB kindColor = ui::KindColor(item.kind);
    LinearGradientBrush placeholder(
        thumb, Color(ui::WithAlpha(kindColor, item.isDir ? 30 : 42)),
        Color(ui::WithAlpha(ui::C(255, 7, 8, 12), 90)), LinearGradientModeVertical);
    g.FillPath(&placeholder, &thumbPath);
    if (item.kind == ItemKind::Folder) {
        const float fw = S(56) * scale;
        const float fh = S(44) * scale;
        DrawFolderGlyph(g,
                        RectF(thumb.X + (thumb.Width - fw) * 0.5f,
                              thumb.Y + (thumb.Height - fh) * 0.5f, fw, fh),
                        Color(ui::WithAlpha(kindColor, 205)));
    } else if (item.kind == ItemKind::Program) {
        const float pw = S(44) * scale;
        const float ph = S(34) * scale;
        DrawProgramGlyph(g,
                         RectF(thumb.X + (thumb.Width - pw) * 0.5f,
                               thumb.Y + (thumb.Height - ph) * 0.5f, pw, ph),
                         Color(ui::WithAlpha(kindColor, 210)));
    } else if (item.kind == ItemKind::Audio) {
        DrawAudioFileArtwork(g, thumb);
    } else {
        Glyph(g, KindGlyph(item.kind),
              RectF(thumb.X, thumb.Y, thumb.Width, thumb.Height),
              S(31) * std::clamp(scale, 0.9f, 1.35f),
              Color(ui::WithAlpha(kindColor, 185)));
    }

    if (thumbs_ && !item.cloudOnly && item.kind != ItemKind::Audio) {
        auto bitmap = thumbs_->Get(item.path);
        if (!bitmap && !thumbs_->Failed(item.path)) {
            thumbs_->Request(item.path);
        }
        if (bitmap && bitmap->GetLastStatus() == Ok) {
            const float iw = static_cast<float>(bitmap->GetWidth());
            const float ih = static_cast<float>(bitmap->GetHeight());
            if (iw > 0 && ih > 0) {
                const bool cover = item.kind == ItemKind::Image ||
                                   item.kind == ItemKind::Video;
                RectF destination = thumb;
                if (cover) {
                    float scale = std::max(thumb.Width / iw, thumb.Height / ih);
                    float w = iw * scale;
                    float h = ih * scale;
                    destination = RectF(thumb.X + (thumb.Width - w) * 0.5f,
                                        thumb.Y + (thumb.Height - h) * 0.5f, w, h);
                } else {
                    const float fitting = std::min(
                        (thumb.Width - S(14) * scale) / iw,
                        (thumb.Height - S(14) * scale) / ih);
                    float w = iw * fitting;
                    float h = ih * fitting;
                    destination = RectF(thumb.X + (thumb.Width - w) * 0.5f,
                                        thumb.Y + (thumb.Height - h) * 0.5f, w, h);
                }
                GraphicsState state = g.Save();
                // Intersect with the grid clip: replacing it would let a
                // thumbnail from a partially visible row spill over the UI.
                g.SetClip(&thumbPath, CombineModeIntersect);
                g.DrawImage(bitmap.get(), destination);
                g.Restore(state);
            }
        }
    }

    StrokeRound(g, thumb, thumbRadius, Color(ui::WithAlpha(ui::TextPrimary, 10)));

    DrawTextIn(g, item.name,
               RectF(r.X + S(11) * scale, r.Y + S(116) * scale,
                     r.Width - S(22) * scale, S(18) * fontScale),
               S(11.8) * fontScale, ui::TextPrimary);

    const RectF meta(r.X + S(11) * scale, r.Y + S(137) * scale,
                     r.Width - S(22) * scale, S(16) * fontScale);
    FillCircle(g, meta.X + S(3) * fontScale, meta.Y + meta.Height * 0.5f,
               S(3) * fontScale, Color(ui::WithAlpha(kindColor, 230)));
    DrawTextIn(g, KindMeta(item),
               RectF(meta.X + S(11) * fontScale, meta.Y,
                     meta.Width - S(11) * fontScale, meta.Height),
               S(9.7) * fontScale, ui::TextSecondary);

    if (!item.isDir && item.cloudOnly) {
        const RectF badge(thumb.GetRight() - S(31) * scale,
                          thumb.GetBottom() - S(25) * scale, S(24) * scale,
                          S(18) * scale);
        FillRound(g, badge, S(7) * scale, Color(150, 3, 5, 10));
        Glyph(g, L"\uE753", badge, S(9.5) * scale, ui::Accent2);
    }

    if (selected) {
        const float cx = thumb.GetRight() - S(17) * scale;
        const float cy = thumb.Y + S(17) * scale;
        FillCircle(g, cx, cy, S(9) * scale, ui::Accent);
        Glyph(g, L"\uE73E",
              RectF(cx - S(7) * scale, cy - S(7) * scale, S(14) * scale,
                    S(14) * scale),
              S(8) * scale, Color(255, 255, 255, 255));
    }
}

void App::DrawStatus(Graphics& g, const Layout& layout) {
    Pen divider(Color(ui::WithAlpha(ui::TextPrimary, 8)), 1.0f);
    g.DrawLine(&divider, layout.status.X, layout.status.Y,
               layout.status.GetRight(), layout.status.Y);

    const ViewState& view = View();
    std::wstring left;
    if (layout.showConnect) {
        left = L"Google Drive · Sin conexión";
    } else {
        wchar_t buffer[160]{};
        const bool searchActive = !view.search.empty();
        swprintf(buffer, 160, L"%zu %s", layout.visible.size(),
                 layout.visible.size() == 1
                     ? (searchActive ? L"resultado" : L"elemento")
                     : (searchActive ? L"resultados" : L"elementos"));
        left = buffer;
        if (!selection_.empty()) {
            wchar_t extra[96]{};
            swprintf(extra, 96, L"  ·  %zu seleccionado%s", selection_.size(),
                     selection_.size() == 1 ? L"" : L"s");
            left += extra;
        }
    }
    DrawTextIn(g, left, RectF(layout.status.X, layout.status.Y + S(5),
                              layout.status.Width * 0.46f, S(22)),
               S(10.0), ui::TextMuted);

    std::wstring right;
    ARGB color = ui::TextMuted;
    if (ToastActive()) {
        right = toast_;
        color = toastIsError_ ? ui::Danger : ui::Accent2;
    } else if (pendingDeep_) {
        right = L"Buscando en subcarpetas…";
        color = ui::Accent2;
    } else {
        std::wstring hoverName = HoverItemName(layout);
        if (!hoverName.empty()) {
            right = hoverName;
            color = ui::TextSecondary;
        } else if (updateReady_) {
            right = L"Actualización " + updateVersion_ +
                    L" lista · se instalará al cerrar";
            color = ui::Accent2;
        } else if (layout.showConnect) {
            right = L"Instala Drive para escritorio o elige una carpeta";
        } else if (tab_ == Tab::Drive) {
            right = L"Arrastra a tu editor · lo que está en la nube se descarga al usarlo";
        } else {
            right = L"Arrastra a tu editor · doble clic para abrir";
        }
    }
    DrawTextIn(g, right,
               RectF(layout.status.X + layout.status.Width * 0.47f,
                     layout.status.Y + S(5), layout.status.Width * 0.53f, S(22)),
               S(9.8), color, FontStyleRegular, StringAlignmentFar);
}

void App::DrawSettings(Graphics& g, const Layout& layout) {
    for (int i = 7; i >= 1; --i) {
        RectF shadow = layout.settingsPanel;
        float expand = S(i * 1.6f);
        shadow.X -= expand;
        shadow.Y -= expand;
        shadow.Width += expand * 2;
        shadow.Height += expand * 2;
        FillRound(g, shadow, S(15) + expand, Color(3, 0, 0, 0));
    }
    FillRound(g, layout.settingsPanel, S(14), Color(252, 20, 21, 30));
    StrokeRound(g, layout.settingsPanel, S(14),
                Color(ui::WithAlpha(ui::TextPrimary, 22)));

    DrawTextIn(g, L"Apariencia",
               RectF(layout.settingsPanel.X + S(18), layout.settingsPanel.Y + S(13),
                     layout.settingsPanel.Width - S(36), S(22)),
               S(12.5), ui::TextPrimary, FontStyleBold);
    const std::wstring versionLabel =
        updateReady_ ? (L"Actualización " + updateVersion_)
                     : (L"v" NEBULA_VERSION_W);
    DrawTextIn(g, versionLabel,
               RectF(layout.settingsPanel.X + S(110), layout.settingsPanel.Y + S(13),
                     layout.settingsPanel.Width - S(128), S(22)),
               S(8.6), updateReady_ ? ui::Accent2 : ui::TextMuted,
               FontStyleRegular, StringAlignmentFar);

    const float pct = 40.0f + (static_cast<float>(opacity_) - 100.0f) / 155.0f * 60.0f;
    wchar_t pctText[16]{};
    swprintf(pctText, 16, L"%.0f%%", pct);

    DrawTextIn(g, L"Transparencia",
               RectF(layout.settingsPanel.X + S(18), layout.settingsPanel.Y + S(49),
                     layout.settingsPanel.Width - S(100), S(20)),
               S(10.2), ui::TextSecondary);
    DrawTextIn(g, pctText,
               RectF(layout.settingsPanel.GetRight() - S(80),
                     layout.settingsPanel.Y + S(49), S(62), S(20)),
               S(10.2), ui::AccentSoft, FontStyleBold, StringAlignmentFar);

    FillRound(g, layout.opacityTrack, S(4), Color(28, 255, 255, 255));
    const float t = (static_cast<float>(opacity_) - 100.0f) / 155.0f;
    const float knobX = layout.opacityTrack.X + layout.opacityTrack.Width * t;
    if (knobX > layout.opacityTrack.X + S(2)) {
        FillRound(g,
                  RectF(layout.opacityTrack.X, layout.opacityTrack.Y,
                        knobX - layout.opacityTrack.X, layout.opacityTrack.Height),
                  S(4), ui::AccentSoft);
    }
    FillCircle(g, knobX, layout.opacityTrack.Y + layout.opacityTrack.Height / 2,
               S(9), Color(60, 0, 0, 0));
    FillCircle(g, knobX, layout.opacityTrack.Y + layout.opacityTrack.Height / 2,
               S(6.5f), ui::TextPrimary);

    Pen divider(Color(ui::WithAlpha(ui::TextPrimary, 12)), 1.0f);
    g.DrawLine(&divider, layout.settingsPanel.X + S(18),
               layout.settingsPanel.Y + S(106),
               layout.settingsPanel.GetRight() - S(18),
               layout.settingsPanel.Y + S(106));

    DrawTextIn(g, L"Mantener siempre arriba",
               RectF(layout.settingsPanel.X + S(18), layout.settingsPanel.Y + S(118),
                     layout.settingsPanel.Width - S(70), S(24)),
               S(10.2), ui::TextPrimary);
    DrawTextIn(g, L"Por encima de otros programas",
               RectF(layout.settingsPanel.X + S(18), layout.settingsPanel.Y + S(141),
                     layout.settingsPanel.Width - S(70), S(18)),
               S(8.6), ui::TextMuted);

    const RectF& toggle = layout.toggleSwitch;
    FillRound(g, toggle, toggle.Height / 2,
              pin_ ? ui::Accent : Color(30, 255, 255, 255));
    const float knobRadius = toggle.Height * 0.35f;
    const float knobCenterX =
        pin_ ? toggle.GetRight() - toggle.Height * 0.5f : toggle.X + toggle.Height * 0.5f;
    FillCircle(g, knobCenterX, toggle.Y + toggle.Height * 0.5f, knobRadius,
               ui::TextPrimary);
}

void App::DrawConnect(Graphics& g, const Layout& layout) {
    FillRound(g, layout.connectPanel, S(16), Color(ui::WithAlpha(ui::TextPrimary, 5)));
    StrokeRound(g, layout.connectPanel, S(16),
                Color(ui::WithAlpha(ui::TextPrimary, 12)));

    const float centerX = layout.connectPanel.X + layout.connectPanel.Width * 0.5f;
    const float iconY = layout.connectPanel.Y + S(34);
    GraphicsPath circle;
    circle.AddEllipse(centerX - S(31), iconY, S(62), S(62));
    LinearGradientBrush iconFill(
        RectF(centerX - S(31), iconY, S(62), S(62)),
        Color(ui::WithAlpha(ui::Accent, 70)), Color(ui::WithAlpha(ui::Accent2, 35)),
        LinearGradientModeForwardDiagonal);
    g.FillPath(&iconFill, &circle);
    Pen iconBorder(Color(ui::WithAlpha(ui::AccentSoft, 80)), 1.0f);
    g.DrawPath(&iconBorder, &circle);
    Glyph(g, L"\uE753",
          RectF(centerX - S(18), iconY + S(16), S(36), S(30)), S(27), ui::AccentSoft);

    const bool missing = !driveRoot_.empty() && !DirExists(driveRoot_);
    DrawTextIn(g, missing ? L"No encontramos tu carpeta de Drive"
                          : L"Conecta tu Google Drive",
               RectF(layout.connectPanel.X + S(20), iconY + S(76),
                     layout.connectPanel.Width - S(40), S(28)),
               S(17), ui::TextPrimary, FontStyleBold, StringAlignmentCenter);

    std::wstring body;
    if (missing) {
        body = L"La carpeta configurada ya no está disponible:\n" + driveRoot_;
    } else {
        body = L"Abre Google Drive para escritorio y gestiónalo como una carpeta más. "
               L"También puedes arrastrar Mi unidad a esta ventana.";
    }
    DrawWrapped(g, body,
                RectF(layout.connectPanel.X + S(38), iconY + S(108),
                      layout.connectPanel.Width - S(76), S(58)),
                S(10.2), ui::TextSecondary, StringAlignmentCenter);

    DrawTextIn(g,
               L"Arrastra archivos a tu editor aunque estén solo en la nube.",
               RectF(layout.connectPanel.X + S(20),
                     layout.connectPanel.GetBottom() - S(89),
                     layout.connectPanel.Width - S(40), S(18)),
               S(9.0), ui::TextMuted, FontStyleRegular, StringAlignmentCenter);

    auto button = [&](const RectF& rect, int id, const std::wstring& label, int style) {
        const bool hover = hoverId_ == id;
        Color fill;
        Color border;
        Color textColor;
        if (style == 0) {
            fill = hover ? Color(255, 159, 122, 255) : Color(ui::Accent);
            border = Color(ui::WithAlpha(ui::TextPrimary, 30));
            textColor = Color(ui::TextPrimary);
        } else if (style == 1) {
            fill = Color(ui::WithAlpha(ui::TextPrimary, hover ? 12 : 7));
            border = Color(ui::WithAlpha(ui::TextPrimary, 16));
            textColor = Color(ui::TextPrimary);
        } else {
            fill = Color(ui::WithAlpha(ui::TextPrimary, hover ? 7 : 3));
            border = Color(ui::WithAlpha(ui::TextPrimary, 9));
            textColor = Color(ui::TextSecondary);
        }
        FillRound(g, rect, S(10), fill);
        StrokeRound(g, rect, S(10), border);
        DrawTextIn(g, label, rect, S(10.2), textColor,
                   style == 0 ? FontStyleBold : FontStyleRegular,
                   StringAlignmentCenter, StringAlignmentCenter);
    };

    button(layout.connectDetect, zid::DriveDetect, L"Detectar automáticamente", 0);
    button(layout.connectPick, zid::DrivePick, L"Elegir carpeta…", 1);
    button(layout.connectWeb, zid::DriveWeb, L"Abrir en el navegador", 2);
}

// ------------------------------------------------------------------ editor

void App::DrawEditor(Graphics& g, const Layout& layout) {
    // Top bar.
    const RectF* back = ZoneRect(layout, zid::EditorBack);
    if (back) {
        if (hoverId_ == zid::EditorBack) {
            FillRound(g, *back, S(9), Color(ui::WithAlpha(ui::TextPrimary, 9)));
        }
        Glyph(g, L"\uE72B", *back, S(13), ui::TextSecondary);
    }

    const float titleX = layout.editor.X + S(46);
    DrawTextIn(g, L"Editor de audio",
               RectF(titleX, layout.editor.Y + S(4), S(330), S(23)), S(15.0),
               ui::TextPrimary, FontStyleBold);

    std::wstring meta = audio_.displayName;
    if (meta.empty()) meta = FileNameOf(audio_.sourcePath);
    meta += L"  ·  " + UpperExtension(audio_.sourcePath) + L"  ·  " +
            RateText(audio_.rate) + L"  ·  " +
            (audio_.channels == 1 ? L"Mono" : L"Estéreo") + L"  ·  " +
            FormatTime(ClipFrames(audio_) / static_cast<double>(audio_.rate), false);
    if (audio_.truncated) meta += L"  ·  primeros 10 min";
    DrawTextIn(g, meta,
               RectF(titleX, layout.editor.Y + S(27), layout.editor.Width - S(390),
                     S(17)),
               S(9.8), audio_.dirty ? ui::AccentSoft : ui::TextMuted);

    const RectF* undo = ZoneRect(layout, zid::EditorUndo);
    if (undo) {
        const bool enabled = canUndo_;
        const bool hover = enabled && hoverId_ == zid::EditorUndo;
        FillRound(g, *undo, S(9),
                  Color(ui::WithAlpha(ui::TextPrimary, hover ? 10 : 5)));
        StrokeRound(g, *undo, S(9), Color(ui::WithAlpha(ui::TextPrimary, 10)));
        Glyph(g, L"\uE7A7", RectF(undo->X + S(9), undo->Y, S(15), undo->Height), S(10),
              enabled ? ui::TextSecondary : Color(ui::WithAlpha(ui::TextMuted, 90)));
        DrawTextIn(g, L"Deshacer",
                   RectF(undo->X + S(27), undo->Y, undo->Width - S(32), undo->Height),
                   S(10.0),
                   enabled ? ui::TextSecondary : Color(ui::WithAlpha(ui::TextMuted, 90)));
    }

    const RectF* save = ZoneRect(layout, zid::EditorSave);
    if (save) {
        const bool hover = hoverId_ == zid::EditorSave;
        FillRound(g, *save, S(9),
                  hover ? Color(255, 159, 122, 255) : ui::Accent);
        StrokeRound(g, *save, S(9), Color(ui::WithAlpha(ui::TextPrimary, 30)));
        Glyph(g, L"\uE74E", RectF(save->X + S(11), save->Y, S(15), save->Height), S(10.5),
              ui::TextPrimary);
        DrawTextIn(g, L"Guardar WAV",
                   RectF(save->X + S(30), save->Y, save->Width - S(34), save->Height),
                   S(10.0), ui::TextPrimary, FontStyleBold);
    }

    // Waveform card.
    FillRound(g, layout.editorWave, S(12), Color(58, 0, 0, 0));
    StrokeRound(g, layout.editorWave, S(12),
                Color(ui::WithAlpha(ui::TextPrimary, 10)));

    const RectF& plot = layout.editorPlot;
    FillRound(g, plot, S(8), Color(36, 0, 0, 0));
    StrokeRound(g, plot, S(8), Color(ui::WithAlpha(ui::TextPrimary, 6)));

    const int64_t frames = ClipFrames(audio_);
    const double duration = audio_.rate > 0 ? frames / static_cast<double>(audio_.rate) : 0.0;
    const float centerY = plot.Y + plot.Height * 0.5f;
    const float amplitude = plot.Height * 0.45f;

    // Ruler.
    double step = 1.0;
    const double steps[] = {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0,
                            120.0, 300.0, 600.0};
    if (duration > 0) {
        for (double candidate : steps) {
            step = candidate;
            if (duration / candidate <= 6.0) break;
        }
    }
    const bool showHundredths = step < 1.0;
    Pen gridPen(Color(ui::WithAlpha(ui::TextPrimary, 10)), 1.0f);
    double lastTick = 0.0;
    for (double t = 0.0; t <= duration + 0.0001; t += step) {
        const float x = plot.X + plot.Width * static_cast<float>(t / std::max(duration, 0.001));
        g.DrawLine(&gridPen, x, plot.Y, x, plot.GetBottom());
        std::wstring label = FormatTime(t, showHundredths);
        const bool nearRight = x > plot.GetRight() - S(52);
        DrawTextIn(g, label,
                   RectF(nearRight ? x - S(58) : x + S(4), layout.editorWave.Y + S(7),
                         S(58), S(16)),
                   S(8.5), ui::TextMuted, FontStyleRegular,
                   nearRight ? StringAlignmentFar : StringAlignmentNear);
        lastTick = t;
    }
    if (duration > 0 && duration - lastTick > step * 0.2) {
        DrawTextIn(g, FormatTime(duration, showHundredths),
                   RectF(plot.GetRight() - S(58), layout.editorWave.Y + S(7), S(58),
                         S(16)),
                   S(8.5), ui::TextMuted, FontStyleRegular, StringAlignmentFar);
    }

    if (HasSelection()) {
        float sx = plot.X + plot.Width * (static_cast<float>(selectionStart_) /
                                          static_cast<float>(std::max<int64_t>(1, frames)));
        float ex = plot.X + plot.Width * (static_cast<float>(selectionEnd_) /
                                          static_cast<float>(std::max<int64_t>(1, frames)));
        FillRound(g, RectF(sx, plot.Y, std::max(1.0f, ex - sx), plot.Height), S(2),
                  Color(34, 34, 211, 238));
    }

    // Center line.
    Pen centerLine(Color(ui::WithAlpha(ui::TextPrimary, 18)), 1.0f);
    g.DrawLine(&centerLine, plot.X, centerY, plot.GetRight(), centerY);

    const int peaks = static_cast<int>(audio_.peakMin.size());
    if (frames > 0 && peaks > 1) {
        auto drawWave = [&](ARGB color, const RectF* clipRect) {
            GraphicsState state = g.Save();
            if (clipRect) g.SetClip(*clipRect, CombineModeIntersect);
            Pen pen(Color(color), 1.0f);
            for (int x = 0; x < static_cast<int>(plot.Width); ++x) {
                const int p0 = static_cast<int>(static_cast<int64_t>(x) * peaks / plot.Width);
                const int p1 = std::max(
                    p0 + 1,
                    static_cast<int>(static_cast<int64_t>(x + 1) * peaks / plot.Width));
                float mn = 1.0f;
                float mx = -1.0f;
                for (int p = p0; p < p1 && p < peaks; ++p) {
                    mn = std::min(mn, audio_.peakMin[p]);
                    mx = std::max(mx, audio_.peakMax[p]);
                }
                if (mn > mx) {
                    mn = mx = 0.0f;
                }
                const float y1 = centerY - mx * amplitude;
                const float y2 = centerY - mn * amplitude;
                g.DrawLine(&pen, plot.X + x, y1, plot.X + x,
                           std::max(y2, y1 + 0.7f));
            }
            g.Restore(state);
        };
        drawWave(ui::WithAlpha(ui::AccentSoft, 190), nullptr);
        if (HasSelection()) {
            float sx = plot.X + plot.Width * (static_cast<float>(selectionStart_) /
                                              static_cast<float>(std::max<int64_t>(1, frames)));
            float ex = plot.X + plot.Width * (static_cast<float>(selectionEnd_) /
                                              static_cast<float>(std::max<int64_t>(1, frames)));
            RectF selection(sx, plot.Y, std::max(1.0f, ex - sx), plot.Height);
            drawWave(ui::WithAlpha(ui::Accent2, 245), &selection);
        }
    }

    if (HasSelection()) {
        float sx = plot.X + plot.Width * (static_cast<float>(selectionStart_) /
                                          static_cast<float>(std::max<int64_t>(1, frames)));
        float ex = plot.X + plot.Width * (static_cast<float>(selectionEnd_) /
                                          static_cast<float>(std::max<int64_t>(1, frames)));
        Pen edge(Color(ui::WithAlpha(ui::Accent2, 160)), 1.0f);
        g.DrawLine(&edge, sx, plot.Y, sx, plot.GetBottom());
        g.DrawLine(&edge, ex, plot.Y, ex, plot.GetBottom());
    }

    if (frames > 0) {
        const float px = plot.X + plot.Width *
                                      (static_cast<float>(playhead_) /
                                       static_cast<float>(frames));
        Pen playheadPen(Color(ui::Accent2), 1.3f);
        g.DrawLine(&playheadPen, px, plot.Y, px, plot.GetBottom());
        GraphicsPath marker;
        marker.AddPolygon(std::vector<PointF>{
                              PointF(px - S(5), plot.Y),
                              PointF(px + S(5), plot.Y),
                              PointF(px, plot.Y + S(7))}
                              .data(),
                          3);
        SolidBrush markerBrush{Color(ui::Accent2)};
        g.FillPath(&markerBrush, &marker);
    }

    // Bottom transport card.
    FillRound(g, layout.editorBottom, S(12),
              Color(ui::WithAlpha(ui::TextPrimary, 5)));
    StrokeRound(g, layout.editorBottom, S(12),
                Color(ui::WithAlpha(ui::TextPrimary, 8)));

    const bool active = player_ && player_->IsActive();
    const bool paused = player_ && player_->IsPaused();
    const bool hoveringPlay = hoverId_ == zid::EditorPlay;
    GraphicsPath playCircle;
    playCircle.AddEllipse(layout.editorPlay.X, layout.editorPlay.Y,
                          layout.editorPlay.Width, layout.editorPlay.Height);
    LinearGradientBrush playBrush(
        layout.editorPlay,
        hoveringPlay ? Color(255, 159, 122, 255) : Color(255, 139, 92, 246),
        Color(255, 34, 211, 238), LinearGradientModeForwardDiagonal);
    g.FillPath(&playBrush, &playCircle);
    Pen playBorder(Color(ui::WithAlpha(ui::TextPrimary, 35)), 1.0f);
    g.DrawPath(&playBorder, &playCircle);
    Glyph(g, (active && !paused) ? L"\uE769" : L"\uE768", layout.editorPlay, S(16),
          ui::TextPrimary);

    const RectF* stop = ZoneRect(layout, zid::EditorStop);
    if (stop) {
        const bool hover = hoverId_ == zid::EditorStop;
        FillRound(g, *stop, S(9), Color(ui::WithAlpha(ui::TextPrimary, hover ? 10 : 6)));
        StrokeRound(g, *stop, S(9), Color(ui::WithAlpha(ui::TextPrimary, 10)));
        Glyph(g, L"\uE71A", *stop, S(11),
              active || paused ? ui::TextSecondary : ui::TextMuted);
    }

    const std::wstring current = FormatTime(
        playhead_ / static_cast<double>(std::max(1, audio_.rate)));
    const std::wstring total = FormatTime(
        frames / static_cast<double>(std::max(1, audio_.rate)));
    const float timeX = layout.editorStop.GetRight() + S(14);
    const float currentW = TextWidth(g, current, S(15.0f), FontStyleBold);
    DrawTextIn(g, current,
               RectF(timeX, layout.editorBottom.Y + S(11), currentW + S(10), S(24)),
               S(15.0f), ui::TextPrimary, FontStyleBold);
    DrawTextIn(g, L"/  " + total,
               RectF(timeX + currentW + S(2), layout.editorBottom.Y + S(14),
                     S(120), S(20)),
               S(10.0f), ui::TextMuted);

    std::wstring info;
    if (HasSelection()) {
        info = L"Selección: " +
               FormatTime(selectionStart_ / static_cast<double>(audio_.rate)) + L" – " +
               FormatTime(selectionEnd_ / static_cast<double>(audio_.rate)) + L"  ·  " +
               FormatTime((selectionEnd_ - selectionStart_) /
                          static_cast<double>(audio_.rate));
    } else {
        info = L"Espacio: reproducir  ·  Ctrl+S: guardar  ·  Ctrl+Z: deshacer";
    }
    DrawTextIn(g, info,
               RectF(timeX, layout.editorBottom.Y + S(42),
                     layout.editorBottom.Width - (timeX - layout.editorBottom.X) - S(18),
                     S(18)),
               S(9.3f), ui::TextMuted);

    struct Action {
        int id;
        const wchar_t* label;
        bool needsSelection;
    };
    const Action actions[6] = {
        {zid::EditorTrim, L"Recortar", true},
        {zid::EditorDelete, L"Eliminar", true},
        {zid::EditorFadeIn, L"Fundido in", false},
        {zid::EditorFadeOut, L"Fundido out", false},
        {zid::EditorVolDown, L"Vol -", false},
        {zid::EditorVolUp, L"Vol +", false},
    };
    for (const auto& action : actions) {
        const RectF* rect = ZoneRect(layout, action.id);
        if (!rect) continue;
        const bool enabled = !action.needsSelection || HasSelection();
        const bool hover = hoverId_ == action.id;
        FillRound(g, *rect, S(8),
                  Color(ui::WithAlpha(ui::TextPrimary, hover ? 10 : 5)));
        StrokeRound(g, *rect, S(8),
                    Color(ui::WithAlpha(ui::TextPrimary, enabled ? 10 : 5)));
        DrawTextIn(g, action.label, *rect, S(9.7f),
                   enabled ? (hover ? ui::TextPrimary : ui::TextSecondary)
                           : Color(ui::WithAlpha(ui::TextMuted, 80)),
                   FontStyleRegular, StringAlignmentCenter, StringAlignmentCenter);
    }
}

}  // namespace nebula
