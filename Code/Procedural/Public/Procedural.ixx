export module Procedural;

export import :Noise;
export import :TerrainSampler;
// The terrain generator (Private/Diffusion): it and the debug facade are public; every other Diffusion
// partition stays internal so ONNX Runtime and FastNoiseLite never reach this surface.
export import :GeneratorV3;
export import :Diffusion.Debug;
export import :TerrainChunk;
export import :TerrainGenerator;
export import :HeightMapBaker;
export import :TerrainStreamer;
export import :Scattering;
export import :OceanRenderer;
