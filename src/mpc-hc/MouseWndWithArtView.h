/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2013, 2015 see Authors.txt
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

#pragma once

#include "MPCPngImage.h"
#include "MouseTouch.h"

class CMainFrame;

// 32 bpp top-down DIB section with premultiplied alpha, selected into its own memory DC.
// GDI leaves the alpha byte at 0, so text is drawn into one as a coverage mask and composited.
class CArgbSurface
{
public:
    CArgbSurface() = default;
    CArgbSurface(const CArgbSurface&) = delete;
    CArgbSurface& operator=(const CArgbSurface&) = delete;
    ~CArgbSurface() { Destroy(); }

    bool Create(int width, int height); // keeps the buffer when the size is unchanged
    void Destroy();
    void Clear(DWORD argb = 0);
    // Sets the alpha of every pixel in rc to 255, after opaque GDI drawing
    void SetOpaque(const CRect& rc);
    // Premultiplied "over" of color through the coverage in mask (its BGR channels; black = none), at pt
    void Composite(const CArgbSurface& mask, CPoint pt, COLORREF color);

    HDC GetDC() const { return m_hDC; }
    int Width() const { return m_width; }
    int Height() const { return m_height; }

private:
    HDC m_hDC = nullptr;
    HBITMAP m_hBmp = nullptr;
    HGDIOBJ m_hOldBmp = nullptr;
    DWORD* m_pBits = nullptr;
    int m_width = 0, m_height = 0;
};

class CMouseWndWithArtView : public CMouseWnd
{
    DECLARE_DYNAMIC(CMouseWndWithArtView)

    CMainFrame* m_pMainFrame;
public:
    explicit CMouseWndWithArtView(CMainFrame* pMainFrame, bool bD3DFS = false);
    ~CMouseWndWithArtView();
    CSize GetLogoSize();
    void LoadImg(const CString& imagePath = _T(""));
    void LoadImg(std::vector<BYTE> buffer);
    bool IsCustomImgLoaded() const { return m_bCustomImgLoaded; };
    void SetVideoRect(const CRect& r = CRect(0, 0, 0, 0));
    CRect GetVideoRect() const { return m_vrect; }

    // Call when lyrics may have changed (media opened or closed, display mode changed).
    // bKeepLayout keeps the lyrics area, empty, while the next queued file loads.
    void UpdateLyrics(bool bKeepLayout = false);

    // How lyrics are shown in the audio-only view, saved in the settings
    enum class LyricsMode { Panel = 0, Line = 1, Off = 2 };
    static LyricsMode GetLyricsMode();
    static void SetLyricsMode(LyricsMode mode);

    // Cover art (or the logo standing in for it) in the audio-only view, saved in the settings.
    // The logo is always shown while nothing is loaded.
    static bool GetShowCoverArt();
    static void SetShowCoverArt(bool bShow);

    // Black outline around the lyrics text, saved in the settings
    static bool GetLyricsOutline();
    static void SetLyricsOutline(bool bOutline);

protected:
    CMPCPngImage m_img;
    CImage m_resizedImg;
    bool m_bFirstMedia;
    bool m_bCustomImgLoaded;
    CRect m_vrect;

    void LoadImgInternal(HGDIOBJ hImg);

    // Cover rectangle centred in area, with m_resizedImg scaled to it; empty without an image
    CRect PrepareArt(const CRect& area);

    // The art view (cover, logo, lyrics) is composed here, then copied to the window in one blit.
    // Its alpha is only used when the window background is see-through (the Floating preset).
    CArgbSurface m_canvas;
    // Coverage masks of the lyrics text and of its outline
    CArgbSurface m_textMask, m_outlineMask;

    bool IsBackgroundTransparent() const;
    bool ShowsArt() const;

    // Lyrics, read from CMainFrame::m_mediaMeta while the media is loaded
    enum class LyricsView { None, Panel, Line };
    LyricsView m_lyricsView = LyricsView::None;
    int m_nLyricsLine = -1;
    CRect m_rcLyrics;
    CFont m_lyricsFont, m_lyricsFontBold, m_lyricsLineFont;
    int m_nLyricsFontPx = 0, m_nLyricsLineFontPx = 0;
    BYTE m_nLyricsFontQuality = 0;

    bool HasLyrics() const;
    void SplitLyricsLayout(const CRect& client, CRect& art, CRect& lyrics) const;
    CRect LyricsLineRect(const CRect& client);
    // Both draw into m_canvas, rc in its (client) coordinates
    void DrawLyrics(const CRect& rc, bool bTransparent);
    void DrawLyricsLine(const CRect& rc);
    void EnsureLyricsFonts(int px, BYTE quality);
    void EnsureLyricsLineFont(int px);
    // Clears the text mask, and the outline mask when used, at the given size
    bool BeginTextMasks(const CSize& size, bool bOutline);
    // Draws text into the masks; opacity scales its coverage, outline is the stroke width (0 = none)
    void DrawTextToMasks(const CStringW& text, const CRect& r, UINT fmt, CFont* pFont, double opacity, int outline);
    // Composites the outline (black) and the text (fg) at pt into m_canvas
    void CompositeTextMasks(CPoint pt, COLORREF fg, bool bOutline);

    DECLARE_MESSAGE_MAP()

    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnTimer(UINT_PTR nIDEvent);

};
