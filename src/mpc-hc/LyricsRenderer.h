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
// style, at a fixed size scaled by the DPI like UI text. Each line is rendered on its own into an
// image, which the view composes.
//
// Lines are written horizontally, or vertically (縦書き, tategaki): top to bottom, wrapping into
// columns right to left, in the vertical form of the tategaki font.
class CLyricsRenderer
{
public:
    enum class Layout {
        Panel, // lines wrapped across the panel and placed by the caller; the style's alignment and margins are unused
        Line,  // a line placed by the style (alignment, margins, placement override) in the whole view
    };

    // A rendered line, premultiplied ARGB
    struct Image {
        // Where the pixels go and the layout box (its lines, without borders or shadow), in render
        // coordinates: the view's for Line; for a horizontal Panel the view's horizontally, vertically
        // arbitrary; for a vertical Panel arbitrary. Place panel lines by their box.
        CRect rc = CRect(0, 0, 0, 0), rcBox = CRect(0, 0, 0, 0);
        std::vector<DWORD> bits; // rc.Width() * rc.Height()
    };

    CLyricsRenderer();
    ~CLyricsRenderer();
    CLyricsRenderer(const CLyricsRenderer&) = delete;
    CLyricsRenderer& operator=(const CLyricsRenderer&) = delete;

    // Call before GetLine with the current layout. Cheap when nothing changed; it also picks up
    // changed subtitle settings. The style is scaled by scale (the DPI) only, so a smaller view
    // shows fewer lines rather than smaller ones.
    // Panel lines wrap within [lineFrom, lineTo) of the view: horizontally, the view height being
    // unused; vertically, only the length being used. Line lines wrap within the style's margins.
    void Prepare(const Lyrics& lyrics, Layout layout, bool bVertical, CSize view, double scale,
                 int lineFrom = 0, int lineTo = 0);

    // Forgets the lyrics and the images, call when the lyrics are replaced
    void Reset();

    // The image of line i, nullptr when it shows nothing
    const Image* GetLine(int i);

    // Box size of line i without rendering it; a blank line's for lines without text
    CSize GetLineSize(int i);
    int GetLineHeight(int i) { return GetLineSize(i).cy; }

    // Frees the images of lines outside [first, last]
    void Retain(int first, int last);

    // Space between the boxes of consecutive panel lines: rows, or columns when vertical
    int GetLineGap() const { return m_lineGap; }

    // Sets the font of a style for vertical lines: the tategaki font setting (face, size, weight,
    // italic), or the style's own when it is not set, in its GDI vertical form ("@" prefixed)
    static void ApplyTategakiFont(STSStyle& style);

private:
    // Everything the images depend on
    struct Settings {
        Layout layout = Layout::Line;
        bool bVertical = false;
        CSize view = CSize(0, 0);
        int lineFrom = 0, lineTo = 0; // vertical Panel: 0 and the length, its position not mattering
        double scale = 1.0;
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

    static Settings CurrentSettings(Layout layout, bool bVertical, CSize view, double scale, int lineFrom, int lineTo);
    void Build(const Lyrics& lyrics);
    Slot& Measure(int i);
    void RenderLine(int i, Image& img);
    // Vertical Line: where the box of a line goes in the view
    CPoint PlaceVerticalLine(const CRect& rcBox) const;

    Settings m_settings;
    size_t m_nLines = 0;
    bool m_bBuilt = false;

    CCritSec m_csRTS; // required by RTS, which is only used from the UI thread; outlives it
    std::unique_ptr<CRenderedTextSubtitle> m_pRTS; // never handed out as a COM object
    std::vector<int> m_entries; // RTS entry of each line, -1 for an empty line
    std::vector<Slot> m_slots;
    CSize m_blankSize = CSize(0, 0); // box of a one-line entry, measured only

    // What RTS renders into: transparent is 0xFF000000, its alpha byte being the transparency
    CSize m_scratchSize = CSize(0, 0);
    std::vector<DWORD> m_scratch;

    int m_lineGap = 0;

    // Vertical Line: the view within the style's margins, and the style's alignment placing the box in it
    CRect m_lineArea = CRect(0, 0, 0, 0);
    int m_lineAlignment = 2;
};
