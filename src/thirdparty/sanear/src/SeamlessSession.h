#pragma once

#include "AudioDeviceManager.h"
#include "DspChunk.h"

namespace SaneAudioRenderer
{
    // A stream is a track, not a WASAPI stream. All mutable fields are protected
    // by its session lock. The renderer owns both objects; the session keeps only
    // weak references, so stopping a graph cannot create an ownership cycle.
    struct SeamlessStream final
    {
        std::deque<DspChunk> pending;
        size_t pendingFrames = 0;
        bool staged = false;
        bool canceled = false;
        bool eos = false;
        bool handedOff = false;
        uint64_t revision = 0;
        bool loggedFirstPcm = false;
        bool loggedReady = false;
        REFERENCE_TIME start = 0;
        REFERENCE_TIME end = -1;
    };

    class SeamlessSession final : public CCritSec
    {
    public:
        explicit SeamlessSession(ISettings* settings);
        ~SeamlessSession();

        std::shared_ptr<SeamlessStream> CreateStream();
        std::shared_ptr<SeamlessStream> PrepareNext(const std::shared_ptr<SeamlessStream>& current);
        void CancelNext();

        // The output format remains fixed for the lifetime of this session.
        AudioDevice* EnsureDevice(SharedWaveFormat inputFormat);
        bool Push(const std::shared_ptr<SeamlessStream>& stream, DspChunk& chunk,
                  CAMEvent& interrupt, CAMEvent* filled);
        bool Finish(const std::shared_ptr<SeamlessStream>& stream, CAMEvent& interrupt, CAMEvent* filled);
        void Play(const std::shared_ptr<SeamlessStream>& stream);
        void Pause(const std::shared_ptr<SeamlessStream>& stream);
        void SetOutputPaused(const std::shared_ptr<SeamlessStream>& stream, bool paused);
        void Reset(const std::shared_ptr<SeamlessStream>& stream);
        REFERENCE_TIME Position(const std::shared_ptr<SeamlessStream>& stream);
        bool HandedOff(const std::shared_ptr<SeamlessStream>& stream);
        void SetVolume(float volume);
        void SetBalance(float balance);

    private:
        void ApplyOutputGain(DspChunk& chunk);
        bool TryPushChunk(DspChunk& chunk, CAMEvent* filled);
        bool TryPushPending(SeamlessStream& stream, CAMEvent* filled);
        size_t PrerollReadyFrames() const;

        static constexpr UINT32 MinimumBufferMS = 200;
        static constexpr size_t PrerollSeconds = 1;
        static constexpr size_t ChunksPerSecond = 10;
        static constexpr DWORD QueueRetryMS = 5;
        static constexpr DWORD BoundaryPollMS = 2;

        HRESULT m_initResult = S_OK;
        AudioDeviceManager m_manager;
        ISettingsPtr m_settings;
        std::unique_ptr<AudioDevice> m_device;
        std::weak_ptr<SeamlessStream> m_active;
        std::weak_ptr<SeamlessStream> m_next;
        std::weak_ptr<SeamlessStream> m_committed;
        std::weak_ptr<SeamlessStream> m_predecessor;
        bool m_running = false;
        bool m_finished = false;
        bool m_canceledHandoff = false;
        float m_volume = 1.0f;
        float m_balance = 0.0f;
    };
}
