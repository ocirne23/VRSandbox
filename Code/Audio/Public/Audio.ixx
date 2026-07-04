export module Audio;

import Core;
import Core.glm;

// miniaudio (output device, mixing, decoding) + Steam Audio (HRTF binaural spatialization). Both
// APIs stay private to this library: buffers and sources are referenced through RAII handles
// storing opaque pointers.

export enum class EAudioFormat : uint8
{
    Mono8,
    Mono16,
    Stereo8,         // stereo sounds skip HRTF spatialization (played as-is, listener-relative)
    Stereo16,
    MonoFloat32,
    StereoFloat32,
};

// A loaded PCM sound. Sources reference it, so it must outlive every source playing it (destroying
// a buffer stops those sources). Movable RAII, like PhysicsMesh.
export class AudioBuffer final
{
public:

    AudioBuffer() = default;
    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;
    AudioBuffer(AudioBuffer&& other) noexcept : m_handle(other.m_handle), m_durationSec(other.m_durationSec) { other.m_handle = 0; }
    AudioBuffer& operator=(AudioBuffer&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            m_handle = other.m_handle;
            m_durationSec = other.m_durationSec;
            other.m_handle = 0;
        }
        return *this;
    }
    ~AudioBuffer() { destroy(); }

    bool isValid() const { return m_handle != 0; }
    void destroy();

    float getDurationSec() const { return m_durationSec; }

private:

    friend class AudioSystem;
    friend class AudioSource;
    uint64 m_handle = 0; // SoundData*, 0 = invalid
    float m_durationSec = 0.0f;
};

// RAII handle to a playable sound instance. Mono sounds are spatialized through the Steam Audio
// HRTF (position set via setPosition); call setRelative(true) for 2D/UI playback instead.
export class AudioSource final
{
public:

    AudioSource() = default;
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;
    AudioSource(AudioSource&& other) noexcept : m_handle(other.m_handle) { other.m_handle = 0; }
    AudioSource& operator=(AudioSource&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            m_handle = other.m_handle;
            other.m_handle = 0;
        }
        return *this;
    }
    ~AudioSource() { destroy(); }

    bool isValid() const { return m_handle != 0; }
    void destroy();

    void setBuffer(const AudioBuffer& buffer); // stops the source if it was playing another buffer

    void play(); // restarts from the beginning if already playing, resumes if paused
    void pause();
    void stop();
    bool isPlaying() const;

    void setLooping(bool loop);
    void setGain(float gain);   // linear, 1 = as recorded
    void setPitch(float pitch); // playback rate multiplier, 1 = as recorded
    void setPosition(const glm::vec3& position);
    void setVelocity(const glm::vec3& velocity); // for doppler only, position is not integrated
    void setRelative(bool relative);             // listener-relative: no HRTF/attenuation (2D/UI sounds)

    // Inverse-clamped distance attenuation: full volume inside referenceDistance, no further
    // attenuation beyond maxDistance, rolloff scales how fast gain drops in between.
    void setAttenuation(float referenceDistance, float maxDistance, float rolloff = 1.0f);

private:

    friend class AudioSystem;
    explicit AudioSource(uint64 handle) : m_handle(handle) {}

    uint64 m_handle = 0; // SourceState*, 0 = invalid
};

export class AudioSystem final
{
public:

    ~AudioSystem() { shutdown(); }

    bool initialize(); // opens the default output device (48kHz stereo) and creates the Steam Audio HRTF
    void shutdown();

    // Call once per frame with the active camera: updates every source's HRTF direction, distance
    // attenuation and doppler. Master volume is a Tweak under Audio/System.
    void setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up,
        const glm::vec3& velocity = glm::vec3(0.0f));

    AudioBuffer createBuffer(EAudioFormat format, std::span<const std::byte> pcmData, uint32 sampleRate);
    AudioBuffer loadSound(std::string_view path); // WAV/FLAC/MP3, relative to Assets/

    AudioSource createSource();

    // Fire-and-forget playback from an internal source pool (steals the oldest slot when full).
    void playOneShot(const AudioBuffer& buffer, const glm::vec3& position, float gain = 1.0f, float pitch = 1.0f);
    void playOneShot2D(const AudioBuffer& buffer, float gain = 1.0f, float pitch = 1.0f);

    bool isInitialized() const { return m_initialized; }

private:

    friend class AudioBuffer;
    friend class AudioSource;

    AudioSource& acquireOneShotSource();
    void detachBuffer(uint64 bufferHandle);  // a buffer is being destroyed: stop + detach sources playing it
    void releaseSource(uint64 sourceHandle);

    bool m_initialized = false;
    float m_masterVolume = 1.0f;
    uint32 m_oneShotSteal = 0;
    std::vector<AudioSource> m_oneShotPool;
};

export namespace Globals
{
    AudioSystem audio;
}
