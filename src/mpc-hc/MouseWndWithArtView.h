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
#include "LyricsRenderer.h"

class CMainFrame;

// 32 bpp top-down DIB section with premultiplied alpha, selected into its own memory DC.
// GDI leaves the alpha byte at 0, see SetOpaque.
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
    // Premultiplied "over" of a premultiplied ARGB image at pt, its opacity scaled, within clip
    void Blend(const DWORD* bits, CSize size, CPoint pt, const CRect& clip, BYTE opacity = 255);

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

    // Whether the view shows something at pt (client coordinates) when its background is
    // see-through: the video, or the box around the cover or logo and the lyrics lines as drawn.
    // The click-through Floating preset lets the mouse through everywhere else.
    bool IsOpaqueAt(CPoint pt) const;

    // Call when lyrics may have changed (media opened or closed, display mode changed).
    // bKeepLayout keeps the lyrics area, empty, while the next queued file loads.
    void UpdateLyrics(bool bKeepLayout = false);

    // How lyrics are shown in the audio-only view, saved in the settings
    enum class LyricsMode { Panel = 0, Line = 1, Off = 2 };
    static LyricsMode GetLyricsMode();
    static void SetLyricsMode(LyricsMode mode);
    // Whether the panel and the line are written vertically (縦書き), saved in the settings
    static bool GetLyricsTategaki();
    static void SetLyricsTategaki(bool bTategaki);

    // Cover art (or the logo standing in for it) in the audio-only view, saved in the settings.
    // The logo is always shown while nothing is loaded.
    static bool GetShowCoverArt();
    static void SetShowCoverArt(bool bShow);

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

    bool IsBackgroundTransparent() const;
    // The click-through Floating preset: the box around the art and lyrics, which takes the mouse
    // in the see-through view only to be dragged around (the video keeps the arrow)
    bool IsGrabArea(const CPoint& clientPoint) override;
    bool ShowsVideo() const;
    bool ShowsArt() const;

    // Lyrics, read from CMainFrame::m_mediaMeta while the media is loaded
    enum class LyricsView { None, Panel, Line };
    LyricsView m_lyricsView = LyricsView::None;
    bool m_bLyricsTategaki = false;
    int m_nLyricsLine = -1;
    CRect m_rcLyrics;
    // The text, in the default subtitle style
    CLyricsRenderer m_lyricsRenderer;

    bool HasLyrics() const;
    void SplitLyricsLayout(const CRect& client, CRect& art, CRect& lyrics) const;
    // The bounding box of what the last composition of m_canvas drew, in client coordinates:
    // the art and each lyrics line (the pixels it covers with its borders and shadow, clipped).
    // For IsOpaqueAt; empty when nothing was drawn.
    CRect m_rcShown = CRect(0, 0, 0, 0);
    // Blends a lyrics line into m_canvas and records where it went
    void BlendLyricsLine(const CLyricsRenderer::Image& img, CPoint pt, const CRect& clip, BYTE opacity = 255);

    // These draw into m_canvas, in its (client) coordinates
    void DrawLyrics(const CRect& rc, const CSize& client);
    // the panel in vertical writing, its columns going across the area right to left
    void DrawLyricsTategaki(const CRect& rc, const CSize& client);
    // the current line alone, placed by the style, horizontal or vertical
    void DrawLyricsLine(const CSize& client);

    DECLARE_MESSAGE_MAP()

    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnTimer(UINT_PTR nIDEvent);

};
