#pragma once

#include <windows.h>
#include <sstream>
#include <utility>

namespace SaneAudioRenderer
{
    inline void SeamlessTraceAppend(std::wostringstream&) {}

    template <typename First, typename... Rest>
    inline void SeamlessTraceAppend(std::wostringstream& out, First&& first, Rest&&... rest)
    {
        out << L" " << std::forward<First>(first);
        SeamlessTraceAppend(out, std::forward<Rest>(rest)...);
    }

    // The opt-in prototype calls this only at lifecycle milestones, not for
    // every sample. Do not gate these messages on TRACE_SANEAR or NDEBUG.
    template <typename... Args>
    inline void SeamlessTrace(Args&&... args)
    {
        if (!IsDebuggerPresent())
            return;
        try {
            std::wostringstream out;
            out << L"[Seamless] tick=" << GetTickCount64() << L" tid=" << GetCurrentThreadId();
            SeamlessTraceAppend(out, std::forward<Args>(args)...);
            out << L"\n";
            OutputDebugStringW(out.str().c_str());
        } catch (...) {
            OutputDebugStringW(L"[Seamless] failed to format lifecycle diagnostic\n");
        }
    }
}
