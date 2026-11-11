/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_AUDIO_AUDIO_DEVICE_H
#define UNISCOPE_AUDIO_AUDIO_DEVICE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uniscope::audio {

// A complete, self-contained RIFF/WAVE image (16-bit PCM, mono or stereo).
//
// One-shots travel as finished images because the caller owns the mixing
// policy: gains are already baked in, and a backend only has to hand the
// bytes to whatever plays them (OpenAL, PlaySound, ...). The type is
// deliberately just the byte container, so a caller can keep owning its
// decoded assets without copying them into a backend-specific wrapper.
using AudioClip = std::vector<std::uint8_t>;

// One decoded chunk of 16-bit signed, little-endian PCM, interleaved by
// channel. Frames, not samples: frameCount * channels is the number of
// int16 values in the buffer.
struct AudioPcm {
    const std::int16_t* samples = nullptr;
    std::size_t frameCount = 0;
    int sampleRate = 0;
    // 1 (mono) or 2 (stereo); any other value is rejected by the voice.
    int channels = 0;
};

struct MusicPlaybackInfo {
    std::string path;
    std::string displayTitle;
    bool hasTrack = false;
    bool playing = false;
    bool paused = false;
    bool finishedNaturally = false;
    // Whether the track repeats when it ends. A story's music plays once:
    // a scene that waits for the music to end cannot wait for a loop.
    bool looping = false;
    float positionSeconds = 0.0f;
    float durationSeconds = 0.0f;
};

// A single streaming playback channel owned by a backend.
//
// The chunk queue is intentionally small and backend-neutral: a streaming
// decoder decodes a chunk, queues it, and later asks how many queued chunks
// have been consumed so it can reclaim them. Implementations copy the PCM
// into their own storage, so the caller may reuse its chunk buffer as soon
// as queue() returns.
class IAudioStreamVoice {
public:
    virtual ~IAudioStreamVoice() = default;

    // Queues one decoded chunk. False when the chunk was rejected (bad
    // format, exhausted resources) or the voice is not initialised.
    virtual bool queue(const AudioPcm& pcm) = 0;

    // How many queued chunks have played to their end and can be reclaimed.
    virtual std::size_t reclaimableChunks() const = 0;

    // Removes one reclaimed chunk. Call at most reclaimableChunks() times.
    virtual bool reclaimOne() = 0;

    virtual bool play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;

    // Stops and discards every queued chunk, returning the voice to the
    // state it had before the first queue().
    virtual void clear() = 0;

    virtual bool playing() const = 0;
    virtual bool paused() const = 0;
    virtual std::size_t queuedChunks() const = 0;

    // Offset into the chunk that is currently sounding, in seconds.
    virtual float positionSeconds() const = 0;

    virtual void setGain(float gain) = 0;
};

// Platform-agnostic playback device. Higher layers own all mixing and
// volume policy; a device only renders finished audio and, when it can,
// reports the state of its single music channel.
//
// Implementations must be safe to call from the thread that owns them. A
// device that cannot emit sound still has to implement the interface, so
// callers never need a null check: available() is the one question.
class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;

    // True when the device can actually emit sound on this platform.
    virtual bool available() const = 0;

    // True when this device has a real streaming music channel: transport
    // commands, position and natural-end reporting all answer. False for a
    // device that plays a whole file and cannot be asked anything about it.
    virtual bool supportsStreamingMusic() const = 0;

    // Uploads one-shot clips ahead of time. Optional; a device that does not
    // cache may ignore it.
    virtual void prewarm(const std::vector<AudioClip>& clips) { (void)clips; }

    // Plays a one-shot clip. The device copies or owns whatever it needs for
    // as long as playback requires.
    virtual void playOneShot(const AudioClip& clip) = 0;

    // True when the very same clip is still sounding. The front end uses this
    // so a long recording is not restarted mid-tail; a device without voice
    // tracking answers false, which keeps the old restart-always behaviour.
    virtual bool oneShotStillPlaying(const AudioClip& clip) const = 0;

    // Stops everything immediately.
    virtual void stopAll() = 0;

    // Music channel. Passing an empty path stops playback.
    virtual void playMusic(const std::string& path, bool loop) = 0;
    virtual void stopMusic() = 0;

    // Per-frame service for streaming and position reporting.
    virtual void update() = 0;

    virtual void setMusicGain(float gain) = 0;
    virtual void setUiGain(float gain) = 0;
    virtual void setMusicLooping(bool loop) = 0;
    virtual MusicPlaybackInfo musicInfo() const = 0;

    virtual bool toggleMusicPause() = 0;
    virtual void pauseMusic() = 0;
    virtual void resumeMusic() = 0;
    virtual bool restartMusic() = 0;
    virtual bool seekMusicNormalized(float progress) = 0;
    virtual bool seekMusicSeconds(float seconds) = 0;

    // The output device this channel is open on. An implementation that has
    // no choice of output returns an empty name and rejects setOutputDevice.
    virtual const std::string& outputDeviceName() const = 0;
    virtual bool outputDeviceConnected() const = 0;
    virtual bool setOutputDevice(std::string deviceName) = 0;

    // Releases any thread-affine handle (an OpenAL context, for example)
    // before the device is handed to another thread. Optional.
    virtual void releaseThreadContext() const {}
};

} // namespace uniscope::audio

#endif
