/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "LyricsRenderer.h"
#include "Lyrics.h"
#include "mplayerc.h"
#include "../Subtitles/RTS.h"

namespace
{
    // Each line gets its own slot on a made-up timeline, so rendering at the middle of a slot
    // shows exactly that line. Untimed text and instrumental gaps need no special handling.
    constexpr REFERENCE_TIME LINE_SLOT = 10000000; // 1 s
    constexpr double RENDER_FPS = 25.0;            // only used by frame based subtitles

    constexpr DWORD RTS_TRANSPARENT = 0xFF000000;

    // The default style is defined for a 384x288 script, scaled to the video as for SRT subtitles
    constexpr double STYLE_RES_X = 384.0, STYLE_RES_Y = 288.0;

    // Lines a panel line may wrap to before its rendering is cut
    constexpr int PANEL_MAX_WRAPS = 8;

    REFERENCE_TIME LineStart(size_t i)
    {
        return REFERENCE_TIME(i) * LINE_SLOT;
    }

    // RTS reads {...} as override tags, so it would hide bracketed text such as "{Chorus}"
    CStringW EscapeText(CStringW text)
    {
        text.Replace(L'{', L'\xFF5B'); // fullwidth curly brackets
        text.Replace(L'}', L'\xFF5D');
        return text;
    }
}

bool CLyricsRenderer::Settings::operator==(const Settings& s) const
{
    return layout == s.layout && view == s.view && columnLeft == s.columnLeft && columnRight == s.columnRight
           && panelScale == s.panelScale && style == s.style && fontScale == s.fontScale && bUseFreeType == s.bUseFreeType && bRoundedBox == s.bRoundedBox
           && bOverridePlacement == s.bOverridePlacement && nHorPos == s.nHorPos && nVerPos == s.nVerPos
           && langHint == s.langHint;
}

CLyricsRenderer::CLyricsRenderer() = default;

CLyricsRenderer::~CLyricsRenderer() = default;

CLyricsRenderer::Settings CLyricsRenderer::CurrentSettings(Layout layout, CSize view, int columnLeft, int columnRight, double panelScale)
{
    const CAppSettings& s = AfxGetAppSettings();

    Settings st;
    st.layout = layout;
    st.view = view;
    if (layout == Layout::Panel) {
        // sized independently of the height, so it does not matter
        st.view.cy = 0;
        st.panelScale = panelScale > 0.0 ? panelScale : 1.0;
    }
    st.style = s.subtitlesDefStyle;
    st.fontScale = s.m_RenderersSettings.fontScaleOverride;
    st.bUseFreeType = s.bUseFreeType;
    st.bRoundedBox = s.bSubtitleRoundedOpaqueBox;
    st.langHint = s.strOpenTypeLangHint;
    if (layout == Layout::Panel) {
        // only what the layout uses, so other changes keep the images
        st.columnLeft = columnLeft;
        st.columnRight = columnRight;
    } else {
        st.bOverridePlacement = s.fOverridePlacement;
        st.nHorPos = s.nHorPos;
        st.nVerPos = s.nVerPos;
    }
    return st;
}

void CLyricsRenderer::Reset()
{
    m_bBuilt = false;
    m_pRTS.reset();
    m_entries.clear();
    m_slots.clear();
    m_scratch.clear();
    m_scratch.shrink_to_fit();
    m_scratchSize = CSize(0, 0);
    m_nLines = 0;
    m_blankHeight = 0;
}

void CLyricsRenderer::Prepare(const Lyrics& lyrics, Layout layout, CSize view,
                              int columnLeft /*= 0*/, int columnRight /*= 0*/, double panelScale /*= 1.0*/)
{
    Settings st = CurrentSettings(layout, view, columnLeft, columnRight, panelScale);
    if (m_bBuilt && st == m_settings && m_nLines == lyrics.lines.size()) {
        return;
    }
    m_settings = std::move(st);
    Build(lyrics);
}

void CLyricsRenderer::Build(const Lyrics& lyrics)
{
    Reset();
    m_bBuilt = true;
    m_nLines = lyrics.lines.size();

    const Settings& st = m_settings;
    const bool bPanel = st.layout == Layout::Panel;
    const CSize view = st.view;
    if (view.cx <= 0 || (!bPanel && view.cy <= 0) || lyrics.lines.empty()) {
        return;
    }

    // The script resolution is the render size, so script units are pixels and the style is
    // scaled here, as RTS scales it for SRT subtitles:
    // - Line: to the view standing in for the video; borders and shadow stay in pixels
    // - Panel: by the DPI only, like UI text, so a short view shows fewer lines rather than
    //   smaller ones; everything scales, keeping the style's proportions
    STSStyle style = st.style;
    if (bPanel) {
        const double scale = st.panelScale;
        style.fontSize *= scale;
        style.fontSpacing *= scale;
        style.fGaussianBlur *= scale;
        style.outlineWidthX *= scale;
        style.outlineWidthY *= scale;
        style.shadowDepthX *= scale;
        style.shadowDepthY *= scale;
        style.scrAlignment = 8; // top center, at the box top given by the margins
        style.marginRect.SetRect(0, 0, 0, 0);
    } else {
        const double scaleX = view.cx / STYLE_RES_X, scaleY = view.cy / STYLE_RES_Y;
        style.fontSize *= scaleY;
        style.fontSpacing *= scaleX;
        style.marginRect.left = std::lround(style.marginRect.left * scaleX);
        style.marginRect.right = std::lround(style.marginRect.right * scaleX);
        style.marginRect.top = std::lround(style.marginRect.top * scaleY);
        style.marginRect.bottom = std::lround(style.marginRect.bottom * scaleY);
        style.fGaussianBlur *= scaleY;
    }

    // Half the font size between the lines, plus what their borders and shadow take:
    // the borders of both lines, and the shadow of the upper one going down or of the lower one going up
    const double fontPx = style.fontSize * st.fontScale * style.fontScaleY / 100.0;
    const double outline = std::max(style.outlineWidthY, 0.0);
    const double shadow = std::abs(style.shadowDepthY);
    const double reach = outline + shadow + 3 * std::max(style.fGaussianBlur, 0.0);
    m_lineGap = std::max(1, int(std::lround(fontPx / 2 + 2 * outline + shadow)));

    // Panel lines are rendered with room around their box for borders, shadow and blur,
    // and room for a line wrapped many times
    const int panelTop = bPanel ? int(std::ceil(reach + fontPx)) + 8 : 0;
    const int height = bPanel ? 2 * panelTop + int(std::ceil(fontPx * 1.5 * PANEL_MAX_WRAPS)) : view.cy;
    m_scratchSize = CSize(view.cx, height);
    m_scratch.assign(size_t(m_scratchSize.cx) * m_scratchSize.cy, RTS_TRANSPARENT);

    m_pRTS = std::make_unique<CRenderedTextSubtitle>(&m_csRTS);
    CRenderedTextSubtitle& rts = *m_pRTS;
    rts.m_storageRes = rts.m_playRes = m_scratchSize;
    rts.m_SubRendererSettings.overrideDefaultStyle = false;
    rts.m_SubRendererSettings.overrideAllStyles = false;
    rts.SetDefaultStyle(style);
    rts.SetUseFreeType(st.bUseFreeType);
    rts.SetRoundedOpaqueBox(st.bRoundedBox);
    rts.SetOpenTypeLangHint(st.langHint);
    if (!bPanel) {
        rts.SetAlignment(st.bOverridePlacement, st.nHorPos, st.nVerPos);
    }

    // Entry margins of 0 fall back to the style's
    CRect margins(0, 0, 0, 0);
    if (bPanel) {
        margins.SetRect(std::max(1, st.columnLeft), panelTop, std::max(1, int(view.cx - st.columnRight)), 0);
    }

    m_entries.reserve(m_nLines);
    for (size_t i = 0; i < m_nLines; i++) {
        const size_t count = rts.GetCount();
        rts.Add(EscapeText(lyrics.lines[i].text), true, LineStart(i), LineStart(i) + LINE_SLOT,
                _T("Default"), _T(""), _T(""), margins);
        // Add drops empty text
        m_entries.push_back(rts.GetCount() > count ? int(count) : -1);
    }
    m_slots.resize(m_nLines);

    // After the lines, never rendered: the box of one line, for those without text.
    // The box comes from the font metrics, so the letter does not matter.
    const int blankEntry = int(rts.GetCount());
    rts.Add(L"x", true, LineStart(m_nLines), LineStart(m_nLines) + LINE_SLOT, _T("Default"), _T(""), _T(""), margins);

    rts.Init(m_scratchSize, CRect(CPoint(0, 0), m_scratchSize));
    m_blankHeight = rts.GetLayoutRect(blankEntry).Height();
}

CLyricsRenderer::Slot& CLyricsRenderer::Measure(int i)
{
    Slot& slot = m_slots[i];
    if (!slot.bMeasured) {
        slot.bMeasured = true;
        const int entry = m_entries[i];
        slot.rcBox = entry >= 0 ? m_pRTS->GetLayoutRect(entry) : CRect(0, 0, 0, 0);
    }
    return slot;
}

int CLyricsRenderer::GetLineHeight(int i)
{
    if (!m_pRTS || i < 0 || i >= (int)m_slots.size()) {
        return 0;
    }
    const Slot& slot = Measure(i);
    return slot.rcBox.IsRectEmpty() ? m_blankHeight : slot.rcBox.Height();
}

const CLyricsRenderer::Image* CLyricsRenderer::GetLine(int i)
{
    if (!m_pRTS || i < 0 || i >= (int)m_slots.size()) {
        return nullptr;
    }
    Slot& slot = Measure(i);
    if (!slot.bRendered) {
        slot.bRendered = true;
        RenderLine(i, slot.img);
        slot.img.rcBox = slot.rcBox;
    }
    return slot.img.bits.empty() ? nullptr : &slot.img;
}

void CLyricsRenderer::Retain(int first, int last)
{
    // the measurements are kept, they are small
    for (int i = 0; i < (int)m_slots.size(); i++) {
        Slot& slot = m_slots[i];
        if ((i < first || i > last) && slot.bRendered) {
            slot.bRendered = false;
            slot.img = Image();
        }
    }
}

void CLyricsRenderer::RenderLine(int i, Image& img)
{
    img = Image();
    const int entry = m_entries[i];
    if (entry < 0) {
        return;
    }
    CRenderedTextSubtitle& rts = *m_pRTS;
    const int w = m_scratchSize.cx, h = m_scratchSize.cy;

    SubPicDesc spd;
    spd.w = w;
    spd.h = h;
    spd.bpp = 32;
    spd.pitch = w * 4;
    spd.bits = reinterpret_cast<BYTE*>(m_scratch.data());
    spd.vidrect = CRect(0, 0, w, h);

    RECT bbox = { 0, 0, 0, 0 };
    rts.Render(spd, LineStart(i) + LINE_SLOT / 2, RENDER_FPS, bbox);
    CRect rc(bbox);
    rc &= CRect(0, 0, w, h);
    if (rc.IsRectEmpty()) {
        return;
    }

    // RTS leaves the color premultiplied by coverage, with the alpha byte as transparency
    img.rc = rc;
    img.bits.resize(size_t(rc.Width()) * rc.Height());
    DWORD* dst = img.bits.data();
    for (int y = rc.top; y < rc.bottom; y++) {
        DWORD* src = m_scratch.data() + size_t(y) * w + rc.left;
        for (int x = 0, n = rc.Width(); x < n; x++) {
            const DWORD p = src[x];
            const DWORD a = 255 - (p >> 24);
            if (!a) {
                *dst++ = 0;
                continue;
            }
            // rounding in RTS can leave a channel a step above the alpha
            const DWORD r = std::min<DWORD>((p >> 16) & 0xFF, a), g = std::min<DWORD>((p >> 8) & 0xFF, a), b = std::min<DWORD>(p & 0xFF, a);
            *dst++ = (a << 24) | (r << 16) | (g << 8) | b;
        }
        // transparent again for the next line
        std::fill_n(src, rc.Width(), RTS_TRANSPARENT);
    }
}
