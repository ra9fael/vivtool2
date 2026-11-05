#pragma once

#include <windows.h>

// Central dark palette for all custom-drawn UI. Colors follow the Windows 11
// dark surface ramp; accent green matches the Vivado brand feel.
namespace theme
{
    constexpr COLORREF FromRgb(int r, int g, int b)
    {
        return RGB(r, g, b);
    }

    // Surfaces
    constexpr COLORREF WindowBg   = FromRgb(31, 33, 38);    // #1f2126 dialog background
    constexpr COLORREF PanelBg    = FromRgb(38, 40, 46);    // #26282e list / edit panels
    constexpr COLORREF PanelBorder= FromRgb(52, 56, 63);    // #34383f
    constexpr COLORREF HoverBg    = FromRgb(44, 47, 53);    // #2c2f35
    constexpr COLORREF PressedBg  = FromRgb(28, 30, 34);
    constexpr COLORREF SelectedBg = FromRgb(33, 52, 45);    // subtle accent-tinted selection
    constexpr COLORREF NavActiveBg= FromRgb(40, 60, 50);    // sidebar active pill

    // Text
    constexpr COLORREF TextPrimary= FromRgb(232, 234, 237); // #e8eaed
    constexpr COLORREF TextMuted  = FromRgb(138, 144, 152); // #8a9098

    // Accent
    constexpr COLORREF Accent     = FromRgb(76, 195, 138);  // #4cc38a
    constexpr COLORREF AccentDim  = FromRgb(42, 157, 110);  // pressed accent
    constexpr COLORREF AccentText = FromRgb(14, 23, 18);    // text on accent fill

    // Controls
    constexpr COLORREF BtnBg      = FromRgb(46, 49, 55);    // #2e3137
    constexpr COLORREF BtnBorder  = FromRgb(61, 66, 74);    // #3d424a
    constexpr COLORREF BtnHover   = FromRgb(56, 60, 67);
}
