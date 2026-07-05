module Audio;

import :Buffer;
import :System;

void AudioBuffer::destroy()
{
    if (m_handle == 0)
        return;
    if (Globals::audio.isInitialized()) // the engine may already be gone at shutdown
        Globals::audio.detachBuffer(m_handle);
    m_handle = 0;
}
