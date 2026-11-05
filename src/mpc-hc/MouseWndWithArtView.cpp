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

    CRect r;

    if ((m_pMainFrame->GetLoadState() != MLS::CLOSED || (!m_bFirstMedia && m_pMainFrame->m_controls.DelayShowNotLoaded())) &&
            !m_pMainFrame->HasDedicatedFSVideoWindow() && !m_pMainFrame->m_fAudioOnly) {
        pDC->ExcludeClipRect(m_vrect);
    } else {
        CRect client;
        GetClientRect(client);
        CRect artArea = client;
        m_rcLyrics.SetRectEmpty();
        if (m_lyricsView == LyricsView::Panel) {
            SplitLyricsLayout(client, artArea, m_rcLyrics);
        }

        const CRect art = PrepareArt(artArea);

        // the lyrics are composed off-screen (in Line mode over the cover), then excluded
        if (m_lyricsView == LyricsView::Panel) {
            DrawLyrics(pDC, m_rcLyrics);
        } else if (m_lyricsView == LyricsView::Line) {
            m_rcLyrics = LyricsLineRect(client);
            DrawLyricsLine(pDC, m_rcLyrics, art);
        }
        if (!m_rcLyrics.IsRectEmpty()) {
            pDC->ExcludeClipRect(m_rcLyrics);
        }

        if (!art.IsRectEmpty()) {
            m_resizedImg.BitBlt(*pDC, art.TopLeft());
            pDC->ExcludeClipRect(art);
        }
    }

    GetClientRect(r);
    pDC->FillSolidRect(r, 0);

    return TRUE;
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

namespace
{
    constexpr UINT_PTR TIMER_LYRICS = 0x4C59; // CMouse uses callback timers with pointer IDs
    constexpr UINT LYRICS_POLL_MS = 50;       // the seek bar position itself is polled every 100 ms by default
    constexpr LPCTSTR LYRICS_MODE_KEY = _T("LyricsDisplay");

    int s_lyricsMode = -1; // not loaded yet

    COLORREF BlendColor(COLORREF fg, COLORREF bg, double a)
    {
        auto mix = [a](int f, int b) {
            return BYTE(std::lround(f * a + b * (1.0 - a)));
        };
        return RGB(mix(GetRValue(fg), GetRValue(bg)), mix(GetGValue(fg), GetGValue(bg)), mix(GetBValue(fg), GetBValue(bg)));
    }
}

CMouseWndWithArtView::LyricsMode CMouseWndWithArtView::GetLyricsMode()
{
    if (s_lyricsMode < 0) {
        s_lyricsMode = (int)AfxGetApp()->GetProfileInt(IDS_R_SETTINGS, LYRICS_MODE_KEY, (int)LyricsMode::Panel);
        if (s_lyricsMode > (int)LyricsMode::Off) {
            s_lyricsMode = (int)LyricsMode::Panel;
        }
    }
    return (LyricsMode)s_lyricsMode;
}

void CMouseWndWithArtView::SetLyricsMode(LyricsMode mode)
{
    s_lyricsMode = (int)mode;
    AfxGetApp()->WriteProfileInt(IDS_R_SETTINGS, LYRICS_MODE_KEY, s_lyricsMode);
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

void CMouseWndWithArtView::EnsureLyricsFonts(int px)
{
    if (px == m_nLyricsFontPx && m_lyricsFont.GetSafeHandle()) {
        return;
    }
    m_lyricsFont.DeleteObject();
    m_lyricsFontBold.DeleteObject();

    LOGFONT lf = {};
    m_pMainFrame->m_dpi.GetMessageFont(&lf);
    lf.lfHeight = -px;
    lf.lfWidth = 0;
    lf.lfWeight = FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    m_lyricsFont.CreateFontIndirect(&lf);
    lf.lfWeight = FW_BOLD;
    m_lyricsFontBold.CreateFontIndirect(&lf);
    m_nLyricsFontPx = px;
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

void CMouseWndWithArtView::DrawLyrics(CDC* pDC, const CRect& rc)
{
    if (rc.IsRectEmpty()) {
        return;
    }

    // drawn off-screen, so that a line change does not flicker
    CDC dc;
    CBitmap bmp;
    if (!dc.CreateCompatibleDC(pDC) || !bmp.CreateCompatibleBitmap(pDC, rc.Width(), rc.Height())) {
        return;
    }
    CBitmap* pOldBmp = dc.SelectObject(&bmp);
    const CRect local(0, 0, rc.Width(), rc.Height());
    const COLORREF bg = 0; // OnEraseBkgnd fills the rest of the view with black as well
    dc.FillSolidRect(local, bg);

    if (HasLyrics()) {
        const Lyrics& lyrics = m_pMainFrame->m_mediaMeta.lyrics;
        const DpiHelper& dpi = m_pMainFrame->m_dpi;
        const int nLines = (int)lyrics.lines.size();
        const int cur = lyrics.LineAt(m_pMainFrame->GetPos());
        m_nLyricsLine = cur;

        EnsureLyricsFonts(std::max(dpi.ScaleY(14), std::min(dpi.ScaleY(28), rc.Height() / 16)));
        CFont* pOldFont = dc.SelectObject(&m_lyricsFont);
        dc.SetBkMode(TRANSPARENT);

        const COLORREF fg = AppIsThemeLoaded() ? CMPCTheme::TextFGColor : RGB(0xFF, 0xFF, 0xFF);
        auto dimmed = [&](int distance) {
            return BlendColor(fg, bg, std::max(0.25, 0.6 - 0.1 * (distance - 1)));
        };

        CRect area = local;
        area.DeflateRect(dpi.ScaleX(16), dpi.ScaleY(16));
        const int gap = m_nLyricsFontPx / 2;
        const UINT fmt = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL;

        auto measure = [&](int i, CFont* pFont) {
            const CStringW& text = lyrics.lines[i].text;
            CRect r(area.left, 0, area.right, 0);
            dc.SelectObject(pFont);
            dc.DrawText(text.IsEmpty() ? CStringW(L" ") : text, r, fmt | DT_CALCRECT);
            return r.Height();
        };
        auto draw = [&](int i, int top, int height, CFont* pFont, COLORREF color) {
            CRect r(area.left, top, area.right, top + height);
            dc.SelectObject(pFont);
            dc.SetTextColor(color);
            dc.DrawText(lyrics.lines[i].text, r, fmt);
        };

        if (lyrics.timed) {
            // the current line centred, its neighbours dimmed by distance;
            // only lines that fit entirely, so no wrapped line is cut at an edge
            const int anchor = std::max(cur, 0);
            CFont* pAnchorFont = cur >= 0 ? &m_lyricsFontBold : &m_lyricsFont;
            const int hAnchor = measure(anchor, pAnchorFont);
            const int topAnchor = area.top + (area.Height() - hAnchor) / 2;
            draw(anchor, topAnchor, hAnchor, pAnchorFont, cur >= 0 ? fg : dimmed(1));

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

        dc.SelectObject(pOldFont);
    }

    pDC->BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), &dc, 0, 0, SRCCOPY);
    dc.SelectObject(pOldBmp);
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

void CMouseWndWithArtView::DrawLyricsLine(CDC* pDC, const CRect& rc, const CRect& art)
{
    if (rc.IsRectEmpty()) {
        return;
    }

    CDC dc;
    CBitmap bmp;
    if (!dc.CreateCompatibleDC(pDC) || !bmp.CreateCompatibleBitmap(pDC, rc.Width(), rc.Height())) {
        return;
    }
    CBitmap* pOldBmp = dc.SelectObject(&bmp);
    const CRect local(0, 0, rc.Width(), rc.Height());
    dc.FillSolidRect(local, 0);

    // the part of the cover under the text
    CRect overlap;
    if (!m_resizedImg.IsNull() && overlap.IntersectRect(rc, art)) {
        m_resizedImg.BitBlt(dc.m_hDC, overlap.left - rc.left, overlap.top - rc.top, overlap.Width(), overlap.Height(),
                            overlap.left - art.left, overlap.top - art.top);
    }

    if (HasLyrics()) {
        const Lyrics& lyrics = m_pMainFrame->m_mediaMeta.lyrics;
        const int cur = lyrics.LineAt(m_pMainFrame->GetPos());
        m_nLyricsLine = cur;

        if (cur >= 0 && !lyrics.lines[cur].text.IsEmpty()) {
            const CStringW& text = lyrics.lines[cur].text;
            const int outline = std::max(1, m_nLyricsLineFontPx / 14);
            const UINT fmt = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL;

            CFont* pOldFont = dc.SelectObject(&m_lyricsLineFont);
            dc.SetBkMode(TRANSPARENT);

            CRect area = local;
            area.DeflateRect(outline, outline);
            CRect measured(area.left, 0, area.right, 0);
            dc.DrawText(text, measured, fmt | DT_CALCRECT);
            // bottom-aligned, as subtitles are
            CRect textRect(area.left, std::max(area.top, area.bottom - measured.Height()), area.right, area.bottom);

            // outline: the text stamped around a circle, then filled
            dc.SetTextColor(RGB(0, 0, 0));
            const double kPi = 3.14159265358979323846;
            for (int k = 0; k < 16; k++) {
                const double a = k * kPi / 8;
                CRect o = textRect;
                o.OffsetRect((int)std::lround(outline * cos(a)), (int)std::lround(outline * sin(a)));
                dc.DrawText(text, o, fmt);
            }
            dc.SetTextColor(RGB(0xFF, 0xFF, 0xFF));
            dc.DrawText(text, textRect, fmt);

            dc.SelectObject(pOldFont);
        }
    }

    pDC->BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), &dc, 0, 0, SRCCOPY);
    dc.SelectObject(pOldBmp);
}
