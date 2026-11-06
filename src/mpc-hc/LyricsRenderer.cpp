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

    // Lines (vertical: columns) a line may wrap to before its rendering is cut
    constexpr int PANEL_MAX_WRAPS = 8;

    REFERENCE_TIME LineStart(size_t i)
    {
        return REFERENCE_TIME(i) * LINE_SLOT;
    }

    // RTS reads {...} as override tags, so it would hide bracketed text such as "{Chorus}"
    CStringW EscapeText(CStringW text, bool bTategaki)
    {
        text.Replace(L'{', L'\xFF5B'); // fullwidth curly brackets
        text.Replace(L'}', L'\xFF5D');
        if (bTategaki) {
            // RTS only wraps at spaces, while Japanese lyrics separate phrases with ideographic ones:
            // a space after each lets a column too long for the view break there
            text.Replace(L"\x3000", L"\x3000 ");
        }
        return text;
    }

    // A box turned a quarter clockwise about its top left corner, the origin of the turn for a
    // line aligned top left: its left edge becomes its top one, its top edge its right one
    CRect TurnBox(const CRect& r)
    {
        return CRect(r.left - r.Height(), r.top, r.left, r.top + r.Width());
    }
}

void CLyricsRenderer::ApplyTategakiFont(STSStyle& style)
{
    // a font of its own, or the style's
    AfxGetAppSettings().lyricsTategakiFont.ApplyTo(style);
    CString face = style.fontName;
    face.TrimLeft(_T('@'));
    // GDI gives the "@" face of a font its vertical writing forms: the glyphs on their side,
    // with vertical punctuation, small kana and long vowel marks
    style.fontName = _T("@") + face;
}

bool CLyricsRenderer::Settings::operator==(const Settings& s) const
{
    return layout == s.layout && bVertical == s.bVertical && view == s.view && lineFrom == s.lineFrom && lineTo == s.lineTo
           && scale == s.scale && style == s.style && fontScale == s.fontScale && bUseFreeType == s.bUseFreeType && bRoundedBox == s.bRoundedBox
           && bOverridePlacement == s.bOverridePlacement && nHorPos == s.nHorPos && nVerPos == s.nVerPos
           && langHint == s.langHint;
}

CLyricsRenderer::CLyricsRenderer() = default;

CLyricsRenderer::~CLyricsRenderer() = default;

CLyricsRenderer::Settings CLyricsRenderer::CurrentSettings(Layout layout, bool bVertical, CSize view, double scale, int lineFrom, int lineTo)
{
    const CAppSettings& s = AfxGetAppSettings();

    Settings st;
    st.layout = layout;
    st.bVertical = bVertical;
    st.view = view;
    st.scale = scale > 0.0 ? scale : 1.0;
    st.style = s.subtitlesDefStyle;
    st.fontScale = s.m_RenderersSettings.fontScaleOverride;
    st.bUseFreeType = s.bUseFreeType;
    st.bRoundedBox = s.bSubtitleRoundedOpaqueBox;
    st.langHint = s.strOpenTypeLangHint;
    if (bVertical) {
        ApplyTategakiFont(st.style);
        // the vertical forms are GDI's, FreeType would draw the glyphs as in a horizontal line
        st.bUseFreeType = false;
    }
    // only what the layout uses, so other changes keep the images
    switch (layout) {
        case Layout::Panel:
            if (bVertical) {
                // placed by the caller, only the length of the columns matters
                st.view = CSize(0, 0);
                st.lineFrom = 0;
                st.lineTo = std::max(0, lineTo - lineFrom);
            } else {
                // sized independently of the height, so it does not matter
                st.view.cy = 0;
                st.lineFrom = lineFrom;
                st.lineTo = lineTo;
            }
            break;
        case Layout::Line:
            st.bOverridePlacement = s.fOverridePlacement;
            st.nHorPos = s.nHorPos;
            st.nVerPos = s.nVerPos;
            break;
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
    m_blankSize = CSize(0, 0);
}

void CLyricsRenderer::Prepare(const Lyrics& lyrics, Layout layout, bool bVertical, CSize view, double scale,
                              int lineFrom /*= 0*/, int lineTo /*= 0*/)
{
    Settings st = CurrentSettings(layout, bVertical, view, scale, lineFrom, lineTo);
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
    const bool bVertical = st.bVertical;
    const CSize view = st.view;
    const bool bNoRoom = bPanel ? (bVertical ? st.lineTo <= st.lineFrom : view.cx <= 0)
                                : view.cx <= 0 || view.cy <= 0;
    if (bNoRoom || lyrics.lines.empty()) {
        return;
    }

    // The script resolution is the render size, so script units are pixels. The style is scaled
    // by the DPI only, like UI text, not with the view as subtitles are with the video: a smaller
    // view shows fewer lines rather than smaller ones. Everything scales, keeping the style's
    // proportions; the margins are in pixels at 96 DPI.
    STSStyle style = st.style;
    {
        const double scale = st.scale;
        style.fontSize *= scale;
        style.fontSpacing *= scale;
        style.fGaussianBlur *= scale;
        style.outlineWidthX *= scale;
        style.outlineWidthY *= scale;
        style.shadowDepthX *= scale;
        style.shadowDepthY *= scale;
        style.marginRect.left = std::lround(style.marginRect.left * scale);
        style.marginRect.right = std::lround(style.marginRect.right * scale);
        style.marginRect.top = std::lround(style.marginRect.top * scale);
        style.marginRect.bottom = std::lround(style.marginRect.bottom * scale);
    }
    if (bPanel) {
        // horizontal: top center, at the box top given by the margins set below
        style.scrAlignment = 8;
        style.marginRect.SetRect(0, 0, 0, 0);
    } else if (bVertical) {
        // Placed by the style's alignment within its margins, like a subtitle, but by this class:
        // RTS places the line before turning it
        m_lineAlignment = (style.scrAlignment >= 1 && style.scrAlignment <= 9) ? style.scrAlignment : 2;
        m_lineArea.SetRect(style.marginRect.left, style.marginRect.top,
                           view.cx - style.marginRect.right, view.cy - style.marginRect.bottom);
        if (m_lineArea.Width() <= 0 || m_lineArea.Height() <= 0) {
            m_lineArea.SetRect(0, 0, view.cx, view.cy);
        }
    }

    if (bVertical) {
        // Top left, the origin of the turn below: the lines a long one wraps to are aligned left,
        // so turned, its columns start at the same top, as in vertical writing
        style.scrAlignment = 7;
        // GDI lays the glyphs of an "@" font on their side along a horizontal line. Turned a quarter
        // clockwise, the line runs top to bottom with the glyphs upright, and the lines it wraps to
        // become columns from right to left.
        style.fontAngleZ = std::fmod(style.fontAngleZ + 270.0, 360.0);
        // the scales apply along and across the line before the turn: vertically and horizontally after it
        std::swap(style.fontScaleX, style.fontScaleY);
        // the shadow offset turns with the text, so it is turned back to stay as set on screen;
        // the borders are widened after the turn
        const double shadowX = style.shadowDepthX;
        style.shadowDepthX = style.shadowDepthY;
        style.shadowDepthY = -shadowX;
    }

    // Half the font size between the lines, plus what their borders and shadow take: the borders
    // of both lines, and the shadow of one of them reaching towards the other. Across the lines:
    // vertically for rows; horizontally for vertical columns, whose font size across is the
    // turned style's Y one, and whose horizontal shadow on screen is the turned style's Y one.
    const double fontPx = style.fontSize * st.fontScale * style.fontScaleY / 100.0;
    const double outline = std::max(bVertical ? style.outlineWidthX : style.outlineWidthY, 0.0);
    const double shadow = std::abs(style.shadowDepthY);
    m_lineGap = std::max(1, int(std::lround(fontPx / 2 + 2 * outline + shadow)));

    // Room for borders, shadow and blur around a box, any way
    const double reach = std::max({ style.outlineWidthX, style.outlineWidthY, 0.0 })
                         + std::max(std::abs(style.shadowDepthX), std::abs(style.shadowDepthY))
                         + 3 * std::max(style.fGaussianBlur, 0.0);
    const double emPx = style.fontSize * st.fontScale * std::max(style.fontScaleX, style.fontScaleY) / 100.0;
    const int pad = int(std::ceil(reach + emPx)) + 8;
    // across the lines, for one wrapped many times
    const int across = int(std::ceil(fontPx * 1.5 * PANEL_MAX_WRAPS));

    if (bVertical) {
        // Wrapped to the length of the columns, then turned about the top left of the box: the
        // columns go left of where the line started, down from its top. Rendered with room for
        // them on the left, the line along the top. The columns of a line wrapped more than
        // that are cut.
        const int length = bPanel ? st.lineTo - st.lineFrom : m_lineArea.Height();
        m_scratchSize = CSize(2 * pad + across + length, 2 * pad + std::max(length, across));
        style.marginRect.SetRect(pad + across, pad, pad, pad);
    } else if (bPanel) {
        // Rendered with room around the box, the lines wrapped within the column
        m_scratchSize = CSize(view.cx, 2 * pad + across);
        style.marginRect.SetRect(std::max(1, st.lineFrom), pad, std::max(1, int(view.cx - st.lineTo)), 0);
    } else {
        m_scratchSize = view;
    }
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
    if (!bPanel && !bVertical) {
        rts.SetAlignment(st.bOverridePlacement, st.nHorPos, st.nVerPos);
    }

    // Entry margins of 0 fall back to the style's
    const CRect margins(0, 0, 0, 0);

    m_entries.reserve(m_nLines);
    for (size_t i = 0; i < m_nLines; i++) {
        const size_t count = rts.GetCount();
        rts.Add(EscapeText(lyrics.lines[i].text, bVertical), true, LineStart(i), LineStart(i) + LINE_SLOT,
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
    CRect blank = rts.GetLayoutRect(blankEntry);
    if (bVertical) {
        blank = TurnBox(blank);
    }
    m_blankSize = blank.Size();
}

CLyricsRenderer::Slot& CLyricsRenderer::Measure(int i)
{
    Slot& slot = m_slots[i];
    if (!slot.bMeasured) {
        slot.bMeasured = true;
        const int entry = m_entries[i];
        slot.rcBox = entry >= 0 ? m_pRTS->GetLayoutRect(entry) : CRect(0, 0, 0, 0);
        if (m_settings.bVertical && !slot.rcBox.IsRectEmpty()) {
            slot.rcBox = TurnBox(slot.rcBox);
        }
    }
    return slot;
}

CPoint CLyricsRenderer::PlaceVerticalLine(const CRect& rcBox) const
{
    const Settings& st = m_settings;
    const int w = rcBox.Width(), h = rcBox.Height();
    if (st.bOverridePlacement) {
        // as for subtitles: centred horizontally at the position, its bottom at the vertical one;
        // kept in the view
        const int x = MulDiv(st.view.cx, st.nHorPos, 100) - w / 2;
        const int y = MulDiv(st.view.cy, st.nVerPos, 100) - h;
        return CPoint(std::max(0, std::min(x, int(st.view.cx - w))), std::max(0, std::min(y, int(st.view.cy - h))));
    }
    // the alignment as on the numpad
    const CRect& a = m_lineArea;
    const int align = m_lineAlignment;
    const int x = (align % 3 == 1) ? a.left : (align % 3 == 0) ? a.right - w : a.left + (a.Width() - w) / 2;
    const int y = (align <= 3) ? a.bottom - h : (align <= 6) ? a.top + (a.Height() - h) / 2 : a.top;
    return CPoint(x, y);
}

CSize CLyricsRenderer::GetLineSize(int i)
{
    if (!m_pRTS || i < 0 || i >= (int)m_slots.size()) {
        return CSize(0, 0);
    }
    const Slot& slot = Measure(i);
    return slot.rcBox.IsRectEmpty() ? m_blankSize : slot.rcBox.Size();
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
        if (m_settings.layout == Layout::Line && m_settings.bVertical && !slot.img.bits.empty()) {
            // from the square it was rendered in to the view
            const CSize offset = PlaceVerticalLine(slot.rcBox) - slot.rcBox.TopLeft();
            slot.img.rc.OffsetRect(offset);
            slot.img.rcBox.OffsetRect(offset);
        }
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
