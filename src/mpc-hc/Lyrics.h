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

#include <vector>

// LRC lyrics: parsing and lookup only, no UI dependencies
class Lyrics
{
public:
    struct Line {
        REFERENCE_TIME t = 0;   // effective time, [offset:] already applied
        CStringW text;          // may be empty: an instrumental gap
    };

    std::vector<Line> lines;    // sorted by time when timed
    bool timed = false;         // false: plain text, shown statically
    CStringW title, artist, album; // [ti:], [ar:], [al:]

    void Clear();
    bool IsEmpty() const { return lines.empty(); }

    // Parses LRC (or plain) text. Returns false if no text was found.
    bool Parse(const CStringW& text);

    // Reads and parses a sidecar file (BOM, strict UTF-8, detected code page, ANSI)
    bool LoadFile(LPCWSTR path);

    // Index of the line current at rt, -1 before the first line or when untimed
    int LineAt(REFERENCE_TIME rt) const;

    // Decodes raw text file contents, see LoadFile
    static bool DecodeText(const BYTE* data, size_t size, CStringW& out);
};
