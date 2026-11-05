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

// Lyrics display modes in the Subtitles menu. For audio-only files that menu is
// otherwise empty, so its item IDs are free for these entries.
// Also the cover art toggle of the audio-only view (View menu).

#include "stdafx.h"
#include "mplayerc.h"
#include "MainFrm.h"
#include "FullscreenWnd.h"

namespace
{
    using LyricsMode = CMouseWndWithArtView::LyricsMode;

    struct LyricsMenuItem {
        LyricsMode mode;
        LPCWSTR label; // not translated: kept out of the resource file
        LPCWSTR osd;
    };

    const LyricsMenuItem kLyricsMenu[] = {
        { LyricsMode::Panel, L"Lyrics &Panel",        L"Lyrics: panel" },
        { LyricsMode::Line,  L"Lyrics as &Subtitles", L"Lyrics: as subtitles" },
        { LyricsMode::Off,   L"&Hide Lyrics",         L"Lyrics: hidden" },
    };
    // after a separator, following the modes
    constexpr int kLyricsOutlineItem = (int)_countof(kLyricsMenu);

    // the mode restored by the subtitle on/off toggle
    LyricsMode s_lastShownMode = LyricsMode::Panel;
}

void CMainFrame::SetupLyricsSubMenu(CMPCThemeMenu& subMenu)
{
    if (GetLoadState() != MLS::LOADED || !m_fAudioOnly || m_mediaMeta.lyrics.IsEmpty()) {
        return;
    }

    const LyricsMode current = CMouseWndWithArtView::GetLyricsMode();
    UINT id = ID_SUBTITLES_SUBITEM_START;
    for (const auto& item : kLyricsMenu) {
        UINT flags = MF_STRING | MF_ENABLED;
        if (item.mode == current) {
            flags |= MF_CHECKED;
        }
        VERIFY(subMenu.AppendMenu(flags, id++, item.label));
    }

    VERIFY(subMenu.AppendMenu(MF_SEPARATOR));
    UINT flags = MF_STRING | MF_ENABLED;
    if (CMouseWndWithArtView::GetLyricsOutline()) {
        flags |= MF_CHECKED;
    }
    VERIFY(subMenu.AppendMenu(flags, ID_SUBTITLES_SUBITEM_START + kLyricsOutlineItem, L"Lyrics &Outline"));
}

void CMainFrame::OnLyricsSubMenu(int i)
{
    if (i == kLyricsOutlineItem) {
        const bool bOutline = !CMouseWndWithArtView::GetLyricsOutline();
        CMouseWndWithArtView::SetLyricsOutline(bOutline);
        m_wndView.Invalidate();
        m_OSD.DisplayMessage(OSD_TOPLEFT, bOutline ? L"Lyrics outline: on" : L"Lyrics outline: off");
        return;
    }
    if (i < 0 || i >= (int)_countof(kLyricsMenu)) {
        return;
    }
    const LyricsMode mode = kLyricsMenu[i].mode;
    if (mode != LyricsMode::Off) {
        s_lastShownMode = mode;
    }
    CMouseWndWithArtView::SetLyricsMode(mode);
    m_wndView.UpdateLyrics();
    m_OSD.DisplayMessage(OSD_TOPLEFT, kLyricsMenu[i].osd);
}

bool CMainFrame::ToggleLyricsDisplay()
{
    if (GetLoadState() != MLS::LOADED || !m_fAudioOnly || m_mediaMeta.lyrics.IsEmpty()) {
        return false;
    }

    LyricsMode mode = CMouseWndWithArtView::GetLyricsMode();
    if (mode == LyricsMode::Off) {
        mode = s_lastShownMode;
    } else {
        s_lastShownMode = mode;
        mode = LyricsMode::Off;
    }
    for (int i = 0; i < (int)_countof(kLyricsMenu); i++) {
        if (kLyricsMenu[i].mode == mode) {
            OnLyricsSubMenu(i);
            break;
        }
    }
    return true;
}

void CMainFrame::OnViewCoverArt()
{
    const bool bShow = !CMouseWndWithArtView::GetShowCoverArt();
    CMouseWndWithArtView::SetShowCoverArt(bShow);
    m_wndView.Invalidate();
    if (HasDedicatedFSVideoWindow()) {
        m_pDedicatedFSVideoWnd->Invalidate();
    }
    m_OSD.DisplayMessage(OSD_TOPLEFT, bShow ? L"Cover art: shown" : L"Cover art: hidden");
}

void CMainFrame::OnUpdateViewCoverArt(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(CMouseWndWithArtView::GetShowCoverArt());
}
