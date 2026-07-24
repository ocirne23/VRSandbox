export module Script:ScriptBindings;

import Core;
import :DSL;

// The engine-exposure registry for the DSL: the ONE table describing what scripts can touch. Exposing an
// engine function that already exists on the ScriptContext ABI (Code/Script/Public/ScriptAPI.h) costs exactly
// one BindingFunc row here; exposing brand-new C++ additionally costs the ABI's own minimal trio (X-macro row +
// thunk + ctor init line in Code/Entity/Private/ScriptContext.cpp).
//
// The transpilation model (Transpiler.cpp): each DSL function becomes its own free `SCRIPT_EXPORT` function,
// like the node system's generated .scr code -- no wrapper class, no per-component member. `ctx`/`self` are
// auto-injected as every generated function's first two parameters (invisible to the DSL author), so a
// binding's `emit` template can reference them directly ("$r" = the receiver's own emitted expression, "$1..$n"
// = arguments): self's own functions/members call straight through the real ctx-> thunks (e.g.
// "ctx->entitySetEnabled($r, $1)"), and a component member (self.physics) resolves to the raw handle-fetch
// expression itself ("ctx->entityGetPhysicsComponent($r)") -- re-fetched at each use, no caching. A script
// declares which components it requires (DSL::requiredComponents); assignment-time validation (future work)
// guarantees required members are never null.
//
// EXTENSIBLE: registerStruct/registerObject/registerEntryPoint are public, so any library can expose its own
// script bindings -- this registry has NO built-in content of its own (not even vec2/3/4 or self): every DSLType
// it knows about, structs and components alike, was registered by SOME caller through this exact same path.
// registerStruct returns the DSLType it just assigned immediately, usable right away in a LATER registration
// call's own parameter/return types; typeByName is the same lookup for an EXISTING struct (e.g. vec3) a new
// registration wants to reference without needing that earlier call's return value handed to it directly.
// Registration order matters: nothing here happens automatically or lazily, so the ENGINE (Entity's
// registerScriptDslBindings, called once from main before anything touches this registry -- see its own
// comment) is what guarantees the base vocabulary (vec2/3/4, self, ...) exists before either the DSL editor or
// any OTHER library's own registration call runs, same as any other cross-library Globals dependency in this
// codebase (see ScriptEventManager::initialize's comment).
//
// Editor-side, the registry replaces the hardcoded builtins: build() constructs the SAME two DSLSymbol vectors
// every consumer (AutoCompleteRules/ScriptLoader/editor) already reads -- sidebar VariableDeclarations for the
// binding objects, builtin FunctionDeclarations for every function (engine free functions plus
// requiresReceiver=true entries for object functions) -- from every struct/object registered SO FAR; a
// registration made AFTER build() runs exists in the registry (queryable via typeByName/structs/objects) but
// never gets symbols built for it, so every registration -- built-in or external -- needs to happen before the
// one build() call (ScriptEditor's, currently). ALL bindings are built regardless of the require set -- stable
// symbol identity forever; DSL::requiredComponents only gates what autocomplete OFFERS and what the loader/
// panel accept.

export struct BindingParam
{
	const char* name;
	DSLType type;
	bool isRef = false;
};

export struct BindingFunc
{
	const char* name;
	DSLType returnType;
	std::vector<BindingParam> params;
	const char* emit;               // "$r" = the receiver's own emitted expression (empty for a free/Engine call), "$1..$n" = args
	bool isPositionalCall = false;  // terse constructors (vec2/3/4): call sites render positionally
};

export struct BindingMember
{
	const char* name;
	DSLType type;
	const char* emit;     // e.g. "$r->pos" (a real field) or "ctx->entityGetPhysicsComponent($r)" (a handle
	                       // fetch) -- "$r" is the receiver's own emitted expression
	bool writable = true; // false = read-only in the DSL (no `x.member = ...` statements)
	// Void = always available; else this member (e.g. self.physics) is only offered/legal while the script
	// requires this component -- always the member's OWN type for a registerComponentType member (see there),
	// but held separately since not every BindingMember is one (e.g. self.pos is never gated).
	DSLType requiredComponent = DSLType::Void;
};

// One engine-defined script STRUCT (vec2/vec3/vec4, and whatever the engine exposes later): a value type with
// members, a positional constructor, and callable member functions. Its DSLType is dslStructType(<table row>).
export struct BindingStruct
{
	const char* name;    // DSL spelling ("vec3") -- also the constructor's callable name
	const char* cppName; // transpiled type ("glm::vec3")
	std::vector<BindingParam> constructorParams;
	const char* constructorEmit;        // "glm::vec3($1, $2, $3)"
	std::vector<BindingMember> members; // { "x", Float, "$r.x" }
	std::vector<BindingFunc> functions; // emit may wrap free functions: { "length", Float, {}, "glm::length($r)" }
};

export struct EntryPointParam
{
	const char* name;
	DSLType type;
};

// One ScriptAPI.h entry point (ScriptOnSpawnFn, ScriptUpdateFn, ...) the sidebar can toggle a matching DSL
// function on/off for. `dslParams` are its DSL-visible, LOCKED parameters (deltaSeconds, eventIdx, ...) -- what
// ScriptEditor seeds the function with on toggle-on, and refuses to let the author add to/remove/retype/rename
// afterward. `cppSuffix` is the REST of the real exported signature, verbatim, after "const ScriptContext* ctx,
// Entity* self" -- raw text (not DSLType-driven) because the real ABI has parameters no DSLType can represent
// yet: `scriptData` (opaque per-instance memory, never useful without a DSL-side ScriptData feature) on every
// kind, and OnPhysicsEvent's `other` (an Entity VALUE -- Entity is a binding-only type today, see
// dslIsEngineObjectType) and `contactId` (int64 -- DSLType has no 64-bit integer). Those stay invisible/
// unreadable from DSL body code until the type system grows to cover them; `dslParams`' names/types MUST match
// `cppSuffix`'s leading parameters exactly (same names, same order), since generated code and what the editor
// shows the author must agree. `registerMacro` is the ScriptAPI.h REGISTER_*() call Transpiler emits after the
// function -- a no-op in the DLL build, what makes the STATIC/cooked build find it by kind.
export struct EntryPointDef
{
	const char* name; // the exported C symbol AND the DSL function's own name, e.g. "Update"
	std::vector<EntryPointParam> dslParams;
	const char* cppSuffix;
	const char* registerMacro;
};

// One binding object ("self", "physics", ...) or the Engine section (objectName == nullptr: its functions are
// FREE calls in the DSL, `ctx.*` in C++). Every object's functions/members are reachable through
// ScriptBindings::objectFor(type) regardless of `sidebarTopLevel` -- physics/audio/force stay full binding
// objects (findReceiverFunction et al. work on them exactly like self does), just not their OWN sidebar root:
// they're reached only by dotting through self's matching member (self.physics), never as a bare identifier.
export struct BindingObject
{
	const char* name;                // the DSL identifier; nullptr = the Engine (free-function) section
	DSLType type;                    // the object's engine-object DSLType (Void for the Engine section)
	bool sidebarTopLevel = true;     // false = no root-level sidebar VariableDeclaration/candidate of its own
	                                  // (see ScriptBindings::build) -- reachable only via another binding's member
	std::vector<BindingFunc> functions;
	std::vector<BindingMember> members;
};

export class ScriptBindings
{
public:
	// Registers one struct type (a value type with members/functions/positional constructor, e.g. vec2/3/4) and
	// returns the DSLType it's assigned -- callable from ANY library, at any time (see the class comment). The
	// returned type is immediately usable as a parameter/return type of a LATER registerStruct/registerObject
	// call from anyone; typeByName is the same lookup for an EXISTING struct this call didn't just register.
	DSLType registerStruct(BindingStruct def);
	// Registers one binding object (or the Engine free-function section, when def.name == nullptr) -- same
	// call-from-anywhere/any-time guarantee as registerStruct.
	void registerObject(BindingObject def);
	// Registers one COMPONENT TYPE (e.g. "PhysicsComponent") -- returns the DSLType it's assigned AND, in the
	// SAME call, exposes it as self.<memberName> (writable=false always -- a component handle is FETCHED off
	// its owning entity, never assigned; gated by the returned type itself, see DSL::requiredComponents -- a
	// component member is only offered/legal while the script requires it, so the gate and the member's own
	// type are always the same value, never a separate parameter to keep in sync). Everything reachable off
	// self/Entity is a component by construction: there is no separate step that patches self's own member
	// list, so an external library registering its own component type (e.g. Particle exposing self.particle)
	// never touches -- or even needs to know about -- self's registration, which the caller (Entity's
	// registerScriptDslBindings) already guaranteed ran before this call. The returned type is also usable as
	// a BindingObject's own `type` (see registerObject) -- e.g. "physics"'s own applyImpulse/getVelocity/...
	// functions, reachable only by dotting through self.physics, matching the sidebarTopLevel=false pattern --
	// with no positional-call shape to it (unlike registerStruct), since a component is fetched, never
	// constructed.
	DSLType registerComponentType(const char* memberName, const char* typeName, const char* memberEmit);
	// Registers one toggleable ScriptAPI entry point (see EntryPointDef) -- same guarantee.
	void registerEntryPoint(EntryPointDef def);

	// `name`'s DSLType among every struct OR component type registered so far (Void if neither spells `name`) --
	// how a NEW registerStruct/registerObject/registerComponentType call, from this library or any other,
	// references an EXISTING struct/component type (e.g. "vec3", "PhysicsComponent") as one of its own
	// parameter/return/member types, without needing that earlier call's DSLType handed to it directly.
	DSLType typeByName(const std::string& name) const;
	// Same lookup, but ONLY among registered component types (Void if `name` isn't one, including when it's a
	// struct name instead) -- what DSL::requiredComponents / the .dsl "//@@require" line resolves a name
	// against, where a struct match would be meaningless.
	DSLType componentTypeByName(const std::string& name) const;

	// Constructs the sidebar/builtin symbol vectors from every struct/object registered SO FAR -- call ONCE (ScriptEditor currently),
	// after every registration whose symbols should exist (symbol identity must be stable for the lifetime of
	// every document that references them); a registration made after this call exists in the registry but
	// never gets symbols built for it.
	void build(std::vector<std::unique_ptr<DSLSymbol>>& sidebarOut, std::vector<std::unique_ptr<DSLSymbol>>& builtinsOut);

	// The full table, in registration order (built-ins first; the Engine section last among them).
	std::span<const BindingObject> objects() const;

	// The BindingObject a receiver's engine-object type belongs to (nullptr for non-object types).
	const BindingObject* objectFor(DSLType type) const;
	// The sidebar VariableDeclaration symbol built for an object (nullptr for the Engine section).
	DSLSymbol* objectDecl(const BindingObject& object) const;
	// The reverse: which object a sidebar declaration is (nullptr if it isn't one of ours).
	const BindingObject* objectForDecl(const DSLSymbol* sidebarDecl) const;

	// The built FunctionDeclaration symbols of one object, parallel to its `functions` vector.
	std::span<DSLSymbol* const> functionSymbols(const BindingObject& object) const;

	// The member definition behind a receiver-type + name pair (nullptr = no such member) -- binding objects
	// AND struct types alike; what stamps MemberAccess::type at author/load time.
	const BindingMember* findMember(DSLType receiverType, const std::string& name) const;

	// `type`'s registered name (nullptr if `type` isn't a registered component type) -- the reverse of
	// registerComponentType/typeByName, what dslTypeName renders a component-type DSLType as.
	const char* componentTypeName(DSLType type) const;

	// ---- structs ----
	std::span<const BindingStruct> structs() const; // every struct registered so far, in registration order
	const BindingStruct* structFor(DSLType type) const; // nullptr for non-struct types
	// The struct's own member functions' built symbols, parallel to structFor(type)->functions.
	std::span<DSLSymbol* const> structFunctionSymbols(DSLType type) const;

	// The emit template a built function symbol was declared with (nullptr if not a registry symbol) -- what
	// Transpiler's callText substitutes against.
	const char* emitFor(const DSLSymbol* funcDecl) const;

	// The toggleable ScriptAPI entry points registered so far (the 5 built-in ones -- OnSpawn/OnDestroy/Update/
	// OnEvent/OnPhysicsEvent -- plus any registerEntryPoint addition), in registration order. Doesn't need
	// `build()`, unlike objects()/structs()' BUILT symbols.
	std::span<const EntryPointDef> entryPoints() const;
	// `name`'s matching EntryPointDef (nullptr if `name` isn't registered).
	const EntryPointDef* entryPointFor(const std::string& name) const;

private:
	struct BuiltObject
	{
		const BindingObject* def = nullptr;
		DSLSymbol* decl = nullptr;              // the sidebar VariableDeclaration (null for the Engine section)
		std::vector<DSLSymbol*> functionSymbols; // parallel to def->functions
	};
	struct BuiltStruct
	{
		const BindingStruct* def = nullptr;
		DSLSymbol* constructorFunc = nullptr;    // the positional constructor builtin ("vec3(...)")
		std::vector<DSLSymbol*> functionSymbols; // parallel to def->functions
	};

	// Registered defs (registerStruct/registerObject/registerComponentType/registerEntryPoint).
	std::vector<BindingStruct> m_structDefs;
	std::vector<BindingObject> m_objectDefs;
	std::vector<const char*> m_componentTypeNames; // index N = DSLType dslComponentType(N)
	std::vector<EntryPointDef> m_entryPointDefs;

	// Symbols built from the above by build() -- called once, well after every registration that should be
	// included.
	std::vector<BuiltObject> m_built;
	std::vector<BuiltStruct> m_builtStructs;     // index N = DSLType dslStructType(N)
	std::vector<std::pair<const DSLSymbol*, const char*>> m_emits; // function symbol -> emit template
};

// The one registry instance (house singleton pattern) -- registrations (built-in and external alike) accumulate
// into it from wherever they're called, build() constructs symbols from them once (the Script Editor's own call,
// currently); consulted by dslTypeName (struct names), the loader, and the transpiler too.
export namespace Globals
{
	ScriptBindings scriptBindings;
}
