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

#pragma once

#include "Lyrics.h"

class CPlayerPlaylistBar;

// Album metadata and lyrics of the open audio file. Title and artist are not
// kept here, they still come from CMainFrame::getBestTitle and IAMMediaContent.
//
// Written on the graph thread while the media is loading (Clear, Load), so the
// UI thread may only read it while CMainFrame::GetLoadState() == MLS::LOADED.
struct MediaMetadata
{
    CString album, albumArtist, genre;
    UINT track = 0, trackCount = 0, disc = 0, discCount = 0;
    Lyrics lyrics;

    void Clear();

    // Sources: MediaInfo for local files, the LAV Splitter tags otherwise, then
    // the CUE sheet of the current playlist item for album and album artist.
    // Lyrics: a sidecar .lrc or the embedded tag, timed beating untimed.
    void Load(IFilterGraph* pGraph, const CString& fn, bool bAudioOnly, CPlayerPlaylistBar& playlist);
};
