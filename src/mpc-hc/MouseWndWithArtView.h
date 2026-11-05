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

protected:
    CMPCPngImage m_img;
    CImage m_resizedImg;
    bool m_bFirstMedia;
    bool m_bCustomImgLoaded;
    CRect m_vrect;

    void LoadImgInternal(HGDIOBJ hImg);

    // Cover rectangle centred in area, with m_resizedImg scaled to it; empty without an image
    CRect PrepareArt(const CRect& area);

    // Lyrics, read from CMainFrame::m_mediaMeta while the media is loaded
    enum class LyricsView { None, Panel, Line };
    LyricsView m_lyricsView = LyricsView::None;
    int m_nLyricsLine = -1;
    CRect m_rcLyrics;
    CFont m_lyricsFont, m_lyricsFontBold, m_lyricsLineFont;
    int m_nLyricsFontPx = 0, m_nLyricsLineFontPx = 0;

    bool HasLyrics() const;
    void SplitLyricsLayout(const CRect& client, CRect& art, CRect& lyrics) const;
    CRect LyricsLineRect(const CRect& client);
    void DrawLyrics(CDC* pDC, const CRect& rc);
    void DrawLyricsLine(CDC* pDC, const CRect& rc, const CRect& art);
    void EnsureLyricsFonts(int px);
    void EnsureLyricsLineFont(int px);

    DECLARE_MESSAGE_MAP()

    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnTimer(UINT_PTR nIDEvent);

};
