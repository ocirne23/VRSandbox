module UI;

import Core;
import :DSL;
import :ScriptBindings;

namespace
{
	using ST = DSLSymbol::SymbolType;

	// THE exposure table. One BindingFunc row exposes one engine function to scripts; emit templates ("$r" =
	// the object's generated-class member, "$1..$n" = arguments, "ctx." = the global context) are consumed by
	// M6's transpiler. Everything here maps to the existing ScriptContext ABI (ScriptAPI.h) -- entries that
	// would need DSL-side Entity VALUES (spawnEntity returning one, destroyEntity taking one) are deferred
	// until the DSL grows a first-class Entity value type.
	const std::vector<BindingObject>& bindingTable()
	{
		using T = DSLType;
		static const std::vector<BindingObject> table = {
			{ "self", T::Entity, DSLComponentKind::None, "self",
				{
					{ "setEnabled",     T::Void,  { { "enabled", T::Bool } },                        "$r.setEnabled($1)" },
					{ "setAnimFloat",   T::Void,  { { "param", T::String }, { "value", T::Float } }, "$r.setAnimFloat($1, $2)" },
					{ "setAnimBool",    T::Void,  { { "param", T::String }, { "value", T::Bool } },  "$r.setAnimBool($1, $2)" },
					{ "setAnimTrigger", T::Void,  { { "param", T::String } },                        "$r.setAnimTrigger($1)" },
					{ "getChildCount",  T::Int,   {},                                               "$r.getChildCount()" },
					{ "getBoundsRadius",T::Float, {},                                               "$r.getBoundsRadius()" },
				},
				{
					{ "pos", T::Vector3, "$r.pos" },
				} },
			{ "physics", T::PhysicsComponent, DSLComponentKind::Physics, "ctx->entityGetPhysicsComponent(self)",
				{
					{ "getVelocity",  T::Vector3, {},                                                     "$r.getVelocity()" },
					{ "setVelocity",  T::Void,    { { "velocity", T::Vector3 } },                         "$r.setVelocity($1)" },
					{ "applyImpulse", T::Void,    { { "impulse", T::Vector3 } },                          "$r.applyImpulse($1)" },
					{ "isAwake",      T::Bool,    {},                                                     "$r.isAwake()" },
					{ "teleport",     T::Void,    { { "position", T::Vector3 }, { "eulerDeg", T::Vector3 } }, "$r.teleport($1, $2)" },
				},
				{} },
			{ "audio", T::AudioComponent, DSLComponentKind::Audio, "ctx->entityGetAudioComponent(self)",
				{
					{ "trigger", T::Void, { { "alias", T::String } }, "$r.trigger($1)" },
					{ "stop",    T::Void, { { "alias", T::String } }, "$r.stop($1)" },
				},
				{} },
			{ "force", T::ForceComponent, DSLComponentKind::Force, "ctx->entityGetForceComponent(self)",
				{
					{ "getOutput",   T::Float, {},                          "$r.getOutput()" },
					{ "setOutput",   T::Void,  { { "output", T::Float } },  "$r.setOutput($1)" },
					{ "getReach",    T::Float, {},                          "$r.getReach()" },
					{ "setReach",    T::Void,  { { "reach", T::Float } },   "$r.setReach($1)" },
					{ "setTeam",     T::Void,  { { "team", T::Int } },      "$r.setTeam($1)" },
					{ "getPressure", T::Float, {},                          "$r.getPressure()" },
				},
				{} },
			// The Engine section: FREE calls in the DSL, ctx.* in generated C++. vec2/3/4 are the terse
			// positional constructors the editor's vector-literal flow builds against.
			{ nullptr, T::Void, DSLComponentKind::None, nullptr,
				{
					{ "print",           T::Void,  {},                                                       "ctx.log($1)" }, // vararg; M6 handles {} interpolation
					{ "rayCast",         T::Float, { { "pos", T::Vector3 }, { "dir", T::Vector3 }, { "maxRayDist", T::Float } }, "ctx.physicsRayCastDistance($1, $2, $3)" },
					{ "isKeyDown",       T::Bool,  { { "keyName", T::String } },                             "(ctx.isKeyDown($1) != 0)" },
					{ "sendEvent",       T::Void,  { { "eventName", T::String } },                           "ctx.sendEvent($1)" },
					{ "setSun",          T::Void,  { { "direction", T::Vector3 }, { "color", T::Vector3 }, { "intensity", T::Float } }, "ctx.setSun($1, $2, $3)" },
					{ "spawnPointLight", T::Void,  { { "position", T::Vector3 }, { "range", T::Float }, { "color", T::Vector3 }, { "intensity", T::Float } }, "ctx.spawnPointLight($1, $2, $3, $4)" },
					{ "vec2", T::Vector2, { { "x", T::Float }, { "y", T::Float } },                                     "glm::vec2($1, $2)",         /*isPositionalCall*/ true },
					{ "vec3", T::Vector3, { { "x", T::Float }, { "y", T::Float }, { "z", T::Float } },                  "glm::vec3($1, $2, $3)",     /*isPositionalCall*/ true },
					{ "vec4", T::Vector4, { { "x", T::Float }, { "y", T::Float }, { "z", T::Float }, { "w", T::Float } }, "glm::vec4($1, $2, $3, $4)", /*isPositionalCall*/ true },
				},
				{} },
		};
		return table;
	}

	DSLSymbol* addSymbol(std::vector<std::unique_ptr<DSLSymbol>>& container, ST type, DSLSymbol::Data data)
	{
		auto symbol = std::make_unique<DSLSymbol>();
		symbol->type = type;
		symbol->data = std::move(data);
		DSLSymbol* ptr = symbol.get();
		container.push_back(std::move(symbol));
		return ptr;
	}
}

void ScriptBindings::build(std::vector<std::unique_ptr<DSLSymbol>>& sidebarOut, std::vector<std::unique_ptr<DSLSymbol>>& builtinsOut)
{
	for (const BindingObject& object : bindingTable())
	{
		BuiltObject& built = m_built.emplace_back();
		built.def = &object;

		if (object.name != nullptr)
		{
			DSLSymbol* typeSymbol = addSymbol(sidebarOut, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ object.type });
			built.decl = addSymbol(sidebarOut, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ object.name, typeSymbol });
		}

		for (const BindingFunc& func : object.functions)
		{
			std::vector<DSLSymbol*> params;
			for (const BindingParam& param : func.params)
			{
				DSLSymbol* paramType = addSymbol(builtinsOut, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ param.type });
				params.push_back(addSymbol(builtinsOut, ST::VariableDeclaration,
					DSLSymbol::VariableDeclaration{ param.name, paramType, nullptr, param.isRef }));
			}
			DSLSymbol* funcSymbol = addSymbol(builtinsOut, ST::FunctionDeclaration, DSLSymbol::FunctionDeclaration{
				func.name, std::move(params), func.returnType, /*requiresReceiver*/ object.name != nullptr, func.isPositionalCall });
			built.functionSymbols.push_back(funcSymbol);
			m_emits.emplace_back(funcSymbol, func.emit);

			if (object.name == nullptr)
			{
				if (func.returnType == DSLType::Vector2)      m_vec2Func = funcSymbol;
				else if (func.returnType == DSLType::Vector3) m_vec3Func = funcSymbol;
				else if (func.returnType == DSLType::Vector4) m_vec4Func = funcSymbol;
			}
		}
	}
}

std::span<const BindingObject> ScriptBindings::objects() const
{
	return bindingTable();
}

const BindingObject* ScriptBindings::objectFor(DSLType type) const
{
	for (const BuiltObject& built : m_built)
		if (built.def->name != nullptr && built.def->type == type)
			return built.def;
	return nullptr;
}

DSLSymbol* ScriptBindings::objectDecl(const BindingObject& object) const
{
	for (const BuiltObject& built : m_built)
		if (built.def == &object)
			return built.decl;
	return nullptr;
}

const BindingObject* ScriptBindings::objectForDecl(const DSLSymbol* sidebarDecl) const
{
	for (const BuiltObject& built : m_built)
		if (built.decl != nullptr && built.decl == sidebarDecl)
			return built.def;
	return nullptr;
}

std::span<DSLSymbol* const> ScriptBindings::functionSymbols(const BindingObject& object) const
{
	for (const BuiltObject& built : m_built)
		if (built.def == &object)
			return built.functionSymbols;
	return {};
}

const BindingMember* ScriptBindings::findMember(DSLType receiverType, const std::string& name) const
{
	const BindingObject* object = objectFor(receiverType);
	if (object == nullptr)
		return nullptr;
	for (const BindingMember& member : object->members)
		if (name == member.name)
			return &member;
	return nullptr;
}

DSLSymbol* ScriptBindings::vectorBuiltin(DSLType vectorType) const
{
	switch (vectorType)
	{
	case DSLType::Vector2: return m_vec2Func;
	case DSLType::Vector3: return m_vec3Func;
	case DSLType::Vector4: return m_vec4Func;
	default:               return nullptr;
	}
}

const char* ScriptBindings::emitFor(const DSLSymbol* funcDecl) const
{
	for (const auto& [symbol, emit] : m_emits)
		if (symbol == funcDecl)
			return emit;
	return nullptr;
}
