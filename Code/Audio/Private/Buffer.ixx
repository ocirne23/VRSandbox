export module Audio:Buffer;

import Core;

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
