export module Script:ScriptBindings;

import Core;
import :DSL;

// The engine-exposure registry for the DSL: the ONE table describing what scripts can touch. Exposing an
// engine function that already exists on the ScriptContext ABI (Code/Script/Public/ScriptAPI.h) costs exactly
// one BindingFunc row here; exposing brand-new C++ additionally costs the ABI's own minimal trio (X-macro row +
// thunk + ctor init line in Code/Entity/Private/ScriptContext.cpp).
//
// The user's transpilation model (M6, emit templates stored here but unconsumed until then): each script
// becomes a generated C++ class whose MEMBERS are the entity's own things -- `self`, plus one wrapper member
// per REQUIRED component, each initialized once at entry-point entry (`memberEmit`) -- so DSL
// `physics.applyImpulse(...)` transpiles to member-pattern access ("$r" in a function's `emit` is that
// member); global engine functionality goes through the ctx struct ("ctx." emits). A script declares which
// components it requires (DSL::requiredComponents); assignment-time validation (M6) guarantees required
// members are never null.
//
// Editor-side, the registry replaces the hardcoded builtins: build() constructs the SAME two DSLSymbol vectors
// every consumer (AutoCompleteRules/ScriptLoader/editor) already reads -- sidebar VariableDeclarations for the
// binding objects, builtin FunctionDeclarations for every function (engine free functions plus
// requiresReceiver=true entries for object functions). ALL bindings are built regardless of the require set --
// stable symbol identity forever; DSL::requiredComponents only gates what autocomplete OFFERS and what the
// loader/panel accept.

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
	const char* emit;               // M6 template: "$r" = the owning object's class member (empty owner = "ctx."), "$1..$n" = args
	bool isPositionalCall = false;  // terse constructors (vec2/3/4): call sites render positionally
};

export struct BindingMember
{
	const char* name;
	DSLType type;
	const char* emit;     // M6 template: e.g. "$r.pos" -- "$r" is the receiver's own emitted expression
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

// One binding object ("self", "physics", ...) or the Engine section (objectName == nullptr: its functions are
// FREE calls in the DSL, `ctx.*` in C++). Every object's functions/members are reachable through
// ScriptBindings::objectFor(type) regardless of `sidebarTopLevel` -- physics/audio/force stay full binding
// objects (findReceiverFunction et al. work on them exactly like self does), just not their OWN sidebar root:
// they're reached only by dotting through self's matching member (self.physics), never as a bare identifier.
export struct BindingObject
{
	const char* name;                // the DSL identifier; nullptr = the Engine (free-function) section
	DSLType type;                    // the object's engine-object DSLType (Void for the Engine section)
	const char* memberEmit;          // M6: the generated class member's initializer, e.g. "ctx->entityGetPhysicsComponent(self)"
	bool sidebarTopLevel = true;     // false = no root-level sidebar VariableDeclaration/candidate of its own
	                                  // (see ScriptBindings::build) -- reachable only via another binding's member
	std::vector<BindingFunc> functions;
	std::vector<BindingMember> members;
};

export class ScriptBindings
{
public:
	// Constructs the sidebar/builtin symbol vectors from the table -- call ONCE (symbol identity must be
	// stable for the lifetime of every document that references them).
	void build(std::vector<std::unique_ptr<DSLSymbol>>& sidebarOut, std::vector<std::unique_ptr<DSLSymbol>>& builtinsOut);

	// The full table, in sidebar-panel display order (Engine section last).
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
	std::span<const BindingStruct> structs() const;
	const BindingStruct* structFor(DSLType type) const;         // nullptr for non-struct types
	DSLType structTypeByName(const std::string& name) const;     // Void when no struct spells `name`
	// The struct's own member functions' built symbols, parallel to structFor(type)->functions.
	std::span<DSLSymbol* const> structFunctionSymbols(DSLType type) const;

	// M6 hook: the emit template a built function symbol was declared with (nullptr if not a registry symbol).
	const char* emitFor(const DSLSymbol* funcDecl) const;

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

	std::vector<BuiltObject> m_built;
	std::vector<BuiltStruct> m_builtStructs;     // index N = DSLType dslStructType(N)
	std::vector<std::pair<const DSLSymbol*, const char*>> m_emits; // function symbol -> emit template
};

// The one registry instance (house singleton pattern) -- built once by the Script Editor at startup; consulted
// by dslTypeName (struct names), the loader, and the transpiler.
export namespace Globals
{
	ScriptBindings scriptBindings;
}
