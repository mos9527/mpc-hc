/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2015 see Authors.txt
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
#include "mplayerc.h"
#include "MouseWndWithArtView.h"
#include "MainFrm.h"
#include "CMPCTheme.h"
#include "CMPCThemeUtil.h"
#include "ColorProfileUtil.h"
#include "SettingsDefines.h"

CMouseWndWithArtView::CMouseWndWithArtView(CMainFrame* pMainFrame, bool bD3DFS/* = false*/)
    : m_vrect(0, 0, 0, 0)
    , CMouseWnd(pMainFrame, bD3DFS)
    , m_pMainFrame(pMainFrame)
{
    LoadImg();
}

CMouseWndWithArtView::~CMouseWndWithArtView()
{
}

void CMouseWndWithArtView::LoadImg(const CString& imagePath)
{
    CMPCPngImage img;

    if (!imagePath.IsEmpty()) {
        img.LoadFromFile(imagePath);
    }

    LoadImgInternal(img.Detach());
}

void CMouseWndWithArtView::LoadImg(std::vector<BYTE> buffer)
{
    CMPCPngImage img;

    if (!buffer.empty()) {
        img.LoadFromBuffer(buffer.data(), (UINT)buffer.size());
    }

    LoadImgInternal(img.Detach());
}

void CMouseWndWithArtView::LoadImgInternal(HGDIOBJ hImg)
{
    CAppSettings& s = AfxGetAppSettings();
    bool bHaveLogo = false;

    m_img.DeleteObject();
    m_resizedImg.Destroy();
    m_bCustomImgLoaded = !!m_img.Attach(hImg);

    if (!m_bCustomImgLoaded && s.fLogoExternal) {
        bHaveLogo = !!m_img.LoadFromFile(s.strLogoFileName);
    }

    if (!bHaveLogo && !m_bCustomImgLoaded) {
        s.fLogoExternal = false;               // use the built-in logo instead
        s.strLogoFileName.Empty();             // clear logo file name
        int useLogoId = s.nLogoId;
        if (useLogoId == -1) { // if the user has never chosen a logo, we can try loading a theme default logo
            if (AppIsThemeLoaded()) {
                useLogoId = CMPCThemeUtil::defaultLogo();
            } else {
                useLogoId = DEF_LOGO;
            }
        }
        if (!m_img.Load(useLogoId)) { // try the latest selected build-in logo
            s.nLogoId = -1;           // upon failure, use default
            m_img.Load(DEF_LOGO);
        }
    }

    if (m_hWnd) {
        Invalidate();
    }
}

CSize CMouseWndWithArtView::GetLogoSize()
{
    return m_img.GetSize();
}

IMPLEMENT_DYNAMIC(CMouseWndWithArtView, CMouseWnd)

BEGIN_MESSAGE_MAP(CMouseWndWithArtView, CMouseWnd)
    ON_WM_ERASEBKGND()
    ON_WM_TIMER()
END_MESSAGE_MAP()

BOOL CMouseWndWithArtView::OnEraseBkgnd(CDC* pDC)
{
    if (!pDC) {
        ASSERT(FALSE);
        return FALSE;
    }

    CRect client;
    GetClientRect(client);

    if ((m_pMainFrame->GetLoadState() != MLS::CLOSED || (!m_bFirstMedia && m_pMainFrame->m_controls.DelayShowNotLoaded())) &&
            !m_pMainFrame->HasDedicatedFSVideoWindow() && !m_pMainFrame->m_fAudioOnly) {
        // video: only the bars around it; GDI black has alpha 0, see-through when floating
        pDC->ExcludeClipRect(m_vrect);
        pDC->FillSolidRect(client, 0);
        return TRUE;
    }

    if (!m_canvas.Create(client.Width(), client.Height())) {
        pDC->FillSolidRect(client, 0);
        return TRUE;
    }
    const bool bTransparent = IsBackgroundTransparent();
    m_canvas.Clear(bTransparent ? 0 : 0xFF000000);

    const bool bArt = ShowsArt();
    CRect artArea = bArt ? client : CRect();
    m_rcLyrics.SetRectEmpty();
    if (m_lyricsView == LyricsView::Panel) {
        if (bArt) {
            SplitLyricsLayout(client, artArea, m_rcLyrics);
        } else {
            m_rcLyrics = client;
        }
    }

    if (!artArea.IsRectEmpty()) {
        const CRect art = PrepareArt(artArea);
        if (!art.IsRectEmpty()) {
            m_resizedImg.BitBlt(m_canvas.GetDC(), art.left, art.top);
            m_canvas.SetOpaque(art);
        }
    }

    if (m_lyricsView == LyricsView::Panel) {
        DrawLyrics(m_rcLyrics, bTransparent);
    } else if (m_lyricsView == LyricsView::Line) {
        m_rcLyrics = LyricsLineRect(client);
        DrawLyricsLine(m_rcLyrics);
    }

    // one blit, so a lyrics line change does not flicker; a 32 bpp source keeps its alpha
    ::BitBlt(pDC->GetSafeHdc(), 0, 0, client.Width(), client.Height(), m_canvas.GetDC(), 0, 0, SRCCOPY);
    return TRUE;
}

bool CMouseWndWithArtView::IsBackgroundTransparent() const
{
    // only the view inside the main frame, not the separate fullscreen window
    return m_pMainFrame->IsFloatingTransparent() && ::GetAncestor(m_hWnd, GA_PARENT) == m_pMainFrame->GetSafeHwnd();
}

bool CMouseWndWithArtView::ShowsArt() const
{
    return GetShowCoverArt() || m_pMainFrame->GetLoadState() == MLS::CLOSED;
}

CRect CMouseWndWithArtView::PrepareArt(const CRect& area)
{
    CRect r;
    CImage img;
    img.Attach(m_img);
    if (!img.IsNull()) {
        const double dImageAR = double(img.GetWidth()) / img.GetHeight();

        int width = area.Width();
        int height = area.Height();
        if (!m_bCustomImgLoaded) {
            // Limit logo size
            // TODO: Use vector logo to preserve quality and remove limit.
            width = std::min(img.GetWidth(), width);
            height = std::min(img.GetHeight(), height);
        }

        double dImgWidth = height * dImageAR;
        double dImgHeight;
        if (width < dImgWidth) {
            dImgWidth = width;
            dImgHeight = dImgWidth / dImageAR;
        } else {
            dImgHeight = height;
        }

        int x = area.left + std::lround((area.Width() - dImgWidth) / 2.0);
        int y = area.top + std::lround((area.Height() - dImgHeight) / 2.0);

        r = CRect(CPoint(x, y), CSize(std::lround(dImgWidth), std::lround(dImgHeight)));

        if (!r.IsRectEmpty()) {
            if (m_resizedImg.IsNull() || r.Width() != m_resizedImg.GetWidth() || r.Height() != m_resizedImg.GetHeight() || img.GetBPP() != m_resizedImg.GetBPP()) {
                m_resizedImg.Destroy();
                m_resizedImg.Create(r.Width(), r.Height(), std::max(img.GetBPP(), 24));

                HDC hDC = m_resizedImg.GetDC();
                SetStretchBltMode(hDC, STRETCH_HALFTONE);
                img.StretchBlt(hDC, 0, 0, r.Width(), r.Height(), SRCCOPY);
                m_resizedImg.ReleaseDC();
                if (AfxGetAppSettings().fLogoColorProfileEnabled) {
                    ColorProfileUtil::applyColorProfile(m_hWnd, m_resizedImg);
                }
            }
        } else {
            r.SetRectEmpty();
        }
    }
    img.Detach();
    return r;
}

void CMouseWndWithArtView::SetVideoRect(const CRect& r)
{
    m_vrect = r;

    Invalidate();
}

//
// CArgbSurface
//

bool CArgbSurface::Create(int width, int height)
{
    if (width <= 0 || height <= 0) {
        Destroy();
        return false;
    }
    if (m_hDC && width == m_width && height == m_height) {
        return true;
    }
    Destroy();

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    m_hDC = ::CreateCompatibleDC(nullptr);
    if (m_hDC) {
        m_hBmp = ::CreateDIBSection(m_hDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    }
    if (!m_hBmp || !pBits) {
        Destroy();
        return false;
    }
    m_hOldBmp = ::SelectObject(m_hDC, m_hBmp);
    m_pBits = static_cast<DWORD*>(pBits);
    m_width = width;
    m_height = height;
    return true;
}

void CArgbSurface::Destroy()
{
    if (m_hDC) {
        if (m_hOldBmp) {
            ::SelectObject(m_hDC, m_hOldBmp);
        }
        ::DeleteDC(m_hDC);
    }
    if (m_hBmp) {
        ::DeleteObject(m_hBmp);
    }
    m_hDC = nullptr;
    m_hBmp = nullptr;
    m_hOldBmp = nullptr;
    m_pBits = nullptr;
    m_width = m_height = 0;
}

void CArgbSurface::Clear(DWORD argb /*= 0*/)
{
    if (m_pBits) {
        ::GdiFlush();
        std::fill_n(m_pBits, size_t(m_width) * m_height, argb);
    }
}

void CArgbSurface::SetOpaque(const CRect& rc)
{
    CRect r;
    if (!m_pBits || !r.IntersectRect(rc, CRect(0, 0, m_width, m_height))) {
        return;
    }
    ::GdiFlush();
    for (int y = r.top; y < r.bottom; y++) {
        DWORD* p = m_pBits + size_t(y) * m_width;
        for (int x = r.left; x < r.right; x++) {
            p[x] |= 0xFF000000;
        }
    }
}

void CArgbSurface::Composite(const CArgbSurface& mask, CPoint pt, COLORREF color)
{
    if (!m_pBits || !mask.m_pBits) {
        return;
    }
    ::GdiFlush();

    const UINT cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
    const int px = pt.x, py = pt.y; // LONG
    const int x0 = std::max(0, -px), y0 = std::max(0, -py);
    const int x1 = std::min(mask.m_width, m_width - px), y1 = std::min(mask.m_height, m_height - py);
    auto over = [](UINT src, UINT coverage, UINT dst) {
        return (src * coverage + dst * (255 - coverage) + 127) / 255;
    };

    for (int y = y0; y < y1; y++) {
        const DWORD* src = mask.m_pBits + size_t(y) * mask.m_width;
        DWORD* dst = m_pBits + size_t(y + py) * m_width + px;
        for (int x = x0; x < x1; x++) {
            const DWORD m = src[x];
            if (!(m & 0x00FFFFFF)) {
                continue;
            }
            // per channel, so ClearType masks keep their subpixel coverage on an opaque canvas
            const UINT mb = m & 0xFF, mg = (m >> 8) & 0xFF, mr = (m >> 16) & 0xFF;
            const UINT ma = std::max(mr, std::max(mg, mb));
            const DWORD d = dst[x];
            const UINT b = over(cb, mb, d & 0xFF);
            const UINT g = over(cg, mg, (d >> 8) & 0xFF);
            const UINT r = over(cr, mr, (d >> 16) & 0xFF);
            const UINT a = ma + ((d >> 24) * (255 - ma) + 127) / 255;
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

//
// Lyrics
//

namespace
{
    constexpr UINT_PTR TIMER_LYRICS = 0x4C59; // CMouse uses callback timers with pointer IDs
    constexpr UINT LYRICS_POLL_MS = 50;       // the seek bar position itself is polled every 100 ms by default
    constexpr LPCTSTR LYRICS_MODE_KEY = _T("LyricsDisplay");
    constexpr LPCTSTR COVER_ART_KEY = _T("AudioViewCoverArt");
    constexpr LPCTSTR LYRICS_OUTLINE_KEY = _T("LyricsOutline");

    // not loaded yet while negative
    int s_lyricsMode = -1;
    int s_showCoverArt = -1;
    int s_lyricsOutline = -1;

    int LoadViewSetting(int& cache, LPCTSTR key, int def)
    {
        if (cache < 0) {
            cache = (int)AfxGetApp()->GetProfileInt(IDS_R_SETTINGS, key, def);
        }
        return cache;
    }

    void StoreViewSetting(int& cache, LPCTSTR key, int value)
    {
        cache = value;
        AfxGetApp()->WriteProfileInt(IDS_R_SETTINGS, key, value);
    }

    // selects a GDI object into a DC for the scope, so fonts are never left selected when deleted
    class CSelectScope
    {
        HDC m_hDC;
        HGDIOBJ m_hOld;
    public:
        CSelectScope(HDC hDC, HGDIOBJ hObj) : m_hDC(hDC), m_hOld(::SelectObject(hDC, hObj)) {}
        ~CSelectScope() { ::SelectObject(m_hDC, m_hOld); }
        CSelectScope(const CSelectScope&) = delete;
        CSelectScope& operator=(const CSelectScope&) = delete;
    };

    int MeasureText(HDC hDC, CFont* pFont, LPCWSTR text, int left, int right, UINT fmt)
    {
        CSelectScope font(hDC, pFont->GetSafeHandle());
        CRect r(left, 0, right, 0);
        ::DrawTextW(hDC, (text && *text) ? text : L" ", -1, r, fmt | DT_CALCRECT);
        return r.Height();
    }
}

CMouseWndWithArtView::LyricsMode CMouseWndWithArtView::GetLyricsMode()
{
    int mode = LoadViewSetting(s_lyricsMode, LYRICS_MODE_KEY, (int)LyricsMode::Panel);
    if (mode > (int)LyricsMode::Off) {
        mode = s_lyricsMode = (int)LyricsMode::Panel;
    }
    return (LyricsMode)mode;
}

void CMouseWndWithArtView::SetLyricsMode(LyricsMode mode)
{
    StoreViewSetting(s_lyricsMode, LYRICS_MODE_KEY, (int)mode);
}

bool CMouseWndWithArtView::GetShowCoverArt()
{
    return !!LoadViewSetting(s_showCoverArt, COVER_ART_KEY, TRUE);
}

void CMouseWndWithArtView::SetShowCoverArt(bool bShow)
{
    StoreViewSetting(s_showCoverArt, COVER_ART_KEY, bShow ? TRUE : FALSE);
}

bool CMouseWndWithArtView::GetLyricsOutline()
{
    return !!LoadViewSetting(s_lyricsOutline, LYRICS_OUTLINE_KEY, TRUE);
}

void CMouseWndWithArtView::SetLyricsOutline(bool bOutline)
{
    StoreViewSetting(s_lyricsOutline, LYRICS_OUTLINE_KEY, bOutline ? TRUE : FALSE);
}

bool CMouseWndWithArtView::HasLyrics() const
{
    return m_pMainFrame->GetLoadState() == MLS::LOADED && m_pMainFrame->m_fAudioOnly
           && !m_pMainFrame->m_mediaMeta.lyrics.IsEmpty();
}

void CMouseWndWithArtView::UpdateLyrics(bool bKeepLayout /*= false*/)
{
    LyricsView view = LyricsView::None;
    bool bTimed = false;
    if (HasLyrics()) {
        bTimed = m_pMainFrame->m_mediaMeta.lyrics.timed;
        switch (GetLyricsMode()) {
            case LyricsMode::Panel:
                view = LyricsView::Panel;
                break;
            case LyricsMode::Line:
                // untimed text has no current line to show
                view = bTimed ? LyricsView::Line : LyricsView::Panel;
                break;
            case LyricsMode::Off:
                break;
        }
    }
    if (view != LyricsView::None || !bKeepLayout) {
        m_lyricsView = view;
    }

    if (!m_hWnd) {
        return;
    }
    if (view != LyricsView::None && bTimed) {
        SetTimer(TIMER_LYRICS, LYRICS_POLL_MS, nullptr);
    } else {
        KillTimer(TIMER_LYRICS);
    }
    m_nLyricsLine = -1;
    Invalidate();
}

void CMouseWndWithArtView::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_LYRICS) {
        if (m_lyricsView == LyricsView::None || !HasLyrics()) {
            return;
        }
        int line = m_pMainFrame->m_mediaMeta.lyrics.LineAt(m_pMainFrame->GetPos());
        if (line != m_nLyricsLine && !m_rcLyrics.IsRectEmpty()) {
            // only the lyrics area, OnEraseBkgnd composes it in one blit
            InvalidateRect(m_rcLyrics);
        }
        return;
    }
    __super::OnTimer(nIDEvent);
}

void CMouseWndWithArtView::SplitLyricsLayout(const CRect& client, CRect& art, CRect& lyrics) const
{
    art = lyrics = client;
    if (client.Width() >= client.Height()) {
        // side by side
        art.right = client.left + client.Width() * 2 / 5;
        lyrics.left = art.right;
    } else {
        // tall and narrow: stacked
        art.bottom = client.top + client.Height() * 2 / 5;
        lyrics.top = art.bottom;
    }
    const int margin = m_pMainFrame->m_dpi.ScaleX(16);
    art.DeflateRect(margin, margin);
    if (art.IsRectEmpty()) {
        art.SetRectEmpty();
    }
}

void CMouseWndWithArtView::EnsureLyricsFonts(int px, BYTE quality)
{
    if (px == m_nLyricsFontPx && quality == m_nLyricsFontQuality && m_lyricsFont.GetSafeHandle()) {
        return;
    }
    m_lyricsFont.DeleteObject();
    m_lyricsFontBold.DeleteObject();

    LOGFONT lf = {};
    m_pMainFrame->m_dpi.GetMessageFont(&lf);
    lf.lfHeight = -px;
    lf.lfWidth = 0;
    lf.lfWeight = FW_NORMAL;
    lf.lfQuality = quality;
    m_lyricsFont.CreateFontIndirect(&lf);
    lf.lfWeight = FW_BOLD;
    m_lyricsFontBold.CreateFontIndirect(&lf);
    m_nLyricsFontPx = px;
    m_nLyricsFontQuality = quality;
}

void CMouseWndWithArtView::EnsureLyricsLineFont(int px)
{
    if (px == m_nLyricsLineFontPx && m_lyricsLineFont.GetSafeHandle()) {
        return;
    }
    m_lyricsLineFont.DeleteObject();

    LOGFONT lf = {};
    m_pMainFrame->m_dpi.GetMessageFont(&lf);
    lf.lfHeight = -px;
    lf.lfWidth = 0;
    lf.lfWeight = FW_SEMIBOLD;
    // drawn over the cover: grayscale antialiasing, ClearType fringes show on images
    lf.lfQuality = ANTIALIASED_QUALITY;
    m_lyricsLineFont.CreateFontIndirect(&lf);
    m_nLyricsLineFontPx = px;
}

bool CMouseWndWithArtView::BeginTextMasks(const CSize& size, bool bOutline)
{
    if (!m_textMask.Create(size.cx, size.cy)) {
        return false;
    }
    m_textMask.Clear();
    if (bOutline) {
        if (!m_outlineMask.Create(size.cx, size.cy)) {
            return false;
        }
        m_outlineMask.Clear();
    }
    return true;
}

void CMouseWndWithArtView::DrawTextToMasks(const CStringW& text, const CRect& r, UINT fmt, CFont* pFont, double opacity, int outline)
{
    if (text.IsEmpty()) {
        return;
    }
    // drawn in gray on black: each channel ends up as coverage * opacity
    const BYTE v = BYTE(std::lround(255.0 * std::max(0.0, std::min(1.0, opacity))));

    auto draw = [&](const CArgbSurface& mask, const CRect& rc) {
        const HDC hDC = mask.GetDC();
        CSelectScope font(hDC, pFont->GetSafeHandle());
        ::SetBkMode(hDC, TRANSPARENT);
        ::SetTextColor(hDC, RGB(v, v, v));
        CRect rr = rc;
        ::DrawTextW(hDC, text, -1, rr, fmt);
    };

    draw(m_textMask, r);

    if (outline > 0) {
        // the text stamped around a circle; overlapping stamps never exceed v
        const int steps = outline <= 2 ? 8 : 16;
        const double kPi = 3.14159265358979323846;
        for (int k = 0; k < steps; k++) {
            const double a = k * 2 * kPi / steps;
            CRect o = r;
            o.OffsetRect((int)std::lround(outline * cos(a)), (int)std::lround(outline * sin(a)));
            draw(m_outlineMask, o);
        }
    }
}

void CMouseWndWithArtView::CompositeTextMasks(CPoint pt, COLORREF fg, bool bOutline)
{
    if (bOutline) {
        m_canvas.Composite(m_outlineMask, pt, RGB(0, 0, 0));
    }
    m_canvas.Composite(m_textMask, pt, fg);
}

void CMouseWndWithArtView::DrawLyrics(const CRect& rc, bool bTransparent)
{
    if (rc.IsRectEmpty() || !HasLyrics()) {
        return;
    }

    const Lyrics& lyrics = m_pMainFrame->m_mediaMeta.lyrics;
    const DpiHelper& dpi = m_pMainFrame->m_dpi;
    const int nLines = (int)lyrics.lines.size();
    const int cur = lyrics.LineAt(m_pMainFrame->GetPos());
    m_nLyricsLine = cur;

    // ClearType needs an opaque background to blend with
    EnsureLyricsFonts(std::max(dpi.ScaleY(14), std::min(dpi.ScaleY(28), rc.Height() / 16)),
                      bTransparent ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY);
    const bool bOutline = GetLyricsOutline();
    const int outline = bOutline ? std::max(1, m_nLyricsFontPx / 14) : 0;
    if (!BeginTextMasks(rc.Size(), bOutline)) {
        return;
    }

    const COLORREF fg = AppIsThemeLoaded() ? CMPCTheme::TextFGColor : RGB(0xFF, 0xFF, 0xFF);
    // neighbours of the current line fade out by distance
    auto dimmed = [](int distance) {
        return std::max(0.25, 0.6 - 0.1 * (distance - 1));
    };

    CRect area(0, 0, rc.Width(), rc.Height());
    area.DeflateRect(dpi.ScaleX(16), dpi.ScaleY(16));
    const int gap = m_nLyricsFontPx / 2;
    const UINT fmt = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL;
    const HDC hMeasureDC = m_textMask.GetDC();

    auto measure = [&](int i, CFont* pFont) {
        return MeasureText(hMeasureDC, pFont, lyrics.lines[i].text, area.left, area.right, fmt);
    };
    auto draw = [&](int i, int top, int height, CFont* pFont, double opacity) {
        DrawTextToMasks(lyrics.lines[i].text, CRect(area.left, top, area.right, top + height), fmt, pFont, opacity, outline);
    };

    if (lyrics.timed) {
        // the current line centred, its neighbours dimmed by distance;
        // only lines that fit entirely, so no wrapped line is cut at an edge
        const int anchor = std::max(cur, 0);
        CFont* pAnchorFont = cur >= 0 ? &m_lyricsFontBold : &m_lyricsFont;
        const int hAnchor = measure(anchor, pAnchorFont);
        const int topAnchor = area.top + (area.Height() - hAnchor) / 2;
        draw(anchor, topAnchor, hAnchor, pAnchorFont, cur >= 0 ? 1.0 : dimmed(1));

        int y = topAnchor;
        for (int i = anchor - 1; i >= 0; i--) {
            int h = measure(i, &m_lyricsFont);
            if (y - gap - h < area.top) {
                break;
            }
            y -= gap + h;
            draw(i, y, h, &m_lyricsFont, dimmed(anchor - i));
        }
        y = topAnchor + hAnchor;
        for (int i = anchor + 1; i < nLines; i++) {
            int h = measure(i, &m_lyricsFont);
            if (y + gap + h > area.bottom) {
                break;
            }
            y += gap;
            draw(i, y, h, &m_lyricsFont, dimmed(i - anchor));
            y += h;
        }
    } else {
        // untimed text: static, centred when it fits
        std::vector<int> heights(nLines);
        int total = 0;
        for (int i = 0; i < nLines; i++) {
            heights[i] = measure(i, &m_lyricsFont);
            total += heights[i] + (i ? gap : 0);
        }
        int y = area.top + std::max(0, (area.Height() - total) / 2);
        for (int i = 0; i < nLines && y + heights[i] <= area.bottom; i++) {
            draw(i, y, heights[i], &m_lyricsFont, dimmed(1));
            y += heights[i] + gap;
        }
    }

    CompositeTextMasks(rc.TopLeft(), fg, bOutline);
}

CRect CMouseWndWithArtView::LyricsLineRect(const CRect& client)
{
    const DpiHelper& dpi = m_pMainFrame->m_dpi;
    EnsureLyricsLineFont(std::max(dpi.ScaleY(18), std::min(dpi.ScaleY(44), client.Height() / 14)));

    // room for two wrapped lines and the outline, near the bottom like a subtitle
    const int px = m_nLyricsLineFontPx;
    const int height = 2 * (px * 4 / 3) + px / 4 + dpi.ScaleY(8);
    const int bottom = client.bottom - std::max(dpi.ScaleY(12), client.Height() / 20);
    CRect rc(client.left + dpi.ScaleX(16), bottom - height, client.right - dpi.ScaleX(16), bottom);
    rc.IntersectRect(rc, client);
    return rc;
}

void CMouseWndWithArtView::DrawLyricsLine(const CRect& rc)
{
    if (rc.IsRectEmpty() || !HasLyrics()) {
        return;
    }

    const Lyrics& lyrics = m_pMainFrame->m_mediaMeta.lyrics;
    const int cur = lyrics.LineAt(m_pMainFrame->GetPos());
    m_nLyricsLine = cur;
    if (cur < 0 || lyrics.lines[cur].text.IsEmpty()) {
        return;
    }

    const CStringW& text = lyrics.lines[cur].text;
    const bool bOutline = GetLyricsOutline();
    const int outline = std::max(1, m_nLyricsLineFontPx / 14);
    const UINT fmt = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL;
    if (!BeginTextMasks(rc.Size(), bOutline)) {
        return;
    }

    CRect area(0, 0, rc.Width(), rc.Height());
    area.DeflateRect(outline, outline);
    const int height = MeasureText(m_textMask.GetDC(), &m_lyricsLineFont, text, area.left, area.right, fmt);
    // bottom-aligned, as subtitles are
    const CRect textRect(area.left, std::max(area.top, area.bottom - height), area.right, area.bottom);

    DrawTextToMasks(text, textRect, fmt, &m_lyricsLineFont, 1.0, bOutline ? outline : 0);
    CompositeTextMasks(rc.TopLeft(), RGB(0xFF, 0xFF, 0xFF), bOutline);
}
