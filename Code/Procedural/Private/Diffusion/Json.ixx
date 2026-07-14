export module Procedural:Diffusion.Json;

import Core;

// Minimal JSON reader, just enough for the two model config files the diffusion pipeline ships
// (world_pipeline_config.json, pipeline_data.json). It replaces the reference's Gson dependency.
//
// Deliberately small: the engine's own asset format (File:AssetParser) can't be used because these files
// come verbatim from the pinned HuggingFace revision and are hash-verified, so they must be read as-is.
export namespace Procedural::Diffusion
{
	class JsonValue
	{
	public:
		enum class EType : uint8
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object
		};

		EType type = EType::Null;
		bool boolean = false;
		double number = 0.0;
		std::string str;
		std::vector<JsonValue> arr;
		// Insertion-ordered; these objects have a handful of keys so a linear find is the right call.
		std::vector<std::pair<std::string, JsonValue>> obj;

		bool isNull() const { return type == EType::Null; }

		// Object member lookup, nullptr when absent. Note a present-but-null member returns a non-null
		// JsonValue with type Null — config files here use that (histogram_raw: null), and the two cases
		// mean different things.
		const JsonValue* find(std::string_view key) const;

		double asNumber(double fallback = 0.0) const { return type == EType::Number ? number : fallback; }
		float asFloat(float fallback = 0.0f) const { return type == EType::Number ? (float)number : fallback; }
		int32 asInt(int32 fallback = 0) const { return type == EType::Number ? (int32)number : fallback; }

		// Flatten a numeric array into floats. Returns false if this isn't an array of numbers, or if
		// expectedCount >= 0 and the length disagrees.
		bool asFloatArray(std::vector<float>& out, int32 expectedCount = -1) const;
		// Flatten an array-of-numeric-arrays (e.g. the [5][64] quantile tables) row-major into `out`.
		bool asFloatArray2D(std::vector<float>& out, int32 expectedRows, int32 expectedCols) const;

		// Parses `text`. On failure returns false and fills `error` with a message including the offset.
		static bool parse(std::string_view text, JsonValue& out, std::string& error);
	};
}
