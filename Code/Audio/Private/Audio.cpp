module;

#pragma warning(push, 3)
#pragma warning(disable: 4701 4702 4245 4244 4310)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_GENERATION
#define MA_NO_ENCODING
#include <miniaudio/miniaudio.h>
#pragma warning(pop)

#include <phonon/phonon.h>

module Audio;

import Core;
import Core.glm;
import Core.Log;
import Core.Tweaks;

constexpr uint32 MaxOneShotSources = 32;
constexpr uint32 IplFrameSize = 256; // HRTF processing block (~5.3ms latency at 48kHz)
constexpr uint32 SampleRate = 48000;
constexpr float SpeedOfSound = 343.0f;

static IPLVector3 toIpl(const glm::vec3& v) { return IPLVector3{ v.x, v.y, v.z }; }

namespace
{
    struct SoundData
    {
        std::vector<float> frames; // interleaved f32
        uint32 channels = 1;
        uint32 sampleRate = SampleRate;
        uint32 frameCount() const { return uint32(frames.size() / channels); }
    };

    // Custom miniaudio node applying Steam Audio's HRTF: stereo in (downmixed to mono) -> binaural
    // stereo out. Steam Audio processes fixed-size blocks while the node graph asks for arbitrary
    // frame counts, so input/output are staged through one-block FIFOs.
    struct BinauralNode
    {
        ma_node_base base; // must be first
        IPLBinauralEffect effect = nullptr;
        std::atomic<float> dirX{ 0.0f }, dirY{ 0.0f }, dirZ{ -1.0f }; // listener-space unit direction to the source
        std::atomic<float> blend{ 1.0f };                             // 0 = unspatialized passthrough (relative sources)

        float inFifo[IplFrameSize] = {};           // downmixed mono, waiting for a full block
        uint32 inCount = 0;
        float outBlock[IplFrameSize * 2] = {};     // interleaved stereo of the last processed block
        uint32 outRead = 0, outAvail = 0;
        float scratchL[IplFrameSize] = {}, scratchR[IplFrameSize] = {};
    };

    struct SourceState
    {
        ma_sound sound;
        ma_audio_buffer buffer;
        BinauralNode node;
        bool soundValid = false;
        bool nodeValid = false;
        bool spatial = false; // routed through the binaural node
        const SoundData* data = nullptr;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        float gain = 1.0f;
        float pitch = 1.0f;
        bool relative = false;
        bool looping = false;
        float refDist = 1.0f;
        float maxDist = FLT_MAX;
        float rolloff = 1.0f;
    };

    struct SystemState
    {
        ma_engine engine;
        IPLContext iplContext = nullptr;
        IPLHRTF hrtf = nullptr;
        IPLAudioSettings iplAudioSettings = { SampleRate, (IPLint32)IplFrameSize };
        std::vector<std::unique_ptr<SoundData>> buffers;
        std::vector<std::unique_ptr<SourceState>> sources;
        glm::vec3 listenerPos = glm::vec3(0.0f);
        glm::vec3 listenerFwd = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 listenerUp = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 listenerVel = glm::vec3(0.0f);
    };
}

static SystemState g_state;

static void binauralNodeProcess(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
{
    BinauralNode& node = *reinterpret_cast<BinauralNode*>(pNode);
    const float* in = ppFramesIn ? ppFramesIn[0] : nullptr;
    float* out = ppFramesOut[0];
    const uint32 inAvail = (pFrameCountIn && in) ? *pFrameCountIn : 0;
    const uint32 outWanted = *pFrameCountOut;

    uint32 framesIn = 0;
    uint32 framesOut = 0;
    while (framesOut < outWanted)
    {
        if (node.outAvail > 0) // drain the staged output block first
        {
            out[framesOut * 2 + 0] = node.outBlock[node.outRead * 2 + 0];
            out[framesOut * 2 + 1] = node.outBlock[node.outRead * 2 + 1];
            node.outRead++;
            node.outAvail--;
            framesOut++;
            continue;
        }
        while (node.inCount < IplFrameSize && framesIn < inAvail) // stage input (stereo -> mono downmix)
        {
            node.inFifo[node.inCount++] = (in[framesIn * 2 + 0] + in[framesIn * 2 + 1]) * 0.5f;
            framesIn++;
        }
        if (node.inCount < IplFrameSize)
            break; // not enough input for a full block yet

        IPLBinauralEffectParams params{};
        params.direction = IPLVector3{ node.dirX.load(std::memory_order_relaxed), node.dirY.load(std::memory_order_relaxed), node.dirZ.load(std::memory_order_relaxed) };
        params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
        params.spatialBlend = node.blend.load(std::memory_order_relaxed);
        params.hrtf = g_state.hrtf;
        float* inChannels[1] = { node.inFifo };
        IPLAudioBuffer inBuffer{ 1, (IPLint32)IplFrameSize, inChannels };
        float* outChannels[2] = { node.scratchL, node.scratchR };
        IPLAudioBuffer outBuffer{ 2, (IPLint32)IplFrameSize, outChannels };
        iplBinauralEffectApply(node.effect, &params, &inBuffer, &outBuffer);

        for (uint32 i = 0; i < IplFrameSize; ++i)
        {
            node.outBlock[i * 2 + 0] = node.scratchL[i];
            node.outBlock[i * 2 + 1] = node.scratchR[i];
        }
        node.outRead = 0;
        node.outAvail = IplFrameSize;
        node.inCount = 0;
    }
    for (; framesOut < outWanted; ++framesOut) // upstream dry: pad with silence
    {
        out[framesOut * 2 + 0] = 0.0f;
        out[framesOut * 2 + 1] = 0.0f;
    }
    *pFrameCountOut = outWanted;
    if (pFrameCountIn)
        *pFrameCountIn = framesIn;
}

static ma_node_vtable g_binauralNodeVTable = { binauralNodeProcess, nullptr, 1, 1, 0 };

// Recomputes the HRTF direction, distance attenuation and doppler for a source from the current
// listener state. Game thread; the audio thread picks the values up through the node's atomics.
static void updateSource(SourceState& s)
{
    if (!s.soundValid)
        return;
    float attenuation = 1.0f;
    float doppler = 1.0f;
    if (s.spatial)
    {
        if (s.relative)
        {
            s.node.blend.store(0.0f, std::memory_order_relaxed);
        }
        else
        {
            const glm::vec3 toSource = s.position - g_state.listenerPos;
            const float dist = glm::length(toSource);
            const IPLVector3 dir = iplCalculateRelativeDirection(g_state.iplContext,
                toIpl(s.position), toIpl(g_state.listenerPos), toIpl(g_state.listenerFwd), toIpl(g_state.listenerUp));
            s.node.dirX.store(dir.x, std::memory_order_relaxed);
            s.node.dirY.store(dir.y, std::memory_order_relaxed);
            s.node.dirZ.store(dir.z, std::memory_order_relaxed);
            s.node.blend.store(1.0f, std::memory_order_relaxed);

            const float clamped = glm::clamp(dist, s.refDist, s.maxDist);
            attenuation = s.refDist / (s.refDist + s.rolloff * (clamped - s.refDist));

            if (dist > 0.001f)
            {
                const glm::vec3 d = toSource / dist;
                const float vListener = glm::dot(g_state.listenerVel, d); // toward the source
                const float vSource = -glm::dot(s.velocity, d);           // toward the listener
                doppler = glm::clamp((SpeedOfSound + vListener) / glm::max(SpeedOfSound - vSource, 1.0f), 0.5f, 2.0f);
            }
        }
    }
    ma_sound_set_volume(&s.sound, s.gain * attenuation);
    ma_sound_set_pitch(&s.sound, s.pitch * doppler);
}

static void clearSourceSound(SourceState& s)
{
    if (!s.soundValid)
        return;
    ma_sound_uninit(&s.sound);
    ma_audio_buffer_uninit(&s.buffer);
    s.soundValid = false;
    s.spatial = false;
    s.data = nullptr;
}

static void destroySourceState(SourceState& s)
{
    clearSourceSound(s);
    if (s.nodeValid)
    {
        ma_node_uninit(&s.node.base, nullptr);
        iplBinauralEffectRelease(&s.node.effect);
        s.nodeValid = false;
    }
}

static SourceState* toState(uint64 handle) { return reinterpret_cast<SourceState*>(handle); }

void AudioBuffer::destroy()
{
    if (m_handle == 0)
        return;
    if (Globals::audio.isInitialized()) // the engine may already be gone at shutdown
        Globals::audio.detachBuffer(m_handle);
    m_handle = 0;
}

void AudioSource::destroy()
{
    if (m_handle == 0)
        return;
    if (Globals::audio.isInitialized())
        Globals::audio.releaseSource(m_handle);
    m_handle = 0;
}

void AudioSource::setBuffer(const AudioBuffer& buffer)
{
    SourceState& s = *toState(m_handle);
    clearSourceSound(s);
    const SoundData* data = reinterpret_cast<const SoundData*>(buffer.m_handle);
    if (!data || data->frames.empty())
        return;

    ma_audio_buffer_config config = ma_audio_buffer_config_init(ma_format_f32, data->channels, data->frameCount(), data->frames.data(), nullptr);
    config.sampleRate = data->sampleRate;
    if (ma_audio_buffer_init(&config, &s.buffer) != MA_SUCCESS)
        return;
    if (ma_sound_init_from_data_source(&g_state.engine, &s.buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &s.sound) != MA_SUCCESS)
    {
        ma_audio_buffer_uninit(&s.buffer);
        return;
    }
    s.soundValid = true;
    s.data = data;
    s.spatial = data->channels == 1;

    if (s.spatial)
    {
        if (!s.nodeValid)
        {
            IPLBinauralEffectSettings effectSettings{ g_state.hrtf };
            if (iplBinauralEffectCreate(g_state.iplContext, &g_state.iplAudioSettings, &effectSettings, &s.node.effect) == IPL_STATUS_SUCCESS)
            {
                ma_node_config nodeConfig = ma_node_config_init();
                ma_uint32 inChannels = 2, outChannels = 2;
                nodeConfig.vtable = &g_binauralNodeVTable;
                nodeConfig.pInputChannels = &inChannels;
                nodeConfig.pOutputChannels = &outChannels;
                if (ma_node_init(ma_engine_get_node_graph(&g_state.engine), &nodeConfig, nullptr, &s.node.base) == MA_SUCCESS)
                {
                    ma_node_attach_output_bus(&s.node.base, 0, ma_engine_get_endpoint(&g_state.engine), 0);
                    s.nodeValid = true;
                }
                else
                {
                    iplBinauralEffectRelease(&s.node.effect);
                }
            }
        }
        if (s.nodeValid)
        {
            iplBinauralEffectReset(s.node.effect);
            ma_node_attach_output_bus(&s.sound, 0, &s.node.base, 0);
        }
        else
        {
            s.spatial = false; // HRTF unavailable: fall back to plain playback
        }
    }
    ma_sound_set_looping(&s.sound, s.looping ? MA_TRUE : MA_FALSE);
    updateSource(s);
}

void AudioSource::play()
{
    SourceState& s = *toState(m_handle);
    if (!s.soundValid)
        return;
    if (ma_sound_is_playing(&s.sound))
        ma_sound_seek_to_pcm_frame(&s.sound, 0);
    ma_sound_start(&s.sound);
}

void AudioSource::pause()
{
    SourceState& s = *toState(m_handle);
    if (s.soundValid)
        ma_sound_stop(&s.sound);
}

void AudioSource::stop()
{
    SourceState& s = *toState(m_handle);
    if (!s.soundValid)
        return;
    ma_sound_stop(&s.sound);
    ma_sound_seek_to_pcm_frame(&s.sound, 0);
}

bool AudioSource::isPlaying() const
{
    if (m_handle == 0)
        return false;
    SourceState& s = *toState(m_handle);
    return s.soundValid && ma_sound_is_playing(&s.sound) == MA_TRUE;
}

void AudioSource::setLooping(bool loop)
{
    SourceState& s = *toState(m_handle);
    s.looping = loop;
    if (s.soundValid)
        ma_sound_set_looping(&s.sound, loop ? MA_TRUE : MA_FALSE);
}

void AudioSource::setGain(float gain)
{
    SourceState& s = *toState(m_handle);
    s.gain = gain;
    updateSource(s);
}

void AudioSource::setPitch(float pitch)
{
    SourceState& s = *toState(m_handle);
    s.pitch = pitch;
    updateSource(s);
}

void AudioSource::setPosition(const glm::vec3& position)
{
    SourceState& s = *toState(m_handle);
    s.position = position;
    updateSource(s);
}

void AudioSource::setVelocity(const glm::vec3& velocity)
{
    SourceState& s = *toState(m_handle);
    s.velocity = velocity;
    updateSource(s);
}

void AudioSource::setRelative(bool relative)
{
    SourceState& s = *toState(m_handle);
    s.relative = relative;
    updateSource(s);
}

void AudioSource::setAttenuation(float referenceDistance, float maxDistance, float rolloff)
{
    SourceState& s = *toState(m_handle);
    s.refDist = glm::max(referenceDistance, 0.001f);
    s.maxDist = maxDistance;
    s.rolloff = rolloff;
    updateSource(s);
}

bool AudioSystem::initialize()
{
    assert(!m_initialized);
    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.channels = 2;
    engineConfig.sampleRate = SampleRate;
    if (ma_engine_init(&engineConfig, &g_state.engine) != MA_SUCCESS)
    {
        Log::error("Audio: failed to open the default output device");
        return false;
    }

    IPLContextSettings contextSettings{};
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
    if (iplContextCreate(&contextSettings, &g_state.iplContext) != IPL_STATUS_SUCCESS)
    {
        Log::error("Audio: failed to create the Steam Audio context");
        ma_engine_uninit(&g_state.engine);
        return false;
    }
    IPLHRTFSettings hrtfSettings{};
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1.0f;
    if (iplHRTFCreate(g_state.iplContext, &g_state.iplAudioSettings, &hrtfSettings, &g_state.hrtf) != IPL_STATUS_SUCCESS)
    {
        Log::error("Audio: failed to create the Steam Audio HRTF");
        iplContextRelease(&g_state.iplContext);
        ma_engine_uninit(&g_state.engine);
        return false;
    }
    m_initialized = true;

    ma_engine_set_volume(&g_state.engine, m_masterVolume);
    Tweak::floatVar("Audio/System", "Master Volume", &m_masterVolume, 0.0f, 2.0f, 0.01f,
        [this]() { if (m_initialized) ma_engine_set_volume(&g_state.engine, m_masterVolume); });

    const ma_device* device = ma_engine_get_device(&g_state.engine);
    Log::info("Audio: miniaudio + Steam Audio HRTF on '" + std::string(device ? device->playback.name : "unknown") + "'");
    return true;
}

void AudioSystem::shutdown()
{
    if (!m_initialized)
        return;
    m_oneShotPool.clear(); // releases its sources from the registry
    for (std::unique_ptr<SourceState>& source : g_state.sources)
        destroySourceState(*source); // sources still held by user handles; those handles no-op after this
    g_state.sources.clear();
    g_state.buffers.clear();
    m_initialized = false;
    ma_engine_uninit(&g_state.engine);
    iplHRTFRelease(&g_state.hrtf);
    iplContextRelease(&g_state.iplContext);
}

void AudioSystem::setListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up, const glm::vec3& velocity)
{
    if (!m_initialized)
        return;
    g_state.listenerPos = position;
    g_state.listenerFwd = forward;
    g_state.listenerUp = up;
    g_state.listenerVel = velocity;
    for (std::unique_ptr<SourceState>& source : g_state.sources)
        updateSource(*source);
}

AudioBuffer AudioSystem::createBuffer(EAudioFormat format, std::span<const std::byte> pcmData, uint32 sampleRate)
{
    if (!m_initialized || pcmData.empty() || sampleRate == 0)
        return {};

    const bool stereo = format == EAudioFormat::Stereo8 || format == EAudioFormat::Stereo16 || format == EAudioFormat::StereoFloat32;
    std::unique_ptr<SoundData> data = std::make_unique<SoundData>();
    data->channels = stereo ? 2 : 1;
    data->sampleRate = sampleRate;

    switch (format)
    {
    case EAudioFormat::Mono8:
    case EAudioFormat::Stereo8:
    {
        const uint8* samples = reinterpret_cast<const uint8*>(pcmData.data());
        data->frames.resize(pcmData.size());
        for (size_t i = 0; i < pcmData.size(); ++i)
            data->frames[i] = (float(samples[i]) - 128.0f) / 128.0f;
        break;
    }
    case EAudioFormat::Mono16:
    case EAudioFormat::Stereo16:
    {
        const int16* samples = reinterpret_cast<const int16*>(pcmData.data());
        const size_t count = pcmData.size() / sizeof(int16);
        data->frames.resize(count);
        for (size_t i = 0; i < count; ++i)
            data->frames[i] = float(samples[i]) / 32768.0f;
        break;
    }
    case EAudioFormat::MonoFloat32:
    case EAudioFormat::StereoFloat32:
    {
        const size_t count = pcmData.size() / sizeof(float);
        data->frames.resize(count);
        memcpy(data->frames.data(), pcmData.data(), count * sizeof(float));
        break;
    }
    }
    if (data->frames.empty())
        return {};

    AudioBuffer buffer;
    buffer.m_handle = reinterpret_cast<uint64>(data.get());
    buffer.m_durationSec = float(data->frameCount()) / float(sampleRate);
    g_state.buffers.push_back(std::move(data));
    return buffer;
}

AudioBuffer AudioSystem::loadSound(std::string_view path)
{
    if (!m_initialized)
        return {};
    const std::string pathStr(path);
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0); // native channels/rate
    ma_decoder decoder;
    if (ma_decoder_init_file(pathStr.c_str(), &decoderConfig, &decoder) != MA_SUCCESS)
    {
        Log::error("Audio: could not open '" + pathStr + "'");
        return {};
    }
    ma_format format = ma_format_unknown;
    ma_uint32 channels = 0, sampleRate = 0;
    ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0);
    if (channels != 1 && channels != 2)
    {
        Log::error("Audio: '" + pathStr + "' has " + std::to_string(channels) + " channels, only mono/stereo is supported");
        ma_decoder_uninit(&decoder);
        return {};
    }

    std::vector<float> frames;
    float chunk[4096];
    const ma_uint64 chunkFrames = 4096 / channels;
    for (;;)
    {
        ma_uint64 framesRead = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk, chunkFrames, &framesRead);
        frames.insert(frames.end(), chunk, chunk + framesRead * channels);
        if (result != MA_SUCCESS || framesRead < chunkFrames)
            break;
    }
    ma_decoder_uninit(&decoder);
    if (frames.empty())
    {
        Log::error("Audio: '" + pathStr + "' contains no audio data");
        return {};
    }
    return createBuffer(channels == 2 ? EAudioFormat::StereoFloat32 : EAudioFormat::MonoFloat32,
        std::as_bytes(std::span(frames)), sampleRate);
}

AudioSource AudioSystem::createSource()
{
    if (!m_initialized)
        return {};
    g_state.sources.push_back(std::make_unique<SourceState>());
    return AudioSource(reinterpret_cast<uint64>(g_state.sources.back().get()));
}

void AudioSystem::releaseSource(uint64 sourceHandle)
{
    SourceState* state = toState(sourceHandle);
    for (size_t i = 0; i < g_state.sources.size(); ++i)
    {
        if (g_state.sources[i].get() == state)
        {
            destroySourceState(*state);
            g_state.sources.erase(g_state.sources.begin() + i);
            return;
        }
    }
}

void AudioSystem::detachBuffer(uint64 bufferHandle)
{
    const SoundData* data = reinterpret_cast<const SoundData*>(bufferHandle);
    for (std::unique_ptr<SourceState>& source : g_state.sources)
        if (source->data == data)
            clearSourceSound(*source);
    std::erase_if(g_state.buffers, [&](const std::unique_ptr<SoundData>& b) { return b.get() == data; });
}

AudioSource& AudioSystem::acquireOneShotSource()
{
    for (AudioSource& source : m_oneShotPool)
        if (!source.isPlaying())
            return source;
    if (m_oneShotPool.size() < MaxOneShotSources)
        return m_oneShotPool.emplace_back(createSource());
    return m_oneShotPool[m_oneShotSteal++ % MaxOneShotSources]; // all busy: steal round-robin
}

void AudioSystem::playOneShot(const AudioBuffer& buffer, const glm::vec3& position, float gain, float pitch)
{
    if (!m_initialized || !buffer.isValid())
        return;
    AudioSource& source = acquireOneShotSource();
    if (!source.isValid())
        return;
    SourceState& s = *toState(source.m_handle);
    s.relative = false;
    s.position = position;
    s.gain = gain;
    s.pitch = pitch;
    s.looping = false;
    source.setBuffer(buffer);
    source.play();
}

void AudioSystem::playOneShot2D(const AudioBuffer& buffer, float gain, float pitch)
{
    if (!m_initialized || !buffer.isValid())
        return;
    AudioSource& source = acquireOneShotSource();
    if (!source.isValid())
        return;
    SourceState& s = *toState(source.m_handle);
    s.relative = true;
    s.gain = gain;
    s.pitch = pitch;
    s.looping = false;
    source.setBuffer(buffer);
    source.play();
}
