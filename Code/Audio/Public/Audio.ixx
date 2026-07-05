export module Audio;

// miniaudio (output device, mixing, decoding) + Steam Audio (HRTF binaural spatialization). Both
// APIs stay private to this library: buffers and sources are referenced through RAII handles
// storing opaque pointers.

export import :Types;
export import :Buffer;
export import :Source;
export import :System;
