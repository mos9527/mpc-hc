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

#include <memory>
#include <vector>
#include "../Subtitles/STSStyle.h"

class Lyrics;
class CRenderedTextSubtitle;

// Renders lyrics lines with the internal subtitle renderer (RTS) in the player's default subtitle
// style, as if the view were the video: they look as subtitles do over a video of the view's size.
// Each line is rendered on its own into an image, which the view composes.
class CLyricsRenderer
{
public:
    enum class Layout {
        Panel, // lines wrapped in a column and placed by the caller; the style's alignment and margins are unused
        Line,  // a line placed by the style (alignment, margins, placement override) over the whole view
    };

    // A rendered line, premultiplied ARGB
    struct Image {
        // Where the pixels go and the layout box (its lines, without borders or shadow), in render
        // coordinates: the view's for Line; for Panel the view's horizontally, vertically arbitrary,
        // so place it by its box.
        CRect rc = CRect(0, 0, 0, 0), rcBox = CRect(0, 0, 0, 0);
        std::vector<DWORD> bits; // rc.Width() * rc.Height()
    };

    CLyricsRenderer();
    ~CLyricsRenderer();
    CLyricsRenderer(const CLyricsRenderer&) = delete;
    CLyricsRenderer& operator=(const CLyricsRenderer&) = delete;

    // Call before GetLine with the current layout. Cheap when nothing changed; it also picks up
    // changed subtitle settings. Line scales the style to the view, as subtitles to the video.
    // Panel scales it by panelScale (the DPI) only, the view's height is unused; its lines wrap
    // within [columnLeft, columnRight) of the view.
    void Prepare(const Lyrics& lyrics, Layout layout, CSize view, int columnLeft = 0, int columnRight = 0, double panelScale = 1.0);

    // Forgets the lyrics and the images, call when the lyrics are replaced
    void Reset();

    // The image of line i, nullptr when it shows nothing
    const Image* GetLine(int i);

    // Box height of line i without rendering it; a blank line's for lines without text
    int GetLineHeight(int i);

    // Frees the images of lines outside [first, last]
    void Retain(int first, int last);

    // Space between the boxes of consecutive panel lines
    int GetLineGap() const { return m_lineGap; }

private:
    // Everything the images depend on
    struct Settings {
        Layout layout = Layout::Line;
        CSize view = CSize(0, 0);
        int columnLeft = 0, columnRight = 0;
        double panelScale = 1.0;
        STSStyle style;
        double fontScale = 1.0;
        bool bUseFreeType = false;
        bool bRoundedBox = false;
        bool bOverridePlacement = false;
        int nHorPos = 0, nVerPos = 0;
        CStringA langHint;

        bool operator==(const Settings& s) const;
        bool operator!=(const Settings& s) const { return !(*this == s); }
    };

    struct Slot {
        bool bMeasured = false;
        CRect rcBox = CRect(0, 0, 0, 0);
        bool bRendered = false; // possibly to nothing
        Image img;
    };

    static Settings CurrentSettings(Layout layout, CSize view, int columnLeft, int columnRight, double panelScale);
    void Build(const Lyrics& lyrics);
    Slot& Measure(int i);
    void RenderLine(int i, Image& img);

    Settings m_settings;
    size_t m_nLines = 0;
    bool m_bBuilt = false;

    CCritSec m_csRTS; // required by RTS, which is only used from the UI thread; outlives it
    std::unique_ptr<CRenderedTextSubtitle> m_pRTS; // never handed out as a COM object
    std::vector<int> m_entries; // RTS entry of each line, -1 for an empty line
    std::vector<Slot> m_slots;
    int m_blankHeight = 0;      // box height of a one-line entry, measured only

    // What RTS renders into: transparent is 0xFF000000, its alpha byte being the transparency
    CSize m_scratchSize = CSize(0, 0);
    std::vector<DWORD> m_scratch;

    int m_lineGap = 0;
};
