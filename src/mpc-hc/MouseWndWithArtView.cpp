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
        if (m_bLyricsTategaki) {
            DrawLyricsTategaki(m_rcLyrics, client.Size());
        } else {
            DrawLyrics(m_rcLyrics, client.Size());
        }
    } else if (m_lyricsView == LyricsView::Line) {
        // placed by the subtitle style, anywhere in the view
        m_rcLyrics = client;
        DrawLyricsLine(client.Size());
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

void CArgbSurface::Blend(const DWORD* bits, CSize size, CPoint pt, const CRect& clip, BYTE opacity /*= 255*/)
{
    CRect r;
    if (!m_pBits || !bits || !opacity
            || !r.IntersectRect(CRect(pt, size), clip) || !r.IntersectRect(r, CRect(0, 0, m_width, m_height))) {
        return;
    }
    ::GdiFlush();

    for (int y = r.top; y < r.bottom; y++) {
        const DWORD* src = bits + size_t(y - pt.y) * size.cx + (r.left - pt.x);
        DWORD* dst = m_pBits + size_t(y) * m_width + r.left;
        for (int x = 0, n = r.Width(); x < n; x++) {
            DWORD s = src[x];
            if (!s) {
                continue;
            }
            if (opacity != 255) {
                // all channels, the color being premultiplied
                auto scale = [opacity](DWORD c) { return (c * opacity + 127) / 255; };
                s = (scale(s >> 24) << 24) | (scale((s >> 16) & 0xFF) << 16) | (scale((s >> 8) & 0xFF) << 8) | scale(s & 0xFF);
            }
            const DWORD ia = 255 - (s >> 24);
            const DWORD d = dst[x];
            auto over = [ia](DWORD sc, DWORD dc) { return sc + (dc * ia + 127) / 255; };
            dst[x] = (over(s >> 24, d >> 24) << 24) | (over((s >> 16) & 0xFF, (d >> 16) & 0xFF) << 16)
                     | (over((s >> 8) & 0xFF, (d >> 8) & 0xFF) << 8) | over(s & 0xFF, d & 0xFF);
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
    constexpr LPCTSTR LYRICS_TATEGAKI_KEY = _T("LyricsTategaki");
    constexpr LPCTSTR COVER_ART_KEY = _T("AudioViewCoverArt");
    // a former mode value: the tategaki panel, now the panel with tategaki on
    constexpr int OLD_LYRICS_MODE_TATEGAKI = 3;

    // not loaded yet while negative
    int s_lyricsMode = -1;
    int s_lyricsTategaki = -1;
    int s_showCoverArt = -1;

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

    // Panels: the neighbours of the current line fade out by distance
    BYTE DimmedLyricsOpacity(int distance)
    {
        return BYTE(std::lround(255.0 * std::max(0.25, 0.6 - 0.1 * (distance - 1))));
    }
}

CMouseWndWithArtView::LyricsMode CMouseWndWithArtView::GetLyricsMode()
{
    int mode = LoadViewSetting(s_lyricsMode, LYRICS_MODE_KEY, (int)LyricsMode::Panel);
    if (mode == OLD_LYRICS_MODE_TATEGAKI) {
        SetLyricsTategaki(true);
        SetLyricsMode(LyricsMode::Panel);
        mode = (int)LyricsMode::Panel;
    } else if (mode > (int)LyricsMode::Off) {
        mode = s_lyricsMode = (int)LyricsMode::Panel;
    }
    return (LyricsMode)mode;
}

void CMouseWndWithArtView::SetLyricsMode(LyricsMode mode)
{
    StoreViewSetting(s_lyricsMode, LYRICS_MODE_KEY, (int)mode);
}

bool CMouseWndWithArtView::GetLyricsTategaki()
{
    return !!LoadViewSetting(s_lyricsTategaki, LYRICS_TATEGAKI_KEY, FALSE);
}

void CMouseWndWithArtView::SetLyricsTategaki(bool bTategaki)
{
    StoreViewSetting(s_lyricsTategaki, LYRICS_TATEGAKI_KEY, bTategaki ? TRUE : FALSE);
}

bool CMouseWndWithArtView::GetShowCoverArt()
{
    return !!LoadViewSetting(s_showCoverArt, COVER_ART_KEY, TRUE);
}

void CMouseWndWithArtView::SetShowCoverArt(bool bShow)
{
    StoreViewSetting(s_showCoverArt, COVER_ART_KEY, bShow ? TRUE : FALSE);
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
    m_bLyricsTategaki = GetLyricsTategaki();
    // the lyrics may have been replaced
    m_lyricsRenderer.Reset();

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

void CMouseWndWithArtView::DrawLyrics(const CRect& rc, const CSize& client)
{
    if (rc.IsRectEmpty() || !HasLyrics()) {
        return;
    }

    const Lyrics& lyrics = m_pMainFrame->m_mediaMeta.lyrics;
    const DpiHelper& dpi = m_pMainFrame->m_dpi;
    const int nLines = (int)lyrics.lines.size();
    const int cur = lyrics.LineAt(m_pMainFrame->GetPos());
    m_nLyricsLine = cur;

    CRect area = rc;
    area.DeflateRect(dpi.ScaleX(16), dpi.ScaleY(16));
    if (area.IsRectEmpty()) {
        return;
    }

    // a fixed size from the style, scaled by the DPI like UI text, wrapped in the area;
    // a short view shows fewer lines, not smaller ones
    m_lyricsRenderer.Prepare(lyrics, CLyricsRenderer::Layout::Panel, false, client, dpi.ScaleFactorY(), area.left, area.right);
    const int gap = m_lyricsRenderer.GetLineGap();
    const auto dimmed = DimmedLyricsOpacity;

    // placed by its box, top at top; borders and shadow may reach past the area, not past rc
    auto draw = [&](int i, int top, BYTE opacity) {
        if (const CLyricsRenderer::Image* img = m_lyricsRenderer.GetLine(i)) {
            const CPoint pt(img->rc.left, top + img->rc.top - img->rcBox.top);
            m_canvas.Blend(img->bits.data(), img->rc.Size(), pt, rc, opacity);
        }
    };
    auto height = [&](int i) {
        return m_lyricsRenderer.GetLineHeight(i);
    };

    if (lyrics.timed) {
        // the current line centred, its neighbours dimmed by distance;
        // only lines that fit entirely, so no wrapped line is cut at an edge
        const int anchor = std::max(cur, 0);
        const int hAnchor = height(anchor);
        const int topAnchor = area.top + (area.Height() - hAnchor) / 2;
        draw(anchor, topAnchor, cur >= 0 ? 255 : dimmed(1));
        int first = anchor, last = anchor;

        int y = topAnchor;
        for (int i = anchor - 1; i >= 0; i--) {
            const int h = height(i);
            if (y - gap - h < area.top) {
                break;
            }
            y -= gap + h;
            draw(i, y, dimmed(anchor - i));
            first = i;
        }
        y = topAnchor + hAnchor;
        for (int i = anchor + 1; i < nLines; i++) {
            const int h = height(i);
            if (y + gap + h > area.bottom) {
                break;
            }
            y += gap;
            draw(i, y, dimmed(i - anchor));
            y += h;
            last = i;
        }
        m_lyricsRenderer.Retain(first, last);
    } else {
        // untimed text: static, centred when it fits
        std::vector<int> heights(nLines);
        int total = 0;
        for (int i = 0; i < nLines; i++) {
            heights[i] = height(i);
            total += heights[i] + (i ? gap : 0);
        }
        int y = area.top + std::max(0, (area.Height() - total) / 2);
        int last = -1;
        for (int i = 0; i < nLines && y + heights[i] <= area.bottom; i++) {
            draw(i, y, dimmed(1));
            y += heights[i] + gap;
            last = i;
        }
        m_lyricsRenderer.Retain(0, last);
    }
}

void CMouseWndWithArtView::DrawLyricsTategaki(const CRect& rc, const CSize& client)
{
    if (rc.IsRectEmpty() || !HasLyrics()) {
        return;
    }

    const Lyrics& lyrics = m_pMainFrame->m_mediaMeta.lyrics;
    const DpiHelper& dpi = m_pMainFrame->m_dpi;
    const int nLines = (int)lyrics.lines.size();
    const int cur = lyrics.LineAt(m_pMainFrame->GetPos());
    m_nLyricsLine = cur;

    CRect area = rc;
    area.DeflateRect(dpi.ScaleX(16), dpi.ScaleY(16));
    if (area.IsRectEmpty()) {
        return;
    }

    // As the panel turned a quarter: the same size, the lines wrapped to the height of the area
    // into columns, which go across it. A narrow view shows fewer columns, not smaller ones.
    m_lyricsRenderer.Prepare(lyrics, CLyricsRenderer::Layout::Panel, true, client, dpi.ScaleFactorY(), area.top, area.bottom);
    const int gap = m_lyricsRenderer.GetLineGap();
    const auto dimmed = DimmedLyricsOpacity;

    // placed by its box, left at left and centred vertically, as panel lines are horizontally;
    // borders and shadow may reach past the area, not past rc
    auto draw = [&](int i, int left, BYTE opacity) {
        if (const CLyricsRenderer::Image* img = m_lyricsRenderer.GetLine(i)) {
            const int top = area.top + (area.Height() - img->rcBox.Height()) / 2;
            const CPoint pt(left + img->rc.left - img->rcBox.left, top + img->rc.top - img->rcBox.top);
            m_canvas.Blend(img->bits.data(), img->rc.Size(), pt, rc, opacity);
        }
    };
    auto width = [&](int i) {
        return m_lyricsRenderer.GetLineSize(i).cx;
    };

    // Read right to left, so the lines go leftwards
    if (lyrics.timed) {
        // the current line centred, the lines sung before it on its right and those to come on its
        // left, dimmed by distance; only lines that fit entirely, so no column is cut at an edge
        const int anchor = std::max(cur, 0);
        const int wAnchor = width(anchor);
        const int leftAnchor = area.left + (area.Width() - wAnchor) / 2;
        draw(anchor, leftAnchor, cur >= 0 ? 255 : dimmed(1));
        int first = anchor, last = anchor;

        int x = leftAnchor + wAnchor;
        for (int i = anchor - 1; i >= 0; i--) {
            const int w = width(i);
            if (x + gap + w > area.right) {
                break;
            }
            x += gap;
            draw(i, x, dimmed(anchor - i));
            x += w;
            first = i;
        }
        x = leftAnchor;
        for (int i = anchor + 1; i < nLines; i++) {
            const int w = width(i);
            if (x - gap - w < area.left) {
                break;
            }
            x -= gap + w;
            draw(i, x, dimmed(i - anchor));
            last = i;
        }
        m_lyricsRenderer.Retain(first, last);
    } else {
        // untimed text: static from the right, centred when it fits
        std::vector<int> widths(nLines);
        int total = 0;
        for (int i = 0; i < nLines; i++) {
            widths[i] = width(i);
            total += widths[i] + (i ? gap : 0);
        }
        int x = area.right - std::max(0, (area.Width() - total) / 2);
        int last = -1;
        for (int i = 0; i < nLines && x - widths[i] >= area.left; i++) {
            x -= widths[i];
            draw(i, x, dimmed(1));
            x -= gap;
            last = i;
        }
        m_lyricsRenderer.Retain(0, last);
    }
}

void CMouseWndWithArtView::DrawLyricsLine(const CSize& client)
{
    if (!HasLyrics()) {
        return;
    }

    const Lyrics& lyrics = m_pMainFrame->m_mediaMeta.lyrics;
    const int cur = lyrics.LineAt(m_pMainFrame->GetPos());
    m_nLyricsLine = cur;

    // placed as a subtitle by the style, but at the panel's fixed size rather than scaled to the view
    m_lyricsRenderer.Prepare(lyrics, CLyricsRenderer::Layout::Line, m_bLyricsTategaki, client, m_pMainFrame->m_dpi.ScaleFactorY());
    m_lyricsRenderer.Retain(cur, cur);
    if (const CLyricsRenderer::Image* img = m_lyricsRenderer.GetLine(cur)) {
        m_canvas.Blend(img->bits.data(), img->rc.Size(), img->rc.TopLeft(), CRect(CPoint(0, 0), client));
    }
}
