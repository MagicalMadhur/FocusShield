#ifndef UI_THEME_H
#define UI_THEME_H

#include <windows.h>
#include <gdiplus.h>

namespace Theme {

// Dark Theme Color Palette
const Gdiplus::Color CLR_BG(255, 12, 16, 24);            // #0C1018 Deep Obsidian
const Gdiplus::Color CLR_CARD(255, 18, 24, 36);          // #121824 Elevated Card
const Gdiplus::Color CLR_CARD_BORDER(255, 34, 44, 64);   // #222C40 Card Border
const Gdiplus::Color CLR_CARD_HOVER(255, 26, 35, 52);    // #1A2334 Card Hover

const Gdiplus::Color CLR_EMERALD(255, 16, 185, 129);     // #10B981 Active Emerald
const Gdiplus::Color CLR_EMERALD_DARK(255, 5, 150, 105); // #059669 Emerald Dark
const Gdiplus::Color CLR_EMERALD_BG(255, 12, 42, 32);    // #0C2A20 Emerald Tint

const Gdiplus::Color CLR_CYAN(255, 6, 182, 212);         // #06B6D4 Laser Cyan
const Gdiplus::Color CLR_CYAN_BG(255, 10, 36, 48);       // #0A2430 Cyan Tint

const Gdiplus::Color CLR_CRIMSON(255, 244, 63, 94);      // #F43F5E Coral Crimson
const Gdiplus::Color CLR_CRIMSON_BG(255, 46, 18, 24);    // #2E1218 Crimson Tint

const Gdiplus::Color CLR_AMBER(255, 245, 158, 11);       // #F59E0B Warm Amber

const Gdiplus::Color CLR_TEXT_TITLE(255, 255, 255, 255);
const Gdiplus::Color CLR_TEXT_BODY(255, 226, 232, 240);  // #E2E8F0
const Gdiplus::Color CLR_TEXT_MUTED(255, 148, 163, 184); // #94A3B8
const Gdiplus::Color CLR_TEXT_DIM(255, 100, 116, 139);   // #64748B

// Win32 GDI COLORREF equivalents
#define RGB_BG          RGB(12, 16, 24)
#define RGB_CARD        RGB(18, 24, 36)
#define RGB_CARD_BORDER RGB(34, 44, 64)
#define RGB_INPUT_BG    RGB(10, 14, 20)
#define RGB_TEXT_WHITE  RGB(255, 255, 255)
#define RGB_TEXT_MUTED  RGB(148, 163, 184)
#define RGB_EMERALD     RGB(16, 185, 129)
#define RGB_CYAN        RGB(6, 182, 212)

inline void AddRoundedRectangle(Gdiplus::GraphicsPath& path, float x, float y, float width, float height, float radius) {
    float diameter = radius * 2.0f;
    if (diameter > width) diameter = width;
    if (diameter > height) diameter = height;

    path.Reset();
    path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

inline void FillAndDrawRoundedRect(Gdiplus::Graphics& g, Gdiplus::Brush* brush, Gdiplus::Pen* pen, float x, float y, float width, float height, float radius) {
    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path, x, y, width, height, radius);
    if (brush) g.FillPath(brush, &path);
    if (pen) g.DrawPath(pen, &path);
}

} // namespace Theme

#endif // UI_THEME_H
