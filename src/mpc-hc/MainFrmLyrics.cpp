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
// otherwise empty, so its item IDs are free for these entries. The lyrics are drawn
// in the default subtitle style, see CLyricsRenderer.
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
        UINT label; // string resources
        UINT osd;
    };

    const LyricsMenuItem kLyricsMenu[] = {
        { LyricsMode::Panel, IDS_LYRICS_PANEL,     IDS_OSD_LYRICS_PANEL },
        { LyricsMode::Line,  IDS_LYRICS_SUBTITLES, IDS_OSD_LYRICS_SUBTITLES },
        { LyricsMode::Off,   IDS_LYRICS_HIDE,      IDS_OSD_LYRICS_HIDDEN },
    };
    // After the modes and a separator: whether they are written vertically (縦書き)
    constexpr int kTategakiItem = (int)_countof(kLyricsMenu);

    // the mode restored by the subtitle on/off toggle
    LyricsMode s_lastShownMode = LyricsMode::Panel;

    struct LyricsDockSettings {
        UINT nearEdge;
        UINT farEdge;
        LPCTSTR edgeKey;
        LPCTSTR sizeKey;
        bool useFarEdge;
        int thickness; // at 96 DPI

        LyricsDockSettings(UINT nearEdge, UINT farEdge, LPCTSTR edgeKey, LPCTSTR sizeKey, int defaultThickness)
            : nearEdge(nearEdge), farEdge(farEdge), edgeKey(edgeKey), sizeKey(sizeKey)
            , useFarEdge(!!AfxGetApp()->GetProfileInt(IDS_R_SETTINGS, edgeKey, TRUE))
            , thickness(std::clamp<int>(AfxGetApp()->GetProfileInt(IDS_R_SETTINGS, sizeKey, defaultThickness), 1, 4096))
        {
        }

        UINT Edge() const { return useFarEdge ? farEdge : nearEdge; }
    };

    LyricsDockSettings& DockSettings()
    {
        static LyricsDockSettings horizontal {
            ID_LYRICS_DOCK_TOP, ID_LYRICS_DOCK_BOTTOM, _T("LyricsDockBottom"), _T("LyricsDockHeight"), 160
        };
        static LyricsDockSettings vertical {
            ID_LYRICS_DOCK_LEFT, ID_LYRICS_DOCK_RIGHT, _T("LyricsDockRight"), _T("LyricsDockWidth"), 240
        };
        return CMouseWndWithArtView::GetLyricsTategaki() ? vertical : horizontal;
    }
}

void CMainFrame::SetupLyricsSubMenu(CMPCThemeMenu& subMenu)
{
    if ((GetLoadState() != MLS::LOADED || !m_fAudioOnly || m_mediaMeta.lyrics.IsEmpty()) && !IsLyricsDocked()) {
        return;
    }

    const LyricsMode current = CMouseWndWithArtView::GetLyricsMode();
    UINT id = ID_SUBTITLES_SUBITEM_START;
    for (const auto& item : kLyricsMenu) {
        UINT flags = MF_STRING | MF_ENABLED;
        if (item.mode == current) {
            flags |= MF_CHECKED;
        }
        VERIFY(subMenu.AppendMenu(flags, id++, ResStr(item.label)));
    }

    VERIFY(subMenu.AppendMenu(MF_SEPARATOR));
    UINT flags = MF_STRING | MF_ENABLED;
    if (CMouseWndWithArtView::GetLyricsTategaki()) {
        flags |= MF_CHECKED;
    }
    VERIFY(subMenu.AppendMenu(flags, ID_SUBTITLES_SUBITEM_START + kTategakiItem, ResStr(IDS_LYRICS_TATEGAKI)));

    if (m_bFloating && !IsFullScreenMode()) {
        const bool docked = IsLyricsDocked();
        const UINT state = CanDockLyrics() ? MF_ENABLED : MF_GRAYED;
        VERIFY(subMenu.AppendMenu(MF_SEPARATOR));
        VERIFY(subMenu.AppendMenu(MF_STRING | state, ID_LYRICS_DOCK_TOGGLE,
                                  ResStr(docked ? IDS_LYRICS_UNDOCK : ID_LYRICS_DOCK_TOGGLE)));
        const auto& dock = DockSettings();
        for (const UINT edge : { dock.nearEdge, dock.farEdge }) {
            const UINT checked = docked && edge == dock.Edge() ? MF_CHECKED : MF_UNCHECKED;
            VERIFY(subMenu.AppendMenu(MF_STRING | state | checked, edge, ResStr(edge)));
        }
    }
}

void CMainFrame::OnLyricsSubMenu(int i)
{
    if (i == kTategakiItem) {
        const bool bTategaki = !CMouseWndWithArtView::GetLyricsTategaki();
        CMouseWndWithArtView::SetLyricsTategaki(bTategaki);
        ApplyLyricsDock();
        m_wndView.UpdateLyrics();
        m_OSD.DisplayMessage(OSD_TOPLEFT, ResStr(bTategaki ? IDS_OSD_LYRICS_TATEGAKI : IDS_OSD_LYRICS_HORIZONTAL));
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
    m_OSD.DisplayMessage(OSD_TOPLEFT, ResStr(kLyricsMenu[i].osd));
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

bool CMainFrame::CanCycleLyricsDock() const
{
    return GetLoadState() == MLS::LOADED && m_fAudioOnly && !m_mediaMeta.lyrics.IsEmpty()
           && !IsFullScreenMode() && !IsIconic();
}

bool CMainFrame::CanDockLyrics() const
{
    return IsLyricsDocked() || (m_bFloating && CanCycleLyricsDock()
           && CMouseWndWithArtView::GetLyricsMode() != LyricsMode::Off);
}

void CMainFrame::CycleLyricsDock(bool vertical)
{
    if (!CanCycleLyricsDock()) {
        return;
    }

    const bool wasVertical = CMouseWndWithArtView::GetLyricsTategaki();
    const bool oppositeEdge = IsLyricsDocked() && wasVertical == vertical && !DockSettings().useFarEdge;
    if (!m_bFloating) {
        OnViewFloating();
    }
    if (wasVertical != vertical) {
        CMouseWndWithArtView::SetLyricsTategaki(vertical);
    }
    if (CMouseWndWithArtView::GetLyricsMode() == LyricsMode::Off) {
        ToggleLyricsDisplay();
    } else if (wasVertical != vertical) {
        m_wndView.UpdateLyrics();
    }

    const auto& dock = DockSettings();
    OnLyricsDock(oppositeEdge ? dock.farEdge : dock.nearEdge);
}

void CMainFrame::OnLyricsDock(UINT id)
{
    if (id == ID_LYRICS_DOCK_TOGGLE && IsLyricsDocked()) {
        UndockLyrics();
        return;
    }
    if (!CanDockLyrics()) {
        return;
    }

    auto& dock = DockSettings();
    if (id != ID_LYRICS_DOCK_TOGGLE) {
        if (id != dock.nearEdge && id != dock.farEdge) {
            return;
        }
        dock.useFarEdge = id == dock.farEdge;
        AfxGetApp()->WriteProfileInt(IDS_R_SETTINGS, dock.edgeKey, dock.useFarEdge);
    }
    if (!IsLyricsDocked()) {
        m_lyricsDockMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        if (IsZoomed()) {
            ShowWindow(SW_RESTORE);
        }
        GetWindowRect(m_undockedLyricsRect);
        m_bWasSnapped = false;

        // Floating is normally frameless. Remove any caption or sizing frame
        // the user enabled before docking so Windows cannot paint it on activation changes.
        m_undockedLyricsFrameStyle = GetStyle() & (WS_CAPTION | WS_THICKFRAME);
        if (m_undockedLyricsFrameStyle) {
            ModifyStyle(m_undockedLyricsFrameStyle, 0, SWP_FRAMECHANGED);
        }
        UpdateFloatingOutline();
    }
    ApplyLyricsDock();
}

void CMainFrame::OnUpdateLyricsDock(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(CanDockLyrics());
}

void CMainFrame::ApplyLyricsDock()
{
    if (!IsLyricsDocked() || IsIconic()) {
        return;
    }

    const CRect rect = GetLyricsDockRect();
    if (!rect.IsRectEmpty()) {
        SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
        m_wndView.Invalidate();
        UpdateFloatingClickThrough();
    }
}

void CMainFrame::UndockLyrics(const CPoint* dragPoint)
{
    if (!IsLyricsDocked()) {
        return;
    }

    CRect rect = m_undockedLyricsRect;
    if (dragPoint) {
        GetWindowRect(rect);
    }
    m_undockedLyricsRect.SetRectEmpty();
    if (m_undockedLyricsFrameStyle) {
        ModifyStyle(0, m_undockedLyricsFrameStyle, SWP_FRAMECHANGED);
        m_undockedLyricsFrameStyle = 0;
    }
    UpdateFloatingOutline();
    m_lyricsDockMonitor = nullptr;
    m_bResizingLyricsDock = false;

    if (dragPoint) {
        rect.MoveToXY(dragPoint->x - rect.Width() / 2, dragPoint->y - rect.Height() / 2);
    }
    const auto monitor = dragPoint ? MonitorFromPoint(*dragPoint, MONITOR_DEFAULTTONEAREST)
                                   : MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = { sizeof(info) };
    if (GetMonitorInfo(monitor, &info)) {
        CRect work(info.rcWork);
        work.InflateRect(GetInvisibleBorderSize());
        const int width = std::min(rect.Width(), work.Width());
        const int height = std::min(rect.Height(), work.Height());
        const CPoint location(std::clamp<int>(rect.left, work.left, work.right - width),
                              std::clamp<int>(rect.top, work.top, work.bottom - height));
        rect = CRect(location, CSize(width, height));
    }

    if (IsIconic()) {
        WINDOWPLACEMENT placement = { sizeof(placement) };
        if (GetWindowPlacement(&placement)) {
            if (!(GetExStyle() & WS_EX_TOOLWINDOW)) {
                rect.OffsetRect(info.rcMonitor.left - info.rcWork.left, info.rcMonitor.top - info.rcWork.top);
            }
            placement.rcNormalPosition = rect;
            SetWindowPlacement(&placement);
        }
    } else {
        SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    m_wndView.Invalidate();
    UpdateFloatingClickThrough();
}

CRect CMainFrame::GetLyricsDockRect(int thickness)
{
    MONITORINFO info = { sizeof(info) };
    if (!GetMonitorInfo(m_lyricsDockMonitor, &info)) {
        m_lyricsDockMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfo(m_lyricsDockMonitor, &info)) {
            return CRect(0, 0, 0, 0);
        }
    }

    const auto& dock = DockSettings();
    const bool vertical = CMouseWndWithArtView::GetLyricsTategaki();
    if (thickness <= 0) {
        thickness = MulDiv(dock.thickness, DpiHelper::GetDPIForMonitor(m_lyricsDockMonitor), 96);
    }

    CRect rect(info.rcWork);
    rect.InflateRect(GetInvisibleBorderSize());
    MINMAXINFO limits = {};
    OnGetMinMaxInfo(&limits);
    const int available = vertical ? rect.Width() : rect.Height();
    const int minimum = std::min<int>(available, vertical ? limits.ptMinTrackSize.x : limits.ptMinTrackSize.y);
    thickness = std::clamp(thickness, minimum, std::max(minimum, available / 2));

    switch (dock.Edge()) {
        case ID_LYRICS_DOCK_TOP:    rect.bottom = rect.top + thickness; break;
        case ID_LYRICS_DOCK_BOTTOM: rect.top = rect.bottom - thickness; break;
        case ID_LYRICS_DOCK_LEFT:   rect.right = rect.left + thickness; break;
        case ID_LYRICS_DOCK_RIGHT:  rect.left = rect.right - thickness; break;
    }
    return rect;
}

LRESULT CMainFrame::GetLyricsDockResizeHit(CPoint point) const
{
    CRect rect;
    GetWindowRect(rect);
    if (!rect.PtInRect(point)) {
        return HTNOWHERE;
    }
    // Give the child view a resize target inside the frameless client area,
    // including when the background passes other mouse input through.
    switch (DockSettings().Edge()) {
        case ID_LYRICS_DOCK_TOP:    return rect.bottom - point.y <= m_dpi.ScaleY(14) ? HTBOTTOM : HTNOWHERE;
        case ID_LYRICS_DOCK_BOTTOM: return point.y - rect.top < m_dpi.ScaleY(14) ? HTTOP : HTNOWHERE;
        case ID_LYRICS_DOCK_LEFT:   return rect.right - point.x <= m_dpi.ScaleX(14) ? HTRIGHT : HTNOWHERE;
        case ID_LYRICS_DOCK_RIGHT:  return point.x - rect.left < m_dpi.ScaleX(14) ? HTLEFT : HTNOWHERE;
        default:                  return HTNOWHERE;
    }
}

void CMainFrame::ResizeLyricsDock(UINT side, LPRECT rect)
{
    const auto edge = DockSettings().Edge();
    const bool innerEdge = (edge == ID_LYRICS_DOCK_TOP && side == WMSZ_BOTTOM)
                        || (edge == ID_LYRICS_DOCK_BOTTOM && side == WMSZ_TOP)
                        || (edge == ID_LYRICS_DOCK_LEFT && side == WMSZ_RIGHT)
                        || (edge == ID_LYRICS_DOCK_RIGHT && side == WMSZ_LEFT);
    if (innerEdge) {
        const CRect proposed(rect);
        const CRect dock = GetLyricsDockRect(CMouseWndWithArtView::GetLyricsTategaki() ? proposed.Width() : proposed.Height());
        if (!dock.IsRectEmpty()) {
            *rect = dock;
            m_bResizingLyricsDock = true;
            return;
        }
    }
    GetWindowRect(rect);
}

void CMainFrame::SaveLyricsDockSize()
{
    if (!m_bResizingLyricsDock) {
        return;
    }

    m_bResizingLyricsDock = false;
    CRect rect;
    GetWindowRect(rect);
    auto& dock = DockSettings();
    const bool vertical = CMouseWndWithArtView::GetLyricsTategaki();
    const int thickness = vertical ? rect.Width() : rect.Height();
    dock.thickness = MulDiv(thickness, 96, vertical ? m_dpi.DPIX() : m_dpi.DPIY());
    AfxGetApp()->WriteProfileInt(IDS_R_SETTINGS, dock.sizeKey, dock.thickness);
}

void CMainFrame::OnViewCoverArt()
{
    const bool bShow = !CMouseWndWithArtView::GetShowCoverArt();
    CMouseWndWithArtView::SetShowCoverArt(bShow);
    m_wndView.Invalidate();
    if (HasDedicatedFSVideoWindow()) {
        m_pDedicatedFSVideoWnd->Invalidate();
    }
    m_OSD.DisplayMessage(OSD_TOPLEFT, ResStr(bShow ? IDS_OSD_COVERART_SHOWN : IDS_OSD_COVERART_HIDDEN));
}

void CMainFrame::OnUpdateViewCoverArt(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(CMouseWndWithArtView::GetShowCoverArt());
}
