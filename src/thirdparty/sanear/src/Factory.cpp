#include "pch.h"
#include "Factory.h"

#include "MyFilter.h"
#include "Settings.h"

namespace SaneAudioRenderer
{
    HRESULT Factory::CreateSettings(ISettings** ppOut)
    {
        IUnknownPtr unknown;
        ReturnIfFailed(CreateSettingsAggregated(nullptr, &unknown));
        return unknown->QueryInterface(IID_PPV_ARGS(ppOut));
    }

    HRESULT Factory::CreateSettingsAggregated(IUnknown* pOwner, IUnknown** ppOut)
    {
        CheckPointer(ppOut, E_POINTER);

        *ppOut = nullptr;

        auto pSettings = new(std::nothrow) Settings(pOwner);

        if (!pSettings)
            return E_OUTOFMEMORY;

        pSettings->NonDelegatingAddRef();

        HRESULT result = pSettings->NonDelegatingQueryInterface(IID_PPV_ARGS(ppOut));

        pSettings->NonDelegatingRelease();

        return result;
    }

    HRESULT Factory::CreateFilter(ISettings* pSettings, IBaseFilter** ppOut)
    {
        IUnknownPtr unknown;
        ReturnIfFailed(CreateFilterAggregated(nullptr, GetFilterGuid(), pSettings, &unknown));
        return unknown->QueryInterface(IID_PPV_ARGS(ppOut));
    }

    HRESULT Factory::CreateFilterAggregated(IUnknown* pOwner, const GUID& guid,
                                            ISettings* pSettings, IUnknown** ppOut)
    {
        CheckPointer(ppOut, E_POINTER);
        CheckPointer(pSettings, E_POINTER);

        *ppOut = nullptr;

        auto pFilter = new(std::nothrow) MyFilter(pOwner, guid);

        if (!pFilter)
            return E_OUTOFMEMORY;

        pFilter->NonDelegatingAddRef();

        HRESULT result = pFilter->Init(pSettings);

        if (SUCCEEDED(result))
            result = pFilter->NonDelegatingQueryInterface(IID_PPV_ARGS(ppOut));

        pFilter->NonDelegatingRelease();

        return result;
    }

    HRESULT Factory::CreateSeamlessFilter(ISettings* pSettings, IBaseFilter** ppOut)
    {
        CheckPointer(ppOut, E_POINTER);
        *ppOut = nullptr;
        IUnknownPtr unknown;
        ReturnIfFailed(CreateFilterAggregated(nullptr, GetSeamlessFilterGuid(), pSettings, &unknown));
        return unknown->QueryInterface(IID_PPV_ARGS(ppOut));
    }

    const GUID& Factory::GetSeamlessFilterGuid()
    {
        static const GUID guid = {0x513a8b4a, 0xe4bc, 0x45e8, {0x9e, 0x54, 0xbb, 0x04, 0x9e, 0x6a, 0x2f, 0x59}};
        return guid;
    }

    const GUID& Factory::GetFilterGuid()
    {
        static const GUID guid = {0x2AE00773, 0x819A, 0x40FB, {0xA5, 0x54, 0x54, 0x82, 0x7E, 0x11, 0x63, 0x59}};
        return guid;
    }
}
