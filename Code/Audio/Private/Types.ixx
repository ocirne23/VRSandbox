export module Audio:Types;

import Core;

export enum class EAudioFormat : uint8
{
    Mono8,
    Mono16,
    Stereo8,         // stereo sounds skip HRTF spatialization (played as-is, listener-relative)
    Stereo16,
    MonoFloat32,
    StereoFloat32,
};
