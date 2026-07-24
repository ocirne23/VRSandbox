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
// script bindings, not just Script itself -- the Script library's OWN vec2/vec3/vec4, self/physics/audio/
// force/Engine, and the 5 ScriptAPI entry points register through this exact same path (see
// ensureBuiltinsRegistered), so there's no "internal" registration mechanism a caller elsewhere can't also use.
// registerStruct returns the DSLType it just assigned immediately, usable right away in a LATER registration
// call's own parameter/return types; typeByName is the same lookup for an EXISTING struct (e.g. vec3) a new
// registration wants to reference without needing that earlier call's return value handed to it directly.
// Built-ins register themselves lazily, the first time ANY public method on this class is called, from
// WHICHEVER call that happens to be -- so nothing needs to run "at startup" for them specifically. A cross-
// library dependency (an external registration that references vec2/3/4 via typeByName) still needs sequencing
// the caller controls, same as any other cross-library Globals dependency in this codebase (see
// ScriptEventManager::initialize's comment) -- just call the referenced registration (or trigger the built-ins
// via any ScriptBindings method) before the one that needs its result.
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
	// None = always available; else this member (e.g. self.physics) is only offered/legal while the script
	// requires this component -- the member-level twin of BindingObject::requiredComponent used to be.
	DSLComponentKind requiredComponent = DSLComponentKind::None;
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
	// returns the DSLType it's assigned -- callable from ANY library, at any time, not just Script's own
	// built-ins (see the class comment). The returned type is immediately usable as a parameter/return type of
	// a LATER registerStruct/registerObject call from anyone; typeByName is the same lookup for an EXISTING
	// struct this call didn't just register.
	DSLType registerStruct(BindingStruct def) const;
	// Registers one binding object (or the Engine free-function section, when def.name == nullptr) -- same
	// call-from-anywhere/any-time guarantee as registerStruct.
	void registerObject(BindingObject def) const;
	// Registers one toggleable ScriptAPI entry point (see EntryPointDef) -- same guarantee.
	void registerEntryPoint(EntryPointDef def) const;

	// `name`'s DSLType among every struct registered so far (Void if none spells `name`) -- how a NEW
	// registerStruct/registerObject call, from this library or any other, references an EXISTING struct (e.g.
	// "vec3") as one of its own parameter/return types, without needing that struct's DSLType handed to it
	// directly. Triggers the built-ins (see the class comment) if nothing has touched this registry yet.
	DSLType typeByName(const std::string& name) const;

	// Constructs the sidebar/builtin symbol vectors from every struct/object registered SO FAR -- call ONCE,
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
	// Registers the Script library's own built-in bindings -- vec2/vec3/vec4, self/physics/audio/force/Engine,
	// and the 5 ScriptAPI entry points -- through registerStruct/registerObject/registerEntryPoint exactly like
	// any OTHER library would (see the class comment). Runs on the FIRST call to any public method on this
	// class, from whichever call that happens to be; m_builtinsRegistered is set BEFORE the calls below, since
	// they recurse back into this same guard through their own registerStruct/registerObject/registerEntryPoint
	// calls -- registration order between libraries therefore never matters on its own, only that a call
	// referencing an earlier one (via typeByName) is sequenced after it, the caller's own responsibility.
	void ensureBuiltinsRegistered() const;

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

	// Registered defs (registerStruct/registerObject/registerEntryPoint) -- `mutable` so the const query methods
	// that read them (typeByName, objects(), structs(), entryPoints(), entryPointFor()) can also trigger
	// ensureBuiltinsRegistered() lazily, the same as the registration methods themselves (also const: nothing
	// about registering needs write access to the CALLER's own state, only this registry's).
	mutable bool m_builtinsRegistered = false;
	mutable std::vector<BindingStruct> m_structDefs;
	mutable std::vector<BindingObject> m_objectDefs;
	mutable std::vector<EntryPointDef> m_entryPointDefs;

	// Symbols built from the above by build() -- unaffected by the mutable/const story above; build() is the
	// one non-const method here, called once, well after every registration that should be included.
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
