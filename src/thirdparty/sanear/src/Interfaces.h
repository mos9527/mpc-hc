#pragma once

#include <comdef.h>
#include <dshow.h>
#include <ocidl.h>

namespace SaneAudioRenderer
{
    struct __declspec(uuid("ED41579C-C96A-4D8C-9813-856AB99F405E"))
    ISettings : IUnknown
    {
        STDMETHOD_(UINT32, GetSerial)() = 0;

        enum
        {
            OUTPUT_DEVICE_BUFFER_MIN_MS = 60,
            OUTPUT_DEVICE_BUFFER_MAX_MS = 1000,
            OUTPUT_DEVICE_BUFFER_DEFAULT_MS = 200,
        };
        STDMETHOD(SetOutputDevice)(LPCWSTR pDeviceId, BOOL bExclusive, UINT32 uBufferMS) = 0;
        STDMETHOD(GetOutputDevice)(LPWSTR* ppDeviceId, BOOL* pbExclusive, UINT32* puBufferMS) = 0;

        STDMETHOD_(void, SetCrossfeedEnabled)(BOOL bEnable) = 0;
        STDMETHOD_(BOOL, GetCrossfeedEnabled)() = 0;

        enum
        {
            CROSSFEED_CUTOFF_FREQ_MIN = 300,
            CROSSFEED_CUTOFF_FREQ_MAX = 2000,
            CROSSFEED_CUTOFF_FREQ_CMOY = 700,
            CROSSFEED_CUTOFF_FREQ_JMEIER = 650,
            CROSSFEED_LEVEL_MIN = 10,
            CROSSFEED_LEVEL_MAX = 150,
            CROSSFEED_LEVEL_CMOY = 60,
            CROSSFEED_LEVEL_JMEIER = 95,
        };
        STDMETHOD(SetCrossfeedSettings)(UINT32 uCutoffFrequency, UINT32 uCrossfeedLevel) = 0;
        STDMETHOD_(void, GetCrossfeedSettings)(UINT32* puCutoffFrequency, UINT32* puCrossfeedLevel) = 0;

        STDMETHOD_(void, SetIgnoreSystemChannelMixer)(BOOL bEnable) = 0;
        STDMETHOD_(BOOL, GetIgnoreSystemChannelMixer)() = 0;

        enum
        {
            TIMESTRETCH_METHOD_SOLA = 0,
            TIMESTRETCH_METHOD_PHASE_VOCODER = 1,
        };
        STDMETHOD(SetTimestretchSettings)(UINT32 uTimestretchMethod) = 0;
        STDMETHOD_(void, GetTimestretchSettings)(UINT32* puTimestretchMethod) = 0;
    };
    _COM_SMARTPTR_TYPEDEF(ISettings, __uuidof(ISettings));

    // In-process prototype control surface; never exposed by legacy sanear.
    struct __declspec(uuid("86434F93-C478-4B06-B447-CE0D5267332C"))
    ISeamlessAudioRenderer : IUnknown
    {
        STDMETHOD(PrepareNext)(ISeamlessAudioRenderer* next) = 0;
        STDMETHOD_(void, CancelNext)() = 0;
        STDMETHOD_(BOOL, HasHandedOff)() = 0;
        STDMETHOD(Activate)() = 0;
        STDMETHOD(GetPlaybackPosition)(REFERENCE_TIME* position) = 0;
        // Explicit host transport intent, unlike a filter's shutdown Pause.
        STDMETHOD(SetOutputPaused)(BOOL paused) = 0;
    };

    struct __declspec(uuid("03481710-D73E-4674-839F-03EDE2D60ED8"))
    ISpecifyPropertyPages2 : ISpecifyPropertyPages
    {
        STDMETHOD(CreatePage)(const GUID& guid, IPropertyPage** ppPage) = 0;
    };
    _COM_SMARTPTR_TYPEDEF(ISpecifyPropertyPages2, __uuidof(ISpecifyPropertyPages2));
}
