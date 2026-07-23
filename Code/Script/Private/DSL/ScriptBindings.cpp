module Script;

import Core;
import :DSL;
import :ScriptBindings;

namespace
{
	using ST = DSLSymbol::SymbolType;

	// The struct types' DSLType values are their table positions -- keep these three lists in sync.
	constexpr DSLType kVec2 = dslStructType(0);
	constexpr DSLType kVec3 = dslStructType(1);
	constexpr DSLType kVec4 = dslStructType(2);

	// THE struct table: engine-defined script value types. One row exposes a struct with its members, its
	// positional constructor, and member functions (whose emit templates may wrap free functions -- "$r" is the
	// receiver's own emitted expression, so chained accesses compose textually).
	const std::vector<BindingStruct>& structTable()
	{
		using T = DSLType;
		static const std::vector<BindingStruct> table = {
			{ "vec2", "glm::vec2", { { "x", T::Float }, { "y", T::Float } }, "glm::vec2($1, $2)",
				{
					{ "x", T::Float, "$r.x" },
					{ "y", T::Float, "$r.y" },
				},
				{
					{ "length",     T::Float, {},                       "glm::length($r)" },
					{ "normalized", kVec2,    {},                       "glm::normalize($r)" },
					{ "dot",        T::Float, { { "other", kVec2 } },   "glm::dot($r, $1)" },
					{ "distance",   T::Float, { { "other", kVec2 } },   "glm::distance($r, $1)" },
				} },
			{ "vec3", "glm::vec3", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float } }, "glm::vec3($1, $2, $3)",
				{
					{ "x", T::Float, "$r.x" },
					{ "y", T::Float, "$r.y" },
					{ "z", T::Float, "$r.z" },
				},
				{
					{ "length",     T::Float, {},                       "glm::length($r)" },
					{ "normalized", kVec3,    {},                       "glm::normalize($r)" },
					{ "dot",        T::Float, { { "other", kVec3 } },   "glm::dot($r, $1)" },
					{ "distance",   T::Float, { { "other", kVec3 } },   "glm::distance($r, $1)" },
				} },
			{ "vec4", "glm::vec4", { { "x", T::Float }, { "y", T::Float }, { "z", T::Float }, { "w", T::Float } }, "glm::vec4($1, $2, $3, $4)",
				{
					{ "x", T::Float, "$r.x" },
					{ "y", T::Float, "$r.y" },
					{ "z", T::Float, "$r.z" },
					{ "w", T::Float, "$r.w" },
				},
				{
					{ "length",     T::Float, {},                       "glm::length($r)" },
					{ "normalized", kVec4,    {},                       "glm::normalize($r)" },
					{ "dot",        T::Float, { { "other", kVec4 } },   "glm::dot($r, $1)" },
					{ "distance",   T::Float, { { "other", kVec4 } },   "glm::distance($r, $1)" },
				} },
		};
		return table;
	}

	// THE exposure table for binding OBJECTS + the Engine free-function section. One BindingFunc row exposes
	// one engine function; emit templates ("$r" = the object's generated-class member, "$1..$n" = arguments,
	// "ctx." = the global context) are consumed by M6's transpiler. Everything maps to the existing
	// ScriptContext ABI (ScriptAPI.h) -- entries needing DSL-side Entity VALUES (spawnEntity returning one)
	// stay deferred until Entity becomes a first-class value.
	const std::vector<BindingObject>& bindingTable()
	{
		using T = DSLType;
		static const std::vector<BindingObject> table = {
			// physics/audio/force are NOT sidebar-top-level -- they're reached only through self's own members
			// below (self.physics.applyImpulse(...)), never as a bare "physics" identifier.
			{ "self", T::Entity, "self", /*sidebarTopLevel*/ true,
				{
					{ "setEnabled",     T::Void,  { { "enabled", T::Bool } },                        "$r.setEnabled($1)" },
					{ "setAnimFloat",   T::Void,  { { "param", T::String }, { "value", T::Float } }, "$r.setAnimFloat($1, $2)" },
					{ "setAnimBool",    T::Void,  { { "param", T::String }, { "value", T::Bool } },  "$r.setAnimBool($1, $2)" },
					{ "setAnimTrigger", T::Void,  { { "param", T::String } },                        "$r.setAnimTrigger($1)" },
					{ "getChildCount",  T::Int,   {},                                               "$r.getChildCount()" },
					{ "getBoundsRadius",T::Float, {},                                               "$r.getBoundsRadius()" },
				},
				{
					{ "pos",     kVec3,               "$r.pos" },
					{ "physics", T::PhysicsComponent, "$r.physics", /*writable*/ false, DSLComponentKind::Physics },
					{ "audio",   T::AudioComponent,   "$r.audio",   /*writable*/ false, DSLComponentKind::Audio },
					{ "force",   T::ForceComponent,   "$r.force",   /*writable*/ false, DSLComponentKind::Force },
				} },
			{ "physics", T::PhysicsComponent, "ctx->entityGetPhysicsComponent(self)", /*sidebarTopLevel*/ false,
				{
					{ "getVelocity",  kVec3,   {},                                          "$r.getVelocity()" },
					{ "setVelocity",  T::Void, { { "velocity", kVec3 } },                   "$r.setVelocity($1)" },
					{ "applyImpulse", T::Void, { { "impulse", kVec3 } },                    "$r.applyImpulse($1)" },
					{ "isAwake",      T::Bool, {},                                          "$r.isAwake()" },
					{ "teleport",     T::Void, { { "position", kVec3 }, { "eulerDeg", kVec3 } }, "$r.teleport($1, $2)" },
				},
				{} },
			{ "audio", T::AudioComponent, "ctx->entityGetAudioComponent(self)", /*sidebarTopLevel*/ false,
				{
					{ "trigger", T::Void, { { "alias", T::String } }, "$r.trigger($1)" },
					{ "stop",    T::Void, { { "alias", T::String } }, "$r.stop($1)" },
				},
				{} },
			{ "force", T::ForceComponent, "ctx->entityGetForceComponent(self)", /*sidebarTopLevel*/ false,
				{
					{ "getOutput",   T::Float, {},                          "$r.getOutput()" },
					{ "setOutput",   T::Void,  { { "output", T::Float } },  "$r.setOutput($1)" },
					{ "getReach",    T::Float, {},                          "$r.getReach()" },
					{ "setReach",    T::Void,  { { "reach", T::Float } },   "$r.setReach($1)" },
					{ "setTeam",     T::Void,  { { "team", T::Int } },      "$r.setTeam($1)" },
					{ "getPressure", T::Float, {},                          "$r.getPressure()" },
				},
				{} },
			// The Engine section: FREE calls in the DSL, ctx.* in generated C++.
			{ nullptr, T::Void, nullptr, /*sidebarTopLevel*/ false,
				{
					{ "print",           T::Void,  {},                                                       "ctx.log($1)" }, // vararg; M6 handles {} interpolation
					{ "rayCast",         T::Float, { { "pos", kVec3 }, { "dir", kVec3 }, { "maxRayDist", T::Float } }, "ctx.physicsRayCastDistance($1, $2, $3)" },
					{ "isKeyDown",       T::Bool,  { { "keyName", T::String } },                             "(ctx.isKeyDown($1) != 0)" },
					{ "sendEvent",       T::Void,  { { "eventName", T::String } },                           "ctx.sendEvent($1)" },
					{ "setSun",          T::Void,  { { "direction", kVec3 }, { "color", kVec3 }, { "intensity", T::Float } }, "ctx.setSun($1, $2, $3)" },
					{ "spawnPointLight", T::Void,  { { "position", kVec3 }, { "range", T::Float }, { "color", kVec3 }, { "intensity", T::Float } }, "ctx.spawnPointLight($1, $2, $3, $4)" },
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
	// One FunctionDeclaration builtin per exposed function -- what the editor/loader resolve against. Shared
	// by binding-object functions, struct member functions (requiresReceiver=true), and struct constructors
	// (free positional calls named like the struct itself).
	auto buildFunction = [&](const char* name, DSLType returnType, const std::vector<BindingParam>& params,
		const char* emit, bool requiresReceiver, bool isPositionalCall) -> DSLSymbol*
	{
		std::vector<DSLSymbol*> built;
		for (const BindingParam& param : params)
		{
			DSLSymbol* paramType = addSymbol(builtinsOut, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ param.type });
			built.push_back(addSymbol(builtinsOut, ST::VariableDeclaration,
				DSLSymbol::VariableDeclaration{ param.name, paramType, nullptr, param.isRef }));
		}
		DSLSymbol* funcSymbol = addSymbol(builtinsOut, ST::FunctionDeclaration, DSLSymbol::FunctionDeclaration{
			name, std::move(built), returnType, requiresReceiver, isPositionalCall });
		m_emits.emplace_back(funcSymbol, emit);
		return funcSymbol;
	};

	for (size_t i = 0; i < structTable().size(); ++i)
	{
		const BindingStruct& def = structTable()[i];
		BuiltStruct& built = m_builtStructs.emplace_back();
		built.def = &def;
		built.constructorFunc = buildFunction(def.name, dslStructType(static_cast<int>(i)), def.constructorParams,
			def.constructorEmit, /*requiresReceiver*/ false, /*isPositionalCall*/ true);
		for (const BindingFunc& func : def.functions)
			built.functionSymbols.push_back(buildFunction(func.name, func.returnType, func.params, func.emit,
				/*requiresReceiver*/ true, func.isPositionalCall));
	}

	for (const BindingObject& object : bindingTable())
	{
		BuiltObject& built = m_built.emplace_back();
		built.def = &object;
		if (object.name != nullptr && object.sidebarTopLevel)
		{
			DSLSymbol* typeSymbol = addSymbol(sidebarOut, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ object.type });
			built.decl = addSymbol(sidebarOut, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ object.name, typeSymbol });
		}
		for (const BindingFunc& func : object.functions)
			built.functionSymbols.push_back(buildFunction(func.name, func.returnType, func.params, func.emit,
				/*requiresReceiver*/ object.name != nullptr, func.isPositionalCall));
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
	const std::vector<BindingMember>* members = nullptr;
	if (const BindingObject* object = objectFor(receiverType); object != nullptr)
		members = &object->members;
	else if (const BindingStruct* structDef = structFor(receiverType); structDef != nullptr)
		members = &structDef->members;
	if (members == nullptr)
		return nullptr;
	for (const BindingMember& member : *members)
		if (name == member.name)
			return &member;
	return nullptr;
}

std::span<const BindingStruct> ScriptBindings::structs() const
{
	return structTable();
}

const BindingStruct* ScriptBindings::structFor(DSLType type) const
{
	if (!dslIsStructType(type))
		return nullptr;
	const int index = dslStructIndex(type);
	if (index < 0 || index >= static_cast<int>(m_builtStructs.size()))
		return nullptr;
	return m_builtStructs[index].def;
}

DSLType ScriptBindings::structTypeByName(const std::string& name) const
{
	for (size_t i = 0; i < m_builtStructs.size(); ++i)
		if (name == m_builtStructs[i].def->name)
			return dslStructType(static_cast<int>(i));
	return DSLType::Void;
}

std::span<DSLSymbol* const> ScriptBindings::structFunctionSymbols(DSLType type) const
{
	if (!dslIsStructType(type))
		return {};
	const int index = dslStructIndex(type);
	if (index < 0 || index >= static_cast<int>(m_builtStructs.size()))
		return {};
	return m_builtStructs[index].functionSymbols;
}

const char* ScriptBindings::emitFor(const DSLSymbol* funcDecl) const
{
	for (const auto& [symbol, emit] : m_emits)
		if (symbol == funcDecl)
			return emit;
	return nullptr;
}
