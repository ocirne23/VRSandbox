export module Core.Tweaks;

import Core;
import Core.glm;

// A lightweight, global registry of "tweakable" variables. Any module can register a
// pointer to a live variable together with how it should be presented; the UI's TweakPanel
// iterates the registry and renders the appropriate widget. Pointers are non-owning: the
// registered variable must outlive the registration (use globals / long-lived members).

export enum class ETweakType : uint8
{
	Float,   // single float, slider (bounded) or drag (unbounded)
	Float2,
	Float3,
	Float4,
	Color3,  // rgb color picker (+ optional intensity)
	Color4,  // rgba color picker
	Bool,
	Enum,    // int index into enumNames
};

export struct TweakVar
{
	std::string_view name;
	std::string_view category;
	ETweakType       type = ETweakType::Float;
	void*            data = nullptr;     // non-owning, points at the live variable

	float            min   = 0.0f;
	float            max   = 1.0f;
	float            speed = 0.01f;      // drag step for unbounded floats

	float*           intensity = nullptr;                 // optional, Color3 only
	std::span<const std::string_view> enumNames;          // Enum only

	std::function<void()> onChange;                       // optional, fired when the value changes

	bool isUnbounded() const { return max >= FLT_MAX * 0.5f; }
};

export class TweakRegistry
{
public:

	static TweakRegistry& get()
	{
		static TweakRegistry instance;
		return instance;
	}

	void registerVar(const TweakVar& var) { m_vars.push_back(var); }

	const std::vector<TweakVar>& vars() const { return m_vars; }

private:

	std::vector<TweakVar> m_vars;
};

// ---- Convenience registration helpers --------------------------------------------------
// Pass a string range of "0-inf" by using FLT_MAX as the max (renders as a drag instead of a slider).

export namespace Tweak
{
	inline void floatVar(std::string_view category, std::string_view name, float* value, float min = 0.0f, float max = 1.0f, float speed = 0.01f, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Float, value, min, max, speed };
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}

	inline void float2(std::string_view category, std::string_view name, glm::vec2* value, float speed = 0.01f, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Float2, value, 0.0f, FLT_MAX, speed };
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}

	inline void float3(std::string_view category, std::string_view name, glm::vec3* value, float speed = 0.01f, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Float3, value, 0.0f, FLT_MAX, speed };
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}

	inline void float4(std::string_view category, std::string_view name, glm::vec4* value, float speed = 0.01f, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Float4, value, 0.0f, FLT_MAX, speed };
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}

	inline void color3(std::string_view category, std::string_view name, glm::vec3* color, float* intensity = nullptr, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Color3, color };
		var.intensity = intensity;
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}

	inline void color4(std::string_view category, std::string_view name, glm::vec4* color, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Color4, color };
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}

	inline void boolean(std::string_view category, std::string_view name, bool* value, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Bool, value };
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}

	inline void enumVar(std::string_view category, std::string_view name, int* value, std::span<const std::string_view> names, std::function<void()> onChange = {})
	{
		TweakVar var{ name, category, ETweakType::Enum, value };
		var.enumNames = names;
		var.onChange = std::move(onChange);
		TweakRegistry::get().registerVar(var);
	}
}
