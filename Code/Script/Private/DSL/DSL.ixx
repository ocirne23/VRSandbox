export module Script:DSL;

import Core;

// ---------------------------------------------------------------------------
// Grammar overview (see ScriptLang.ixx's example program for the full spec).
//
// OWNERSHIP MODEL: a DSLCodeLine's `symbols` vector owns EVERY symbol that
// appears anywhere in that logical line/statement, flattened -- not just its
// top-level symbol(s). E.g. `physics.getMass() * force` on one line produces
// several peer entries in that line's `symbols` (the VariableReference to
// `physics`, the FunctionCall to `getMass`, the VariableReference to `force`,
// and the outer Expression tying them together via `*`) rather than a tree of
// unique_ptrs nested inside each other. Structural relationships (which
// symbol is whose operand/argument/receiver) are expressed purely through
// the raw DSLSymbol* cross-reference fields below, which may point at a peer
// earlier in the SAME line, or (for VariableReference::declaration and
// FunctionCall::functionSymbol) at a symbol owned by an EARLIER line, the
// DSL::sidebar list, or a static builtin registry (vec3/print/component
// methods -- not user-authored, owned by ScriptLang/Transpiler, not by any
// DSLCodeLine). This is deliberate: every sub-expression is its own flat,
// independently addressable entry, which is exactly what the editor needs --
// "select just the `getMass` call out of a larger expression" is "select one
// entry in this line's symbols", not a tree-navigation problem.
//
// Storage order within a line's `symbols` is POST-ORDER: every symbol a
// statement depends on (its operands/arguments/nested calls) is appended
// before the statement's own head symbol, so `symbols.back()` is always the
// line's primary/defining symbol (the FunctionDeclaration on a function
// header line, the top-level FunctionCall or Expression on a statement
// line). This is a construction/bookkeeping convention only -- it has no
// bearing on reading/render order, which the Syntax formatter (M2) derives
// by walking the pointer relationships, not by iterating this vector in order.
// Builtin function declarations (vec3/print/physics.applyForce/...) are NOT
// owned by any line at all -- they live in a static registry (ScriptLang /
// Transpiler), so a FunctionCall's `functionSymbol` for a builtin points
// outside the document entirely.
//
// Block nesting (if/while/for/function bodies) is expressed ENTIRELY through
// DSLCodeLine::scopeLevel, never through a stored "end" symbol/line -- the
// closing `end` shown in the example syntax is a SYNTHETIC render artifact
// the formatter (Syntax, in ScriptLang.ixx) inserts wherever scopeLevel steps
// back down. There is nothing to desync (only one source of truth), and
// "delete this if" is a single well-defined operation on its header line,
// never "delete two independently-existing lines that happen to pair up".
// Blank lines used for visual spacing in the example are likewise not stored
// -- purely a formatter choice, not part of the model.
//
// Per-line-kind symbol sequences (DSLCodeLine::symbols), the working convention:
//   variable declaration (with or without an initializer) : [VariableDeclaration]
//     (VariableDeclaration::initialValue holds the initializer symbol directly --
//     no separate "assign" symbol; the value's own dependent symbols are still
//     owned as earlier peers on this line, VariableDeclaration itself stays head)
//   bare assignment/compound-assign : [Expression]
//   call statement                  : [FunctionCall, ...its nested sub-expressions as peers]
//   if / elseif / while             : [FlowControl{control, condition}, ...condition's nested peers]
//   else                             : [FlowControl{control=Else, condition=nullptr}]
//   for                              : [..., VariableDeclaration(loop var) [own initialValue = init clause],
//                                       ..., Expression(condition), ..., Expression(increment),
//                                       FlowControl{control=For, forLoopVar, forCondition, forIncrement}]
//     (the three comma-separated clauses are referenced directly by FlowControl's
//     dedicated for* fields -- not discovered by scanning the line -- so the loop
//     var/condition/increment symbols just need to be SOME earlier peers on this
//     line, in whatever order their own dependencies require)
//   return <value>                  : [FlowControl{control=Return, condition=returnValueExpr}]
//   return (bare) / break            : [FlowControl{control, condition=nullptr}]
//   function declaration header     : [FunctionDeclaration]
//     (parameters live on FunctionDeclaration::parameterVarDeclarations --
//     symbols owned by this same header line, referenced directly by that field --
//     not discovered by scanning the line, same convention as `for`'s clauses above)
//
// `ref` rendering: there is no standalone "ref" symbol/keyword token. A
// parameter's VariableDeclaration::isRef marks it as by-reference; the
// formatter renders the `ref` keyword wherever that parameter is declared, at
// a call site that targets it, or (checking VariableReference::declaration)
// wherever it's assigned into (`ref appliedForce = toApply`).
//
// Comparison operator spelling: the example's "lesseq" (expanded view) vs
// "<=" (compact view) is treated as an authoring slip, not a deliberate
// per-view spelling scheme -- DSLOperator has exactly one canonical form per
// operator, rendered the same symbolic way in both views. Revisit if wrong.
// ---------------------------------------------------------------------------

export class DSLCodeLine;

// A type is either one of the fixed builtin kinds below, or a DYNAMIC engine-defined struct: value
// `FirstStruct + N` names entry N of the ScriptBindings struct registry (vec2/vec3/vec4 are the first three
// table rows -- adding a new engine struct is one table row, no enum edit). Struct types carry members,
// a positional constructor, and callable member functions, all defined by the registry.
export enum class DSLType : uint16
{
	Void,               // no value (a function with no return type)
	Int,
	Float,
	String,
	Bool,
	Function,           // reserved: a symbol whose value IS a function reference; unused by the current example
	World,              // -- sidebar-bindable engine object kinds (never user-declarable; see
	Entity,             //    dslIsEngineObjectType below and the ScriptBindings registry) --
	PhysicsComponent,
	AudioComponent,
	ForceComponent,
	// The document's OWN persistent per-instance data (self.data.*, see DSL::dataFields) -- a binding like the
	// component kinds above (dotted into, never a value/declarable type -- dslIsEngineObjectType covers it too),
	// but PER-DOCUMENT rather than a fixed ScriptBindings table row: self.data's own member ("data") is a real
	// static BindingMember (ScriptBindings.cpp), while ITS members (the fields) are looked up against
	// DSL::dataFields directly wherever a receiver of this type is resolved (ScriptLang.cpp's receiverCandidates,
	// ScriptEditor's chain-building, ScriptLoader's dot-chain parser, Transpiler's memberText).
	ScriptData,
	FirstStruct = 256,  // + struct registry index (see dslStructType/dslStructIndex)
};

export constexpr bool dslIsStructType(DSLType type) { return type >= DSLType::FirstStruct; }
export constexpr int dslStructIndex(DSLType type) { return static_cast<int>(type) - static_cast<int>(DSLType::FirstStruct); }
export constexpr DSLType dslStructType(int index) { return static_cast<DSLType>(static_cast<int>(DSLType::FirstStruct) + index); }

// Engine-object kinds are BINDINGS (dotted into for their members/functions), never values: excluded from
// variable/reassign candidates, literal slots, and declarable types alike. ScriptData (self.data) is included
// here too -- same binding-only treatment, even though it has no ScriptBindings::objectFor() row of its own
// (per-document instead, see DSLType::ScriptData); every objectFor(ScriptData) call already handles "no match"
// gracefully (skip/continue), which is exactly the right behavior for a type resolved elsewhere.
export inline bool dslIsEngineObjectType(DSLType type)
{
	return type == DSLType::World || type == DSLType::Entity || type == DSLType::PhysicsComponent
		|| type == DSLType::AudioComponent || type == DSLType::ForceComponent || type == DSLType::ScriptData;
}

// A type with further members/functions reachable by dotting into it -- an engine-defined STRUCT value
// (vec2/3/4) or a bound engine-object kind (self.physics and friends). What decides whether a member that
// doesn't itself match a value slot is still offered as a dot-into WAYPOINT rather than excluded outright.
export inline bool dslIsChainableType(DSLType type) { return dslIsStructType(type) || dslIsEngineObjectType(type); }

// The component kinds a script can REQUIRE (see DSL::requiredComponents below) -- an editor-side mirror of the
// scriptable subset of Entity's component types, deliberately not the engine's own EComponentID.
export enum class DSLComponentKind : uint8
{
	Physics,
	Audio,
	Force,
	Count,
	None = Count, // sentinel: a binding that is always available (self / engine globals)
};

// The kind's serialized spelling (the .dsl "//@@require Physics, Audio" line) -- by-name so the enum can grow
// without breaking saved files.
export inline const char* dslComponentKindName(DSLComponentKind kind)
{
	switch (kind)
	{
	case DSLComponentKind::Physics: return "Physics";
	case DSLComponentKind::Audio:   return "Audio";
	case DSLComponentKind::Force:   return "Force";
	default:                        return "?";
	}
}
export enum class DSLFlowControl
{
	If,
	ElseIf,
	Else,
	While,
	For,
	Return,
	Break,
};
export enum class DSLOperator
{
	Assign,
	AssignAdd,
	AssignSubtract,
	AssignMultiply,
	AssignDivide,
	AssignModulus,
	Add,
	Subtract,
	Multiply,
	Divide,
	Modulus,
	Equal,
	NotEqual,
	LessThan,
	GreaterThan,
	LessThanOrEqual,
	GreaterThanOrEqual,
	And, // && -- logical, joining BOOL-valued operands (comparisons, bool values, grouped chains)
	Or,  // ||
};

// The four operator classes (assign / arithmetic / comparison / logical) -- every Expression's operators are
// uniformly one class (see DSLSymbol::Expression below), and which class decides both what an operator span
// offers as replacements and whether a chain operand/operator pair may be deleted (chain operators --
// arithmetic and logical -- only; the two sides of a comparison or assignment aren't individually removable).
// Range checks lean on the enum's declaration order.
export inline bool dslIsAssignOperator(DSLOperator op)     { return op >= DSLOperator::Assign && op <= DSLOperator::AssignModulus; }
export inline bool dslIsArithmeticOperator(DSLOperator op) { return op >= DSLOperator::Add && op <= DSLOperator::Modulus; }
export inline bool dslIsComparisonOperator(DSLOperator op) { return op >= DSLOperator::Equal && op <= DSLOperator::GreaterThanOrEqual; }
export inline bool dslIsLogicalOperator(DSLOperator op)    { return op == DSLOperator::And || op == DSLOperator::Or; }
export inline bool dslIsChainOperator(DSLOperator op)      { return dslIsArithmeticOperator(op) || dslIsLogicalOperator(op); }

export class DSLSymbol
{
public:

	~DSLSymbol() {}

	struct Constant
	{
		DSLType type;
		std::string value;
	};
	struct TypeDeclaration
	{
		DSLType type;
	};
	// A use of a variable (or sidebar binding) as a value somewhere in an
	// expression -- e.g. the `counter` in `thing += counter`, or the `physics`
	// being dotted into in `physics.getMass()`.
	struct VariableReference
	{
		DSLSymbol* declaration = nullptr; // -> VariableDeclaration
	};
	struct VariableDeclaration
	{
		std::string name;
		DSLSymbol* typeSymbol = nullptr;   // -> TypeDeclaration
		DSLSymbol* initialValue = nullptr; // optional initializer value; nullptr = no initializer (also unused on parameters)
		bool isRef = false; // by-reference output parameter (only meaningful on a function's parameterVarDeclarations)
		int numReferences = 0; // maintained by the editor: live VariableReference count, guards rename/delete
	};
	// One call argument. `parameter == nullptr` means positional matching
	// (used by terse builtin constructors like vec3(0,1,0)); non-null points
	// directly at the callee's matching parameter VariableDeclaration for
	// named/keyword matching, order-independent (ordinary calls, e.g. `force = 1.0`).
	// For a `ref` parameter, `value` is a VariableReference to the caller's
	// local that receives the output (`ref appliedForce = applied`).
	struct CallArgument
	{
		DSLSymbol* parameter = nullptr; // -> VariableDeclaration (callee's param), or nullptr for positional
		DSLSymbol* value = nullptr;
	};
	struct FunctionCall
	{
		DSLSymbol* functionSymbol = nullptr; // -> FunctionDeclaration (user-authored or a static builtin, e.g. vec3/print/applyForce)
		DSLSymbol* receiver = nullptr;       // -> VariableReference; null = free call (print(...)), non-null = dot-call (physics.getMass())
		std::vector<CallArgument> arguments;
	};
	struct FunctionDeclaration
	{
		std::string name;
		std::vector<DSLSymbol*> parameterVarDeclarations; // -> VariableDeclaration, owned by this decl's header line
		DSLType returnType = DSLType::Void;
		bool requiresReceiver = false; // builtin component methods (physics.getMass(), world.rayCast(...)) --
		                                // AutoCompleteRules excludes these from plain call-statement/value
		                                // candidates until M5 formalizes inserting a dot-call with its receiver
		bool isPositionalCall = false;  // terse builtin constructors (vec3(0,1,0)) -- editor-inserted calls get
		                                 // positional CallArgument::parameter=nullptr slots instead of named
		                                 // `x = ...` ones, even though parameterVarDeclarations is still populated
		                                 // (it's what tells the editor how many/what-typed placeholders to create)
		int numReferences = 0; // maintained by the editor: live FunctionCall count, guards rename/delete
	};
	// If/ElseIf/While carry their condition in `condition`; Return carries its
	// value (nullptr for a bare `return`); Else/Break leave everything null.
	// For ignores `condition` and uses its own three dedicated fields instead
	// (a 3-ary node in spirit, without inventing a generic N-ary symbol shape
	// just for this one construct): forLoopVar's own VariableDeclaration
	// carries the init clause via its `initialValue` field.
	struct FlowControl
	{
		DSLFlowControl control;
		DSLSymbol* condition = nullptr;
		DSLSymbol* forLoopVar = nullptr;   // For only: -> VariableDeclaration (initialValue = init clause)
		DSLSymbol* forCondition = nullptr; // For only: -> Expression
		DSLSymbol* forIncrement = nullptr; // For only: -> Expression
	};
	// An operator CHAIN, stored FLAT and exactly as authored: operators.size() == operands.size() - 1,
	// operators[i] sitting between operands[i] and operands[i+1]. operands.size() >= 2, with ONE exception: a
	// `grouped` chain may hold a single operand and no operators at all -- authored parens around a lone value
	// ("(a)") are kept verbatim, deliberate edit anchors rather than redundancy to strip.
	// No precedence is encoded here -- `1 + 2 * 3` is one flat chain -- the transpiler (M6) applies ordinary
	// arithmetic precedence (*, /, % over +, -; left-associative) when emitting C++. Explicit parentheses are
	// the ONLY structural nesting: an operand that is itself an Expression with `grouped` set renders in
	// parens and always evaluates as one unit. Grouping is stored user intent, never derived from precedence,
	// which is what lets the editor replace any single operator in place without restructuring anything.
	// Structural statements reuse the same shape as exactly-binary chains: an assignment is [target, value]
	// around an assign-class operator, a comparison [left, right] around a comparison operator -- and by
	// construction those never grow more operands (arithmetic editing wraps their SIDES in nested ungrouped
	// sub-chains instead, so every Expression's operators are uniformly one class -- see ScriptEditor's
	// in-place expression editing).
	struct Expression
	{
		std::vector<DSLSymbol*> operands;
		std::vector<DSLOperator> operators;
		bool grouped = false; // explicit parentheses around this whole chain (only meaningful as another chain's operand)
	};
	// Field-style access on a receiver, e.g. `self.pos` or `self.pos.x` -- NOT a call (see
	// FunctionCall::receiver for method-style dot-calls like physics.getVelocity()).
	// `type` is stamped from the ScriptBindings member registry when the access is
	// authored/loaded, so dslValueType stays registry-free.
	struct MemberAccess
	{
		DSLSymbol* receiver = nullptr; // -> VariableReference, or a nested MemberAccess (chained access)
		std::string memberName;
		DSLType type = DSLType::Void;
	};
	// A whole-line comment ("# ...") -- pure annotation: no cross-references, never a value, freely deletable.
	// The Transpiler emits it as a C++ comment.
	struct Comment
	{
		std::string text;
	};
	// A yet-unfilled slot -- what keeps the document "always compilable" even mid-edit. `expectedType` names
	// what belongs here: DSLType::Void means a whole STATEMENT slot (AutoCompleteRules offers if/while/return/
	// break/declare-variable/call-statement candidates); any other DSLType means a VALUE slot of that type
	// (offers matching constants/variables/functions). Confirming a candidate REPLACES the placeholder
	// outright -- it never partially resolves. The editor renders one distinctly (e.g. "<float>") and
	// Transpiler (M6) emits a type-appropriate default for any left unfilled, so a document with placeholders
	// still compiles.
	struct Placeholder
	{
		DSLType expectedType;
	};

	enum class SymbolType
	{
		Constant,
		TypeDeclaration,
		VariableReference,
		VariableDeclaration,
		FunctionCall,
		FunctionDeclaration,
		FlowControl,
		Expression,
		MemberAccess,
		Comment,
		Placeholder,
	};
	// Named so code building a symbol from one of the alternatives above (e.g. ScriptEditor's pushSymbol helper)
	// can take it as a plain, non-template parameter -- any alternative converts to this implicitly.
	using Data = std::variant<Constant, TypeDeclaration, VariableReference, VariableDeclaration, FunctionCall, FunctionDeclaration, FlowControl, Expression, MemberAccess, Comment, Placeholder>;

	SymbolType type;
	Data data;
	DSLCodeLine* line = nullptr;
};

export class DSLCodeLine
{
public:
	int scopeLevel = 0;
	int scopeId = 0;

	std::vector<std::unique_ptr<DSLSymbol>> symbols;

	// The line's primary/defining symbol -- symbols.back(), per the post-order ownership convention above.
	// Null only for a line with no symbols at all (a transient state mid-edit, never a resting one).
	DSLSymbol* head() const { return symbols.empty() ? nullptr : symbols.back().get(); }
};

export class DSLScriptFile
{
public:
	std::vector<std::unique_ptr<DSLCodeLine>> lines;
};

// ---------------------------------------------------------------------------
// Structural queries every layer above (editor ops, autocomplete scoping,
// formatting) keeps needing -- defined once here, next to the model they walk,
// instead of each caller hand-rolling the same index/scan loops.
// ---------------------------------------------------------------------------

// The DSLType `symbol` evaluates to when used as a VALUE -- what constrains candidate lists when editing one
// operand of an existing expression in place. An Expression resolves through its operands: for an assignment
// that's the target's own type, for a comparison/arithmetic chain any resolvable operand (all operands of a
// chain share one type by construction). Void = unresolvable (a statement-only construct, or a MemberAccess
// whose member isn't known here -- only `pos` exists until M5 formalizes a member registry).
export inline DSLType dslValueType(const DSLSymbol* symbol)
{
	if (symbol == nullptr)
		return DSLType::Void;
	switch (symbol->type)
	{
	case DSLSymbol::SymbolType::Constant:
		return std::get<DSLSymbol::Constant>(symbol->data).type;
	case DSLSymbol::SymbolType::VariableReference:
	{
		const DSLSymbol* decl = std::get<DSLSymbol::VariableReference>(symbol->data).declaration;
		if (decl == nullptr)
			return DSLType::Void;
		const DSLSymbol* typeSymbol = std::get<DSLSymbol::VariableDeclaration>(decl->data).typeSymbol;
		return typeSymbol != nullptr ? std::get<DSLSymbol::TypeDeclaration>(typeSymbol->data).type : DSLType::Void;
	}
	case DSLSymbol::SymbolType::FunctionCall:
	{
		const DSLSymbol* callee = std::get<DSLSymbol::FunctionCall>(symbol->data).functionSymbol;
		return callee != nullptr ? std::get<DSLSymbol::FunctionDeclaration>(callee->data).returnType : DSLType::Void;
	}
	case DSLSymbol::SymbolType::Placeholder:
		return std::get<DSLSymbol::Placeholder>(symbol->data).expectedType;
	case DSLSymbol::SymbolType::MemberAccess:
		return std::get<DSLSymbol::MemberAccess>(symbol->data).type; // stamped from the member registry at author/load time
	case DSLSymbol::SymbolType::Expression:
	{
		const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(symbol->data);
		// A comparison or logical chain EVALUATES to Bool regardless of what its operands are.
		if (!e.operators.empty() && (dslIsComparisonOperator(e.operators[0]) || dslIsLogicalOperator(e.operators[0])))
			return DSLType::Bool;
		for (const DSLSymbol* operand : e.operands)
			if (const DSLType t = dslValueType(operand); t != DSLType::Void)
				return t;
		return DSLType::Void;
	}
	default:
		return DSLType::Void;
	}
}

// Index of `line` within file.lines, or -1 -- the raw-pointer lookup every structural edit/query starts from.
export inline int dslLineIndex(const DSLScriptFile& file, const DSLCodeLine* line)
{
	for (int i = 0; i < static_cast<int>(file.lines.size()); ++i)
		if (file.lines[i].get() == line)
			return i;
	return -1;
}

// First index past the block opened at `headerIndex` -- every following line with a DEEPER scopeLevel belongs
// to the block's body (see the scopeLevel-only nesting model above), so this is headerIndex + 1 when the body
// is empty, and also correct (trivially) for a non-block line.
export inline int dslBlockEnd(const DSLScriptFile& file, int headerIndex)
{
	const int headerScopeLevel = file.lines[headerIndex]->scopeLevel;
	int i = headerIndex + 1;
	while (i < static_cast<int>(file.lines.size()) && file.lines[i]->scopeLevel > headerScopeLevel)
		++i;
	return i;
}

// Index of the block-header line whose body directly contains `lineIndex` -- the nearest EARLIER line one (or
// more) scope levels shallower -- or -1 at top level. What "am I inside an if branch?" style queries start from.
export inline int dslEnclosingBlockHeader(const DSLScriptFile& file, int lineIndex)
{
	const int scope = file.lines[lineIndex]->scopeLevel;
	for (int i = lineIndex - 1; i >= 0; --i)
		if (file.lines[i]->scopeLevel < scope)
			return i;
	return -1;
}

// Index of the FunctionDeclaration header line enclosing `fromIndex` (walking backward; `fromIndex` itself
// counts if it IS a header -- its parameters belong to it), or -1 when outside any function.
export inline int dslEnclosingFunctionHeader(const DSLScriptFile& file, int fromIndex)
{
	for (int i = fromIndex; i >= 0; --i)
	{
		const DSLSymbol* head = file.lines[i]->head();
		if (head != nullptr && head->type == DSLSymbol::SymbolType::FunctionDeclaration)
			return i;
	}
	return -1;
}

// One field of the document's own persistent per-instance data (self.data.<name>, see DSL::dataFields) --
// authored via the SCRIPT DATA sidebar section's add/remove controls, serialized as one .dsl "//@@data <type>
// <name>" line each. `type` is never an engine-object kind (self.data.self makes no sense) -- only
// Int/Float/Bool/String or an engine-defined struct (vec2/3/4).
export struct DSLDataField
{
	std::string name;
	DSLType type;
};

// Top-level document: one script's function bodies plus its sidebar of bound
// engine objects (World/Entity/component instances the body may dot-call
// into -- see ScriptLang.ixx's "sidebar" example section). Kept free of any
// ImGui/editor state on purpose; ScriptEditor owns a DSL instance and layers
// editing/rendering on top of it.
export class DSL
{
public:
	std::string filePath;
	std::vector<std::unique_ptr<DSLSymbol>> sidebar; // VariableDeclaration entries (self + engine-object kinds,
	                                                  // built once by ScriptBindings); line == nullptr. ALL bindings
	                                                  // exist here for stable identity -- requiredComponents below
	                                                  // gates which are offered/legal, never which are built
	DSLScriptFile file;
	// The component kinds this script REQUIRES on its entity (authored via the sidebar panel's checkboxes,
	// serialized as the .dsl "//@@require" line). Only required components' bindings appear in autocomplete,
	// and M6's transpiler exports the set so a script is only assignable to entities that have them all --
	// which is what makes every component binding non-null by construction.
	std::vector<DSLComponentKind> requiredComponents;
	// This script's persistent per-instance data fields, reachable in the body as self.data.<name> -- Transpiler
	// emits them as a real "struct ScriptData { ... };" (+ ScriptDataSize()/REGISTER_SCRIPT_DATA_SIZE()) that
	// self.data casts the host's zeroed scriptData block to. Insertion order (append on add, like
	// requiredComponents); names unique within this list (enforced at add time).
	std::vector<DSLDataField> dataFields;
};

export inline bool dslIsComponentRequired(const DSL& document, DSLComponentKind kind)
{
	return std::find(document.requiredComponents.begin(), document.requiredComponents.end(), kind)
		!= document.requiredComponents.end();
}

// `name`'s field in `document.dataFields` (nullptr if none) -- the ONE lookup every self.data.<name> resolution
// site shares (ScriptLang.cpp's receiverCandidates, ScriptEditor's chain-building, ScriptLoader's dot-chain
// parser, Transpiler's memberText), so they can't drift apart on what counts as a match.
export inline const DSLDataField* dslFindDataField(const DSL& document, const std::string& name)
{
	for (const DSLDataField& field : document.dataFields)
		if (field.name == name)
			return &field;
	return nullptr;
}
