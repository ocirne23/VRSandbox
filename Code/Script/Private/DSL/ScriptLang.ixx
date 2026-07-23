export module Script:ScriptLang;

import Core;
import :DSL;
import :ScriptBindings;

// Human-readable name for a DSLType, as used in rendered code ("float", "vec3", "PhysicsComponent") and in the
// editor's compose UI (e.g. building up "float " before a variable name is typed). One canonical spelling
// shared by rendering and editing so they can't drift apart.
export const char* dslTypeName(DSLType type);

// Same for a DSLOperator ("+", "<=", "&&", "+=") -- the ONE spelling rendering, candidate labels, and every
// compose-prefix builder share.
export const char* dslOperatorText(DSLOperator op);

// Identifies a MUTABLE pointer location a span's symbol occupies, so an edit knows what to overwrite when
// replacing it -- a whole statement, a flow-control condition, a call argument's value, a declaration's
// initializer, or one operand of an Expression chain. Replacing a slot swaps its ENTIRE occupant; chain-level
// editing (inserting/removing individual operands and operators) goes through Kind::ExpressionOperand plus
// SyntaxSpan::operatorIndex below. `line` is always the symbol's owning line (see DSL.ixx's ownership model)
// -- where a freshly-built replacement gets pushed.
export struct SlotRef
{
	enum class Kind
	{
		None,                            // not editable (e.g. an assignment's target operand, glue text)
		LineHead,                        // this line's own top-level statement symbol
		FlowControlCondition,            // -> parent is the FlowControl symbol
		CallArgumentValue,               // -> parent is the FunctionCall symbol; argIndex selects which argument
		VariableDeclarationInitialValue, // -> parent is the VariableDeclaration symbol
		FunctionReturnType,              // -> parent is the FunctionDeclaration symbol; always present/selectable
		                                 // in expanded view (rendered blank when returnType == Void), so a
		                                 // function's return type can be set or changed at any time
		ExpressionOperand,               // -> parent is the Expression (operator chain) symbol; argIndex selects
		                                 // which operand -- what in-place term replace/insert/delete acts on
	};
	Kind kind = Kind::None;
	DSLSymbol* parent = nullptr; // unused for LineHead
	DSLCodeLine* line = nullptr;
	int argIndex = -1; // CallArgumentValue / ExpressionOperand only
};

// One contiguous, independently-selectable range of a rendered line's text, mapped back to the DSLSymbol it
// represents (see DSL.ixx's ownership-model comment: a span always identifies a real symbol, never a bare tree
// position). Ranges are left-to-right and non-overlapping within a line. `slot` is set (Kind != None) only when
// this span sits in one of the editable positions above -- that's what autocomplete-driven replacement acts on.
export struct SyntaxSpan
{
	DSLSymbol* symbol = nullptr;
	int startCol = 0;
	int endCol = 0; // exclusive
	SlotRef slot;
	int operatorIndex = -1;  // >= 0: this span IS operator #N of `symbol`'s Expression chain (the target of
	                         // in-place operator replacement and operator+operand deletion, see ScriptEditor)
	bool groupClose = false; // this span is a parenthesized group's own closing ')' -- selects the WHOLE group
	                         // as one unit ("after the parens"): typing an operator there chains a new term
	                         // after the group, Backspace deletes the group as a single chain term. `slot` is
	                         // the group's own position (same slot its enclosing context passed in).
};

// One rendered visual line. `sourceLine` is null only for a synthetic `end` line (see DSL.ixx: blocks close via
// DSLCodeLine::scopeLevel transitions, never a stored symbol) -- `endOfSymbol` then names the block-header
// symbol (If/While/For/FunctionDeclaration) that this `end` closes, so it's still a legitimate cursor stop.
// Every line produced by Syntax::format has at least one span (there is always a primary/head symbol to select).
export struct SyntaxLine
{
	DSLCodeLine* sourceLine = nullptr;
	DSLSymbol*   endOfSymbol = nullptr;
	int scopeLevel = 0;
	std::string text;
	std::vector<SyntaxSpan> spans; // left-to-right, non-overlapping, covers every selectable substring of text
};

// Turns a DSLScriptFile into rendered text + a symbol-tagged span list per line -- the one place that knows how
// a DSLSymbol tree reads as text. Deliberately independent from Transpiler (per the DSL editor's architecture:
// "what this looks like" and "how this becomes C++" are separately maintained systems, even though both walk
// the same symbol shapes) and from AutoCompleteRules ("what can be typed here" is a third, also-separate concern).
//
// `compact` hides ONLY function-parameter type annotations (both at a function's own declaration and at each of
// its call sites) and a function's return-type arrow -- NOT local variable declarations, which always show
// their type in both views. (Worked from the example program's compact/expanded comparison: `int thing = 1`
// and the for-loop header are byte-identical between views; only `doForce(...)`'s call-site types and
// `function update(...)`/`canJump() -> bool`'s own signatures differ.)
export class Syntax
{
public:
	// Takes a non-const DSLScriptFile so the returned spans/endOfSymbol carry mutable DSLSymbol* -- Syntax
	// itself never mutates anything, but M3's editing operations act directly through these same pointers.
	static std::vector<SyntaxLine> format(DSLScriptFile& file, bool compact);

	// True if `head` (a line's own top-level symbol) opens a nested block -- i.e. the FOLLOWING lines at
	// scopeLevel+1 are its body, closed by a synthetic `end`. Shared between format()'s end-insertion algorithm
	// and ScriptEditor's Enter-makes-a-new-line handling (which needs the same answer to decide whether the new
	// line belongs one level deeper or as a plain sibling).
	static bool isBlockOpener(const DSLSymbol* head);
};

export class TabGroup
{
public:
	TabGroup* parent = nullptr;
	std::vector<TabGroup> children;
};

// One offered completion. `refSymbol` names the existing declaration a Variable/Function/Reassign candidate
// resolves to; `declareType` is only meaningful for DeclareType (picking a type keyword to start a new local
// variable -- AutoCompleteRules doesn't invent the variable's NAME, that's a free-typed second step the editor
// drives); `op` is only meaningful for Comparator/AssignOperator (one of the comparison/compound-assign
// operators offered while building an if/while/for-loop clause). Reassign is a STATEMENT candidate (offered in
// the Void branch, like Function) naming an existing in-scope variable -- confirming it starts building a
// `name = value` assignment statement targeting it (see ScriptEditor's ReassignValue/ValueRight flow), distinct
// from Variable (a use of that same variable as a VALUE somewhere else).
export struct Candidate
{
	enum class Kind
	{
		KeywordIf, KeywordWhile, KeywordReturn, KeywordBreak, KeywordTrue, KeywordFalse,
		Variable, Function, DeclareType, Literal, Comparator, DeclareFunction, KeywordFor, AssignOperator, Reassign,
		ArithmeticOperator, LogicalOperator,
		KeywordElseIf, KeywordElse, // offered only on a statement slot INSIDE an if/elseif branch -- confirming
		                            // grows the chain with a new branch after the enclosing one (the elseif
		                            // stages its condition first, like any if; else needs the chain else-less)
		BindingObject, // a sidebar engine-object binding ("physics", "self") -- consumable only by dotting into
		               // it: '.' (or a confirm) opens its member/function list (see receiverCandidates)
		Member,        // one MEMBER of a binding object ("pos"): refSymbol = the RECEIVER's declaration,
		               // declareType = the member's value type -- resolves to a MemberAccess
	};
	std::string label;
	Kind kind = Kind::Variable;
	DSLSymbol* refSymbol = nullptr;
	DSLType declareType = DSLType::Void;
	DSLOperator op = DSLOperator::Equal;
	DSLSymbol* receiver = nullptr; // Function candidates from receiverCandidates: the receiver chain's ROOT
	                               // declaration (a sidebar binding or a struct-typed variable; null = free call)
	std::string receiverPath;      // the dotted member path between the root and the call, "" for a direct
	                               // dot-call -- "pos" in `self.pos.length()`. For Kind::Member the PATH rides
	                               // in `label` instead ("pos.x"), always relative to refSymbol as the root
	bool memberWritable = true;    // Kind::Member only: the underlying BindingMember::writable -- a chainable-
	                               // only member (self.physics) refuses to resolve as a bare assignment target
	                               // in statement context (confirmCompose), it must keep dotting further in
};

// "What can be typed here" -- kept independent from Syntax/Transpiler (see the DSL editor's architecture: three
// separately-maintained concerns that happen to walk the same symbol shapes). `expectedType` (read directly off
// the selected Placeholder, or DSLType::Void for a statement slot -- see DSLSymbol::Placeholder) decides the
// candidate shape: Void offers control-flow keywords + "declare a new variable of type X" per known type +
// in-scope function names (free call statements); any other type offers matching literals/keywords (true/false
// for Bool), in-scope variables of that type, and functions returning that type. `atLine` anchors the in-scope
// variable scan: sidebar + this function's own parameters + locals declared earlier in the SAME function
// (walking file.lines backward from atLine to its enclosing FunctionDeclaration header) -- cruder than real
// block scoping (doesn't distinguish sibling if/else branches) but enough for M3. `typedPrefix` filters by
// case-insensitive substring-from-start, same convention the old word-list autocomplete used. `excludeVariable`
// (optional) drops one specific VariableDeclaration from the in-scope-variables candidates -- used when
// re-editing an EXISTING declaration's own initializer, so e.g. `float test = ...` can't offer `test` itself
// as its own value (self-referential, meaningless before the declaration completes).
export class AutoCompleteRules
{
public:
	// `offerComparisonLeads` (Bool slots only) additionally offers NUMERIC variables/functions as a
	// comparison's left operand ("bool b = i < 5") -- passed only by value flows that support the comparator
	// continuation (the editor refuses such a lead as a direct value; only a typed comparator consumes it).
	static std::vector<Candidate> candidatesFor(DSLType expectedType, const DSLCodeLine& atLine, const DSLScriptFile& file,
		const std::vector<std::unique_ptr<DSLSymbol>>& sidebar, const std::vector<std::unique_ptr<DSLSymbol>>& builtins,
		const std::string& typedPrefix, DSLSymbol* excludeVariable = nullptr, bool offerComparisonLeads = false);

	// For an if/while condition's FIRST operand, whose type isn't known yet (it's what FIXES the type the
	// comparator/second operand must then match) -- every in-scope variable and every non-Void-returning
	// function, regardless of type. No free-typed literal option here: a bare literal's type would be
	// ambiguous (Int? Float? String?) with nothing yet to disambiguate it against -- comparisons in this DSL
	// always lead with a variable/call (`height <= 0.1`, `counter < 5`), matching the example program.
	// `excludeVariable` (optional) drops one declaration -- a declaration's own initializer (or a `&&`/`||`
	// chain grown from it) must never be able to reference the not-yet-existing variable it's building.
	static std::vector<Candidate> candidatesForAnyValue(const DSLCodeLine& atLine, const DSLScriptFile& file,
		const std::vector<std::unique_ptr<DSLSymbol>>& sidebar, const std::vector<std::unique_ptr<DSLSymbol>>& builtins,
		const std::string& typedPrefix, DSLSymbol* excludeVariable = nullptr);

	// Functions/members reachable by dotting into a receiver of `receiverType` -- a binding object (self) or any
	// engine-defined STRUCT value (vec3 and friends). `receiverDecl` is the chain's ROOT declaration, carried
	// into every candidate (`receiver` for functions, `refSymbol` for members) -- for a chained access
	// (`self.pos.x`, `self.physics.applyImpulse(...)`) the type is the PATH END's, while the root stays the
	// decl. expectedType Void + !anyValue = statement context (every function -- a call statement may ignore its
	// result -- plus WRITABLE members, the lead-in of a member assignment, plus chainable-but-unwritable members
	// like self.physics as dot-into waypoints); anyValue = condition-lead style (non-Void functions + every
	// member); otherwise a value slot of that type (chainable members always pass, so a chain can continue
	// through them toward a matching leaf). `document` gates component-bound members (self.physics/audio/force)
	// by DSL::requiredComponents, the receiver-side twin of appendBindingObjects' top-level gating.
	static std::vector<Candidate> receiverCandidates(const ScriptBindings& bindings, const DSL& document, DSLSymbol* receiverDecl,
		DSLType receiverType, DSLType expectedType, bool anyValue, const std::string& typedPrefix);

	// The six comparison operators (==, !=, <, >, <=, >=) offered while building an if/while condition's middle
	// term, filtered by typedPrefix same as any other candidate list (typing "<" narrows to "<" and "<=").
	static std::vector<Candidate> comparisonOperatorCandidates(const std::string& typedPrefix);

	// The five compound-assign operators (+=, -=, *=, /=, %=) offered while building a for-loop's increment
	// clause (`counter += 1`) -- a plain `=` isn't offered here, matching the example program's convention that
	// a for-loop's increment always compounds the loop variable rather than replacing it outright.
	static std::vector<Candidate> compoundAssignOperatorCandidates(const std::string& typedPrefix);

	// The five arithmetic operators (+, -, *, /, %) -- offered when REPLACING an arithmetic operator inside an
	// existing expression chain in place (see ScriptEditor's ReplaceOperator mode).
	static std::vector<Candidate> arithmeticOperatorCandidates(const std::string& typedPrefix);

	// The two logical operators (&&, ||) -- offered when replacing one inside an existing logical chain.
	static std::vector<Candidate> logicalOperatorCandidates(const std::string& typedPrefix);

	// All six assign-class operators (=, +=, -=, *=, /=, %=) -- offered when replacing an assignment
	// statement's (or a for-increment's) own operator in place. Superset of compoundAssignOperatorCandidates:
	// unlike a for-loop being AUTHORED, an existing statement's operator may become a plain `=` too.
	static std::vector<Candidate> assignOperatorCandidates(const std::string& typedPrefix);

	// What DSLType belongs at `slot` (DSLType::Void = a statement slot). Derived from the slot's STRUCTURAL
	// position (which field of which parent it is), not from whatever symbol currently occupies it -- so
	// replacing an already-filled real value offers the same, structurally-correct candidates a placeholder
	// in the same spot would.
	static DSLType expectedTypeForSlot(const SlotRef& slot, const DSLScriptFile& file);

	// Walks `file.lines` backward from `atLine` to its enclosing FunctionDeclaration header and returns its
	// declared return type (DSLType::Void if none is found).
	static DSLType enclosingFunctionReturnType(const DSLCodeLine& atLine, const DSLScriptFile& file);

	// Whether `name` collides with any variable declared anywhere in atLine's enclosing function (sidebar
	// bindings always count too) -- used to block declaring or renaming a variable to an already-taken name.
	// Deliberately broader than candidatesFor's in-scope-variables scan (which is direction-sensitive, only
	// "visible so far"): a collision must be caught regardless of whether the other declaration comes before
	// or after this point, or sits at a shallower or deeper nesting level -- renaming an outer/earlier variable
	// to match one declared later inside a nested block is still a collision, not just the reverse.
	// `excludeVariable` (optional) ignores one declaration -- e.g. a rename's own prior name shouldn't count
	// as a collision against itself.
	static bool isNameInScope(const std::string& name, const DSLCodeLine& atLine, const DSLScriptFile& file,
		const std::vector<std::unique_ptr<DSLSymbol>>& sidebar, DSLSymbol* excludeVariable = nullptr);

	// Whether `text` reads as a valid literal of `type` (String: always; Int/Float: must look like a real
	// number -- see candidatesFor's literal-candidate check). Exported so ScriptEditor can apply the SAME rule
	// when validating vec2/3/4's comma-separated component list, so both places agree on "a valid number".
	static bool isValidLiteralText(DSLType type, const std::string& text);

	// The same fixed list of primitive type keywords (int/float/bool/string/vec2/vec3/vec4) offered wherever a
	// TYPE is being picked rather than a value -- declaring a local variable (candidatesFor's Void branch),
	// declaring a new function's parameter, or setting a function's return type. One shared list so all three
	// stay in sync; each Candidate carries Kind::DeclareType (picking a type is picking a type, regardless of
	// what it's for -- the caller decides what to do with `declareType`).
	static std::vector<Candidate> typeKeywordCandidates(const std::string& typedPrefix);

	// Whether `name` is already used by ANY function -- a builtin (vec2/3/4, print, component methods) or a
	// user-declared one -- so a newly-declared function can't collide with or shadow an existing name. Unlike
	// isNameInScope, this is global (function names aren't scoped to where they're declared).
	// `excludeFunction` (optional) ignores one user declaration -- a re-authored function's own current name
	// shouldn't count as a collision against itself.
	static bool isFunctionNameTaken(const std::string& name, const DSLScriptFile& file, const std::vector<std::unique_ptr<DSLSymbol>>& builtins,
		DSLSymbol* excludeFunction = nullptr);

	// Whether `varDecl` (a local variable's own VariableDeclaration) is used by any VariableReference anywhere
	// in its enclosing function -- guards deleting the line that declares it (see ScriptEditor::handleKeyEvent's
	// Backspace handling): a still-used declaration can't be removed out from under its uses.
	static bool isVariableReferenced(const DSLSymbol* varDecl, const DSLScriptFile& file);

	// Whether `funcDecl` is called anywhere in the document (any FunctionCall::functionSymbol pointing at it) --
	// guards deleting an (empty) function declaration the same way isVariableReferenced guards a local's. Global,
	// not scoped to one function, since a function can be called from anywhere (see knownFunctions).
	static bool isFunctionReferenced(const DSLSymbol* funcDecl, const DSLScriptFile& file);
};

/*
// --------------
// sidebar
World world
Entity self
PhysicsComponent physics

// --------------
// main (compact view)
function update(deltaSec)
	float applied = 0.0
	if canJump()
		doForce(dir = vec3(0, 1, 0), force = 1.0, ref appliedForce = applied)
		print("Jumped! (force: {})", applied)
	end

	int thing = 1;
	for int counter = 0, counter < 5, counter += 1
		print("counter is {}", counter)
		thing += counter
	end

	while thing > 5
		print("thing is {}", thing)
		thing -= 5
	end

end

function doForce(dir, force, ref appliedForce)
	float toApply = physics.getMass() * force;
	physics.applyForce(direction = dir, force = toApply)
	ref appliedForce = toApply
end

function canJump()
	float height = world.rayCast(pos = self.pos + vec3(0, 0.5, 0), dir = vec3(0, -1, 0), maxRayDist = 1.0);
	if height <= 0.1
		return true
	else
		return false
	end
end

// --------------
// main (expanded view)
function update(float deltaSec)
	float applied = 0.0
	if canJump()
		doForce(vec3 dir = vec3(0, 1, 0), float force = 1.0, ref float appliedForce = applied)
		print("Jumped! (force: {})", applied)
	end

	int thing = 1;
	for int counter = 0, counter < 5, counter += 1
		print("counter is {}", counter)
		thing += counter
	end

	while thing > 5
		print("thing is {}", thing)
		thing -= 5
	end
end

function doForce(vec3 dir, float force, ref float appliedForce)
	float toApply = physics.getMass() * force;
	physics.applyForce(vec3 direction = dir, float force = toApply)
	ref appliedForce = toApply
end

function canJump() -> bool
	float height = world.rayCast(vec3 pos = self.pos + vec3(0, 0.5, 0), vec3 dir = vec3(0, -1, 0), float maxRayDist = 1.0);
	if height lesseq 0.1
		return true
	else
		return false
	end
end


*/