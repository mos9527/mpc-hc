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
#include "MediaMetadata.h"
#include "mplayerc.h"
#include "PlayerPlaylistBar.h"
#include "PathUtils.h"
#include "MediaInfo/MediaInfoDLL.h"

namespace
{
    // Same values as GUID_LAVSplitter and GUID_LAVSplitterSource in FGFilterLAV.h
    const CLSID kLAVSplitter       = { 0x171252A0, 0x8820, 0x4AFE, { 0x9D, 0xF8, 0x5C, 0x92, 0xB2, 0xD6, 0x6B, 0x04 } };
    const CLSID kLAVSplitterSource = { 0xB98D13E7, 0x55DB, 0x4385, { 0xA3, 0x3D, 0x09, 0xFD, 0x1B, 0xA2, 0x63, 0x38 } };

    // Leading decimal digits, 0 when there are none
    UINT ParseUInt(const CString& s)
    {
        LPCWSTR p = s.GetString();
        while (*p == L' ' || *p == L'\t') {
            p++;
        }
        if (*p < L'0' || *p > L'9') {
            return 0;
        }
        unsigned long v = wcstoul(p, nullptr, 10);
        return v > 100000 ? 0 : UINT(v);
    }

    // "2/14" or "2"
    void ParsePair(const CString& s, UINT& value, UINT& total)
    {
        value = ParseUInt(s);
        int slash = s.Find(L'/');
        total = slash >= 0 ? ParseUInt(s.Mid(slash + 1)) : 0;
    }

    // The LAV Splitter of the file: it exposes the container tags through IPropertyBag.
    // When the graph has several (an external audio file), prefer the one reading fn.
    CComPtr<IPropertyBag> FindLAVTags(IFilterGraph* pGraph, const CString& fn)
    {
        CComPtr<IPropertyBag> pFirst;
        BeginEnumFilters(pGraph, pEF, pBF) {
            CLSID clsid = GetCLSID(pBF);
            if (clsid != kLAVSplitter && clsid != kLAVSplitterSource) {
                continue;
            }
            CComQIPtr<IPropertyBag> pPB = pBF;
            if (!pPB) {
                continue;
            }
            if (!pFirst) {
                pFirst = pPB;
            }
            if (CComQIPtr<IFileSourceFilter> pFSF = pBF) {
                LPOLESTR pszFile = nullptr;
                if (SUCCEEDED(pFSF->GetCurFile(&pszFile, nullptr)) && pszFile) {
                    bool bMatch = fn.CompareNoCase(pszFile) == 0;
                    CoTaskMemFree(pszFile);
                    if (bMatch) {
                        return CComPtr<IPropertyBag>(pPB);
                    }
                }
            }
        }
        EndEnumFilters;
        return pFirst;
    }

    // LAV matches the key as a case-insensitive prefix and returns the first such tag
    CString ReadTag(IPropertyBag* pPB, LPCWSTR key)
    {
        CString value;
        CComVariant var;
        if (SUCCEEDED(pPB->Read(key, &var, nullptr)) && var.vt == VT_BSTR && var.bstrVal) {
            value = var.bstrVal;
            value.Trim();
        }
        return value;
    }

    // "track" can also match a TRACKTOTAL tag that comes first, which then reads the
    // same as "tracktotal"; such a value is only used as the total
    void ReadNumberTags(IPropertyBag* pPB, LPCWSTR key, LPCWSTR totalKey, UINT& value, UINT& total)
    {
        CString s = ReadTag(pPB, key);
        ParsePair(s, value, total);
        if (s.Find(L'/') < 0) {
            CString t = ReadTag(pPB, totalKey);
            if (!t.IsEmpty()) {
                if (t == s) {
                    value = 0;
                }
                total = ParseUInt(t);
            }
        }
    }

    struct CueInfo {
        CString album, albumArtist;
        UINT track = 0, trackCount = 0;
    };

    // The disc-level TITLE and PERFORMER, and the track number when the file at
    // cueIndex holds a single track. Counts files like CMainFrame::SetupCueChapters.
    bool ReadCueSheet(const CString& cuefn, int cueIndex, CueInfo& info)
    {
        CWebTextFile f(CTextFile::UTF8);
        f.SetFallbackEncoding(CTextFile::ANSI);
        if (!f.Open(cuefn)) {
            return false;
        }

        CString str, lastfile;
        int fileIndex = -1;
        UINT tracksInFile = 0, trackNumber = 0, totalTracks = 0;
        while (f.ReadString(str)) {
            str.Trim();
            if (fileIndex == -1 && str.Left(5) == _T("TITLE")) {
                info.album = str.Mid(6).Trim(_T("\""));
            } else if (fileIndex == -1 && str.Left(9) == _T("PERFORMER")) {
                info.albumArtist = str.Mid(10).Trim(_T("\""));
            } else if (str.Left(4) == _T("FILE")) {
                CString entry;
                if (ParseCUEFileLine(str, entry) && entry != lastfile) {
                    fileIndex++;
                    lastfile = entry;
                }
            } else if (fileIndex >= 0 && str.Left(5) == _T("TRACK") && str.Right(5) == _T("AUDIO")) {
                totalTracks++;
                if (fileIndex == cueIndex) {
                    tracksInFile++;
                    trackNumber = ParseUInt(str.Mid(6));
                }
            }
        }

        if (tracksInFile == 1 && trackNumber) {
            info.track = trackNumber;
            info.trackCount = totalTracks;
        }
        return true;
    }
}

void MediaMetadata::Clear()
{
    album.Empty();
    albumArtist.Empty();
    genre.Empty();
    track = trackCount = disc = discCount = 0;
    lyrics.Clear();
}

void MediaMetadata::Load(IFilterGraph* pGraph, const CString& fn, bool bAudioOnly, CPlayerPlaylistBar& playlist)
{
    Clear();

    // only shown for music: SMTC music properties and the audio-only view
    if (!bAudioOnly || fn.IsEmpty()) {
        return;
    }

    CString fnCopy = fn;
    const bool bLocal = !PathUtils::IsURL(fnCopy) && CLongPath(fn).FileExists();
    CComPtr<IPropertyBag> pTags = FindLAVTags(pGraph, fn);

    // Album fields. MediaInfo reads exact tag names; the LAV lookup is a prefix
    // match, so it is only used when MediaInfo cannot read the file.
    bool bHaveTags = false;
    CString miLyrics;
    if (bLocal) {
        MediaInfoDLL::MediaInfo mi;
        if (mi.IsReady() && mi.Open(fn.GetString()) > 0) {
            auto get = [&mi](LPCWSTR field) {
                CString s(mi.Get(MediaInfoDLL::Stream_General, 0, field).c_str());
                s.Trim();
                return s;
            };
            album       = get(L"Album");
            albumArtist = get(L"Album/Performer");
            genre       = get(L"Genre");
            track       = ParseUInt(get(L"Track/Position"));
            trackCount  = ParseUInt(get(L"Track/Position_Total"));
            disc        = ParseUInt(get(L"Part/Position"));
            discCount   = ParseUInt(get(L"Part/Position_Total"));
            // joins lines with " / ", so only a last resort (Ogg keeps its tags
            // per stream, out of reach of the LAV property bag)
            miLyrics    = get(L"Lyrics");
            bHaveTags = true;
        }
    }
    if (!bHaveTags && pTags) {
        CString a = ReadTag(pTags, L"album");
        albumArtist = ReadTag(pTags, L"album_artist");
        // "album" also matches an album_artist tag that comes first. This loses
        // self-titled albums, which streams rarely carry anyway.
        if (a != albumArtist) {
            album = a;
        }
        ReadNumberTags(pTags, L"track", L"tracktotal", track, trackCount);
        ReadNumberTags(pTags, L"disc", L"disctotal", disc, discCount);
    }
    if (genre.IsEmpty() && pTags) {
        genre = ReadTag(pTags, L"genre");
    }

    // CUE sheets override the album. Re-read here as CMainFrame::SetupCueChapters
    // does, the disc-level fields are not kept in the playlist item.
    CPlaylistItem pli(true);
    if (playlist.GetCur(pli, true) && pli.m_cue && !pli.m_cue_filename.IsEmpty()) {
        CueInfo cue;
        if (ReadCueSheet(pli.m_cue_filename, pli.m_cue_index, cue)) {
            if (!cue.album.IsEmpty()) {
                album = cue.album;
            }
            if (!cue.albumArtist.IsEmpty()) {
                albumArtist = cue.albumArtist;
            }
            if (cue.track) {
                track = cue.track;
                trackCount = cue.trackCount;
            }
        }
    }

    // Lyrics: a timed source beats an untimed one, the sidecar wins between equals
    Lyrics embedded;
    if (pTags) {
        // MP4 lyr, ID3 USLT (lyrics-<desc>-<lang>), Vorbis LYRICS or UNSYNCEDLYRICS
        CString s = ReadTag(pTags, L"lyrics");
        if (s.IsEmpty()) {
            s = ReadTag(pTags, L"unsyncedlyrics");
        }
        if (!s.IsEmpty()) {
            embedded.Parse(s);
        }
    }
    if (embedded.IsEmpty() && !miLyrics.IsEmpty()) {
        miLyrics.Replace(L" / ", L"\n");
        embedded.Parse(miLyrics);
    }

    Lyrics sidecar;
    if (bLocal) {
        CLongPath lrc(fn);
        if (lrc.RenameExtension(_T(".lrc")) && lrc.FileExists()) {
            sidecar.LoadFile(lrc.m_strPath);
        }
    }

    if (!sidecar.IsEmpty() && (sidecar.timed || !embedded.timed)) {
        lyrics = std::move(sidecar);
    } else if (!embedded.IsEmpty()) {
        lyrics = std::move(embedded);
    }

    TRACE(_T("MediaMetadata: album \"%s\", album artist \"%s\", track %u/%u, disc %u/%u, lyrics %u lines (%s)\n"),
          album.GetString(), albumArtist.GetString(), track, trackCount, disc, discCount,
          (UINT)lyrics.lines.size(), lyrics.timed ? _T("timed") : _T("untimed"));
}
