#include "pch.h"
#include "SeamlessSession.h"
#include "SeamlessTrace.h"
#include "Factory.h"

namespace SaneAudioRenderer
{
    SeamlessSession::SeamlessSession(ISettings* settings)
        : m_manager(m_initResult)
    {
        ThrowIfFailed(m_initResult);
        ThrowIfFailed(Factory::CreateSettings(&m_settings));
        LPWSTR deviceId = nullptr;
        BOOL exclusive = FALSE;
        UINT32 buffer = 0;
        ThrowIfFailed(settings->GetOutputDevice(&deviceId, &exclusive, &buffer));
        std::unique_ptr<WCHAR, CoTaskMemFreeDeleter> ownedId(deviceId);
        buffer = std::max<UINT32>(UINT32(MinimumBufferMS), buffer);
        // Prototype: shared-mode PCM only. Do not inherit exclusive/bitstream
        // settings, or open another endpoint while the next graph prerolls.
        ThrowIfFailed(m_settings->SetOutputDevice(deviceId, FALSE, buffer));
        m_settings->SetIgnoreSystemChannelMixer(FALSE);
        SeamlessTrace(L"session create", static_cast<const void*>(this), L"bufferMs", buffer,
                      L"shared event PCM requested");
    }

    SeamlessSession::~SeamlessSession()
    {
        SeamlessTrace(L"session destroy", static_cast<const void*>(this), L"device", static_cast<const void*>(m_device.get()));
        if (m_device)
            m_device->Stop();
    }

    std::shared_ptr<SeamlessStream> SeamlessSession::CreateStream()
    {
        CAutoLock lock(this);
        auto stream = std::make_shared<SeamlessStream>();
        m_active = stream;
        SeamlessTrace(L"stream create", static_cast<const void*>(stream.get()), L"session", static_cast<const void*>(this));
        return stream;
    }

    std::shared_ptr<SeamlessStream> SeamlessSession::PrepareNext(const std::shared_ptr<SeamlessStream>& current)
    {
        CAutoLock lock(this);
        if (m_finished || m_active.lock() != current || current->eos) {
            SeamlessTrace(L"prepare refused", static_cast<const void*>(current.get()), L"session", static_cast<const void*>(this),
                          L"finished", m_finished, L"activeMatches", m_active.lock() == current, L"decoderEOS", current->eos);
            return nullptr;
        }
        CancelNext();
        auto stream = std::make_shared<SeamlessStream>();
        stream->staged = true;
        m_next = stream;
        SeamlessTrace(L"next attached", static_cast<const void*>(stream.get()), L"current", static_cast<const void*>(current.get()),
                      L"session", static_cast<const void*>(this));
        return stream;
    }

    void SeamlessSession::CancelNext()
    {
        CAutoLock lock(this);
        auto cancel = [&](const std::shared_ptr<SeamlessStream>& next) {
            if (!next)
                return;
            SeamlessTrace(L"cancel stream", static_cast<const void*>(next.get()), L"staged", next->staged,
                          L"pendingFrames", next->pendingFrames, L"session", static_cast<const void*>(this));
            next->canceled = true;
            next->pending.clear();
            next->pendingFrames = 0;
            // A committed next track may already be feeding the device. A user
            // cancel must stop that audio too, not merely release its graph.
            if (m_active.lock() == next && m_device) {
                m_device->Stop();
                m_device->Reset();
                m_running = false;
                m_finished = true;
                m_canceledHandoff = true;
            }
        };
        cancel(m_next.lock());
        cancel(m_committed.lock());
        if (m_canceledHandoff) {
            if (auto previous = m_predecessor.lock()) {
                ++previous->revision;
                previous->handedOff = false;
                previous->eos = false;
                previous->start = 0;
                previous->end = -1;
                m_active = previous;
            }
        }
        m_next.reset();
        m_committed.reset();
        m_predecessor.reset();
    }

    AudioDevice* SeamlessSession::EnsureDevice(SharedWaveFormat inputFormat)
    {
        CAutoLock lock(this);
        if (!m_device) {
            const ULONGLONG started = GetTickCount64();
            SeamlessTrace(L"device create begin", static_cast<const void*>(this), L"inputRate", inputFormat->nSamplesPerSec,
                          L"inputChannels", inputFormat->nChannels, L"inputBits", inputFormat->wBitsPerSample);
            auto device = m_manager.CreateDevice(inputFormat, false, m_settings, true);
            if (!device) {
                SeamlessTrace(L"device create failed", static_cast<const void*>(this));
                throw HResultException{ E_FAIL };
            }
            // A push-mode device has no persistent software feeding queue.
            // Reject it rather than silently claiming gapless support.
            if (!device->GetWaveFormat() || device->IsExclusive() || !device->IsEventMode()) {
                SeamlessTrace(L"device rejected", L"exclusive", device->IsExclusive(), L"eventMode", device->IsEventMode());
                throw HResultException{ E_NOTIMPL };
            }
            SeamlessTrace(L"device ready", static_cast<const void*>(this), L"device", static_cast<const void*>(device.get()),
                          L"rate", device->GetRate(), L"channels", device->GetChannelCount(),
                          L"bufferMs", device->GetBufferDuration(), L"latencyMs", device->GetStreamLatency() / OneMillisecond,
                          L"eventMode", device->IsEventMode(), L"exclusive", device->IsExclusive(),
                          L"elapsedMs", GetTickCount64() - started);
            m_device = std::move(device);
            if (m_running)
                m_device->Start();
        }
        return m_device.get();
    }

    void SeamlessSession::SetVolume(float volume)
    {
        CAutoLock lock(this);
        m_volume = volume;
    }

    void SeamlessSession::SetBalance(float balance)
    {
        CAutoLock lock(this);
        m_balance = balance;
    }

    void SeamlessSession::ApplyOutputGain(DspChunk& chunk)
    {
        assert(CritCheckIn(this));
        const bool stereoBalance = m_balance != 0.0f && chunk.GetChannelCount() == 2;
        if (m_volume != 1.0f || stereoBalance) {
            DspChunk::ToFloat(chunk);
            auto data = reinterpret_cast<float*>(chunk.GetData());
            for (size_t i = 0, n = chunk.GetSampleCount(); i < n; ++i) {
                float gain = m_volume;
                if (stereoBalance && i % 2 == (m_balance < 0.0f ? 1u : 0u))
                    gain *= std::abs(m_balance);
                data[i] *= gain;
            }
        }
        DspChunk::ToFormat(m_device->GetDspFormat(), chunk);
    }

    size_t SeamlessSession::PrerollReadyFrames() const
    {
        const size_t rate = m_device->GetRate();
        return std::min(rate * PrerollSeconds, rate * m_device->GetBufferDuration() * 2 / 1000);
    }

    bool SeamlessSession::TryPushChunk(DspChunk& chunk, CAMEvent* filled)
    {
        assert(CritCheckIn(this));
        const size_t frames = std::min(chunk.GetFrameCount(),
            std::max<size_t>(1, m_device->GetRate() / ChunksPerSecond));
        DspChunk part(chunk.GetFormat(), chunk.GetChannelCount(), frames, chunk.GetRate());
        memcpy(part.GetData(), chunk.GetData(), part.GetSize());
        ApplyOutputGain(part);
        m_device->Push(part, filled);
        // AudioDeviceEvent leaves the entire chunk untouched when its queue is
        // full. Retry later without consuming input or applying gain twice.
        if (!part.IsEmpty())
            return false;
        chunk.ShrinkHead(chunk.GetFrameCount() - frames);
        return true;
    }

    bool SeamlessSession::TryPushPending(SeamlessStream& stream, CAMEvent* filled)
    {
        assert(CritCheckIn(this));
        auto& chunk = stream.pending.front();
        const size_t before = chunk.GetFrameCount();
        if (!TryPushChunk(chunk, filled))
            return false;
        stream.pendingFrames -= before - chunk.GetFrameCount();
        if (chunk.IsEmpty())
            stream.pending.pop_front();
        return true;
    }

    bool SeamlessSession::Push(const std::shared_ptr<SeamlessStream>& stream, DspChunk& chunk,
                               CAMEvent& interrupt, CAMEvent* filled)
    {
        while (!chunk.IsEmpty()) {
            if (interrupt.Wait(0))
                return false;
            {
                CAutoLock lock(this);
                if (stream->canceled || stream->eos || m_finished)
                    return false;
                if (!stream->loggedFirstPcm) {
                    stream->loggedFirstPcm = true;
                    SeamlessTrace(L"first PCM", static_cast<const void*>(stream.get()), L"session", static_cast<const void*>(this),
                                  L"staged", stream->staged, L"frames", chunk.GetFrameCount(), L"rate", chunk.GetRate());
                }
                const size_t rate = m_device->GetRate();
                if (stream->staged) {
                    const size_t capacity = rate * PrerollSeconds;
                    const size_t available = capacity > stream->pendingFrames ? capacity - stream->pendingFrames : 0;
                    if (available) {
                        const size_t frames = std::min(available, chunk.GetFrameCount());
                        DspChunk part(chunk.GetFormat(), chunk.GetChannelCount(), frames, chunk.GetRate());
                        memcpy(part.GetData(), chunk.GetData(), part.GetSize());
                        stream->pending.push_back(std::move(part));
                        stream->pendingFrames += frames;
                        chunk.ShrinkHead(chunk.GetFrameCount() - frames);
                        const size_t minimumFrames = PrerollReadyFrames();
                        if (!stream->loggedReady && stream->pendingFrames >= minimumFrames) {
                            stream->loggedReady = true;
                            SeamlessTrace(L"preroll PCM ready", static_cast<const void*>(stream.get()),
                                          L"pendingFrames", stream->pendingFrames, L"requiredFrames", minimumFrames);
                        }
                        if (filled)
                            filled->Set();
                        continue;
                    }
                } else if (m_active.lock() == stream) {
                    // Committed preroll always precedes newly decoded PCM. Never
                    // block or sleep while holding the session/device locks.
                    const bool progress = !stream->pending.empty() ? TryPushPending(*stream, filled) :
                                                                     TryPushChunk(chunk, filled);
                    if (progress)
                        continue;
                } else {
                    return false;
                }
            }
            if (interrupt.Wait(QueueRetryMS))
                return false;
        }
        return true;
    }

    bool SeamlessSession::Finish(const std::shared_ptr<SeamlessStream>& stream,
                                 CAMEvent& interrupt, CAMEvent* filled)
    {
        uint64_t revision;
        {
            CAutoLock lock(this);
            revision = stream->revision;
            SeamlessTrace(L"decoder EOS", static_cast<const void*>(stream.get()), L"staged", stream->staged,
                          L"pendingFrames", stream->pendingFrames, L"session", static_cast<const void*>(this));
        }
        // A very short prepared track can reach EOS before it becomes active.
        // Remember that fact, but do not finish the entire output device yet.
        for (;;) {
            if (interrupt.Wait(0))
                return false;
            {
                CAutoLock lock(this);
                if (stream->canceled || stream->revision != revision)
                    return false;
                stream->eos = true;
                if (!stream->staged) {
                    if (!m_device)
                        return true;
                    // A short staged track may have decoded all of its PCM
                    // before handoff. Submit its retained prefix before marking
                    // the end; a full device queue is backpressure, not failure.
                    if (!stream->pending.empty()) {
                        if (TryPushPending(*stream, filled))
                            continue;
                    } else {
                        stream->end = m_device->GetEnd();
                        auto next = m_next.lock();
                        const size_t minimumFrames = PrerollReadyFrames();
                        if (next && !next->canceled &&
                            (next->pendingFrames >= minimumFrames || (next->eos && next->pendingFrames))) {
                            SeamlessTrace(L"handoff committed", static_cast<const void*>(stream.get()), L"next", static_cast<const void*>(next.get()),
                                          L"session", static_cast<const void*>(this), L"boundaryMs", stream->end / OneMillisecond,
                                          L"positionMs", m_device->GetPosition() / OneMillisecond,
                                          L"nextFrames", next->pendingFrames, L"requiredFrames", minimumFrames);
                            next->start = stream->end;
                            next->staged = false;
                            stream->handedOff = true;
                            m_predecessor = stream;
                            m_active = next;
                            m_committed = next;
                            m_next.reset();
                            // Prime what fits now. The successor's streaming thread
                            // drains the rest in order as WASAPI frees queue space;
                            // waiting here would delay A's audible completion event.
                            if (!next->pending.empty())
                                TryPushPending(*next, filled);
                        } else {
                            SeamlessTrace(L"EOS fallback: no ready successor", static_cast<const void*>(stream.get()),
                                          L"next", static_cast<const void*>(next.get()), L"nextCanceled", next && next->canceled,
                                          L"nextFrames", next ? next->pendingFrames : 0, L"requiredFrames", minimumFrames,
                                          L"nextEOS", next && next->eos, L"boundaryMs", stream->end / OneMillisecond);
                            if (next) {
                                next->canceled = true;
                                next->pending.clear();
                                next->pendingFrames = 0;
                            }
                            m_next.reset();
                            m_finished = true;
                            m_device->Finish(filled);
                        }
                        break;
                    }
                }
            }
            if (interrupt.Wait(QueueRetryMS))
                return false;
        }
        // EC_COMPLETE remains an audible completion event, not decoder EOS.
        for (;;) {
            if (interrupt.Wait(BoundaryPollMS))
                return false;
            CAutoLock lock(this);
            if (stream->canceled || stream->revision != revision)
                return false;
            const REFERENCE_TIME position = m_device->GetPosition();
            if (position >= stream->end) {
                SeamlessTrace(L"audible boundary reached", static_cast<const void*>(stream.get()),
                              L"handedOff", stream->handedOff, L"boundaryMs", stream->end / OneMillisecond,
                              L"positionMs", position / OneMillisecond, L"deviceSilenceMs", m_device->GetSilence() / OneMillisecond);
                return true;
            }
        }
    }

    void SeamlessSession::Play(const std::shared_ptr<SeamlessStream>& stream)
    {
        CAutoLock lock(this);
        SeamlessTrace(L"play/activate", static_cast<const void*>(stream.get()), L"staged", stream->staged,
                      L"handedOff", stream->handedOff, L"session", static_cast<const void*>(this));
        if (!stream->staged && !stream->canceled && !stream->handedOff) {
            // The successor is now the visible graph. It is no longer pending.
            if (m_committed.lock() == stream) {
                m_committed.reset();
                m_predecessor.reset();
            }
            m_running = true;
            if (m_device)
                m_device->Start();
        }
    }

    void SeamlessSession::Pause(const std::shared_ptr<SeamlessStream>& stream)
    {
        CAutoLock lock(this);
        const bool ownsOutput = m_active.lock() == stream && !stream->staged &&
            !stream->canceled && !stream->handedOff;
        SeamlessTrace(L"pause", static_cast<const void*>(stream.get()), L"staged", stream->staged,
                      L"canceled", stream->canceled, L"handedOff", stream->handedOff, L"ownsOutput", ownsOutput);
        if (ownsOutput) {
            m_running = false;
            if (m_device)
                m_device->Stop();
        } else if (stream->handedOff) {
            // DirectShow may transition A through Pause while stopping it. Only
            // A's graph state changes: B already owns the persistent output.
            SeamlessTrace(L"retired graph pause ignored; successor output continues", static_cast<const void*>(stream.get()));
        }
    }

    void SeamlessSession::SetOutputPaused(const std::shared_ptr<SeamlessStream>& stream, bool paused)
    {
        CAutoLock lock(this);
        if (stream->canceled || (m_active.lock() != stream && m_predecessor.lock() != stream))
            return;
        // The visible graph can still be A for the buffered tail after commit.
        // Explicit host transport intent controls output; graph retirement does
        // not. Do not clear m_committed/m_predecessor here as Play/Activate does.
        SeamlessTrace(L"host output transport", L"paused", paused, L"visibleStream", static_cast<const void*>(stream.get()));
        m_running = !paused;
        if (m_device) {
            if (paused)
                m_device->Stop();
            else
                m_device->Start();
        }
    }

    void SeamlessSession::Reset(const std::shared_ptr<SeamlessStream>& stream)
    {
        CAutoLock lock(this);
        SeamlessTrace(L"flush/reset", static_cast<const void*>(stream.get()), L"staged", stream->staged,
                      L"canceled", stream->canceled, L"handedOff", stream->handedOff,
                      L"preserveOutput", stream->handedOff || stream->canceled, L"session", static_cast<const void*>(this));
        if (stream->canceled)
            return; // Discarding a canceled graph must not reset the restored active track.
        if (stream->handedOff)
            return; // Retiring graph A must not stop graph B's output.
        ++stream->revision;
        stream->loggedFirstPcm = false;
        stream->loggedReady = false;
        if (stream->staged) {
            stream->canceled = true;
            stream->pending.clear();
            stream->pendingFrames = 0;
            return;
        }
        CancelNext();
        if (m_device) {
            m_device->Stop();
            m_device->Reset();
        }
        stream->eos = false;
        stream->end = -1;
        stream->start = 0;
        m_finished = false;
        m_canceledHandoff = false;
        m_active = stream;
        if (m_running && m_device)
            m_device->Start();
    }

    REFERENCE_TIME SeamlessSession::Position(const std::shared_ptr<SeamlessStream>& stream)
    {
        CAutoLock lock(this);
        if (!m_device || stream->staged || stream->canceled)
            return 0;
        return std::max<REFERENCE_TIME>(0, m_device->GetPosition() - stream->start);
    }

    bool SeamlessSession::HandedOff(const std::shared_ptr<SeamlessStream>& stream)
    {
        CAutoLock lock(this);
        return stream->handedOff;
    }
}
