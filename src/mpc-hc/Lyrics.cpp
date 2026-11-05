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
#include "Lyrics.h"
#include <mlang.h>

namespace
{
    constexpr LONGLONG kMaxLyricsFileSize = 1024 * 1024;

    bool IsDigit(wchar_t c)
    {
        return c >= L'0' && c <= L'9';
    }

    // Parses "mm:ss", "mm:ss.f", "mm:ss.ff", "mm:ss.fff" and "mm:ss:ff" into milliseconds.
    // Minutes may have any number of digits (up to 5).
    bool ParseTimestamp(LPCWSTR s, int len, LONGLONG& ms)
    {
        int i = 0, n = 0;
        LONGLONG minutes = 0;
        while (i < len && IsDigit(s[i])) {
            minutes = minutes * 10 + (s[i++] - L'0');
            if (++n > 5) {
                return false;
            }
        }
        if (n == 0 || i >= len || s[i] != L':') {
            return false;
        }
        i++;

        int seconds = 0;
        n = 0;
        while (i < len && IsDigit(s[i])) {
            seconds = seconds * 10 + (s[i++] - L'0');
            if (++n > 2) {
                return false;
            }
        }
        if (n == 0) {
            return false;
        }

        int frac = 0;
        if (i < len) {
            if (s[i] != L'.' && s[i] != L':') {
                return false;
            }
            i++;
            n = 0;
            while (i < len && IsDigit(s[i])) {
                frac = frac * 10 + (s[i++] - L'0');
                if (++n > 3) {
                    return false;
                }
            }
            if (n == 0 || i != len) {
                return false;
            }
            if (n == 1) {
                frac *= 100;
            } else if (n == 2) {
                frac *= 10;
            }
        }

        ms = (minutes * 60 + seconds) * 1000 + frac;
        return true;
    }

    // Removes enhanced LRC word timings such as "<00:12.34>"
    CStringW StripWordTimings(const CStringW& text)
    {
        if (text.Find(L'<') < 0) {
            return text;
        }
        CStringW out;
        const int len = text.GetLength();
        for (int i = 0; i < len;) {
            if (text[i] == L'<') {
                int close = text.Find(L'>', i + 1);
                LONGLONG ms;
                if (close > i && ParseTimestamp(text.GetString() + i + 1, close - i - 1, ms)) {
                    i = close + 1;
                    continue;
                }
            }
            out += text[i++];
        }
        return out;
    }

    bool IsHeaderKey(LPCWSTR s, int len)
    {
        if (len <= 0) {
            return false;
        }
        for (int i = 0; i < len; i++) {
            wchar_t c = s[i];
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'#')) {
                return false;
            }
        }
        return true;
    }

    bool DecodeCodePage(UINT cp, DWORD flags, const BYTE* data, size_t size, CStringW& out)
    {
        out.Empty();
        if (size == 0) {
            return true;
        }
        if (size > INT_MAX) {
            return false;
        }
        int n = MultiByteToWideChar(cp, flags, (LPCSTR)data, (int)size, nullptr, 0);
        if (n <= 0) {
            return false;
        }
        LPWSTR buf = out.GetBuffer(n);
        n = MultiByteToWideChar(cp, flags, (LPCSTR)data, (int)size, buf, n);
        out.ReleaseBuffer(std::max(n, 0));
        return n > 0;
    }

    // Guesses the code page of text that is not valid UTF-8 (Shift-JIS, GBK, Big5, ...).
    // Needs COM on the calling thread; returns 0 when unknown.
    UINT DetectCodePage(const BYTE* data, size_t size)
    {
        CComPtr<IMultiLanguage2> pML;
        if (FAILED(pML.CoCreateInstance(__uuidof(CMultiLanguage)))) {
            return 0;
        }
        DetectEncodingInfo info[8] = {};
        INT count = _countof(info);
        INT srcSize = (INT)std::min<size_t>(size, 256 * 1024);
        if (pML->DetectInputCodepage(MLDETECTCP_NONE, 0, (CHAR*)data, &srcSize, info, &count) != S_OK || count <= 0) {
            return 0;
        }
        int best = 0;
        for (int i = 1; i < count; i++) {
            if (info[i].nConfidence > info[best].nConfidence) {
                best = i;
            }
        }
        return info[best].nCodePage;
    }
}

void Lyrics::Clear()
{
    lines.clear();
    timed = false;
    title.Empty();
    artist.Empty();
    album.Empty();
}

bool Lyrics::Parse(const CStringW& input)
{
    Clear();

    std::vector<Line> timedLines, plainLines;
    LONGLONG offsetMs = 0;

    LPCWSTR p = input.GetString();
    int len = input.GetLength();
    if (len > 0 && p[0] == 0xFEFF) {
        p++;
        len--;
    }

    std::vector<LONGLONG> stamps;
    for (int pos = 0; pos < len;) {
        int end = pos;
        while (end < len && p[end] != L'\r' && p[end] != L'\n') {
            end++;
        }
        CStringW line(p + pos, end - pos);
        pos = (end + 1 < len && p[end] == L'\r' && p[end + 1] == L'\n') ? end + 2 : end + 1;

        line.TrimLeft();
        stamps.clear();
        bool header = false;
        int i = 0;
        while (i < line.GetLength() && line[i] == L'[') {
            int close = line.Find(L']', i + 1);
            if (close < 0) {
                break;
            }
            LPCWSTR tag = line.GetString() + i + 1;
            int tagLen = close - i - 1;
            LONGLONG ms;
            if (ParseTimestamp(tag, tagLen, ms)) {
                stamps.push_back(ms);
            } else {
                int colon = 0;
                while (colon < tagLen && tag[colon] != L':') {
                    colon++;
                }
                if (colon >= tagLen || !IsHeaderKey(tag, colon)) {
                    break; // not a tag, e.g. "[Chorus]": part of the text
                }
                CStringW key(tag, colon);
                CStringW value(tag + colon + 1, tagLen - colon - 1);
                key.MakeLower();
                value.Trim();
                if (key == L"ti") {
                    title = value;
                } else if (key == L"ar") {
                    artist = value;
                } else if (key == L"al") {
                    album = value;
                } else if (key == L"offset") {
                    offsetMs = _wtoi64(value);
                }
                header = true;
            }
            i = close + 1;
        }

        if (!stamps.empty()) {
            CStringW text = StripWordTimings(line.Mid(i));
            text.Trim();
            for (LONGLONG ms : stamps) {
                timedLines.push_back({ ms * 10000, text });
            }
        } else if (!header) {
            line.Trim();
            plainLines.push_back({ 0, line });
        }
    }

    if (!timedLines.empty()) {
        // a positive offset makes the lyrics appear earlier
        for (auto& l : timedLines) {
            l.t -= offsetMs * 10000;
        }
        std::stable_sort(timedLines.begin(), timedLines.end(), [](const Line& a, const Line& b) {
            return a.t < b.t;
        });
        lines = std::move(timedLines);
        timed = true;
    } else {
        while (!plainLines.empty() && plainLines.back().text.IsEmpty()) {
            plainLines.pop_back();
        }
        auto first = std::find_if(plainLines.begin(), plainLines.end(), [](const Line& l) {
            return !l.text.IsEmpty();
        });
        lines.assign(first, plainLines.end());
        timed = false;
    }

    bool hasText = std::any_of(lines.begin(), lines.end(), [](const Line& l) {
        return !l.text.IsEmpty();
    });
    if (!hasText) {
        lines.clear();
        timed = false;
    }
    return hasText;
}

int Lyrics::LineAt(REFERENCE_TIME rt) const
{
    if (!timed || lines.empty()) {
        return -1;
    }
    auto it = std::upper_bound(lines.begin(), lines.end(), rt, [](REFERENCE_TIME v, const Line& l) {
        return v < l.t;
    });
    return int(it - lines.begin()) - 1;
}

bool Lyrics::DecodeText(const BYTE* data, size_t size, CStringW& out)
{
    out.Empty();
    if (!data || !size) {
        return false;
    }

    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        return DecodeCodePage(CP_UTF8, 0, data + 3, size - 3, out);
    }
    if (size >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        const bool bigEndian = data[0] == 0xFE;
        const size_t count = (size - 2) / 2;
        if (count > INT_MAX) {
            return false;
        }
        LPWSTR buf = out.GetBuffer((int)count);
        for (size_t i = 0; i < count; i++) {
            BYTE b0 = data[2 + i * 2], b1 = data[3 + i * 2];
            buf[i] = bigEndian ? WCHAR((b0 << 8) | b1) : WCHAR((b1 << 8) | b0);
        }
        out.ReleaseBuffer((int)count);
        return true;
    }

    if (DecodeCodePage(CP_UTF8, MB_ERR_INVALID_CHARS, data, size, out)) {
        return true;
    }
    UINT cp = DetectCodePage(data, size);
    if (cp && cp != CP_UTF8 && IsValidCodePage(cp) && DecodeCodePage(cp, 0, data, size, out)) {
        return true;
    }
    return DecodeCodePage(CP_ACP, 0, data, size, out);
}

bool Lyrics::LoadFile(LPCWSTR path)
{
    Clear();

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::vector<BYTE> data;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(hFile, &size) && size.QuadPart > 0 && size.QuadPart <= kMaxLyricsFileSize;
    if (ok) {
        data.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = ReadFile(hFile, data.data(), (DWORD)data.size(), &read, nullptr) && read > 0;
        data.resize(read);
    }
    CloseHandle(hFile);

    CStringW text;
    return ok && DecodeText(data.data(), data.size(), text) && Parse(text);
}
