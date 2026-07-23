module UI;

import Core;
import Core.Log;
import Core.imgui;
import Core.SDL;
import Script;
import :ScriptEditor;

using ST = DSLSymbol::SymbolType;

namespace
{
	// Maps a raw SDL keycode to the printable character it produces, or 0 if it doesn't produce one. SDL
	// keycodes for the printable ASCII range ARE the unshifted ASCII value (see SDL_keycode.h, e.g. SDLK_A ==
	// 'a', SDLK_COMMA == ','), so shift is applied by hand here -- US QWERTY only, no layout/IME awareness.
	char charFromKeycode(int keycode, bool shift)
	{
		if (keycode < 0x20 || keycode > 0x7E)
			return 0;
		const char c = static_cast<char>(keycode);

		if (c >= 'a' && c <= 'z')
			return shift ? static_cast<char>(c - 'a' + 'A') : c;
		if (!shift)
			return c;

		switch (c)
		{
		case '1': return '!'; case '2': return '@'; case '3': return '#'; case '4': return '$';
		case '5': return '%'; case '6': return '^'; case '7': return '&'; case '8': return '*';
		case '9': return '('; case '0': return ')';
		case '-': return '_'; case '=': return '+';
		case '[': return '{'; case ']': return '}'; case '\\': return '|';
		case ';': return ':'; case '\'': return '"';
		case ',': return '<'; case '.': return '>'; case '/': return '?';
		case '`': return '~';
		default: return c;
		}
	}

	// Only letters/digits/underscore (identifiers, keywords) and '.' (decimal literals) are meaningful while
	// composing -- everything else (=, +, -, etc.) is scaffolding the box/box-template already supplies
	// (e.g. confirming a declare-type candidate already appends " = " -- retyping '=' would double it up), so
	// it's silently dropped rather than corrupting the composed text.
	bool isComposeChar(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
	}

	// A new variable's name (DeclareName) is stricter than general composing: letters/digits/underscore only
	// (no '.', names never contain one) and never a digit as the very FIRST character -- "1.0" is a valid
	// number but not a valid identifier, and AutoCompleteRules can't be asked to reject it since a freshly
	// typed name isn't matched against anything.
	bool isIdentifierChar(char c, bool isFirstChar)
	{
		if (c == '_')
			return true;
		if (std::isalpha(static_cast<unsigned char>(c)))
			return true;
		return std::isdigit(static_cast<unsigned char>(c)) && !isFirstChar;
	}

	// Declaring a vec2/3/4 (DeclareValue, free-typed components -- see applyDeclareVariable) accepts digits,
	// '.', '-' (negative components, e.g. `vec3(0, -1, 0)`), and ',' as the component separator -- notably NOT
	// letters, since a component is always a number here, never a variable reference.
	bool isVectorComponentChar(char c)
	{
		return std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ',';
	}

	// While picking an if/while condition's comparator (ConditionOp), only the symbols the six comparison
	// operators are actually built from are meaningful -- letters/digits would never match any of them anyway.
	bool isOperatorChar(char c)
	{
		return c == '<' || c == '>' || c == '=' || c == '!';
	}

	// While picking a for-loop's increment operator (ForIncrementOp), only the symbols the five compound-assign
	// operators are built from are meaningful.
	bool isAssignOperatorChar(char c)
	{
		return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '=';
	}

	// Typing one of these while a value candidate is already matched (DeclareValue/ReassignValue) continues a
	// compound expression -- see the class comment and the expr* family of helpers.
	bool isArithmeticOperatorChar(char c)
	{
		return c == '+' || c == '-' || c == '*' || c == '/' || c == '%';
	}

	DSLOperator arithmeticOperatorFromChar(char c)
	{
		switch (c)
		{
		case '+': return DSLOperator::Add;
		case '-': return DSLOperator::Subtract;
		case '*': return DSLOperator::Multiply;
		case '/': return DSLOperator::Divide;
		case '%': return DSLOperator::Modulus;
		default:  return DSLOperator::Add;
		}
	}

	// The DSLType a resolved condition operand will have once built -- lets the comparator/second-operand
	// steps constrain their own candidates to match it. Literal/true/false matter for RE-authored conditions
	// (see tryWidenFlowHeaderEdit): in-place editing may have put a literal on the left side, and its restored
	// candidate must still pin the right side's type.
	DSLType candidateValueType(const Candidate& candidate)
	{
		if (candidate.kind == Candidate::Kind::Variable)
		{
			// A SENTINEL loop-variable candidate (see ScriptEditor's for-loop staging) has no symbol yet --
			// its type rides in declareType instead.
			if (candidate.refSymbol == nullptr)
				return candidate.declareType;
			const DSLSymbol::VariableDeclaration& v = std::get<DSLSymbol::VariableDeclaration>(candidate.refSymbol->data);
			return std::get<DSLSymbol::TypeDeclaration>(v.typeSymbol->data).type;
		}
		if (candidate.kind == Candidate::Kind::Function)
			return std::get<DSLSymbol::FunctionDeclaration>(candidate.refSymbol->data).returnType;
		if (candidate.kind == Candidate::Kind::Literal)
			return candidate.declareType;
		if (candidate.kind == Candidate::Kind::KeywordTrue || candidate.kind == Candidate::Kind::KeywordFalse)
			return DSLType::Bool;
		if (candidate.kind == Candidate::Kind::Member)
			return candidate.declareType; // the member's own registry-stamped type (see receiverCandidates)
		return DSLType::Void;
	}

	// A Function candidate whose callee takes parameters -- consumable as a value TERM only after its
	// arguments stage through the CallArgValue sub-flow (never with placeholder arguments).
	bool isParameterizedFunction(const Candidate& candidate)
	{
		return candidate.kind == Candidate::Kind::Function && candidate.refSymbol != nullptr
			&& !std::get<DSLSymbol::FunctionDeclaration>(candidate.refSymbol->data).parameterVarDeclarations.empty();
	}

	// The element type a chain of pending terms evaluates in -- the first resolvable term's type (groups
	// recurse). Void = nothing resolvable (or an empty chain).
	DSLType chainElementType(const std::vector<PendingExprTerm>& terms)
	{
		for (const PendingExprTerm& term : terms)
		{
			const DSLType t = term.isGroup ? chainElementType(term.groupTerms) : candidateValueType(term.candidate);
			if (t != DSLType::Void)
				return t;
		}
		return DSLType::Void;
	}

	// Whether any composed term (recursing into groups) is NOT Bool-typed -- guards the Bool value commits: a
	// numeric comparison LEAD offered in a Bool slot ("bool b = i < 5"'s `i`) must never commit as the value
	// itself; only a typed comparator consumes it.
	bool containsNonBoolTerm(const std::vector<PendingExprTerm>& terms)
	{
		for (const PendingExprTerm& term : terms)
		{
			if (term.isGroup)
			{
				if (containsNonBoolTerm(term.groupTerms))
					return true;
			}
			else if (candidateValueType(term.candidate) != DSLType::Bool)
			{
				return true;
			}
		}
		return false;
	}

	// "if "/"elseif "/"while " -- the fixed prefix a condition-building flow's box always starts from,
	// reconstructable at any stage from just m_conditionControl (used both to seed the box and to trim it back
	// on step-back). ElseIf only occurs when RE-authoring an existing header (the fresh-statement candidates
	// never offer it -- see m_flowEditLine).
	const char* conditionKeywordPrefix(DSLFlowControl control)
	{
		if (control == DSLFlowControl::ElseIf)
			return "elseif ";
		return control == DSLFlowControl::If ? "if " : "while ";
	}

	// A real (non-synthetic-end) line whose sole content is an unfilled STATEMENT placeholder -- renders as a
	// blank line (see Syntax's Placeholder case), so Backspace there removes it outright instead of opening it
	// for editing (there's nothing meaningful to "edit" on a line that's empty).
	bool isBlankStatementLine(const SyntaxLine& line)
	{
		if (line.sourceLine == nullptr)
			return false;
		const DSLSymbol* head = line.sourceLine->head();
		return head != nullptr && head->type == ST::Placeholder && std::get<DSLSymbol::Placeholder>(head->data).expectedType == DSLType::Void;
	}

	// Same check as isBlankStatementLine, but directly against a DSLCodeLine (no formatted SyntaxLine at hand) --
	// used by isBlockBodyEmpty to judge a block's body without re-running Syntax::format. A line with zero
	// symbols at all (shouldn't normally occur) counts as blank too, rather than as "real content".
	bool isBlankStatementDSLLine(const DSLCodeLine& line)
	{
		const DSLSymbol* head = line.head();
		return head == nullptr
			|| (head->type == ST::Placeholder && std::get<DSLSymbol::Placeholder>(head->data).expectedType == DSLType::Void);
	}

	// How a chosen-but-not-yet-applied candidate should read in a growing compose prefix (e.g. ConditionLeft's
	// "if height " or "if canJump() ") -- a bare Candidate::label is correct for everything except Function,
	// which needs its call parens (and one <type> placeholder marker per parameter it'll be built with) shown
	// too, or e.g. "canJump() == true" would misleadingly read as "canJump == true" before it's even committed.
	std::string candidateDisplayText(const Candidate& candidate)
	{
		// A binding object's MEMBER reads with its receiver ("self.pos") -- refSymbol IS the receiver's
		// declaration for Kind::Member (see receiverCandidates).
		if (candidate.kind == Candidate::Kind::Member)
			return std::get<DSLSymbol::VariableDeclaration>(candidate.refSymbol->data).name + "." + candidate.label;
		if (candidate.kind != Candidate::Kind::Function)
			return candidate.label;

		const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(candidate.refSymbol->data);
		std::string text = (candidate.receiver != nullptr
			? std::get<DSLSymbol::VariableDeclaration>(candidate.receiver->data).name + "."
				+ (candidate.receiverPath.empty() ? std::string() : candidate.receiverPath + ".")
			: std::string())
			+ candidate.label + "(";
		for (size_t i = 0; i < callee.parameterVarDeclarations.size(); ++i)
		{
			if (i > 0)
				text += ", ";
			const DSLSymbol::VariableDeclaration& param = std::get<DSLSymbol::VariableDeclaration>(callee.parameterVarDeclarations[i]->data);
			const DSLType paramType = std::get<DSLSymbol::TypeDeclaration>(param.typeSymbol->data).type;
			text += "<" + std::string(dslTypeName(paramType)) + ">";
		}
		text += ")";
		return text;
	}

	DSLType declaredTypeOf(const DSLSymbol* varDecl)
	{
		const DSLSymbol::VariableDeclaration& decl = std::get<DSLSymbol::VariableDeclaration>(varDecl->data);
		return std::get<DSLSymbol::TypeDeclaration>(decl.typeSymbol->data).type;
	}

	std::string joinedMemberPath(const std::vector<std::string>& path)
	{
		std::string text;
		for (const std::string& segment : path)
			text += (text.empty() ? "" : ".") + segment;
		return text;
	}

	std::vector<std::string> splitMemberPath(const std::string& dottedPath)
	{
		std::vector<std::string> path;
		size_t start = 0;
		while (start < dottedPath.size())
		{
			const size_t dot = dottedPath.find('.', start);
			path.push_back(dottedPath.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
			if (dot == std::string::npos)
				break;
			start = dot + 1;
		}
		return path;
	}

	// The comma-component shorthand ("vec3 test = 1,2,3") is REMOVED: struct types (vec2/3/4 and every future
	// engine struct) compose as ordinary value slots -- their registry constructor is a normal parameterized
	// call staged like any other, so computed components work too. Every branch gated on this is now dead and
	// awaits a cleanup pass; returning false here is what turned them all off at once.
	bool isVectorType(DSLType)
	{
		return false;
	}

	int vectorComponentCount(DSLType)
	{
		return 0;
	}

	// Splits "1.0,2.0,3.0" into its comma-separated parts (no trimming needed: space is never accepted as a
	// vector-component character in the first place, see handleKeyEvent, so parts never contain whitespace).
	std::vector<std::string> splitOnCommas(const std::string& text)
	{
		std::vector<std::string> parts;
		std::string current;
		for (char c : text)
		{
			if (c == ',')
			{
				parts.push_back(current);
				current.clear();
			}
			else
			{
				current += c;
			}
		}
		parts.push_back(current);
		return parts;
	}

	// Whether `text` is a COMPLETE, valid comma-component list for `type` -- what every vector-typed value
	// stage requires before its confirm goes through (the editor never commits a partial value; there are no
	// placeholder fallbacks to lean on).
	bool vectorComponentsValid(DSLType type, const std::string& text)
	{
		const std::vector<std::string> parts = splitOnCommas(text);
		if (static_cast<int>(parts.size()) != vectorComponentCount(type))
			return false;
		for (const std::string& part : parts)
			if (!AutoCompleteRules::isValidLiteralText(DSLType::Float, part))
				return false;
		return true;
	}
}

// Builds the binding registry (sidebar objects + builtin functions -- see ScriptBindings) once at startup,
// then seeds the document with a single empty entry-point function -- a starting point to build from.
void ScriptEditor::buildExampleDocument()
{
	m_bindings.build(m_document.sidebar, m_builtins);

	auto newLine = [](int scopeLevel) -> std::unique_ptr<DSLCodeLine>
	{
		auto line = std::make_unique<DSLCodeLine>();
		line->scopeLevel = scopeLevel;
		return line;
	};

	// ---- update(deltaSec): the sole starting function, empty -- the user builds everything else from here ----
	auto updateHeader = newLine(0);
	DSLSymbol* deltaSecType  = pushSymbol(*updateHeader, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ DSLType::Float });
	DSLSymbol* deltaSecParam = pushSymbol(*updateHeader, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ "deltaSec", deltaSecType });
	pushSymbol(*updateHeader, ST::FunctionDeclaration, DSLSymbol::FunctionDeclaration{ "update", { deltaSecParam }, DSLType::Void });
	m_document.file.lines.push_back(std::move(updateHeader));

	// A single blank statement slot for the body -- see render()'s "always one blank line" invariant.
	auto blankBodyLine = newLine(1);
	seedStatementPlaceholder(*blankBodyLine);
	m_document.file.lines.push_back(std::move(blankBodyLine));
}

void ScriptEditor::clampCursor()
{
	if (m_formatted.empty())
	{
		m_cursorLine = 0;
		m_cursorSpan = 0;
		return;
	}
	m_cursorLine = std::clamp(m_cursorLine, 0, static_cast<int>(m_formatted.size()) - 1);
	const int spanCount = static_cast<int>(m_formatted[m_cursorLine].spans.size());
	m_cursorSpan = std::clamp(m_cursorSpan, 0, std::max(0, spanCount - 1));
}

void ScriptEditor::moveHorizontal(int delta)
{
	if (m_formatted.empty())
		return;

	int line = m_cursorLine;
	int span = m_cursorSpan + delta;

	while (span < 0 && line > 0)
	{
		--line;
		span += static_cast<int>(m_formatted[line].spans.size());
	}
	while (line < static_cast<int>(m_formatted.size()) - 1 && span >= static_cast<int>(m_formatted[line].spans.size()))
	{
		span -= static_cast<int>(m_formatted[line].spans.size());
		++line;
	}

	m_cursorLine = line;
	m_cursorSpan = span;
	clampCursor();
}

// Up/Down always lands at the END of the target line -- the spot typing/backspacing continues from (matching
// deleteLine's landing rule), rather than a nearest-column guess that changes meaning line to line.
void ScriptEditor::moveVertical(int delta)
{
	if (m_formatted.empty())
		return;

	const int targetLine = std::clamp(m_cursorLine + delta, 0, static_cast<int>(m_formatted.size()) - 1);
	if (targetLine == m_cursorLine)
		return;

	m_cursorLine = targetLine;
	m_cursorSpan = std::max(0, static_cast<int>(m_formatted[targetLine].spans.size()) - 1);
}

void ScriptEditor::moveHome()
{
	m_cursorSpan = 0;
}

void ScriptEditor::moveEnd()
{
	if (m_formatted.empty())
		return;
	m_cursorSpan = static_cast<int>(m_formatted[m_cursorLine].spans.size()) - 1;
}

// Finds `afterLine` in m_document.file.lines and inserts a fresh empty line right after it (falls back to
// appending at the end if afterLine isn't found, which shouldn't happen).
DSLCodeLine& ScriptEditor::insertLineAfter(DSLCodeLine& afterLine, int scopeLevel)
{
	auto& lines = m_document.file.lines;
	const int index = dslLineIndex(m_document.file, &afterLine);
	auto newLine = std::make_unique<DSLCodeLine>();
	newLine->scopeLevel = scopeLevel;
	DSLCodeLine& ref = *newLine;
	lines.insert(index >= 0 ? lines.begin() + index + 1 : lines.end(), std::move(newLine));
	return ref;
}

DSLSymbol* ScriptEditor::seedStatementPlaceholder(DSLCodeLine& line)
{
	auto symbol = std::make_unique<DSLSymbol>();
	symbol->type = ST::Placeholder;
	symbol->data = DSLSymbol::Placeholder{ DSLType::Void };
	symbol->line = &line;
	DSLSymbol* ptr = symbol.get();
	line.symbols.push_back(std::move(symbol));
	return ptr;
}

// The (line, span) currently selected, resolved defensively against m_formatted's current bounds. Returns
// nullptr if there's nothing sensible to look at (empty document/line).
namespace
{
	const SyntaxSpan* currentSpan(const std::vector<SyntaxLine>& formatted, int cursorLine, int cursorSpan)
	{
		if (formatted.empty() || cursorLine < 0 || cursorLine >= static_cast<int>(formatted.size()))
			return nullptr;
		const SyntaxLine& line = formatted[cursorLine];
		if (line.spans.empty())
			return nullptr;
		return &line.spans[std::clamp(cursorSpan, 0, static_cast<int>(line.spans.size()) - 1)];
	}
}

void ScriptEditor::beginCompose()
{
	const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
	if (span == nullptr)
		return;

	// A VariableDeclaration's own name span (its declaration site -- a use elsewhere is a VariableReference,
	// unaffected) is always a rename, regardless of what slot it happens to sit in (e.g. a bare local decl
	// statement's name doubles as that whole line's LineHead slot, and a call site's parameter-name span IS
	// the callee's parameter declaration) -- a name isn't a value with candidates to pick from, so slot-based
	// replacement doesn't apply to it.
	if (span->symbol != nullptr && span->symbol->type == ST::VariableDeclaration)
	{
		if (span->symbol->line == nullptr)
			return; // a BUILTIN's parameter (vec3's components, component methods' params) -- not user-editable
		// Renaming only happens at the DECLARATION site: a call site's parameter-name span carries the same
		// symbol but sits on the CALL's own line, and it stays a highlight/navigation stop only -- editing a
		// function's signature belongs at the function.
		if (m_formatted[m_cursorLine].sourceLine != span->symbol->line)
			return;
		// A parameter of a LOCKED entry-point function (its line's own head) -- its name/type/existence are
		// fixed by the real ScriptAPI signature (see EntryPointDef), never user-editable.
		if (isLockedEntryFunction(span->symbol->line->head()))
			return;
		m_renameTarget = span->symbol;
		enterCompose(ComposeMode::Rename, "");
		return;
	}

	// A comment's span re-opens its free-text compose with the text restored for editing.
	if (span->symbol != nullptr && span->symbol->type == ST::Comment)
	{
		enterCompose(ComposeMode::CommentText, "# ", std::get<DSLSymbol::Comment>(span->symbol->data).text);
		return;
	}

	// A function's own NAME span (its declaration header -- call sites carry FunctionCall symbols instead, so
	// this can only match there) renames the function, same idiom as a variable's declaration name: call sites
	// reference the symbol and follow automatically. Never the generic statement-slot replacement its LineHead
	// slot would otherwise get -- wholesale-replacing a header would dangle its parameters.
	if (span->symbol != nullptr && span->symbol->type == ST::FunctionDeclaration && span->slot.kind == SlotRef::Kind::LineHead)
	{
		// A LOCKED entry-point function's own name is fixed -- it's the exported symbol ScriptHost looks up by
		// that exact spelling (see EntryPointDef); renaming it would silently stop it being a real entry point.
		if (isLockedEntryFunction(span->symbol))
			return;
		m_renameTarget = span->symbol;
		enterCompose(ComposeMode::Rename, "");
		return;
	}

	if (span->slot.kind == SlotRef::Kind::FunctionReturnType)
	{
		// A LOCKED entry-point function always returns void, non-negotiably (see EntryPointDef).
		if (isLockedEntryFunction(span->symbol))
			return;
		// A called function's return type is load-bearing at every call site's typing -- it can only change
		// while nothing references the function.
		if (AutoCompleteRules::isFunctionReferenced(span->symbol, m_document.file))
			return;
		// "-> " is only PART of the compose box when the span itself is blank (returnType == Void, see
		// renderSymbol's FunctionDeclaration case) -- when a type is already set, " -> " is already part of the
		// surrounding line text BEFORE this span (only the type name itself is the span/what's being replaced),
		// so prefixing it again here would visually double it up.
		const DSLSymbol::FunctionDeclaration& f = std::get<DSLSymbol::FunctionDeclaration>(span->symbol->data);
		enterCompose(ComposeMode::FunctionReturnType, (f.returnType == DSLType::Void) ? "-> " : "");
		return;
	}

	if (span->slot.kind == SlotRef::Kind::None)
		return; // this span isn't an editable position

	// A whole-statement slot composes against the statement candidate list; every VALUE slot (a condition, a
	// call argument, an initializer, one operand of an expression chain) instead begins an in-place expression
	// edit -- same candidate lists, but the replacement may itself grow into a compound/parenthesized chain.
	if (span->slot.kind == SlotRef::Kind::LineHead)
		enterCompose(ComposeMode::FilterCandidates, "");
	else
		beginEditExprReplace(*span);
}

void ScriptEditor::refreshCandidates()
{
	// The sidebar-root binding objects (just "self" today) offered wherever a dot-into makes sense --
	// consumable only via '.'/confirm into MemberSelect, never as bare values (see receiverCandidates).
	// Component bindings (physics/audio/force) are reached through self's own members instead, gated there by
	// requiredComponent -- see receiverCandidates. An exact-name match goes first, same courtesy
	// sortExactMatchFirst gives the regular lists.
	auto appendBindingObjects = [&]()
	{
		for (const std::unique_ptr<DSLSymbol>& s : m_document.sidebar)
		{
			if (s->type != ST::VariableDeclaration)
				continue;
			const BindingObject* object = m_bindings.objectForDecl(s.get());
			if (object == nullptr)
				continue;
			const std::string label = object->name;
			if (m_pendingWord.size() > label.size())
				continue;
			bool matches = true;
			for (size_t i = 0; i < m_pendingWord.size() && matches; ++i)
				matches = std::tolower(static_cast<unsigned char>(label[i])) == std::tolower(static_cast<unsigned char>(m_pendingWord[i]));
			if (!matches)
				continue;
			Candidate c{ label, Candidate::Kind::BindingObject, s.get() };
			if (label.size() == m_pendingWord.size())
				m_candidates.insert(m_candidates.begin(), std::move(c));
			else
				m_candidates.push_back(std::move(c));
		}
	};

	// The in-place editing modes anchor to state captured at entry (the cursor span they started from may
	// carry no slot of its own -- e.g. a chain operator span past the first), not to the current span.
	if (m_composeMode == ComposeMode::ReplaceOperator)
	{
		// Same class as the operator being replaced -- a `+` can become `*`, an `&&` an `||`, never a `<=`.
		const DSLOperator current = std::get<DSLSymbol::Expression>(m_replaceOpExpr->data).operators[m_replaceOpIndex];
		m_candidates = dslIsComparisonOperator(current) ? AutoCompleteRules::comparisonOperatorCandidates(m_pendingWord)
			: dslIsAssignOperator(current) ? AutoCompleteRules::assignOperatorCandidates(m_pendingWord)
			: dslIsLogicalOperator(current) ? AutoCompleteRules::logicalOperatorCandidates(m_pendingWord)
			: AutoCompleteRules::arithmeticOperatorCandidates(m_pendingWord);
		m_candidateSelected = 0;
		return;
	}
	if (m_composeMode == ComposeMode::MemberSelect)
	{
		m_candidates = AutoCompleteRules::receiverCandidates(m_bindings, m_document, m_memberReceiver, m_memberReceiverType,
			m_memberExpectedType, m_memberAnyValue, m_pendingWord);
		m_candidateSelected = 0;
		return;
	}
	if (m_composeMode == ComposeMode::EditExpr)
	{
		if (m_editSlot.line == nullptr || isVectorType(m_editValueType))
			return; // vector slots free-type comma components -- no list to filter
		// Same self-reference exclusion as re-editing a declaration's initializer via the old slot path.
		DSLSymbol* excludeVariable = (m_editSlot.kind == SlotRef::Kind::VariableDeclarationInitialValue) ? m_editSlot.parent : nullptr;
		m_candidates = (m_editValueType == DSLType::Void)
			? AutoCompleteRules::candidatesForAnyValue(*m_editSlot.line, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, excludeVariable)
			: AutoCompleteRules::candidatesFor(m_editValueType, *m_editSlot.line, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, excludeVariable, /*offerComparisonLeads*/ true);
		appendBindingObjects();
		m_candidateSelected = 0;
		return;
	}

	const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
	if (span == nullptr || span->slot.line == nullptr)
		return;
	DSLCodeLine& atLine = *span->slot.line;

	// A for-loop still being STAGED has no committed loop-variable symbol, so its own name is offered as a
	// SENTINEL candidate (refSymbol null -- see m_forBuildLoopVar) in the clauses that may reference it;
	// re-edits skip this, their real symbol comes through the normal scope scan.
	auto injectLoopVarCandidate = [&](DSLType expectedType)
	{
		if (m_flowEditLoopVar != nullptr || m_forVarName.empty())
			return;
		if (expectedType != DSLType::Void && expectedType != m_forVarType)
			return;
		if (m_pendingWord.size() > m_forVarName.size())
			return;
		for (size_t i = 0; i < m_pendingWord.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(m_forVarName[i])) != std::tolower(static_cast<unsigned char>(m_pendingWord[i])))
				return;
		m_candidates.insert(m_candidates.begin(), Candidate{ m_forVarName, Candidate::Kind::Variable, nullptr, m_forVarType });
	};
	// Mid-chain terms lock to the chain's element type; Void = nothing composed yet (the first term is free).
	const DSLType liveChainType = (!m_exprStack.empty() && !m_exprStack[0].terms.empty())
		? chainElementType(m_exprStack[0].terms) : DSLType::Void;

	switch (m_composeMode)
	{
	case ComposeMode::FilterCandidates:
		// Statement slots only -- every VALUE slot routes into EditExpr instead (see beginCompose).
		m_candidates = AutoCompleteRules::candidatesFor(DSLType::Void, atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord);
		break;
	case ComposeMode::DeclareValue:
		// vec2/3/4 build their initializer from typed comma-separated components instead (see
		// applyDeclareVariable) -- nothing to filter, same as DeclareName's free-typing. Otherwise, every term
		// of the (possibly compound) expression is constrained to the SAME declared type. When RE-authoring an
		// existing declaration (m_redeclareTarget, else null = no-op) it can't offer itself as its own value.
		if (isVectorType(m_pendingDeclareType))
			return;
		m_candidates = AutoCompleteRules::candidatesFor(m_pendingDeclareType, atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, m_redeclareTarget, /*offerComparisonLeads*/ true);
		break;
	case ComposeMode::ConditionLeft:
	{
		// Excludes the declaration being built/re-edited through a suspended value-mode handoff ("bool test =
		// ... && test" must never resolve -- see conditionValueExcludeVariable). Irrelevant for a genuine
		// if/while/elseif condition (m_conditionValueReturnMode is None there), where it's just nullptr.
		DSLSymbol* excludeVariable = conditionValueExcludeVariable();
		m_candidates = (liveChainType == DSLType::Void)
			? AutoCompleteRules::candidatesForAnyValue(atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, excludeVariable)
			: AutoCompleteRules::candidatesFor(liveChainType, atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, excludeVariable);
		break;
	}
	case ComposeMode::ConditionOp:
		m_candidates = AutoCompleteRules::comparisonOperatorCandidates(m_pendingWord);
		break;
	case ComposeMode::ConditionRight:
		// Constrained to the left side's own type -- `height <= "hi"` isn't a sensible comparison.
		m_candidates = AutoCompleteRules::candidatesFor(chainElementType(m_conditionLeftChain.terms), atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, conditionValueExcludeVariable());
		break;
	case ComposeMode::ForConditionLeft:
		m_candidates = (liveChainType == DSLType::Void)
			? AutoCompleteRules::candidatesForAnyValue(atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord)
			: AutoCompleteRules::candidatesFor(liveChainType, atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord);
		injectLoopVarCandidate(liveChainType);
		break;
	case ComposeMode::FunctionParamType:
	case ComposeMode::FunctionReturnType:
	case ComposeMode::FunctionDeclareReturnType:
	case ComposeMode::ForVarType:
		m_candidates = AutoCompleteRules::typeKeywordCandidates(m_pendingWord);
		break;
	case ComposeMode::ForVarValue:
		// vec2/3/4 build their initializer from typed comma-separated components instead, same as DeclareValue.
		if (isVectorType(m_forVarType))
			return;
		m_candidates = AutoCompleteRules::candidatesFor(m_forVarType, atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord);
		break;
	case ComposeMode::ForConditionOp:
		m_candidates = AutoCompleteRules::comparisonOperatorCandidates(m_pendingWord);
		break;
	case ComposeMode::ForConditionValue:
	{
		// The bound matches the condition's LEFT side (usually the loop variable's type).
		DSLType boundType = chainElementType(m_forConditionLeftChain.terms);
		if (boundType == DSLType::Void)
			boundType = m_forVarType;
		m_candidates = AutoCompleteRules::candidatesFor(boundType, atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord);
		injectLoopVarCandidate(boundType);
		break;
	}
	case ComposeMode::ForIncrementValue:
		// The increment's step is constrained to the loop variable's own type.
		m_candidates = AutoCompleteRules::candidatesFor(m_forVarType, atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord);
		injectLoopVarCandidate(m_forVarType);
		break;
	case ComposeMode::ForIncrementOp:
		m_candidates = AutoCompleteRules::compoundAssignOperatorCandidates(m_pendingWord);
		break;
	case ComposeMode::ReassignValue:
		// Same as DeclareValue: vec2/3/4 free-type their components instead; otherwise every term is
		// constrained to the target variable's own type.
		if (isVectorType(reassignTargetType()))
			return;
		m_candidates = AutoCompleteRules::candidatesFor(reassignTargetType(), atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, nullptr, /*offerComparisonLeads*/ true);
		break;
	case ComposeMode::ReassignOp:
		m_candidates = AutoCompleteRules::assignOperatorCandidates(m_pendingWord);
		break;
	case ComposeMode::ReturnValue:
		m_candidates = AutoCompleteRules::candidatesFor(AutoCompleteRules::enclosingFunctionReturnType(atLine, m_document.file),
			atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord, nullptr, /*offerComparisonLeads*/ true);
		break;
	case ComposeMode::CallArgValue:
	{
		// A parameterized call/dot-into waypoint IS offered here now -- an argument is a full chain compose
		// (m_exprStack), so a nested call stages via the same tryBeginValueCallStaging every other chain mode
		// uses (see m_callStack), and dotting into a struct-typed value works too.
		m_candidates = AutoCompleteRules::candidatesFor(currentCallParamType(), atLine, m_document.file, m_document.sidebar, m_builtins, m_pendingWord);
		// A `ref` parameter receives the callee's OUTPUT -- only an actual variable can stand there, never a
		// nested call/compound expression, so those (and dot-into waypoints) are excluded for it specifically.
		const CallStage& stage = m_callStack.back();
		const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(stage.func->data);
		if (std::get<DSLSymbol::VariableDeclaration>(callee.parameterVarDeclarations[stage.argChains.size()]->data).isRef)
			std::erase_if(m_candidates, [](const Candidate& c) { return c.kind != Candidate::Kind::Variable; });
		break;
	}
	default:
		return; // DeclareName/Rename/FunctionDeclareName/FunctionParamName/ForVarName: free-typing an identifier, nothing to filter against
	}

	// Binding objects join every statement/value list (never the operator/type-keyword pickers above -- those
	// broke out through their own cases' breaks but are excluded here).
	switch (m_composeMode)
	{
	case ComposeMode::FilterCandidates:
	case ComposeMode::DeclareValue:
	case ComposeMode::ConditionLeft:
	case ComposeMode::ConditionRight:
	case ComposeMode::ForConditionLeft:
	case ComposeMode::ForVarValue:
	case ComposeMode::ForConditionValue:
	case ComposeMode::ForIncrementValue:
	case ComposeMode::ReassignValue:
	case ComposeMode::ReturnValue:
		appendBindingObjects();
		break;
	case ComposeMode::CallArgValue:
	{
		// Same ref-parameter exclusion as the erase above -- a `ref` slot only ever accepts a bare Variable.
		const CallStage& stage = m_callStack.back();
		const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(stage.func->data);
		if (!std::get<DSLSymbol::VariableDeclaration>(callee.parameterVarDeclarations[stage.argChains.size()]->data).isRef)
			appendBindingObjects();
		break;
	}
	default:
		break;
	}
	m_candidateSelected = 0;
}

void ScriptEditor::cancelCompose()
{
	m_composeMode = ComposeMode::None;
	m_composePrefix.clear();
	m_pendingWord.clear();
	m_candidates.clear();
	m_candidateSelected = 0;
	m_renameTarget = nullptr;
	m_redeclareTarget = nullptr;
	m_reassignEditExpr = nullptr;
	m_conditionChainHeader = nullptr;
	m_conditionOriginLine = nullptr;
	m_conditionValueReturnMode = ComposeMode::None;
	m_logicalTerms.clear();
	m_logicalOps.clear();
	m_exprStack.clear();
	m_exprHasPendingGroup = false;
	m_editChainExpr = nullptr;
	m_editAnchorSymbol = nullptr;
	m_replaceOpExpr = nullptr;
	m_callStack.clear();
	m_memberReceiver = nullptr;
	m_memberPath.clear();
	m_memberReceiverType = DSLType::Void;
	m_memberReturnMode = ComposeMode::None;
	m_memberExpectedType = DSLType::Void;
	m_memberAnyValue = false;
	m_reassignMemberPath.clear();
	m_flowEditLine = nullptr;
	m_flowEditLoopVar = nullptr;
	m_composeCoverStart = -1;
	m_composeCoverEnd = -1;
}

const Candidate* ScriptEditor::selectedCandidate() const
{
	if (m_candidates.empty())
		return nullptr;
	return &m_candidates[std::clamp(m_candidateSelected, 0, static_cast<int>(m_candidates.size()) - 1)];
}

bool ScriptEditor::tryCompleteCandidateOnSpace()
{
	if (m_pendingWord.empty())
		return false; // same "must have typed something" guard as tryBeginValueCallStaging -- see the .ixx comment
	const Candidate* picked = selectedCandidate();
	if (picked == nullptr || isParameterizedFunction(*picked))
		return false;
	const std::string full = candidateDisplayText(*picked);
	if (full == m_pendingWord)
		return false; // already fully typed -- nothing to complete
	m_pendingWord = full;
	refreshCandidates();
	return true;
}

// See the declaration in ScriptEditor.ixx. Clearing the old candidate list before refreshing matters for the
// free-typing modes (whose refresh is a no-op) -- they must not keep showing/cycling the PREVIOUS stage's list.
void ScriptEditor::enterCompose(ComposeMode mode, std::string prefix, std::string pendingWord)
{
	m_composeMode = mode;
	m_composePrefix = std::move(prefix);
	m_pendingWord = std::move(pendingWord);
	m_candidates.clear();
	m_candidateSelected = 0;
	refreshCandidates();
}

bool ScriptEditor::hasCandidateList() const
{
	switch (m_composeMode)
	{
	case ComposeMode::FilterCandidates:
	case ComposeMode::ConditionLeft:
	case ComposeMode::ConditionOp:
	case ComposeMode::ConditionRight:
	case ComposeMode::FunctionParamType:
	case ComposeMode::FunctionReturnType:
	case ComposeMode::FunctionDeclareReturnType:
	case ComposeMode::ForVarType:
	case ComposeMode::ForConditionLeft:
	case ComposeMode::ForConditionOp:
	case ComposeMode::ForConditionValue:
	case ComposeMode::ForIncrementOp:
	case ComposeMode::ForIncrementValue:
	case ComposeMode::ReplaceOperator:
	case ComposeMode::ReturnValue:
	case ComposeMode::ReassignOp:
	case ComposeMode::MemberSelect:
		return true;
	case ComposeMode::EditExpr:
		return !isVectorType(m_editValueType); // vector slots free-type comma components
	case ComposeMode::CallArgValue:
		return !isVectorType(currentCallParamType());
	case ComposeMode::DeclareValue:
		// vec2/3/4 free-type their components instead (see applyDeclareVariable) -- no list to show/cycle,
		// same as DeclareName.
		return !isVectorType(m_pendingDeclareType);
	case ComposeMode::ForVarValue:
		return !isVectorType(m_forVarType);
	case ComposeMode::ReassignValue:
		return !isVectorType(reassignTargetType());
	default:
		return false;
	}
}

void ScriptEditor::confirmCompose(bool allowCommit)
{
	if (m_composeMode == ComposeMode::None)
		return;

	// A matched parameterized-Function candidate in a chain compose stages its arguments before anything else
	// can happen to it -- there is no bare form to confirm (placeholder arguments never exist).
	if (isChainComposeMode() && tryBeginValueCallStaging())
		return;

	// A matched BindingObject candidate has no bare form either -- confirming it (like typing '.') dots into
	// its member/function list.
	if (m_composeMode != ComposeMode::MemberSelect)
	{
		const Candidate* picked = selectedCandidate();
		if (hasCandidateList() && picked != nullptr && picked->kind == Candidate::Kind::BindingObject)
		{
			enterMemberSelect(picked->refSymbol);
			return;
		}
	}

	if (m_composeMode == ComposeMode::MemberSelect)
	{
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		const ComposeMode back = m_memberReturnMode;
		const bool statementContext = back == ComposeMode::FilterCandidates;

		// Completes the CURRENTLY-TYPED SEGMENT ("phy" -> "physics") to the picked candidate's own bare name --
		// NOT candidateDisplayText/tryCompleteCandidateOnSpace, which render the FULL receiver-prefixed text
		// ("self.physics") and would double up against m_composePrefix's own "self." lead-in. What lets
		// Space/Enter turn "self.phy" into "self.physics" instead of silently doing nothing when the candidate
		// can't resolve as a bare value/statement yet (see the two refusal sites below) -- '.' (or further
		// typing) continues the chain from there.
		auto completeSegment = [&]()
		{
			if (!m_pendingWord.empty() && m_pendingWord != picked->label)
			{
				m_pendingWord = picked->label;
				refreshCandidates();
			}
		};

		if (picked->kind == Candidate::Kind::Function
			&& !std::get<DSLSymbol::FunctionDeclaration>(picked->refSymbol->data).parameterVarDeclarations.empty())
		{
			// Stage the dot-call's arguments -- completion commits the line (statement) or returns the
			// resolved term to the suspended chain (value), commitCallStatement's two existing paths. The
			// SUSPENDED context (whatever `back` was composing, e.g. an outer call's own argument) is saved
			// into the new CallStage exactly like tryBeginValueCallStaging -- this dot-call may itself be
			// nested inside another call's argument. Entering MemberSelect never touched m_exprStack, so it
			// still holds that suspended state here -- captured into the prefix BEFORE the save/reset below.
			const std::string outerLead = statementContext ? std::string() : (chainLeadTextFor(back) + exprComposePrefixFromStack());
			CallStage& stage = m_callStack.emplace_back();
			stage.func = picked->refSymbol;
			stage.receiver = picked->receiver;
			stage.receiverPath = joinedMemberPath(m_memberPath);
			stage.returnMode = statementContext ? ComposeMode::None : back;
			stage.outerLeadText = outerLead;
			stage.savedExprStack = std::move(m_exprStack);
			stage.savedPendingGroup = std::move(m_exprPendingGroup);
			stage.savedHasPendingGroup = m_exprHasPendingGroup;
			m_memberReceiver = nullptr;
			m_memberReturnMode = ComposeMode::None;
			m_memberPath.clear();
			m_exprStack.assign(1, ExprFrame{});
			m_exprHasPendingGroup = false;
			enterCompose(ComposeMode::CallArgValue, callStagePrefix());
			return;
		}
		if (picked->kind != Candidate::Kind::Function && picked->kind != Candidate::Kind::Member)
			return;

		// A picked candidate carries the FULL chain context: functions remember the path they're called
		// through (receiverPath), a member's own path rides in its label ("pos.x", root = refSymbol).
		Candidate chosen = *picked;
		if (chosen.kind == Candidate::Kind::Function)
			chosen.receiverPath = joinedMemberPath(m_memberPath);
		else if (!m_memberPath.empty())
			chosen.label = joinedMemberPath(m_memberPath) + "." + chosen.label;

		if (statementContext)
		{
			if (chosen.kind == Candidate::Kind::Member)
			{
				// A non-writable, chainable member (self.physics) has no bare statement form of its own -- it
				// must keep dotting toward an actual call ('.' extends it, same refusal as the value-context
				// waypoint case below); confirming it here would otherwise stage a bogus "physics = ..." assign.
				if (!picked->memberWritable)
				{
					completeSegment();
					return;
				}
				// A writable member as an assignment TARGET: stage `root.path = value` through the Reassign
				// flow ('=' authored; compound member assigns are a later nicety).
				m_reassignTarget = m_memberReceiver;
				m_reassignMemberPath.clear();
				for (const std::string& segment : m_memberPath)
					m_reassignMemberPath.push_back(segment);
				m_reassignMemberPath.push_back(picked->label);
				m_reassignOp = DSLOperator::Assign;
				m_reassignEditExpr = nullptr;
				m_memberReceiver = nullptr;
				m_memberReturnMode = ComposeMode::None;
				m_memberPath.clear();
				enterChainStage(ComposeMode::ReassignValue);
				return;
			}
			// A zero-argument dot-call statement ("physics.isAwake()") commits its whole line -- Enter only.
			if (!allowCommit)
				return;
			DSLCodeLine* linePtr = currentLineHeadOrCancel();
			if (linePtr == nullptr)
				return;
			DSLCodeLine& line = *linePtr;
			cancelCompose();
			line.symbols.clear();
			m_pendingSelectTarget = buildValueFromCandidate(chosen, line);
			return;
		}

		// Value context: a chainable member that doesn't MATCH the slot's type is only a waypoint -- it must
		// keep dotting toward a matching leaf ('.' extends it), never deliver as the value itself.
		if (chosen.kind == Candidate::Kind::Member && !m_memberAnyValue && m_memberExpectedType != DSLType::Void
			&& chosen.declareType != m_memberExpectedType)
		{
			completeSegment();
			return;
		}

		// The member / zero-argument dot-call becomes an already-resolved pending term of the suspended chain
		// compose -- exactly commitCallStatement's value-branch delivery.
		PendingExprTerm term;
		term.candidate = std::move(chosen);
		m_memberReceiver = nullptr;
		m_memberReturnMode = ComposeMode::None;
		m_memberPath.clear();
		enterCompose(back, "");
		m_exprPendingGroup = std::move(term);
		m_exprHasPendingGroup = true;
		m_candidates.clear(); // nothing is being typed right after the resolved term -- operators continue it
		m_composePrefix = exprBasePrefix() + exprComposePrefixFromStack();
		return;
	}

	if (m_composeMode == ComposeMode::CommentText)
	{
		if (!allowCommit)
			return; // a comment line commits whole -- Enter only (Space is content, handled before confirm)
		DSLCodeLine* linePtr = currentLineHeadOrCancel();
		if (linePtr == nullptr)
			return;
		DSLCodeLine& line = *linePtr;
		const std::string text = m_pendingWord;
		cancelCompose();
		line.symbols.clear();
		m_pendingSelectTarget = pushSymbol(line, ST::Comment, DSLSymbol::Comment{ text });
		return;
	}

	if (m_composeMode == ComposeMode::Rename)
	{
		if (!allowCommit)
			return; // a rename IS the finishing edit -- Enter only
		if (m_pendingWord.empty() || m_renameTarget == nullptr)
		{
			cancelCompose();
			return;
		}
		if (m_renameTarget->type == ST::FunctionDeclaration)
		{
			// Renaming a FUNCTION at its declaration header -- global collision rules, excluding itself. An
			// entry-point name (Update, OnSpawn, ...) is additionally off-limits here -- becoming one of those
			// happens ONLY through its EXPORTS checkbox (toggleEntryFunction), which is what keeps every
			// function actually named that way locked (isLockedEntryFunction) and genuinely ABI-shaped; renaming
			// an ordinary function into the name would create an unlocked impostor instead.
			if (AutoCompleteRules::isFunctionNameTaken(m_pendingWord, m_document.file, m_builtins, m_renameTarget)
				|| isEntryPointName(m_pendingWord))
				return; // name already taken -- keep editing instead of accepting the collision
			std::get<DSLSymbol::FunctionDeclaration>(m_renameTarget->data).name = m_pendingWord;
			m_pendingSelectTarget = m_renameTarget;
			cancelCompose();
			return;
		}
		if (m_renameTarget->line != nullptr
			&& AutoCompleteRules::isNameInScope(m_pendingWord, *m_renameTarget->line, m_document.file, m_document.sidebar, m_renameTarget))
			return; // name already taken in this scope -- keep editing instead of accepting the collision
		std::get<DSLSymbol::VariableDeclaration>(m_renameTarget->data).name = m_pendingWord;
		m_pendingSelectTarget = m_renameTarget;
		cancelCompose();
		return;
	}

	if (m_composeMode == ComposeMode::DeclareName)
	{
		if (m_pendingWord.empty())
			return; // need a name before moving on
		const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		if (span == nullptr || span->slot.line == nullptr)
		{
			cancelCompose();
			return;
		}
		// A re-declare's own current name must not count as a collision against itself (m_redeclareTarget is
		// null for a brand-new declaration, changing nothing there).
		if (AutoCompleteRules::isNameInScope(m_pendingWord, *span->slot.line, m_document.file, m_document.sidebar, m_redeclareTarget))
			return; // name already taken in this scope -- keep editing instead of accepting the collision
		m_pendingDeclareName = m_pendingWord;
		enterChainStage(ComposeMode::DeclareValue);
		return;
	}

	if (m_composeMode == ComposeMode::DeclareValue)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace(); // can't finish the declaration (Enter only) -- but Space can still complete the box
			return;
		}
		// No placeholder fallbacks: an incomplete value (nothing typed, or a partial/invalid component list)
		// refuses to confirm -- the compose stays until the declaration is fully valid; Escape abandons cleanly.
		if (isVectorType(m_pendingDeclareType) && !vectorComponentsValid(m_pendingDeclareType, m_pendingWord))
			return;

		// Re-authoring an EXISTING declaration commits in place (name + initializer swapped on the same
		// symbol), never through the whole-line rebuild below -- see m_redeclareTarget's comment.
		if (m_redeclareTarget != nullptr)
		{
			if (isVectorType(m_pendingDeclareType))
			{
				commitRedeclare({}, {}, m_pendingWord);
				return;
			}
			std::vector<PendingExprTerm> terms;
			std::vector<DSLOperator> ops;
			if (!exprTryFinalize(terms, ops) || terms.empty())
				return;
			if (m_pendingDeclareType == DSLType::Bool && containsNonBoolTerm(terms))
				return; // a numeric comparison lead isn't a bool value -- type a comparator to continue
			commitRedeclare(terms, ops, "");
			return;
		}

		if (isVectorType(m_pendingDeclareType))
		{
			// vec2/3/4: comma-separated components only, no expression chain involved.
			DSLCodeLine* linePtr = currentLineHeadOrCancel();
			if (linePtr == nullptr)
				return;
			DSLCodeLine& line = *linePtr;
			const std::string name = m_pendingDeclareName;
			const DSLType type = m_pendingDeclareType;
			const std::string rawText = m_pendingWord;
			cancelCompose();
			applyDeclareVariable(name, type, {}, {}, rawText, line);
			return;
		}

		std::vector<PendingExprTerm> terms;
		std::vector<DSLOperator> ops;
		if (!exprTryFinalize(terms, ops) || terms.empty())
			return; // open paren, dangling operator, or nothing typed at all -- keep composing until valid
		if (m_pendingDeclareType == DSLType::Bool && containsNonBoolTerm(terms))
			return; // a numeric comparison lead isn't a bool value -- type a comparator to continue

		DSLCodeLine* linePtr = currentLineHeadOrCancel();
		if (linePtr == nullptr)
			return;
		DSLCodeLine& line = *linePtr;
		const std::string name = m_pendingDeclareName;
		const DSLType type = m_pendingDeclareType;
		cancelCompose();
		applyDeclareVariable(name, type, terms, ops, "", line);
		return;
	}

	if (m_composeMode == ComposeMode::ReassignOp)
	{
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr || m_reassignTarget == nullptr)
			return;
		m_reassignOp = picked->op;
		enterChainStage(ComposeMode::ReassignValue);
		return;
	}

	if (m_composeMode == ComposeMode::ReassignValue)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace();
			return; // finishing stage -- Enter only
		}
		// Same no-placeholder rule as DeclareValue: incomplete values refuse to confirm.
		if (isVectorType(reassignTargetType()))
		{
			if (!vectorComponentsValid(reassignTargetType(), m_pendingWord))
				return;
			const std::string rawText = m_pendingWord;
			if (m_reassignEditExpr != nullptr)
				commitReassignInPlace({}, {}, rawText);
			else
				commitReassignStatement({}, {}, rawText);
			return;
		}

		std::vector<PendingExprTerm> terms;
		std::vector<DSLOperator> ops;
		if (!exprTryFinalize(terms, ops) || terms.empty())
			return;
		if (reassignTargetType() == DSLType::Bool && containsNonBoolTerm(terms))
			return; // a numeric comparison lead isn't a bool value -- type a comparator to continue
		if (m_reassignEditExpr != nullptr)
			commitReassignInPlace(terms, ops, "");
		else
			commitReassignStatement(terms, ops, "");
		return;
	}

	if (m_composeMode == ComposeMode::EditExpr)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace();
			return; // an in-place edit commits on confirm -- Enter only
		}
		// A vector-typed slot free-types comma components (no candidate list, like DeclareValue's vectors):
		// nothing typed leaves the occupant as-is; a partial/invalid list refuses to confirm.
		if (isVectorType(m_editValueType))
		{
			if (m_pendingWord.empty())
			{
				cancelCompose();
				return;
			}
			if (!vectorComponentsValid(m_editValueType, m_pendingWord))
				return;
			applyEditExpr({}, {}, m_pendingWord);
			return;
		}

		std::vector<PendingExprTerm> terms;
		std::vector<DSLOperator> ops;
		if (!exprTryFinalize(terms, ops))
			return; // open paren, or a dangling operator with nothing typed yet -- keep composing
		if (terms.empty())
		{
			cancelCompose(); // nothing was ever typed -- the existing occupant stays exactly as it was
			return;
		}
		if (m_editValueType == DSLType::Bool && containsNonBoolTerm(terms))
			return; // a numeric comparison lead isn't a bool value -- type a comparator to continue
		applyEditExpr(terms, ops, "");
		return;
	}

	if (m_composeMode == ComposeMode::ReturnValue)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace();
			return; // finishing stage -- Enter only
		}
		// A value-returning function's `return` commits WITH its whole value in one shot -- possibly a
		// compound/parenthesized chain ("return a + 5"), same expression machinery as an initializer; there is
		// no committed "return <type>" intermediate state (no placeholders anywhere, per the editing model).
		std::vector<PendingExprTerm> terms;
		std::vector<DSLOperator> ops;
		if (!exprTryFinalize(terms, ops) || terms.empty())
			return; // open paren, dangling operator, or nothing typed -- keep composing until valid
		// Re-editing an existing return line resolves it directly (the cursor sits on its value span, not a
		// LineHead slot); a fresh statement goes through the cursor's slot as usual.
		DSLCodeLine* linePtr = (m_flowEditLine != nullptr) ? m_flowEditLine : currentLineHeadOrCancel();
		if (linePtr == nullptr)
			return;
		DSLCodeLine& line = *linePtr;
		if (AutoCompleteRules::enclosingFunctionReturnType(line, m_document.file) == DSLType::Bool && containsNonBoolTerm(terms))
			return; // a numeric comparison lead isn't a bool value -- type a comparator to continue
		cancelCompose();
		line.symbols.clear();
		DSLSymbol* value = buildExpressionFromTerms(terms, ops, line);
		pushSymbol(line, ST::FlowControl, DSLSymbol::FlowControl{ DSLFlowControl::Return, value });
		selectExpressionTail(value);
		return;
	}

	if (m_composeMode == ComposeMode::CallArgValue)
	{
		// One argument resolves per confirm (Space advances between them); the call itself only commits once
		// every parameter has a value -- and that FINAL confirm needs Enter or ')' (allowCommit). Each argument
		// is a FULL chain compose (m_exprStack), exactly like every other value slot -- so it may be compound
		// ("a + b") or contain a nested call ("vec3(other.x, 1, 2)"), staged via tryBeginValueCallStaging like
		// anywhere else (see m_callStack).
		CallStage& stage = m_callStack.back();
		const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(stage.func->data);
		if (!allowCommit && stage.returnMode == ComposeMode::None
			&& stage.argChains.size() + 1 == callee.parameterVarDeclarations.size())
		{
			tryCompleteCandidateOnSpace(); // can't commit the call (Enter/')' only) -- but Space can still complete the box
			return; // a call STATEMENT's final argument commits the line -- Enter/')' only; a call VALUE just resolves a term
		}

		std::vector<PendingExprTerm> terms;
		std::vector<DSLOperator> ops;
		if (!exprTryFinalize(terms, ops) || terms.empty())
			return; // open paren, dangling operator, or nothing typed -- keep composing until valid
		const DSLSymbol::VariableDeclaration& param = std::get<DSLSymbol::VariableDeclaration>(
			callee.parameterVarDeclarations[stage.argChains.size()]->data);
		const DSLType paramType = std::get<DSLSymbol::TypeDeclaration>(param.typeSymbol->data).type;
		if (param.isRef && (terms.size() != 1 || !ops.empty() || terms[0].isGroup || !terms[0].callArgs.empty()
			|| terms[0].candidate.kind != Candidate::Kind::Variable))
			return; // a `ref` parameter receives the callee's OUTPUT -- only a bare existing variable, never a
			         // compound expression or a nested call result
		if (paramType == DSLType::Bool && containsNonBoolTerm(terms))
			return; // a numeric comparison lead isn't a bool value -- type a comparator to continue

		stage.argChains.push_back(PendingExprChain{ std::move(terms), std::move(ops) });
		if (stage.argChains.size() == callee.parameterVarDeclarations.size())
			commitCallStatement();
		else
		{
			m_exprStack.assign(1, ExprFrame{});
			m_exprHasPendingGroup = false;
			enterCompose(ComposeMode::CallArgValue, callStagePrefix());
		}
		return;
	}

	if (m_composeMode == ComposeMode::ReplaceOperator)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace();
			return; // the swap commits on confirm -- Enter only
		}
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		DSLSymbol* expr = m_replaceOpExpr;
		const int index = m_replaceOpIndex;
		const DSLOperator op = picked->op;
		cancelCompose();
		// Flat chain storage is what makes this a one-field write: precedence lives in the transpiler, so
		// swapping `+` for `*` never restructures anything (see DSL.ixx's Expression comment).
		std::get<DSLSymbol::Expression>(expr->data).operators[index] = op;
		m_pendingSelectTarget = expr;
		m_pendingSelectOperatorIndex = index;
		return;
	}

	if (m_composeMode == ComposeMode::FunctionDeclareName)
	{
		if (m_pendingWord.empty())
			return; // need a name before moving on
		// An entry-point name is off-limits here too (fresh declare AND re-declaring an existing function INTO
		// one) -- see the Rename branch's comment; only toggleEntryFunction may create a function actually named
		// this way.
		if (isEntryPointName(m_pendingWord))
			return;
		// A re-authored function's own current name must not collide with itself.
		if (AutoCompleteRules::isFunctionNameTaken(m_pendingWord, m_document.file, m_builtins,
			m_flowEditLine != nullptr ? m_flowEditLine->head() : nullptr))
			return; // name already taken -- keep editing instead of accepting the collision
		m_pendingFunctionName = m_pendingWord;
		m_pendingParamTypes.clear();
		m_pendingParamNames.clear();
		m_pendingParamRefs.clear();
		enterCompose(ComposeMode::FunctionParamType, functionDeclarePrefix());
		return;
	}

	if (m_composeMode == ComposeMode::FunctionParamType)
	{
		// Confirming (rather than typing ')', see handleKeyEvent) always picks the highlighted type and moves
		// on to naming this parameter -- same list-then-free-type idiom as declaring a variable.
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		m_pendingParamType = picked->declareType;
		m_pendingParamRef = false; // freshly authored parameters are never by-reference (see m_pendingParamRefs)
		enterCompose(ComposeMode::FunctionParamName, currentParamPrefix());
		return;
	}

	if (m_composeMode == ComposeMode::FunctionParamName)
	{
		// Space/Enter here means "no more parameters" -- same as typing ')' (handleKeyEvent): finalize this
		// last parameter (if it has a name) and close the list into the FunctionDeclareDone stage (commit,
		// or append a "-> type" first). A duplicate name refuses (keeps editing) instead of silently dropping
		// the parameter, same as any other collision.
		if (!m_pendingWord.empty())
		{
			if (isPendingParamNameTaken(m_pendingWord))
				return;
			m_pendingParamTypes.push_back(m_pendingParamType);
			m_pendingParamNames.push_back(m_pendingWord);
			m_pendingParamRefs.push_back(m_pendingParamRef);
		}
		enterCompose(ComposeMode::FunctionDeclareDone, functionDeclarePrefix() + ")");
		return;
	}

	if (m_composeMode == ComposeMode::FunctionDeclareDone)
	{
		// The parameter list is closed -- Enter commits the function as staged so far (typing '-' would
		// instead have opened the return-type pick, see handleKeyEvent).
		if (!allowCommit)
			return;
		commitFunctionDeclaration();
		return;
	}

	if (m_composeMode == ComposeMode::FunctionDeclareReturnType)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace();
			return; // picking the type commits the whole function -- Enter only
		}
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		m_pendingReturnType = picked->declareType;
		commitFunctionDeclaration();
		return;
	}

	if (m_composeMode == ComposeMode::FunctionReturnType)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace();
			return; // writes the header's return type -- Enter only
		}
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		if (span == nullptr || span->slot.kind != SlotRef::Kind::FunctionReturnType || span->slot.parent == nullptr)
		{
			cancelCompose();
			return;
		}
		DSLSymbol* funcSymbol = span->slot.parent;
		DSLSymbol::FunctionDeclaration& f = std::get<DSLSymbol::FunctionDeclaration>(funcSymbol->data);
		const DSLType oldReturnType = f.returnType;
		f.returnType = picked->declareType;
		m_pendingSelectTarget = funcSymbol;
		cancelCompose();
		if (f.returnType != oldReturnType)
			applyFunctionReturnChange(funcSymbol, f.returnType); // old returns out, fresh `return |` seeded
		return;
	}

	if (m_composeMode == ComposeMode::ForVarType)
	{
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		m_forVarType = picked->declareType;
		enterCompose(ComposeMode::ForVarName, "for " + std::string(dslTypeName(m_forVarType)) + " ");
		return;
	}

	if (m_composeMode == ComposeMode::ForVarName)
	{
		if (m_pendingWord.empty())
			return; // need a name before moving on
		const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		if (span == nullptr || span->slot.line == nullptr)
		{
			cancelCompose();
			return;
		}
		// A re-edited loop's own current name must not collide with itself (m_flowEditLoopVar is null when
		// authoring a fresh loop, changing nothing there).
		if (AutoCompleteRules::isNameInScope(m_pendingWord, *span->slot.line, m_document.file, m_document.sidebar, m_flowEditLoopVar))
			return; // name already taken in this scope -- keep editing instead of accepting the collision
		m_forVarName = m_pendingWord;
		m_forVarInitRawText.clear();
		m_forVarInitChain = PendingExprChain{};
		if (isVectorType(m_forVarType))
			enterCompose(ComposeMode::ForVarValue, forVarPrefix()); // comma components, no chain machinery
		else
			enterChainStage(ComposeMode::ForVarValue); // the init is a full compound chain ("= n - 1")
		return;
	}

	if (m_composeMode == ComposeMode::ForVarValue)
	{
		// vec2/3/4 free-type their components instead (see commitForStatement/buildVectorLiteral) -- an
		// incomplete/invalid component list refuses to advance, same no-placeholder rule as DeclareValue.
		// Scalar inits are full compound chains ("for int i = n - 1").
		if (isVectorType(m_forVarType))
		{
			if (!vectorComponentsValid(m_forVarType, m_pendingWord))
				return;
			m_forVarInitRawText = m_pendingWord;
			m_forVarInitChain = PendingExprChain{};
		}
		else
		{
			PendingExprChain init;
			if (!captureComposedChain(init))
				return;
			m_forVarInitChain = init;
			m_forVarInitRawText.clear();
		}
		// The condition opens seeded with the loop variable as its left side -- extendable ("i + 2"), or a
		// comparator character/confirm advances it as-is.
		const PendingExprChain seed = loopVarSeedChain();
		enterChainStage(ComposeMode::ForConditionLeft, &seed);
		return;
	}

	if (m_composeMode == ComposeMode::ForConditionLeft)
	{
		PendingExprChain left;
		if (!captureComposedChain(left))
			return;
		m_forConditionLeftChain = left;
		enterCompose(ComposeMode::ForConditionOp, forVarDeclPrefix() + ", " + chainDisplayText(left) + " ");
		return;
	}

	if (m_composeMode == ComposeMode::ForConditionOp)
	{
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		m_forConditionOpCandidate = *picked;
		enterChainStage(ComposeMode::ForConditionValue);
		return;
	}

	if (m_composeMode == ComposeMode::ForConditionValue)
	{
		PendingExprChain bound;
		if (!captureComposedChain(bound))
			return;
		m_forConditionValueChain = bound;
		enterCompose(ComposeMode::ForIncrementOp, forConditionPrefix() + ", " + m_forVarName + " ");
		return;
	}

	if (m_composeMode == ComposeMode::ForIncrementOp)
	{
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		m_forIncrementOpCandidate = *picked;
		enterChainStage(ComposeMode::ForIncrementValue);
		return;
	}

	if (m_composeMode == ComposeMode::ForIncrementValue)
	{
		if (!allowCommit)
		{
			tryCompleteCandidateOnSpace();
			return; // the increment's value is the for-loop's LAST stage -- confirming commits (Enter only)
		}
		// Nothing valid to complete the increment with yet -- do NOT commit a broken/partial for-loop; same
		// "won't commit unless every field is valid" guarantee as if/while/function.
		PendingExprChain step;
		if (!captureComposedChain(step))
			return;
		m_forIncrementValueChain = step;
		commitForStatement();
		return;
	}

	if (m_composeMode == ComposeMode::ConditionLeft)
	{
		// A BOOL-valued compose IS a whole term on its own -- the entire condition ("if canJump()") or a
		// logical chain's final term; comparing a bool instead still works by TYPING a comparator rather than
		// confirming. Anything non-bool continues into the comparator stage as the comparison's (possibly
		// compound) left side. Type peeked BEFORE capturing -- a refused commit must not consume the compose.
		if (composedChainPeekType() == DSLType::Bool)
		{
			if (!allowCommit)
				return; // committing a bare bool term finishes the line -- Enter only ('&'/'|' chains instead)
			PendingExprChain bare;
			if (!captureComposedChain(bare))
				return;
			const PendingLogicalTerm finalTerm{ false, bare, DSLOperator::Equal, PendingExprChain{} };
			if (m_conditionValueReturnMode != ComposeMode::None)
				commitBoolValue(finalTerm);
			else
				applyConditionalStatement(finalTerm);
			return;
		}
		PendingExprChain left;
		if (!captureComposedChain(left))
			return; // open paren, dangling operator, or nothing typed -- keep composing
		m_conditionLeftChain = left;
		enterCompose(ComposeMode::ConditionOp, stagedConditionPrefix() + chainDisplayText(left) + " ");
		return;
	}

	if (m_composeMode == ComposeMode::ConditionOp)
	{
		const Candidate* picked = selectedCandidate();
		if (picked == nullptr)
			return;
		m_conditionOp = picked->op;
		enterChainStage(ComposeMode::ConditionRight); // right side is a chain compose; prefix derives from left + op
		return;
	}

	if (m_composeMode == ComposeMode::ConditionRight)
	{
		// Nothing valid to complete the comparison with yet -- do NOT commit a broken/partial if/while;
		// this is exactly the "won't commit unless every field is valid" guarantee for this whole flow.
		// Copied, not referenced: applyConditionalStatement cancels the compose state (freeing m_candidates)
		// before it builds from the candidate.
		if (!allowCommit)
			return; // the right side completes the condition -- Enter commits, '&'/'|' chains onward
		PendingExprChain right;
		if (!captureComposedChain(right))
			return; // open paren, dangling operator, or nothing typed -- keep composing
		const PendingLogicalTerm finalTerm{ true, m_conditionLeftChain, m_conditionOp, right };
		if (m_conditionValueReturnMode != ComposeMode::None)
		{
			commitBoolValue(finalTerm); // the comparison/chain IS a value ("bool b = i < 5"), not a header
			return;
		}
		applyConditionalStatement(finalTerm);
		return;
	}

	// FilterCandidates: confirm the highlighted candidate. Every other ComposeMode is handled by one of the
	// staged-flow blocks above and returns before reaching here -- if a future mode's block gets missed, this
	// assert catches it instead of silently misapplying whatever's sitting in m_candidates as a slot-fill.
	// Copied, not referenced: both enterCompose and applyCandidate's preceding cancelCompose free m_candidates.
	assert(m_composeMode == ComposeMode::FilterCandidates);
	const Candidate* picked = selectedCandidate();
	if (picked == nullptr)
	{
		cancelCompose();
		return;
	}
	const Candidate chosen = *picked;

	if (chosen.kind == Candidate::Kind::DeclareType)
	{
		// Staged flow starts here: this step only picks the TYPE. The box keeps growing ("float ", then
		// "float test", then "float test = ...") instead of applying anything to the document yet.
		m_pendingDeclareType = chosen.declareType;
		enterCompose(ComposeMode::DeclareName, std::string(dslTypeName(chosen.declareType)) + " ");
		return;
	}

	if (chosen.kind == Candidate::Kind::DeclareFunction)
	{
		// Staged flow starts here too: nothing about the function (name, params, return type) touches the
		// document until commitFunctionDeclaration -- see the class-level comment and the FunctionDeclare* modes.
		m_pendingReturnType = DSLType::Void;
		enterCompose(ComposeMode::FunctionDeclareName, "function ");
		return;
	}

	if (chosen.kind == Candidate::Kind::Reassign)
	{
		// Staged flow starts here too: a reassignment never appears in the document until its WHOLE (possibly
		// compound, possibly parenthesized) value is resolved -- see commitReassignStatement. Space-confirming
		// authors a plain `=`; typing an operator character instead routes through the ReassignOp stage first
		// (see handleKeyEvent), which is where "i += 1" gets its compound operator.
		m_reassignTarget = chosen.refSymbol;
		m_reassignOp = DSLOperator::Assign;
		enterChainStage(ComposeMode::ReassignValue);
		return;
	}

	if (chosen.kind == Candidate::Kind::KeywordIf || chosen.kind == Candidate::Kind::KeywordWhile)
	{
		// Staged flow starts here too: if/while never appear in the document until their WHOLE condition
		// (left operand, comparator, right operand) is resolved -- see applyConditionalStatement.
		m_conditionControl = (chosen.kind == Candidate::Kind::KeywordIf) ? DSLFlowControl::If : DSLFlowControl::While;
		m_conditionChainHeader = nullptr; // a plain if/while replaces the cursor line, never chain-inserts
		m_logicalTerms.clear();
		m_logicalOps.clear();
		enterChainStage(ComposeMode::ConditionLeft);
		return;
	}

	if (chosen.kind == Candidate::Kind::KeywordElse || chosen.kind == Candidate::Kind::KeywordElseIf)
	{
		// Growing the enclosing if/elseif chain from INSIDE one of its branches (the only place these are
		// offered -- re-verified structurally here). The blank line being typed on is consumed; the new branch
		// goes after the enclosing one at its level. `else` has no condition, so it applies immediately;
		// `elseif` stages its condition first, exactly like an if.
		DSLCodeLine* linePtr = currentLineHeadOrCancel();
		if (linePtr == nullptr)
			return;
		const int lineIndex = dslLineIndex(m_document.file, linePtr);
		const int headerIndex = (lineIndex >= 0) ? dslEnclosingBlockHeader(m_document.file, lineIndex) : -1;
		DSLSymbol* chainHead = (headerIndex >= 0) ? m_document.file.lines[headerIndex]->head() : nullptr;
		const bool validBranch = chainHead != nullptr && chainHead->type == ST::FlowControl
			&& (std::get<DSLSymbol::FlowControl>(chainHead->data).control == DSLFlowControl::If
				|| std::get<DSLSymbol::FlowControl>(chainHead->data).control == DSLFlowControl::ElseIf);
		if (!validBranch)
		{
			cancelCompose();
			return;
		}
		if (chosen.kind == Candidate::Kind::KeywordElse)
		{
			if (!allowCommit)
				return; // an else applies immediately (no condition to stage) -- Enter only
			DSLCodeLine& origin = *linePtr;
			cancelCompose();
			applyElseStatement(chainHead, origin);
			return;
		}
		m_conditionControl = DSLFlowControl::ElseIf;
		m_conditionChainHeader = chainHead;
		m_conditionOriginLine = linePtr;
		m_logicalTerms.clear();
		m_logicalOps.clear();
		enterChainStage(ComposeMode::ConditionLeft);
		return;
	}

	if (chosen.kind == Candidate::Kind::KeywordFor)
	{
		// Staged flow starts here too: a for-loop never appears in the document until its loop variable,
		// condition, AND increment are all resolved -- see commitForStatement and the class-level comment.
		m_forVarName.clear();
		m_forVarInitRawText.clear();
		enterCompose(ComposeMode::ForVarType, "for ");
		return;
	}

	if (chosen.kind == Candidate::Kind::KeywordReturn)
	{
		// In a value-returning function, `return` stages its value first (ReturnValue) so no "return <type>"
		// placeholder ever lands; a Void function's bare `return` commits immediately via applyCandidate below.
		const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		if (span != nullptr && span->slot.line != nullptr
			&& AutoCompleteRules::enclosingFunctionReturnType(*span->slot.line, m_document.file) != DSLType::Void)
		{
			enterChainStage(ComposeMode::ReturnValue); // the value may be a compound/parenthesized chain ("return a + 5")
			return;
		}
	}

	if (chosen.kind == Candidate::Kind::Function
		&& !std::get<DSLSymbol::FunctionDeclaration>(chosen.refSymbol->data).parameterVarDeclarations.empty())
	{
		// A parameterized call statement stages every argument before anything commits (CallArgValue) --
		// the reason value-candidate lists exclude parameterized functions entirely (see addFunctionCandidates).
		// Parameter-less calls commit directly below, argument-free by construction.
		CallStage& stage = m_callStack.emplace_back();
		stage.func = chosen.refSymbol;
		stage.returnMode = ComposeMode::None; // a STATEMENT call -- completion commits the line
		m_exprStack.assign(1, ExprFrame{});
		m_exprHasPendingGroup = false;
		enterCompose(ComposeMode::CallArgValue, callStagePrefix());
		return;
	}

	if (!allowCommit)
		return; // everything below commits the statement -- Enter only (stage-starting picks were handled above)
	cancelCompose(); // clears state before applyCandidate mutates the document
	applyCandidate(chosen);
}

// Constructs a fresh symbol owned by `line`: allocates it, tags it with `type`/`data`, sets its back-pointer to
// `line`, and appends it to `line.symbols` (the post-order convention -- see DSL.ixx -- relies on every symbol a
// statement depends on being pushed before that statement's own head symbol). Every apply/commit function below
// builds its symbols through this one helper instead of each redefining an identical local lambda.
DSLSymbol* ScriptEditor::pushSymbol(DSLCodeLine& line, DSLSymbol::SymbolType type, DSLSymbol::Data data)
{
	auto symbol = std::make_unique<DSLSymbol>();
	symbol->type = type;
	symbol->data = std::move(data);
	symbol->line = &line;
	DSLSymbol* ptr = symbol.get();
	line.symbols.push_back(std::move(symbol));
	return ptr;
}

// Resolves the LineHead slot's line at the current cursor -- every commit step (declaring a variable, an
// if/while, a function, a for-loop, a reassignment) needs exactly this same "am I still on a committable
// statement slot" check before mutating anything. Cancels the compose flow and returns nullptr if not.
DSLCodeLine* ScriptEditor::currentLineHeadOrCancel()
{
	const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
	if (span == nullptr || span->slot.kind != SlotRef::Kind::LineHead || span->slot.line == nullptr)
	{
		cancelCompose();
		return nullptr;
	}
	return span->slot.line;
}

// Constructs a fresh value symbol (owned by `line`) from a VALUE-slot candidate -- shared by ordinary
// value-slot replacement (applyCandidate) and declare-variable's final "now build the initializer" step.
DSLSymbol* ScriptEditor::buildValueFromCandidate(const Candidate& candidate, DSLCodeLine& line)
{
	switch (candidate.kind)
	{
	case Candidate::Kind::KeywordTrue:
		return pushSymbol(line, ST::Constant, DSLSymbol::Constant{ DSLType::Bool, "true" });
	case Candidate::Kind::KeywordFalse:
		return pushSymbol(line, ST::Constant, DSLSymbol::Constant{ DSLType::Bool, "false" });
	case Candidate::Kind::Variable:
		// A null refSymbol is the SENTINEL loop variable of a for-loop still being staged (its symbol doesn't
		// exist until commitForStatement builds it, then rides in via m_forBuildLoopVar).
		if (candidate.refSymbol == nullptr && m_forBuildLoopVar == nullptr)
			return pushSymbol(line, ST::Placeholder, DSLSymbol::Placeholder{ candidate.declareType }); // defensive -- shouldn't occur
		return pushSymbol(line, ST::VariableReference,
			DSLSymbol::VariableReference{ candidate.refSymbol != nullptr ? candidate.refSymbol : m_forBuildLoopVar });
	case Candidate::Kind::Function:
	{
		const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(candidate.refSymbol->data);
		std::vector<DSLSymbol::CallArgument> args;
		for (DSLSymbol* param : callee.parameterVarDeclarations)
		{
			const DSLSymbol::VariableDeclaration& p = std::get<DSLSymbol::VariableDeclaration>(param->data);
			const DSLType pType = std::get<DSLSymbol::TypeDeclaration>(p.typeSymbol->data).type;
			DSLSymbol* argPlaceholder = pushSymbol(line, ST::Placeholder, DSLSymbol::Placeholder{ pType });
			// Positional builtins (vec3(...)) get parameter=nullptr slots -- named ones identify which
			// parameter each fills, per CallArgument's convention (see DSL.ixx).
			args.push_back(DSLSymbol::CallArgument{ callee.isPositionalCall ? nullptr : param, argPlaceholder });
		}
		// A dot-call carries its receiver chain (root declaration + dotted path -- see receiverCandidates).
		DSLSymbol* receiverRef = (candidate.receiver != nullptr)
			? buildReceiverChain(candidate.receiver, candidate.receiverPath, line) : nullptr;
		return pushSymbol(line, ST::FunctionCall, DSLSymbol::FunctionCall{ candidate.refSymbol, receiverRef, args });
	}
	case Candidate::Kind::Member:
		// refSymbol = the chain's ROOT declaration; label = the dotted member path ("pos" / "pos.x").
		return buildReceiverChain(candidate.refSymbol, candidate.label, line);
	case Candidate::Kind::Literal:
		// A string literal's label carries its quotes for display; the stored Constant holds the CONTENT
		// (rendering re-adds the quotes).
		if (candidate.declareType == DSLType::String)
			return pushSymbol(line, ST::Constant,
				DSLSymbol::Constant{ DSLType::String, candidate.label.substr(1, candidate.label.size() - 2) });
		return pushSymbol(line, ST::Constant, DSLSymbol::Constant{ candidate.declareType, candidate.label });
	default:
		return nullptr; // statement-only kinds (If/While/Return/Break/DeclareType) never reach here
	}
}

DSLSymbol* ScriptEditor::vectorBuiltinFor(DSLType) const
{
	return nullptr; // the comma shorthand is gone -- struct constructors stage as ordinary calls now
}

// Builds a positional vecN(<c0>, <c1>, ...) call (owned by `line`) directly from typed components -- returns
// nullptr (no document mutation at all) if the component count doesn't match vectorType or any component isn't
// a valid Float literal, so the caller can fall back exactly the way an unresolved scalar initializer does
// (see applyDeclareVariable) rather than half-building something broken.
DSLSymbol* ScriptEditor::buildVectorLiteral(DSLType vectorType, const std::vector<std::string>& components, DSLCodeLine& line)
{
	DSLSymbol* builtin = vectorBuiltinFor(vectorType);
	if (builtin == nullptr || static_cast<int>(components.size()) != vectorComponentCount(vectorType))
		return nullptr;
	for (const std::string& component : components)
		if (!AutoCompleteRules::isValidLiteralText(DSLType::Float, component))
			return nullptr;

	std::vector<DSLSymbol::CallArgument> args;
	for (const std::string& component : components)
	{
		DSLSymbol* value = pushSymbol(line, ST::Constant, DSLSymbol::Constant{ DSLType::Float, component });
		args.push_back(DSLSymbol::CallArgument{ nullptr, value }); // positional, matching isPositionalCall
	}
	return pushSymbol(line, ST::FunctionCall, DSLSymbol::FunctionCall{ builtin, nullptr, args });
}

// Fills a LineHead statement slot with a brand-new local variable declaration, `type name = value`, built in
// ONE shot from the fully-resolved staged flow (see confirmCompose's DeclareName/DeclareValue handling). For a
// vec2/3/4 type, `rawInitializerText` (the comma-separated components typed, e.g. "1.0,2.0,3.0") is tried FIRST
// via buildVectorLiteral; otherwise the (possibly multi-term, possibly containing parenthesized groups)
// expression `terms`/`ops` resolves the value; failing both, the initializer is left as a placeholder rather
// than accepting whatever raw text was typed verbatim -- "bla" must never become a Float constant just because
// it also isn't an existing variable.
void ScriptEditor::applyDeclareVariable(const std::string& name, DSLType type, const std::vector<PendingExprTerm>& terms,
	const std::vector<DSLOperator>& ops, const std::string& rawInitializerText, DSLCodeLine& line)
{
	line.symbols.clear();

	DSLSymbol* typeDecl = pushSymbol(line, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ type });
	DSLSymbol* value = resolveValueOrPlaceholder(type, rawInitializerText, terms, ops, line);
	pushSymbol(line, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ name, typeDecl, value });
	selectExpressionTail(value); // land on the END of what was just composed, right where typing left off
}

// Re-commits an EXISTING declaration edited through the staged Declare flow (see m_redeclareTarget): the
// (possibly renamed) name and freshly composed initializer are applied IN PLACE on the same VariableDeclaration
// symbol, so every VariableReference elsewhere keeps pointing at it -- the one thing applyDeclareVariable's
// wholesale line rebuild could never do. An empty compose (nothing typed for the value) leaves a type-matched
// placeholder, same tolerance as a brand-new declaration.
void ScriptEditor::commitRedeclare(const std::vector<PendingExprTerm>& terms, const std::vector<DSLOperator>& ops, const std::string& rawInitializerText)
{
	DSLSymbol* target = m_redeclareTarget;
	const DSLType type = m_pendingDeclareType;
	const std::string name = m_pendingDeclareName;
	cancelCompose();
	if (target == nullptr || target->line == nullptr)
		return;
	DSLCodeLine& line = *target->line;
	DSLSymbol* originalHead = line.head();

	DSLSymbol* value = resolveValueOrPlaceholder(type, rawInitializerText, terms, ops, line);
	DSLSymbol::VariableDeclaration& decl = std::get<DSLSymbol::VariableDeclaration>(target->data);
	decl.name = name;
	decl.initialValue = value;

	restoreHeadAndCollect(line, originalHead);
	selectExpressionTail(value);
}

// See the declaration in ScriptEditor.ixx: shared by applyDeclareVariable, commitReassignStatement, and
// commitForStatement's loop-variable init.
DSLSymbol* ScriptEditor::resolveValueOrPlaceholder(DSLType type, const std::string& rawText,
	const std::vector<PendingExprTerm>& terms, const std::vector<DSLOperator>& ops, DSLCodeLine& line)
{
	if (isVectorType(type))
	{
		DSLSymbol* vec = buildVectorLiteral(type, splitOnCommas(rawText), line);
		if (vec != nullptr)
			return vec;
	}
	if (!terms.empty())
		return buildExpressionFromTerms(terms, ops, line);
	return pushSymbol(line, ST::Placeholder, DSLSymbol::Placeholder{ type });
}

// A plain candidate resolves directly; a parenthesized group becomes a nested `grouped` Expression chain --
// rendered back in its parens and always evaluated as one unit by the transpiler, exactly as parentheses force
// in ordinary arithmetic. Authored parens are ALWAYS kept, even where they're semantically redundant (a lone
// "(a + b)" initializer, or "(a)" around a single value, which wraps in a one-operand grouped chain) -- they're
// deliberate edit anchors: the group's ')' span is what later chaining "after the parens" hangs off.
DSLSymbol* ScriptEditor::buildExpressionTerm(const PendingExprTerm& term, DSLCodeLine& line)
{
	if (!term.isGroup)
	{
		// A resolved parameterized call carries its STAGED arguments (see the CallArgValue value sub-flow) --
		// never placeholder ones. A dot-call's receiver rides in the candidate itself.
		if (!term.callArgs.empty())
			return buildCallFromStagedArgs(term.candidate.refSymbol, term.candidate.receiver, term.candidate.receiverPath,
				term.callArgs, line);
		return buildValueFromCandidate(term.candidate, line);
	}
	DSLSymbol* built = buildExpressionFromTerms(term.groupTerms, term.groupOps, line);
	if (built != nullptr && built->type == ST::Expression)
	{
		std::get<DSLSymbol::Expression>(built->data).grouped = true; // an inner lone group stays ONE level of parens, not doubled
		return built;
	}
	return pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { built }, {}, /*grouped*/ true });
}

// Builds terms=[t0, t1, ..., tn] / ops=[o0, ..., o(n-1)] (oI between tI and t(I+1)) into one FLAT Expression
// chain, stored exactly as authored -- no precedence folding here (see DSL.ixx: the transpiler applies *, /, %
// over +, - at emit time; the stored structure only encodes explicit parens, via buildExpressionTerm's groups).
// `terms` must be non-empty; a single PLAIN term returns unwrapped (no Expression node needed), while a single
// GROUP keeps its parens (see buildExpressionTerm's comment).
DSLSymbol* ScriptEditor::buildExpressionFromTerms(const std::vector<PendingExprTerm>& terms, const std::vector<DSLOperator>& ops, DSLCodeLine& line)
{
	if (terms.size() == 1)
		return buildExpressionTerm(terms[0], line); // plain value / resolved call unwrapped; a lone group KEEPS its parens

	std::vector<DSLSymbol*> built;
	built.reserve(terms.size());
	for (const PendingExprTerm& term : terms)
		built.push_back(buildExpressionTerm(term, line));
	return pushSymbol(line, ST::Expression, DSLSymbol::Expression{ built, ops });
}

// Renders a single term back to text -- a plain candidate's display text, or (recursively) a fully-parenthesized
// group. Used both to rebuild the live compose-box preview (exprComposePrefixFromStack) and, transitively, for
// any groups nested inside a group.
std::string ScriptEditor::exprTermText(const PendingExprTerm& term) const
{
	if (!term.isGroup)
	{
		// A resolved parameterized call shows its actual staged arguments -- "func(1, x)" / "physics.teleport(...)".
		if (!term.callArgs.empty())
		{
			std::string text = (term.candidate.receiver != nullptr
				? std::get<DSLSymbol::VariableDeclaration>(term.candidate.receiver->data).name + "."
					+ (term.candidate.receiverPath.empty() ? std::string() : term.candidate.receiverPath + ".")
				: std::string())
				+ term.candidate.label + "(";
			for (size_t i = 0; i < term.callArgs.size(); ++i)
			{
				if (i > 0)
					text += ", ";
				text += chainDisplayText(term.callArgs[i]);
			}
			return text + ")";
		}
		return candidateDisplayText(term.candidate);
	}
	std::string inner;
	for (size_t i = 0; i < term.groupTerms.size(); ++i)
	{
		if (i > 0)
			inner += " " + std::string(dslOperatorText(term.groupOps[i - 1])) + " ";
		inner += exprTermText(term.groupTerms[i]);
	}
	return "(" + inner + ")";
}

std::string ScriptEditor::exprBasePrefixFor(ComposeMode mode) const
{
	if (mode == ComposeMode::DeclareValue)
		return std::string(dslTypeName(m_pendingDeclareType)) + " " + m_pendingDeclareName + " = ";
	if (mode == ComposeMode::ReassignValue && m_reassignTarget != nullptr)
	{
		// A widened re-edit keeps the existing statement's own operator (`thing += |`); the staged flow for a
		// brand-new reassignment shows whatever the ReassignOp stage picked (plain `=` when it was skipped).
		const char* opText = (m_reassignEditExpr != nullptr)
			? dslOperatorText(std::get<DSLSymbol::Expression>(m_reassignEditExpr->data).operators[0])
			: dslOperatorText(m_reassignOp);
		std::string target = std::get<DSLSymbol::VariableDeclaration>(m_reassignTarget->data).name;
		if (!m_reassignMemberPath.empty())
			target += "." + joinedMemberPath(m_reassignMemberPath); // a member-assign statement ("self.pos.x = ...")
		return target + " " + opText + " ";
	}
	// Inserting after an existing value: the compose box (which renders in place of the anchor's own span)
	// keeps showing that anchor plus the operator that started the insert; a replacement shows segment only.
	if (mode == ComposeMode::EditExpr && m_editInsert)
		return m_editAnchorText + " " + std::string(dslOperatorText(m_editLeadOp)) + " ";
	if (mode == ComposeMode::ReturnValue)
		return "return ";
	// The staged flows' chain-composing value stages (see enterChainStage -- the prefix is always derived,
	// so forward typing, operator chaining, and Backspace undo can never drift apart).
	if (mode == ComposeMode::ConditionLeft)
		return stagedConditionPrefix();
	if (mode == ComposeMode::ConditionRight)
		return stagedConditionPrefix() + chainDisplayText(m_conditionLeftChain) + " " + dslOperatorText(m_conditionOp) + " ";
	if (mode == ComposeMode::ForVarValue)
		return forVarPrefix();
	if (mode == ComposeMode::ForConditionLeft)
		return forVarDeclPrefix() + ", ";
	if (mode == ComposeMode::ForConditionValue)
		return forVarDeclPrefix() + ", " + chainDisplayText(m_forConditionLeftChain) + " " + m_forConditionOpCandidate.label + " ";
	if (mode == ComposeMode::ForIncrementValue)
		return forConditionPrefix() + ", " + m_forVarName + " " + m_forIncrementOpCandidate.label + " ";
	// A call argument's own lead-in ("outer context + funcName(arg0 = ..., arg1 = ") -- cached per-stage in
	// CallStage::outerLeadText (see callStagePrefix), never re-derived dynamically here, so this can't recurse
	// even when the SUSPENDED context being captured is itself another CallArgValue level (nested calls).
	if (mode == ComposeMode::CallArgValue)
		return callStagePrefix();
	return std::string();
}

std::string ScriptEditor::exprBasePrefix() const
{
	return exprBasePrefixFor(m_composeMode);
}

std::string ScriptEditor::conditionFlowBasePrefix() const
{
	return (m_conditionValueReturnMode != ComposeMode::None)
		? exprBasePrefixFor(m_conditionValueReturnMode)
		: std::string(conditionKeywordPrefix(m_conditionControl));
}

DSLSymbol* ScriptEditor::conditionValueExcludeVariable() const
{
	switch (m_conditionValueReturnMode)
	{
	case ComposeMode::DeclareValue:
		return m_redeclareTarget; // null for a brand-new declaration -- nothing to exclude, it doesn't exist yet
	case ComposeMode::EditExpr:
		return (m_editSlot.kind == SlotRef::Kind::VariableDeclarationInitialValue) ? m_editSlot.parent : nullptr;
	default:
		return nullptr; // ReassignValue (and everything else): self-reference is legitimate or not applicable
	}
}

// See the declaration in ScriptEditor.ixx: the whole staged logical chain so far, ending in the dangling
// &&/|| awaiting its next term -- "if i > 0 && ".
std::string ScriptEditor::stagedConditionPrefix() const
{
	std::string text = conditionFlowBasePrefix();
	for (size_t i = 0; i < m_logicalTerms.size(); ++i)
	{
		const PendingLogicalTerm& term = m_logicalTerms[i];
		text += term.isComparison
			? chainDisplayText(term.left) + " " + dslOperatorText(term.comparator) + " " + chainDisplayText(term.right)
			: chainDisplayText(term.left);
		text += std::string(" ") + dslOperatorText(m_logicalOps[i]) + " ";
	}
	return text;
}

// See the declaration in ScriptEditor.ixx: one comparison/bare value, or the flat &&/|| chain of them.
DSLSymbol* ScriptEditor::buildStagedBool(const std::vector<PendingLogicalTerm>& terms, const std::vector<DSLOperator>& ops,
	const PendingLogicalTerm& finalTerm, DSLCodeLine& line)
{
	auto buildTerm = [&](const PendingLogicalTerm& term) -> DSLSymbol*
	{
		if (!term.isComparison)
			return buildExpressionFromTerms(term.left.terms, term.left.ops, line);
		DSLSymbol* left = buildExpressionFromTerms(term.left.terms, term.left.ops, line);
		DSLSymbol* right = buildExpressionFromTerms(term.right.terms, term.right.ops, line);
		return pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { left, right }, { term.comparator } });
	};

	std::vector<DSLSymbol*> built;
	built.reserve(terms.size() + 1);
	for (const PendingLogicalTerm& term : terms)
		built.push_back(buildTerm(term));
	built.push_back(buildTerm(finalTerm));
	if (built.size() == 1)
		return built[0];
	return pushSymbol(line, ST::Expression, DSLSymbol::Expression{ built, ops });
}

// See the declaration in ScriptEditor.ixx: rebuilt fresh from m_exprStack (+ m_exprPendingGroup, if any) after
// every state change, forward or backward, instead of hand-patching m_composePrefix incrementally.
std::string ScriptEditor::exprComposePrefixFromStack() const
{
	std::string text;
	for (size_t depth = 0; depth < m_exprStack.size(); ++depth)
	{
		if (depth > 0)
			text += "(";
		const ExprFrame& frame = m_exprStack[depth];
		for (size_t i = 0; i < frame.terms.size(); ++i)
		{
			if (i > 0)
				text += " " + std::string(dslOperatorText(frame.ops[i - 1])) + " ";
			text += exprTermText(frame.terms[i]);
		}
		if (!frame.ops.empty() && frame.ops.size() == frame.terms.size())
			text += " " + std::string(dslOperatorText(frame.ops.back())) + " "; // dangling operator, awaiting the next term
	}
	if (m_exprHasPendingGroup)
		text += exprTermText(m_exprPendingGroup);
	return text;
}

// See the declaration in ScriptEditor.ixx.
bool ScriptEditor::exprTryFinalize(std::vector<PendingExprTerm>& outTerms, std::vector<DSLOperator>& outOps)
{
	if (m_exprStack.size() != 1)
		return false; // an open paren still needs to be closed first

	ExprFrame& top = m_exprStack[0];
	if (m_exprHasPendingGroup)
	{
		top.terms.push_back(m_exprPendingGroup);
		m_exprHasPendingGroup = false;
	}
	else if (top.ops.size() == top.terms.size()) // awaiting a term: either the very first one, or a dangling operator
	{
		const Candidate* picked = selectedCandidate();
		if (picked != nullptr && isParameterizedFunction(*picked))
			return false; // a parameterized call can't stand bare -- its arguments must stage first (type '(')
		if (picked != nullptr)
			top.terms.push_back(PendingExprTerm{ false, *picked, {}, {} });
		else if (!top.terms.empty() || !top.ops.empty())
			return false; // mid-chain with nothing typed for the trailing term yet -- keep composing
		// else: nothing typed at all -- terms stay empty, and every commit path refuses an empty value
	}

	outTerms = top.terms;
	outOps = top.ops;
	return true;
}

std::string ScriptEditor::chainDisplayText(const PendingExprChain& chain) const
{
	std::string text;
	for (size_t i = 0; i < chain.terms.size(); ++i)
	{
		if (i > 0)
			text += " " + std::string(dslOperatorText(chain.ops[i - 1])) + " ";
		text += exprTermText(chain.terms[i]);
	}
	return text;
}

bool ScriptEditor::captureComposedChain(PendingExprChain& out)
{
	return exprTryFinalize(out.terms, out.ops) && !out.terms.empty();
}

// See the declaration in ScriptEditor.ixx -- the same shape the compound-Backspace undo and the group reopen
// use: everything but the last term rests in the stack, the last term reopens in the box for further editing.
void ScriptEditor::restoreChainIntoCompose(const PendingExprChain& chain)
{
	ExprFrame frame{ chain.terms, chain.ops };
	PendingExprTerm last = std::move(frame.terms.back());
	frame.terms.pop_back();
	m_exprStack.assign(1, std::move(frame));
	m_exprHasPendingGroup = false;
	m_pendingWord.clear();
	restoreTermIntoBox(std::move(last));
}

// See the declaration in ScriptEditor.ixx: a group or a resolved parameterized call is atomic -- it re-opens
// as the PENDING term (further Backspace unpacks it); a plain candidate re-opens as its typed text.
void ScriptEditor::restoreTermIntoBox(PendingExprTerm&& term)
{
	// Receiver-carrying calls and members can't restore as typed text -- their label alone would never
	// re-resolve through the normal candidate lists -- so they ride as an already-resolved pending term too.
	if (term.isGroup || !term.callArgs.empty()
		|| term.candidate.receiver != nullptr || term.candidate.kind == Candidate::Kind::Member)
	{
		m_exprPendingGroup = std::move(term);
		m_exprHasPendingGroup = true;
	}
	else
	{
		m_pendingWord = term.candidate.label;
	}
}

// See the declaration in ScriptEditor.ixx: the single entry point into every chain-composing stage.
void ScriptEditor::enterChainStage(ComposeMode mode, const PendingExprChain* restore)
{
	m_exprStack.assign(1, ExprFrame{});
	m_exprHasPendingGroup = false;
	enterCompose(mode, "");
	if (restore != nullptr && !restore->terms.empty())
		restoreChainIntoCompose(*restore);
	m_composePrefix = exprBasePrefix() + exprComposePrefixFromStack();
	refreshCandidates();
}

DSLType ScriptEditor::composedChainPeekType() const
{
	if (!m_exprStack.empty() && !m_exprStack[0].terms.empty())
		return chainElementType(m_exprStack[0].terms);
	if (m_exprHasPendingGroup)
	{
		return m_exprPendingGroup.isGroup ? chainElementType(m_exprPendingGroup.groupTerms)
		                                  : candidateValueType(m_exprPendingGroup.candidate); // a resolved call
	}
	const Candidate* picked = selectedCandidate();
	return picked != nullptr ? candidateValueType(*picked) : DSLType::Void;
}

bool ScriptEditor::isChainComposeMode() const
{
	switch (m_composeMode)
	{
	case ComposeMode::DeclareValue:  return !isVectorType(m_pendingDeclareType);
	case ComposeMode::ReassignValue: return !isVectorType(reassignTargetType());
	case ComposeMode::EditExpr:      return !isVectorType(m_editValueType);
	case ComposeMode::ForVarValue:   return !isVectorType(m_forVarType);
	case ComposeMode::ReturnValue:
	case ComposeMode::ConditionLeft:
	case ComposeMode::ConditionRight:
	case ComposeMode::ForConditionLeft:
	case ComposeMode::ForConditionValue:
	case ComposeMode::ForIncrementValue:
	case ComposeMode::CallArgValue: // an argument is a full chain compose too, per-parameter (see m_callStack)
		return true;
	default:
		return false;
	}
}

PendingExprChain ScriptEditor::loopVarSeedChain() const
{
	PendingExprChain seed;
	seed.terms.assign(1, PendingExprTerm{ false, Candidate{ m_forVarName, Candidate::Kind::Variable, nullptr, m_forVarType }, {}, {} });
	return seed;
}

DSLType ScriptEditor::resolveMemberType(DSLType receiverType, const std::string& name) const
{
	if (receiverType == DSLType::ScriptData)
	{
		const DSLDataField* field = dslFindDataField(m_document, name);
		return field != nullptr ? field->type : DSLType::Void;
	}
	const BindingMember* member = m_bindings.findMember(receiverType, name);
	return member != nullptr ? member->type : DSLType::Void;
}

DSLType ScriptEditor::reassignTargetType() const
{
	if (m_reassignTarget == nullptr)
		return DSLType::Void;
	DSLType type = declaredTypeOf(m_reassignTarget);
	// A member-assign statement's value composes against the written MEMBER's type, not the root's.
	for (const std::string& segment : m_reassignMemberPath)
		type = resolveMemberType(type, segment);
	return type;
}

// Fills a LineHead statement slot with a `name = value` reassignment of an EXISTING variable (m_reassignTarget),
// built in ONE shot from the fully-resolved staged flow (see confirmCompose's ReassignValue handling) -- same
// value-resolution rules (vector components / compound expression / placeholder fallback) and `terms`/`ops`
// convention as applyDeclareVariable. Unlike a declaration, this doesn't open a block, so there's no body to
// seed -- lands directly on the assignment itself.
void ScriptEditor::commitReassignStatement(const std::vector<PendingExprTerm>& terms, const std::vector<DSLOperator>& ops, const std::string& rawInitializerText)
{
	DSLCodeLine* linePtr = currentLineHeadOrCancel();
	if (linePtr == nullptr)
		return;
	DSLCodeLine& line = *linePtr;
	DSLSymbol* target = m_reassignTarget;
	const std::string memberPath = joinedMemberPath(m_reassignMemberPath);
	const DSLType targetType = reassignTargetType();
	const DSLOperator assignOp = m_reassignOp; // plain `=`, or the ReassignOp stage's compound pick ("i += 1")
	cancelCompose();

	line.symbols.clear();

	DSLSymbol* value = resolveValueOrPlaceholder(targetType, rawInitializerText, terms, ops, line);
	// The target: the variable itself, or -- for a member-assign statement -- its dotted member chain
	// ("self.pos.x = ...", see m_reassignMemberPath).
	DSLSymbol* targetRef = buildReceiverChain(target, memberPath, line);
	pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { targetRef, value }, { assignOp } });

	selectExpressionTail(value);
}

// Re-commits an EXISTING assignment statement edited through the widened Reassign flow (see
// m_reassignEditExpr): ONLY the right-hand value swaps, in place -- the target reference and the statement's
// own operator (which may be a compound +=/-=/... the staged flow never authors) stay exactly as they were.
void ScriptEditor::commitReassignInPlace(const std::vector<PendingExprTerm>& terms, const std::vector<DSLOperator>& ops, const std::string& rawInitializerText)
{
	DSLSymbol* expr = m_reassignEditExpr;
	const DSLType targetType = reassignTargetType();
	cancelCompose();
	if (expr == nullptr || expr->line == nullptr)
		return;
	DSLCodeLine& line = *expr->line;
	DSLSymbol* originalHead = line.head();

	DSLSymbol* value = resolveValueOrPlaceholder(targetType, rawInitializerText, terms, ops, line);
	std::get<DSLSymbol::Expression>(expr->data).operands[1] = value;

	restoreHeadAndCollect(line, originalHead);
	selectExpressionTail(value);
}

// The staged re-edit flows' final Backspace: the line's old statement is gone, leaving a selected blank
// statement placeholder -- ready to type something new, or to Backspace once more and remove the line itself.
// Callers cancel the compose FIRST (this destroys symbols the compose state may still name).
void ScriptEditor::clearLineToBlankStatement(DSLCodeLine& line)
{
	line.symbols.clear();
	m_pendingSelectTarget = seedStatementPlaceholder(line);
}

// Commits a whole `if a op b` / `while a op b` in ONE shot from the fully-resolved staged flow (see
// confirmCompose's ConditionLeft/ConditionOp/ConditionRight handling) -- nothing about the condition touches
// the document until all three parts are in hand, so there is no way to end up with a half-built if/while
// sitting in the file. Replaces the ORIGINAL statement-placeholder's LineHead slot wholesale, same as any other
// statement replacement -- deleting the whole thing later (M7) is thus already just "replace this LineHead
// with a placeholder again", no special-casing needed for a multi-field statement.
void ScriptEditor::applyConditionalStatement(const PendingLogicalTerm& finalTerm)
{
	const std::vector<PendingLogicalTerm> logicalTerms = m_logicalTerms; // captured -- cancelCompose clears them
	const std::vector<DSLOperator> logicalOps = m_logicalOps;

	// A NEW elseif branch (picked inside an if/elseif's own body -- see m_conditionChainHeader): the blank
	// origin line is consumed and the header inserts AFTER the enclosing branch's block, at its level, with a
	// fresh body seeded inside -- growing the chain rather than replacing the cursor line.
	if (m_conditionChainHeader != nullptr)
	{
		DSLSymbol* chainHead = m_conditionChainHeader;
		DSLCodeLine* origin = m_conditionOriginLine;
		const DSLFlowControl control = m_conditionControl;
		cancelCompose();
		if (chainHead->line == nullptr)
			return;
		auto& lines = m_document.file.lines;
		if (origin != nullptr)
		{
			const int originIndex = dslLineIndex(m_document.file, origin);
			if (originIndex >= 0)
				lines.erase(lines.begin() + originIndex); // consumed -- erased FIRST, it sits inside the branch's block
		}
		const int headerIndex = dslLineIndex(m_document.file, chainHead->line);
		if (headerIndex < 0)
			return;
		const int headerScope = lines[headerIndex]->scopeLevel;
		DSLCodeLine& newLine = insertLineAfter(*lines[dslBlockEnd(m_document.file, headerIndex) - 1], headerScope);
		DSLSymbol* condition = buildStagedBool(logicalTerms, logicalOps, finalTerm, newLine);
		pushSymbol(newLine, ST::FlowControl, DSLSymbol::FlowControl{ control, condition });
		m_pendingSelectTarget = seedStatementPlaceholder(insertLineAfter(newLine, headerScope + 1));
		return;
	}

	// Re-authoring an existing header (m_flowEditLine) rebuilds it on ITS line regardless of where the cursor
	// span sits; a fresh statement still resolves through the cursor's LineHead slot.
	DSLCodeLine* linePtr = (m_flowEditLine != nullptr) ? m_flowEditLine : currentLineHeadOrCancel();
	if (linePtr == nullptr)
		return;
	DSLCodeLine& line = *linePtr;
	const bool reedit = (m_flowEditLine != nullptr);
	const DSLFlowControl control = m_conditionControl;
	cancelCompose();

	line.symbols.clear(); // safe for a re-edit too: a condition owns no declarations anything else references

	DSLSymbol* condition = buildStagedBool(logicalTerms, logicalOps, finalTerm, line);
	pushSymbol(line, ST::FlowControl, DSLSymbol::FlowControl{ control, condition });

	if (reedit)
	{
		// The block already has its body -- land at the end of the re-authored header, typing-continues style.
		selectExpressionTail(condition);
		return;
	}

	// Land the cursor inside the new block's body instead of on the header itself -- matches Enter's own
	// one-level-deeper rule for block openers (see insertLineAfterCursor/Syntax::isBlockOpener), so completing
	// an if/while is immediately ready to receive its first statement without an extra manual Enter.
	m_pendingSelectTarget = seedStatementPlaceholder(insertLineAfter(line, line.scopeLevel + 1));
}

// The staged bool value ("bool b = i < 5", "b = x > 0 && canJump()" -- see m_conditionValueReturnMode):
// builds the comparison/bare value/logical chain and commits it through the originating value mode's own shape
// -- new/in-place declaration, new/in-place reassignment, a return, or an in-place slot edit. The built
// Expressions are the same shapes conditions use, so all in-place editing applies afterwards.
void ScriptEditor::commitBoolValue(const PendingLogicalTerm& finalTerm)
{
	const ComposeMode returnMode = m_conditionValueReturnMode;
	const std::vector<PendingLogicalTerm> logicalTerms = m_logicalTerms; // captured -- cancelCompose clears them
	const std::vector<DSLOperator> logicalOps = m_logicalOps;

	auto buildComparison = [&](DSLCodeLine& line) -> DSLSymbol*
	{
		return buildStagedBool(logicalTerms, logicalOps, finalTerm, line);
	};

	if (returnMode == ComposeMode::DeclareValue)
	{
		if (m_redeclareTarget != nullptr)
		{
			DSLSymbol* target = m_redeclareTarget;
			const std::string name = m_pendingDeclareName;
			cancelCompose();
			if (target->line == nullptr)
				return;
			DSLCodeLine& line = *target->line;
			DSLSymbol* originalHead = line.head();
			DSLSymbol* comparison = buildComparison(line);
			DSLSymbol::VariableDeclaration& decl = std::get<DSLSymbol::VariableDeclaration>(target->data);
			decl.name = name;
			decl.initialValue = comparison;
			restoreHeadAndCollect(line, originalHead);
			selectExpressionTail(comparison);
			return;
		}
		DSLCodeLine* linePtr = currentLineHeadOrCancel();
		if (linePtr == nullptr)
			return;
		DSLCodeLine& line = *linePtr;
		const std::string name = m_pendingDeclareName;
		const DSLType type = m_pendingDeclareType;
		cancelCompose();
		line.symbols.clear();
		DSLSymbol* typeDecl = pushSymbol(line, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ type });
		DSLSymbol* comparison = buildComparison(line);
		pushSymbol(line, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ name, typeDecl, comparison });
		selectExpressionTail(comparison);
		return;
	}

	if (returnMode == ComposeMode::ReassignValue)
	{
		if (m_reassignEditExpr != nullptr)
		{
			DSLSymbol* expr = m_reassignEditExpr;
			cancelCompose();
			if (expr->line == nullptr)
				return;
			DSLCodeLine& line = *expr->line;
			DSLSymbol* originalHead = line.head();
			DSLSymbol* comparison = buildComparison(line);
			std::get<DSLSymbol::Expression>(expr->data).operands[1] = comparison;
			restoreHeadAndCollect(line, originalHead);
			selectExpressionTail(comparison);
			return;
		}
		DSLCodeLine* linePtr = currentLineHeadOrCancel();
		if (linePtr == nullptr)
			return;
		DSLCodeLine& line = *linePtr;
		DSLSymbol* target = m_reassignTarget;
		const DSLOperator assignOp = m_reassignOp;
		cancelCompose();
		line.symbols.clear();
		DSLSymbol* comparison = buildComparison(line);
		DSLSymbol* targetRef = pushSymbol(line, ST::VariableReference, DSLSymbol::VariableReference{ target });
		pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { targetRef, comparison }, { assignOp } });
		selectExpressionTail(comparison);
		return;
	}

	if (returnMode == ComposeMode::ReturnValue)
	{
		DSLCodeLine* linePtr = (m_flowEditLine != nullptr) ? m_flowEditLine : currentLineHeadOrCancel();
		if (linePtr == nullptr)
			return;
		DSLCodeLine& line = *linePtr;
		cancelCompose();
		line.symbols.clear();
		DSLSymbol* comparison = buildComparison(line);
		pushSymbol(line, ST::FlowControl, DSLSymbol::FlowControl{ DSLFlowControl::Return, comparison });
		selectExpressionTail(comparison);
		return;
	}

	// EditExpr: the comparison replaces the edited slot's occupant / chain operand in place.
	const SlotRef slot = m_editSlot;
	DSLSymbol* chainExpr = m_editChainExpr;
	const int operandIndex = m_editOperandIndex;
	cancelCompose();
	if (slot.line == nullptr)
		return;
	DSLCodeLine& line = *slot.line;
	DSLSymbol* originalHead = line.head();
	DSLSymbol* comparison = buildComparison(line);
	if (chainExpr != nullptr)
		std::get<DSLSymbol::Expression>(chainExpr->data).operands[operandIndex] = comparison;
	else
		writeSlot(slot, comparison);
	restoreHeadAndCollect(line, originalHead);
	selectExpressionTail(comparison);
}

// "else" picked on a blank statement inside an if/elseif branch: the blank line is consumed and an `else`
// header (plus a fresh body placeholder to land in) appends at the END of the whole chain -- an else always
// closes it, so any elseifs already following the enclosing branch are walked past first. No staging needed:
// an else carries no condition, so the insert is valid the moment it's picked (the candidates already
// guaranteed the chain has no else yet).
void ScriptEditor::applyElseStatement(DSLSymbol* chainHead, DSLCodeLine& originLine)
{
	auto& lines = m_document.file.lines;
	const int originIndex = dslLineIndex(m_document.file, &originLine);
	if (originIndex >= 0)
		lines.erase(lines.begin() + originIndex); // consumed -- erased FIRST, it sits inside the branch's block

	int branchIndex = (chainHead->line != nullptr) ? dslLineIndex(m_document.file, chainHead->line) : -1;
	if (branchIndex < 0)
		return;
	const int headerScope = lines[branchIndex]->scopeLevel;
	while (true)
	{
		const int next = dslBlockEnd(m_document.file, branchIndex);
		if (next >= static_cast<int>(lines.size()) || lines[next]->scopeLevel != headerScope)
			break;
		const DSLSymbol* nextHead = lines[next]->head();
		if (nextHead == nullptr || nextHead->type != ST::FlowControl)
			break;
		const DSLFlowControl control = std::get<DSLSymbol::FlowControl>(nextHead->data).control;
		if (control != DSLFlowControl::ElseIf && control != DSLFlowControl::Else)
			break;
		branchIndex = next;
	}

	DSLCodeLine& elseLine = insertLineAfter(*lines[dslBlockEnd(m_document.file, branchIndex) - 1], headerScope);
	pushSymbol(elseLine, ST::FlowControl, DSLSymbol::FlowControl{ DSLFlowControl::Else, nullptr });
	m_pendingSelectTarget = seedStatementPlaceholder(insertLineAfter(elseLine, headerScope + 1));
}

// "function name(type0 name0, type1 name1" -- rebuilt from the pending declare-function state on every stage
// transition (including step-back), rather than hand-appended piecewise, so the compose box can never drift
// out of sync with what's actually been resolved so far.
std::string ScriptEditor::functionDeclarePrefix() const
{
	std::string text = "function " + m_pendingFunctionName + "(";
	for (size_t i = 0; i < m_pendingParamNames.size(); ++i)
	{
		if (i > 0)
			text += ", ";
		if (m_pendingParamRefs[i])
			text += "ref ";
		text += std::string(dslTypeName(m_pendingParamTypes[i])) + " " + m_pendingParamNames[i];
	}
	return text;
}

std::string ScriptEditor::currentParamPrefix() const
{
	return functionDeclarePrefix() + (m_pendingParamNames.empty() ? "" : ", ")
		+ (m_pendingParamRef ? "ref " : "") + dslTypeName(m_pendingParamType) + " ";
}

bool ScriptEditor::isPendingParamNameTaken(const std::string& name) const
{
	if (AutoCompleteRules::isReservedWord(name))
		return true;
	for (const std::string& existing : m_pendingParamNames)
		if (existing == name)
			return true;
	return false;
}

// Commits a whole new `function name(type0 name0, ...)` in ONE shot from the fully-resolved staged flow (see
// confirmCompose's FunctionDeclareName/FunctionParamType/FunctionParamName handling and handleKeyEvent's ')'
// interception) -- nothing about the function touches the document until every parameter is in hand, same
// "never half-built" guarantee as applyDeclareVariable/applyConditionalStatement. Replaces the ORIGINAL
// statement-placeholder's LineHead slot wholesale, then seeds a fresh body placeholder one scope level deeper
// and lands the cursor there -- same courtesy as completing an if/while (see applyConditionalStatement).
void ScriptEditor::commitFunctionDeclaration()
{
	if (m_flowEditLine != nullptr)
	{
		commitFunctionRedeclare();
		return;
	}

	DSLCodeLine* linePtr = currentLineHeadOrCancel();
	if (linePtr == nullptr)
		return;
	DSLCodeLine& line = *linePtr;
	const std::string name = m_pendingFunctionName;
	const std::vector<DSLType> paramTypes = m_pendingParamTypes;
	const std::vector<std::string> paramNames = m_pendingParamNames;
	const DSLType returnType = m_pendingReturnType;
	cancelCompose();

	line.symbols.clear();

	std::vector<DSLSymbol*> params;
	for (size_t i = 0; i < paramNames.size(); ++i)
	{
		DSLSymbol* typeDecl = pushSymbol(line, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ paramTypes[i] });
		params.push_back(pushSymbol(line, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ paramNames[i], typeDecl }));
	}
	pushSymbol(line, ST::FunctionDeclaration, DSLSymbol::FunctionDeclaration{ name, params, returnType });

	m_pendingSelectTarget = seedStatementPlaceholder(insertLineAfter(line, line.scopeLevel + 1));
	if (returnType != DSLType::Void)
		m_pendingComposeReturnValue = true; // the seeded body line doubles as the enforced `return |` slot
}

// See the declaration in ScriptEditor.ixx: the staged Declare-function flow re-applied to an EXISTING header,
// preserving every symbol identity anything else points at. Parameters match POSITIONALLY: an unchanged type
// keeps the existing VariableDeclaration (renamed in place -- body references and named call arguments follow
// automatically); a changed type or a dropped tail builds/frees symbols, which is only allowed when nothing
// would dangle (guards below refuse the commit, keeping the flow composing -- Escape still abandons cleanly).
void ScriptEditor::commitFunctionRedeclare()
{
	DSLCodeLine& line = *m_flowEditLine;
	DSLSymbol* funcSymbol = line.head();
	if (funcSymbol == nullptr || funcSymbol->type != ST::FunctionDeclaration)
	{
		cancelCompose();
		return;
	}
	DSLSymbol::FunctionDeclaration& f = std::get<DSLSymbol::FunctionDeclaration>(funcSymbol->data);
	auto paramTypeOf = [](const DSLSymbol* param) -> DSLType
	{
		const DSLSymbol::VariableDeclaration& d = std::get<DSLSymbol::VariableDeclaration>(param->data);
		return std::get<DSLSymbol::TypeDeclaration>(d.typeSymbol->data).type;
	};

	// Guards run BEFORE anything mutates or the compose ends -- a refusal keeps the flow alive.
	const size_t oldCount = f.parameterVarDeclarations.size();
	bool signatureChanged = (oldCount != m_pendingParamTypes.size());
	for (size_t i = 0; !signatureChanged && i < oldCount; ++i)
		signatureChanged = (paramTypeOf(f.parameterVarDeclarations[i]) != m_pendingParamTypes[i]);
	if ((signatureChanged || m_pendingReturnType != f.returnType) && AutoCompleteRules::isFunctionReferenced(funcSymbol, m_document.file))
		return; // existing call sites are built against the old signature/return type -- they'd all break
	for (size_t i = 0; i < oldCount; ++i)
	{
		const bool kept = i < m_pendingParamTypes.size() && paramTypeOf(f.parameterVarDeclarations[i]) == m_pendingParamTypes[i];
		if (!kept && AutoCompleteRules::isVariableReferenced(f.parameterVarDeclarations[i], m_document.file))
			return; // the body still uses a parameter this edit would remove/retype
	}

	const std::string name = m_pendingFunctionName;
	const std::vector<DSLType> paramTypes = m_pendingParamTypes;
	const std::vector<std::string> paramNames = m_pendingParamNames;
	const std::vector<bool> paramRefs = m_pendingParamRefs;
	const DSLType returnType = m_pendingReturnType;
	cancelCompose();

	DSLSymbol* originalHead = line.head();
	std::vector<DSLSymbol*> params;
	for (size_t i = 0; i < paramTypes.size(); ++i)
	{
		DSLSymbol* existing = (i < oldCount) ? f.parameterVarDeclarations[i] : nullptr;
		if (existing != nullptr && paramTypeOf(existing) == paramTypes[i])
		{
			DSLSymbol::VariableDeclaration& d = std::get<DSLSymbol::VariableDeclaration>(existing->data);
			d.name = paramNames[i];
			d.isRef = paramRefs[i];
			params.push_back(existing);
		}
		else
		{
			DSLSymbol* typeDecl = pushSymbol(line, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ paramTypes[i] });
			params.push_back(pushSymbol(line, ST::VariableDeclaration,
				DSLSymbol::VariableDeclaration{ paramNames[i], typeDecl, nullptr, paramRefs[i] }));
		}
	}
	f.name = name;
	f.parameterVarDeclarations = params;
	const DSLType oldReturnType = f.returnType;
	f.returnType = returnType; // restored at widen, possibly re-picked through the "-> type" stage

	restoreHeadAndCollect(line, originalHead); // superseded (guaranteed-unreferenced) params + their types sweep here
	m_pendingSelectLineEnd = m_cursorLine;     // end of the re-authored header; the body already exists
	if (returnType != oldReturnType)
		applyFunctionReturnChange(funcSymbol, returnType); // old returns out; a non-Void change seeds/selects the fresh `return |`
}

DSLType ScriptEditor::currentCallParamType() const
{
	const CallStage& stage = m_callStack.back();
	const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(stage.func->data);
	const DSLSymbol::VariableDeclaration& param = std::get<DSLSymbol::VariableDeclaration>(
		callee.parameterVarDeclarations[stage.argChains.size()]->data);
	return std::get<DSLSymbol::TypeDeclaration>(param.typeSymbol->data).type;
}

// "name(param0 = <resolved>, param1 = " -- rebuilt from the staged-call state (m_callStack.back()) on every
// stage transition (including step-back), same never-hand-patched convention as functionDeclarePrefix.
// Positional builtins (vec3 as a statement) skip the "name = " labels, matching how their calls render.
std::string ScriptEditor::callComposePrefix() const
{
	const CallStage& stage = m_callStack.back();
	const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(stage.func->data);
	std::string text = (stage.receiver != nullptr
		? std::get<DSLSymbol::VariableDeclaration>(stage.receiver->data).name + "."
			+ (stage.receiverPath.empty() ? std::string() : stage.receiverPath + ".")
		: std::string())
		+ callee.name + "(";
	const size_t shownParams = std::min(stage.argChains.size() + 1, callee.parameterVarDeclarations.size());
	for (size_t i = 0; i < shownParams; ++i)
	{
		if (i > 0)
			text += ", ";
		const DSLSymbol::VariableDeclaration& param = std::get<DSLSymbol::VariableDeclaration>(callee.parameterVarDeclarations[i]->data);
		if (!callee.isPositionalCall)
		{
			if (param.isRef)
				text += "ref ";
			text += param.name + " = ";
		}
		if (i < stage.argChains.size())
			text += chainDisplayText(stage.argChains[i]);
	}
	return text;
}

// The CURRENT call stage's own full lead-in: whatever context it was staged FROM (cached once at push, see
// CallStage::outerLeadText) plus this call's own "name(args so far" text.
std::string ScriptEditor::callStagePrefix() const
{
	return m_callStack.back().outerLeadText + callComposePrefix();
}

std::string ScriptEditor::chainLeadTextFor(ComposeMode mode) const
{
	return exprBasePrefixFor(mode); // now handles CallArgValue itself (see exprBasePrefixFor)
}

// See the declaration in ScriptEditor.ixx: the chain compose suspends while the call's arguments stage -- a
// new CallStage saves it (m_exprStack included, since CallArgValue is itself chain-composing now: an
// argument may contain another staged call, i.e. this same function firing again from WITHIN CallArgValue).
bool ScriptEditor::tryBeginValueCallStaging()
{
	if (m_pendingWord.empty())
		return false; // only over TYPED text -- the highlighted default must never resolve off a stray key
	const Candidate* picked = selectedCandidate();
	if (picked == nullptr || !isParameterizedFunction(*picked))
		return false;

	// Captured BEFORE the save/reset below, while m_exprStack/m_composeMode still reflect what's being suspended.
	const std::string outerLead = chainLeadTextFor(m_composeMode) + exprComposePrefixFromStack();

	CallStage& stage = m_callStack.emplace_back();
	stage.func = picked->refSymbol;
	stage.receiver = picked->receiver;
	stage.receiverPath = picked->receiverPath;
	stage.returnMode = m_composeMode;
	stage.outerLeadText = outerLead;
	stage.savedExprStack = std::move(m_exprStack);
	stage.savedPendingGroup = std::move(m_exprPendingGroup);
	stage.savedHasPendingGroup = m_exprHasPendingGroup;
	m_exprStack.assign(1, ExprFrame{});
	m_exprHasPendingGroup = false;

	enterCompose(ComposeMode::CallArgValue, callStagePrefix());
	return true;
}

// The value constraints of `mode`'s current slot -- mirrors refreshCandidates' per-mode candidate queries, so
// a MemberSelect entered from any stage filters the receiver's functions/members the same way the stage itself
// filters plain candidates. Void + !outAnyValue = a statement context.
DSLType ScriptEditor::valueContextExpectedType(ComposeMode mode, bool& outAnyValue) const
{
	outAnyValue = false;
	const DSLType liveChainType = (!m_exprStack.empty() && !m_exprStack[0].terms.empty())
		? chainElementType(m_exprStack[0].terms) : DSLType::Void;
	switch (mode)
	{
	case ComposeMode::DeclareValue:
		return m_pendingDeclareType;
	case ComposeMode::ConditionLeft:
	case ComposeMode::ForConditionLeft:
		outAnyValue = liveChainType == DSLType::Void;
		return liveChainType;
	case ComposeMode::ConditionRight:
		return chainElementType(m_conditionLeftChain.terms);
	case ComposeMode::ForVarValue:
	case ComposeMode::ForIncrementValue:
		return m_forVarType;
	case ComposeMode::ForConditionValue:
	{
		const DSLType boundType = chainElementType(m_forConditionLeftChain.terms);
		return boundType != DSLType::Void ? boundType : m_forVarType;
	}
	case ComposeMode::ReassignValue:
		return reassignTargetType();
	case ComposeMode::ReturnValue:
	{
		const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		return (span != nullptr && span->slot.line != nullptr)
			? AutoCompleteRules::enclosingFunctionReturnType(*span->slot.line, m_document.file) : DSLType::Void;
	}
	case ComposeMode::CallArgValue:
		return currentCallParamType();
	case ComposeMode::EditExpr:
		outAnyValue = m_editValueType == DSLType::Void;
		return m_editValueType;
	default:
		return DSLType::Void; // FilterCandidates: a statement slot
	}
}

// See the declaration in ScriptEditor.ixx: '.' (or a confirm) over a matched BindingObject candidate opens the
// receiver's member/function list; the current stage suspends exactly like the call-value sub-flow.
// Re-applies a dotted member path onto a just-entered MemberSelect (the reverse of the '.'-extension steps) --
// how an abandoned chained dot-call staging ("self.pos.length(|" backspaced empty) reopens at its member list.
void ScriptEditor::restoreMemberPath(const std::string& dottedPath)
{
	size_t start = 0;
	while (start < dottedPath.size())
	{
		const size_t dot = dottedPath.find('.', start);
		const std::string segment = dottedPath.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		m_memberPath.push_back(segment);
		m_memberReceiverType = resolveMemberType(m_memberReceiverType, segment);
		m_composePrefix += segment + ".";
		if (dot == std::string::npos)
			break;
		start = dot + 1;
	}
}

void ScriptEditor::enterMemberSelect(DSLSymbol* receiverDecl)
{
	m_memberReceiver = receiverDecl;
	m_memberPath.clear();
	m_memberReceiverType = declaredTypeOf(receiverDecl);
	m_memberReturnMode = m_composeMode;
	m_memberExpectedType = valueContextExpectedType(m_composeMode, m_memberAnyValue);
	const std::string& name = std::get<DSLSymbol::VariableDeclaration>(receiverDecl->data).name;
	enterCompose(ComposeMode::MemberSelect, m_composePrefix + name + ".");
}

// See the declaration in ScriptEditor.ixx: the fully-staged arguments become real CallArgument values -- a
// picked candidate or a complete vector component list each, never a placeholder.
DSLSymbol* ScriptEditor::buildReceiverChain(DSLSymbol* rootDecl, const std::string& dottedPath, DSLCodeLine& line)
{
	DSLSymbol* current = pushSymbol(line, ST::VariableReference, DSLSymbol::VariableReference{ rootDecl });
	DSLType currentType = declaredTypeOf(rootDecl);
	size_t start = 0;
	while (start < dottedPath.size())
	{
		const size_t dot = dottedPath.find('.', start);
		const std::string name = dottedPath.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		const DSLType memberType = resolveMemberType(currentType, name);
		current = pushSymbol(line, ST::MemberAccess, DSLSymbol::MemberAccess{ current, name, memberType });
		currentType = memberType;
		if (dot == std::string::npos)
			break;
		start = dot + 1;
	}
	return current;
}

DSLSymbol* ScriptEditor::buildCallFromStagedArgs(DSLSymbol* funcSymbol, DSLSymbol* receiverDecl, const std::string& receiverPath,
	const std::vector<PendingExprChain>& argChains, DSLCodeLine& line)
{
	const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(funcSymbol->data);
	std::vector<DSLSymbol::CallArgument> args;
	for (size_t i = 0; i < argChains.size(); ++i)
	{
		DSLSymbol* value = buildExpressionFromTerms(argChains[i].terms, argChains[i].ops, line);
		args.push_back(DSLSymbol::CallArgument{ callee.isPositionalCall ? nullptr : callee.parameterVarDeclarations[i], value });
	}
	DSLSymbol* receiverRef = (receiverDecl != nullptr) ? buildReceiverChain(receiverDecl, receiverPath, line) : nullptr;
	return pushSymbol(line, ST::FunctionCall, DSLSymbol::FunctionCall{ funcSymbol, receiverRef, args });
}

// The last argument's confirm. A call VALUE (CallStage::returnMode != None) POPS this stage and returns the
// resolved term to the SUSPENDED chain compose beneath it (restoring its own m_exprStack exactly where it left
// off) -- nothing commits; a call STATEMENT commits its whole line in one shot, same "never half-built"
// guarantee as every other staged statement.
void ScriptEditor::commitCallStatement()
{
	CallStage stage = std::move(m_callStack.back());
	m_callStack.pop_back();

	if (stage.returnMode != ComposeMode::None)
	{
		const ComposeMode back = stage.returnMode;
		PendingExprTerm term;
		term.candidate = Candidate{ std::get<DSLSymbol::FunctionDeclaration>(stage.func->data).name,
			Candidate::Kind::Function, stage.func };
		term.candidate.receiver = stage.receiver;
		term.candidate.receiverPath = stage.receiverPath;
		term.callArgs = std::move(stage.argChains);

		// Resume the SUSPENDED context (restoring ITS OWN m_exprStack, frozen since this call's staging
		// began) with the resolved call as the pending term, exactly like a group's ')' just closed.
		m_exprStack = std::move(stage.savedExprStack);
		m_exprPendingGroup = std::move(stage.savedPendingGroup);
		m_exprHasPendingGroup = stage.savedHasPendingGroup;
		enterCompose(back, "");
		m_exprPendingGroup = std::move(term);
		m_exprHasPendingGroup = true;
		m_candidates.clear(); // nothing is being typed right after the resolved call -- operators continue it
		m_composePrefix = chainLeadTextFor(back) + exprComposePrefixFromStack();
		return;
	}

	// Re-authoring an existing call (m_flowEditLine) targets its own line -- a call statement's symbols are
	// referenced by nothing else, so the wholesale rebuild below is safe for both paths.
	DSLCodeLine* linePtr = (m_flowEditLine != nullptr) ? m_flowEditLine : currentLineHeadOrCancel();
	if (linePtr == nullptr)
		return;
	DSLCodeLine& line = *linePtr;
	DSLSymbol* func = stage.func;
	DSLSymbol* receiver = stage.receiver;
	const std::string receiverPath = stage.receiverPath;
	const std::vector<PendingExprChain> argChains = std::move(stage.argChains);
	cancelCompose();

	line.symbols.clear();
	buildCallFromStagedArgs(func, receiver, receiverPath, argChains, line);
	m_pendingSelectLineEnd = m_cursorLine; // end of the committed call (its last argument), typing-continues style
}

// "for <type> <name> = " -- everything up to (not including) the loop variable's own initial value.
std::string ScriptEditor::forVarPrefix() const
{
	return "for " + std::string(dslTypeName(m_forVarType)) + " " + m_forVarName + " = ";
}

// forVarPrefix() + the resolved initial value (a possibly-compound chain) -- the whole loop-variable clause,
// exactly as it'll read once committed. Only ever called after ForVarValue has confirmed, so the init chain is
// always meaningful by then when the type isn't a vector.
std::string ScriptEditor::forVarDeclPrefix() const
{
	const std::string initText = isVectorType(m_forVarType) ? m_forVarInitRawText : chainDisplayText(m_forVarInitChain);
	return forVarPrefix() + initText;
}

// forVarDeclPrefix() + ", <left> <op> <value>" -- the whole condition clause too, once resolved (both sides
// possibly-compound chains, e.g. "i + 2 < n * 2").
std::string ScriptEditor::forConditionPrefix() const
{
	return forVarDeclPrefix() + ", " + chainDisplayText(m_forConditionLeftChain) + " "
		+ m_forConditionOpCandidate.label + " " + chainDisplayText(m_forConditionValueChain);
}

// Commits a whole new `for type name = init, name op bound, name incrOp step` in ONE shot from the fully-
// resolved staged flow (see confirmCompose's ForVarType/Name/Value, ForConditionOp/Value, and
// ForIncrementOp/Value handling) -- nothing about the for-loop touches the document until every clause is in
// hand, same "never half-built" guarantee as if/while/function. Replaces the ORIGINAL statement-placeholder's
// LineHead slot wholesale, then seeds a fresh body placeholder one scope level deeper and lands the cursor
// there -- same courtesy as completing an if/while/function.
void ScriptEditor::commitForStatement()
{
	// Re-authoring an existing loop (m_flowEditLine) targets its own line; a fresh statement resolves through
	// the cursor's LineHead slot.
	DSLCodeLine* linePtr = (m_flowEditLine != nullptr) ? m_flowEditLine : currentLineHeadOrCancel();
	if (linePtr == nullptr)
		return;
	DSLCodeLine& line = *linePtr;
	DSLSymbol* existingLoopVar = m_flowEditLoopVar;
	const DSLType varType = m_forVarType;
	const std::string varName = m_forVarName;
	const std::string varInitRawText = m_forVarInitRawText;
	const PendingExprChain initChain = m_forVarInitChain;
	const PendingExprChain conditionLeftChain = m_forConditionLeftChain;
	const Candidate conditionOpCandidate = m_forConditionOpCandidate;
	const PendingExprChain conditionValueChain = m_forConditionValueChain;
	const Candidate incrementOpCandidate = m_forIncrementOpCandidate;
	const PendingExprChain incrementValueChain = m_forIncrementValueChain;
	cancelCompose();

	if (existingLoopVar != nullptr)
	{
		// RE-EDIT: the loop variable's symbol identity must survive (body statements reference it), so the
		// header rebuilds around it in place -- name and initializer swapped on the SAME declaration (its type
		// stays fixed, see m_flowEditLoopVar's comment), fresh condition/increment expressions repointed on
		// the SAME FlowControl head, everything superseded swept by the reachability pass.
		DSLSymbol* originalHead = line.head();
		m_forBuildLoopVar = existingLoopVar; // sentinel loop-var candidates in the chains resolve to it
		DSLSymbol::VariableDeclaration& decl = std::get<DSLSymbol::VariableDeclaration>(existingLoopVar->data);
		decl.name = varName;
		decl.initialValue = resolveValueOrPlaceholder(varType, varInitRawText, initChain.terms, initChain.ops, line);

		DSLSymbol* condLeft = buildExpressionFromTerms(conditionLeftChain.terms, conditionLeftChain.ops, line);
		DSLSymbol* condRight = buildExpressionFromTerms(conditionValueChain.terms, conditionValueChain.ops, line);
		DSLSymbol* condition = pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { condLeft, condRight }, { conditionOpCandidate.op } });
		DSLSymbol* incrLeft = pushSymbol(line, ST::VariableReference, DSLSymbol::VariableReference{ existingLoopVar });
		DSLSymbol* incrRight = buildExpressionFromTerms(incrementValueChain.terms, incrementValueChain.ops, line);
		DSLSymbol* increment = pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { incrLeft, incrRight }, { incrementOpCandidate.op } });
		m_forBuildLoopVar = nullptr;

		DSLSymbol::FlowControl& fc = std::get<DSLSymbol::FlowControl>(originalHead->data);
		fc.forCondition = condition;
		fc.forIncrement = increment;

		restoreHeadAndCollect(line, originalHead);
		selectExpressionTail(increment); // end of the re-authored header; the body already exists
		return;
	}

	line.symbols.clear();

	// Loop variable: type name = init (the init a full compound chain). Built FIRST so the sentinel loop-var
	// candidates inside the condition/increment chains have a real symbol to resolve to (m_forBuildLoopVar).
	DSLSymbol* typeDecl = pushSymbol(line, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ varType });
	DSLSymbol* initValue = resolveValueOrPlaceholder(varType, varInitRawText, initChain.terms, initChain.ops, line);
	DSLSymbol* loopVar = pushSymbol(line, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ varName, typeDecl, initValue });
	m_forBuildLoopVar = loopVar;

	// Condition: left op bound -- the left usually the loop variable it was seeded with, possibly extended
	// ("i + 2 < n * 2"); the increment's left is always the loop variable itself.
	DSLSymbol* condLeft = buildExpressionFromTerms(conditionLeftChain.terms, conditionLeftChain.ops, line);
	DSLSymbol* condRight = buildExpressionFromTerms(conditionValueChain.terms, conditionValueChain.ops, line);
	DSLSymbol* condition = pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { condLeft, condRight }, { conditionOpCandidate.op } });

	DSLSymbol* incrLeft = pushSymbol(line, ST::VariableReference, DSLSymbol::VariableReference{ loopVar });
	DSLSymbol* incrRight = buildExpressionFromTerms(incrementValueChain.terms, incrementValueChain.ops, line);
	DSLSymbol* increment = pushSymbol(line, ST::Expression, DSLSymbol::Expression{ { incrLeft, incrRight }, { incrementOpCandidate.op } });
	m_forBuildLoopVar = nullptr;

	pushSymbol(line, ST::FlowControl, DSLSymbol::FlowControl{ DSLFlowControl::For, nullptr, loopVar, condition, increment });

	m_pendingSelectTarget = seedStatementPlaceholder(insertLineAfter(line, line.scopeLevel + 1));
}

// Replaces the currently-selected STATEMENT slot's line wholesale with `candidate` -- the core of the "always
// compilable" editing model. A freshly-inserted call gets Placeholder arguments for everything its callee
// requires. Value slots never come through here anymore (beginCompose routes them into the in-place EditExpr
// flow instead); growing the document with new lines is Enter's job (insertLineAfterCursor), not this
// function's -- confirming a candidate never inserts additional lines on its own.
void ScriptEditor::applyCandidate(const Candidate& candidate)
{
	const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
	if (span == nullptr || span->slot.kind != SlotRef::Kind::LineHead || span->slot.line == nullptr)
		return;
	DSLCodeLine& line = *span->slot.line;
	line.symbols.clear(); // the old placeholder-statement (or prior statement) is wholly replaced

	DSLSymbol* newHead = nullptr;

	switch (candidate.kind)
	{
	// KeywordIf/KeywordWhile are intercepted earlier in confirmCompose (they start the ConditionLeft/Op/Right
	// staged flow instead of applying here directly) and never reach this switch.
	case Candidate::Kind::KeywordReturn:
	{
		const DSLType retType = AutoCompleteRules::enclosingFunctionReturnType(line, m_document.file);
		DSLSymbol* value = (retType == DSLType::Void) ? nullptr : pushSymbol(line, ST::Placeholder, DSLSymbol::Placeholder{ retType });
		newHead = pushSymbol(line, ST::FlowControl, DSLSymbol::FlowControl{ DSLFlowControl::Return, value });
		break;
	}
	case Candidate::Kind::KeywordBreak:
		newHead = pushSymbol(line, ST::FlowControl, DSLSymbol::FlowControl{ DSLFlowControl::Break, nullptr });
		break;
	case Candidate::Kind::DeclareType:
		break; // handled earlier in confirmCompose -- never reaches here
	default:
		newHead = buildValueFromCandidate(candidate, line);
		break;
	}

	if (newHead != nullptr)
		m_pendingSelectTarget = newHead;
}

// ---- In-place expression editing (EditExpr / ReplaceOperator -- see the .ixx class comment) ----

namespace
{
	// Every symbol reachable from `symbol` through the structural cross-reference fields (see DSL.ixx's
	// ownership model). Cross-line/builtin targets get marked too -- harmless, since the sweep below only ever
	// erases symbols owned by the ONE line being collected. Linear lookup: lines own a few dozen symbols at most.
	void markReachable(const DSLSymbol* symbol, std::vector<const DSLSymbol*>& reachable)
	{
		if (symbol == nullptr || std::find(reachable.begin(), reachable.end(), symbol) != reachable.end())
			return;
		reachable.push_back(symbol);
		switch (symbol->type)
		{
		case ST::VariableReference:
			markReachable(std::get<DSLSymbol::VariableReference>(symbol->data).declaration, reachable);
			break;
		case ST::VariableDeclaration:
		{
			const DSLSymbol::VariableDeclaration& v = std::get<DSLSymbol::VariableDeclaration>(symbol->data);
			markReachable(v.typeSymbol, reachable);
			markReachable(v.initialValue, reachable);
			break;
		}
		case ST::FunctionCall:
		{
			const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(symbol->data);
			markReachable(call.functionSymbol, reachable);
			markReachable(call.receiver, reachable);
			for (const DSLSymbol::CallArgument& arg : call.arguments)
			{
				markReachable(arg.parameter, reachable);
				markReachable(arg.value, reachable);
			}
			break;
		}
		case ST::FunctionDeclaration:
			for (DSLSymbol* param : std::get<DSLSymbol::FunctionDeclaration>(symbol->data).parameterVarDeclarations)
				markReachable(param, reachable);
			break;
		case ST::FlowControl:
		{
			const DSLSymbol::FlowControl& fc = std::get<DSLSymbol::FlowControl>(symbol->data);
			markReachable(fc.condition, reachable);
			markReachable(fc.forLoopVar, reachable);
			markReachable(fc.forCondition, reachable);
			markReachable(fc.forIncrement, reachable);
			break;
		}
		case ST::Expression:
			for (DSLSymbol* operand : std::get<DSLSymbol::Expression>(symbol->data).operands)
				markReachable(operand, reachable);
			break;
		case ST::MemberAccess:
			markReachable(std::get<DSLSymbol::MemberAccess>(symbol->data).receiver, reachable);
			break;
		default:
			break; // Constant/TypeDeclaration/Placeholder hold no cross-references
		}
	}
}

namespace
{
	bool vectorRawTextFromValue(const DSLSymbol* value, std::string& out); // defined below -- termFromSymbol restores vector arguments through it
	bool chainFromSymbol(const DSLSymbol* symbol, PendingExprChain& out); // defined below -- mutually recursive with termFromSymbol (a call's arguments restore as full chains, which may themselves contain calls)

	// Walks a receiver expression (a VariableReference, or a MemberAccess chain over one) down to its ROOT
	// declaration, collecting the dotted member path ("" when `symbol` IS the plain reference). False = any
	// other shape (nothing else is a legal receiver).
	bool receiverChainToRoot(const DSLSymbol* symbol, DSLSymbol*& outRootDecl, std::string& outPath)
	{
		std::string path;
		const DSLSymbol* current = symbol;
		while (current != nullptr && current->type == ST::MemberAccess)
		{
			const DSLSymbol::MemberAccess& m = std::get<DSLSymbol::MemberAccess>(current->data);
			path = m.memberName + (path.empty() ? "" : "." + path);
			current = m.receiver;
		}
		if (current == nullptr || current->type != ST::VariableReference)
			return false;
		outRootDecl = std::get<DSLSymbol::VariableReference>(current->data).declaration;
		outPath = std::move(path);
		return outRootDecl != nullptr;
	}

	// The REVERSE of buildExpressionTerm: converts a committed value symbol back into the compose-flow's
	// PendingExprTerm form, so an existing parenthesized group can reopen for editing exactly as if it were
	// still being typed (beginReopenGroup). False = not round-trippable: placeholders, member accesses, and
	// receiver-carrying dot-calls have no Candidate representation (yet -- M5 for dot-calls). A parameterized
	// FunctionCall restores WITH its staged arguments -- or refuses when one can't re-stage (a compound
	// argument, a vararg builtin): a restore never approximates, and placeholders never exist.
	bool termFromSymbol(const DSLSymbol* symbol, PendingExprTerm& out)
	{
		switch (symbol->type)
		{
		case ST::Constant:
		{
			const DSLSymbol::Constant& c = std::get<DSLSymbol::Constant>(symbol->data);
			const std::string label = (c.type == DSLType::String) ? "\"" + c.value + "\"" : c.value;
			out = PendingExprTerm{ false, Candidate{ label, Candidate::Kind::Literal, nullptr, c.type }, {}, {} };
			return true;
		}
		case ST::VariableReference:
		{
			const DSLSymbol::VariableReference& r = std::get<DSLSymbol::VariableReference>(symbol->data);
			if (r.declaration == nullptr)
				return false;
			out = PendingExprTerm{ false,
				Candidate{ std::get<DSLSymbol::VariableDeclaration>(r.declaration->data).name, Candidate::Kind::Variable, r.declaration }, {}, {} };
			return true;
		}
		case ST::MemberAccess:
		{
			// A member chain ("self.pos.x"): refSymbol = the chain's ROOT declaration, label = the dotted
			// path, declareType = the OUTER member's stamped type -- receiverCandidates' exact shape.
			DSLSymbol* rootDecl = nullptr;
			std::string path;
			if (!receiverChainToRoot(symbol, rootDecl, path))
				return false;
			out = PendingExprTerm{ false, Candidate{ path, Candidate::Kind::Member, rootDecl,
				std::get<DSLSymbol::MemberAccess>(symbol->data).type }, {}, {} };
			return true;
		}
		case ST::FunctionCall:
		{
			const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(symbol->data);
			if (call.functionSymbol == nullptr)
				return false;
			// A dot-call restores WITH its receiver chain riding in the candidate (see buildExpressionTerm).
			DSLSymbol* receiverDecl = nullptr;
			std::string receiverPath;
			if (call.receiver != nullptr)
			{
				std::string fullPath;
				if (!receiverChainToRoot(call.receiver, receiverDecl, fullPath))
					return false;
				receiverPath = std::move(fullPath); // "" for a direct dot-call (receiver = the root reference)
			}
			const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(call.functionSymbol->data);
			PendingExprTerm term{ false, Candidate{ callee.name, Candidate::Kind::Function, call.functionSymbol }, {}, {} };
			term.candidate.receiver = receiverDecl;
			term.candidate.receiverPath = std::move(receiverPath);
			if (!call.arguments.empty())
			{
				if (callee.parameterVarDeclarations.size() != call.arguments.size())
					return false; // vararg-style builtins (print) have no parameters to re-stage against
				for (const DSLSymbol::CallArgument& arg : call.arguments)
				{
					PendingExprChain argChain;
					if (!chainFromSymbol(arg.value, argChain))
						return false; // not restorable (a placeholder, ...) -- refuse, never approximate
					term.callArgs.push_back(std::move(argChain));
				}
			}
			out = std::move(term);
			return true;
		}
		case ST::Expression:
		{
			// Grouped or not, a nested chain re-composes as a group -- inside an arithmetic chain the two are
			// only distinguishable by parens, and a group is the shape the compose flow can hold.
			const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(symbol->data);
			PendingExprTerm group{ true, {}, {}, {} };
			for (const DSLSymbol* operand : e.operands)
			{
				PendingExprTerm term;
				if (!termFromSymbol(operand, term))
					return false;
				group.groupTerms.push_back(std::move(term));
			}
			group.groupOps = e.operators;
			out = std::move(group);
			return true;
		}
		default:
			return false;
		}
	}

	// A committed value symbol as ONE plain staged candidate. False for anything a single candidate can't
	// hold: a compound chain, or the non-round-trippable kinds termFromSymbol refuses (chainFromSymbol below
	// is the multi-term variant the staged restores actually use).
	bool candidateFromSymbol(const DSLSymbol* symbol, Candidate& out)
	{
		PendingExprTerm term;
		if (!termFromSymbol(symbol, term) || term.isGroup)
			return false;
		out = term.candidate;
		return true;
	}

	// A committed value back into a staged CHAIN: a flat (ungrouped) arithmetic Expression becomes its terms
	// and operators; anything else round-trips as a single term. False = not restorable (see termFromSymbol).
	bool chainFromSymbol(const DSLSymbol* symbol, PendingExprChain& out)
	{
		if (symbol != nullptr && symbol->type == ST::Expression)
		{
			const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(symbol->data);
			if (!e.grouped && !e.operators.empty() && dslIsArithmeticOperator(e.operators[0]))
			{
				out.terms.clear();
				for (const DSLSymbol* operand : e.operands)
				{
					PendingExprTerm term;
					if (!termFromSymbol(operand, term))
						return false;
					out.terms.push_back(std::move(term));
				}
				out.ops = e.operators;
				return true;
			}
		}
		PendingExprTerm term;
		if (!termFromSymbol(symbol, term))
			return false;
		out.terms.assign(1, std::move(term));
		out.ops.clear();
		return true;
	}

	// A committed positional vecN(constants...) call back into "x,y,z" component text -- restoring a VECTOR
	// loop variable's staged init. False if any argument isn't a plain constant.
	bool vectorRawTextFromValue(const DSLSymbol* value, std::string& out)
	{
		if (value == nullptr || value->type != ST::FunctionCall)
			return false;
		std::string text;
		const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(value->data);
		for (size_t i = 0; i < call.arguments.size(); ++i)
		{
			const DSLSymbol* component = call.arguments[i].value;
			if (component == nullptr || component->type != ST::Constant)
				return false;
			if (i > 0)
				text += ",";
			text += std::get<DSLSymbol::Constant>(component->data).value;
		}
		out = std::move(text);
		return true;
	}
}

// See the declaration in ScriptEditor.ixx: post-order head restore + unreachable-symbol sweep, the shared tail
// of every in-place structural edit.
void ScriptEditor::restoreHeadAndCollect(DSLCodeLine& line, DSLSymbol* originalHead)
{
	if (originalHead != nullptr && line.head() != originalHead)
	{
		auto it = std::find_if(line.symbols.begin(), line.symbols.end(),
			[&](const std::unique_ptr<DSLSymbol>& s) { return s.get() == originalHead; });
		if (it != line.symbols.end())
		{
			std::unique_ptr<DSLSymbol> moved = std::move(*it);
			line.symbols.erase(it);
			line.symbols.push_back(std::move(moved));
		}
	}

	std::vector<const DSLSymbol*> reachable;
	markReachable(line.head(), reachable);
	std::erase_if(line.symbols, [&](const std::unique_ptr<DSLSymbol>& s)
		{ return std::find(reachable.begin(), reachable.end(), s.get()) == reachable.end(); });
}

// Repoints whichever structural field currently holds this slot's occupant. LineHead never comes through here
// (whole-line replacement clears and rebuilds the line instead).
void ScriptEditor::writeSlot(const SlotRef& slot, DSLSymbol* newSymbol)
{
	switch (slot.kind)
	{
	case SlotRef::Kind::FlowControlCondition:
		std::get<DSLSymbol::FlowControl>(slot.parent->data).condition = newSymbol;
		break;
	case SlotRef::Kind::CallArgumentValue:
		std::get<DSLSymbol::FunctionCall>(slot.parent->data).arguments[slot.argIndex].value = newSymbol;
		break;
	case SlotRef::Kind::VariableDeclarationInitialValue:
		std::get<DSLSymbol::VariableDeclaration>(slot.parent->data).initialValue = newSymbol;
		break;
	case SlotRef::Kind::ExpressionOperand:
		std::get<DSLSymbol::Expression>(slot.parent->data).operands[slot.argIndex] = newSymbol;
		break;
	default:
		break;
	}
}

// Every value-position field in `line` pointing at oldSymbol -> newSymbol. Total by design (no "find the one
// parent" bookkeeping to go stale): values are only ever referenced from within their own line, so one flat
// walk covers every possible holder -- what unwrapping a one-operand-left chain runs.
void ScriptEditor::repointSymbol(DSLCodeLine& line, DSLSymbol* oldSymbol, DSLSymbol* newSymbol)
{
	auto fix = [&](DSLSymbol*& field) { if (field == oldSymbol) field = newSymbol; };
	for (const std::unique_ptr<DSLSymbol>& s : line.symbols)
	{
		switch (s->type)
		{
		case ST::VariableDeclaration:
			fix(std::get<DSLSymbol::VariableDeclaration>(s->data).initialValue);
			break;
		case ST::FunctionCall:
		{
			DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(s->data);
			fix(call.receiver);
			for (DSLSymbol::CallArgument& arg : call.arguments)
				fix(arg.value);
			break;
		}
		case ST::FlowControl:
		{
			DSLSymbol::FlowControl& fc = std::get<DSLSymbol::FlowControl>(s->data);
			fix(fc.condition);
			fix(fc.forCondition);
			fix(fc.forIncrement);
			break;
		}
		case ST::Expression:
			for (DSLSymbol*& operand : std::get<DSLSymbol::Expression>(s->data).operands)
				fix(operand);
			break;
		case ST::MemberAccess:
			fix(std::get<DSLSymbol::MemberAccess>(s->data).receiver);
			break;
		default:
			break;
		}
	}
}

// See the declaration in ScriptEditor.ixx: the selection lands where typing left off -- the END of the value.
void ScriptEditor::selectExpressionTail(DSLSymbol* value)
{
	DSLSymbol* target = value;
	bool groupClose = false;
	while (target != nullptr && target->type == ST::Expression)
	{
		const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(target->data);
		if (e.grouped)
		{
			groupClose = true; // the group's own ')' span IS its end
			break;
		}
		target = e.operands.back(); // an ungrouped chain ends at its last operand (itself possibly a group)
	}
	m_pendingSelectTarget = target;
	m_pendingSelectGroupClose = groupClose;
}

// Shared tail of both chain deletions: a chain left with ONE operand is indistinguishable from that operand
// alone, so it unwraps (every holder repointed at the survivor; the Expression node itself becomes unreachable
// and is swept). Redundant parens a surviving group may keep are harmless -- never wrong, only cosmetic.
void ScriptEditor::finishChainShrink(DSLSymbol* exprSymbol, DSLSymbol* originalHead, int selectOperand)
{
	DSLCodeLine& line = *exprSymbol->line;
	DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(exprSymbol->data);
	DSLSymbol* select = e.operands[std::clamp(selectOperand, 0, static_cast<int>(e.operands.size()) - 1)];
	if (e.operands.size() == 1)
		repointSymbol(line, exprSymbol, e.operands[0]);
	restoreHeadAndCollect(line, originalHead);
	selectExpressionTail(select); // a surviving group lands on its ')' -- the natural "continue from here" spot
}

// Backspace on a chain operand: removes it together with its adjacent operator (the one before it; for the
// first operand, the one after) -- arithmetic only, callers pre-check via the same class test: a comparison's
// or assignment's side isn't half-deletable.
void ScriptEditor::deleteChainOperand(DSLSymbol* exprSymbol, int operandIndex)
{
	DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(exprSymbol->data);
	const int opIndex = (operandIndex > 0) ? operandIndex - 1 : 0;
	if (opIndex >= static_cast<int>(e.operators.size()) || !dslIsChainOperator(e.operators[opIndex]))
		return;
	DSLSymbol* originalHead = exprSymbol->line->head();
	e.operands.erase(e.operands.begin() + operandIndex);
	e.operators.erase(e.operators.begin() + opIndex);
	finishChainShrink(exprSymbol, originalHead, operandIndex);
}

// Backspace on a chain operator's own span: removes it together with the operand AFTER it (the pair one
// keystroke added together, mirroring the compose flow's own Backspace undo).
void ScriptEditor::deleteChainOperator(DSLSymbol* exprSymbol, int operatorIndex)
{
	DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(exprSymbol->data);
	if (operatorIndex >= static_cast<int>(e.operators.size()) || !dslIsChainOperator(e.operators[operatorIndex]))
		return;
	DSLSymbol* originalHead = exprSymbol->line->head();
	e.operands.erase(e.operands.begin() + operatorIndex + 1);
	e.operators.erase(e.operators.begin() + operatorIndex);
	finishChainShrink(exprSymbol, originalHead, operatorIndex);
}

// See the declaration in ScriptEditor.ixx: typing over a selected value-slot occupant/chain operand.
void ScriptEditor::beginEditExprReplace(const SyntaxSpan& span)
{
	m_editSlot = span.slot;
	m_editChainExpr = (span.slot.kind == SlotRef::Kind::ExpressionOperand) ? span.slot.parent : nullptr;
	m_editOperandIndex = (span.slot.kind == SlotRef::Kind::ExpressionOperand) ? span.slot.argIndex : 0;
	m_editInsert = false;
	m_editAnchorSymbol = span.symbol;
	m_editAnchorText.clear();
	m_editValueType = AutoCompleteRules::expectedTypeForSlot(span.slot, m_document.file);
	enterChainStage(ComposeMode::EditExpr);
}

// See the declaration in ScriptEditor.ixx: an arithmetic operator typed over a selected value -- the composed
// segment chains in right AFTER that value. Splices into its parent chain when that parent is itself an
// arithmetic chain; otherwise (a standalone slot value, or one side of a comparison/assignment) the anchor
// wraps into a fresh nested chain with itself as the first operand, keeping structural Expressions binary.
void ScriptEditor::beginEditExprInsert(const SyntaxSpan& span, DSLOperator leadOp, const std::string& anchorText)
{
	m_editSlot = span.slot;
	m_editChainExpr = nullptr;
	m_editOperandIndex = 0;
	if (span.slot.kind == SlotRef::Kind::ExpressionOperand)
	{
		const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(span.slot.parent->data);
		// Chains are uniformly one operator class (see DSL.ixx); a one-operand group ("(a)") has no operators
		// at all and splices the same way -- inserting after its operand grows the chain INSIDE the parens.
		if (e.operators.empty() || dslIsArithmeticOperator(e.operators[0]))
		{
			m_editChainExpr = span.slot.parent;
			m_editOperandIndex = span.slot.argIndex;
		}
	}
	m_editInsert = true;
	m_editLeadOp = leadOp;
	m_editAnchorSymbol = span.symbol;
	m_editAnchorText = anchorText;
	m_editValueType = AutoCompleteRules::expectedTypeForSlot(span.slot, m_document.file);
	enterChainStage(ComposeMode::EditExpr);
}

// See the declaration in ScriptEditor.ixx: Backspace on a committed group's ')' reopens it mid-authoring.
bool ScriptEditor::beginReopenGroup(const SyntaxSpan& span)
{
	PendingExprTerm groupTerm;
	if (!termFromSymbol(span.symbol, groupTerm) || !groupTerm.isGroup || groupTerm.groupTerms.empty())
		return false;

	// Same replace-target bookkeeping as typing over the group wholesale (beginEditExprReplace).
	m_editSlot = span.slot;
	m_editChainExpr = (span.slot.kind == SlotRef::Kind::ExpressionOperand) ? span.slot.parent : nullptr;
	m_editOperandIndex = (span.slot.kind == SlotRef::Kind::ExpressionOperand) ? span.slot.argIndex : 0;
	m_editInsert = false;
	m_editAnchorSymbol = span.symbol;
	m_editAnchorText.clear();
	m_editValueType = AutoCompleteRules::expectedTypeForSlot(span.slot, m_document.file);

	// Stack = [outer empty frame, the group's own frame with its LAST term popped back into the box] -- the
	// exact state authoring was in right before its ')' was typed. The open paren means confirming is
	// impossible until it's re-closed (exprTryFinalize), which is what "re-confirmed as a whole" enforces.
	ExprFrame reopened{ std::move(groupTerm.groupTerms), std::move(groupTerm.groupOps) };
	PendingExprTerm lastTerm = std::move(reopened.terms.back());
	reopened.terms.pop_back();
	m_exprStack.assign(1, ExprFrame{});
	m_exprStack.push_back(std::move(reopened));
	m_exprHasPendingGroup = false;

	enterCompose(ComposeMode::EditExpr, "");
	if (lastTerm.isGroup)
	{
		m_exprPendingGroup = std::move(lastTerm);
		m_exprHasPendingGroup = true;
	}
	else
	{
		m_pendingWord = lastTerm.candidate.label;
	}
	m_composePrefix = exprBasePrefix() + exprComposePrefixFromStack();
	refreshCandidates();
	computeComposeCover(span);
	return true;
}

// The compose box must replace the group's WHOLE "(...)" render, not just its selected ')' span -- otherwise
// the group's own text would show doubled next to the box. Columns are stable while composing (the document
// doesn't change mid-compose, so the line renders identically each frame).
void ScriptEditor::computeComposeCover(const SyntaxSpan& groupCloseSpan)
{
	// The group's render starts N unspanned '(' characters before its leftmost inner span, where N is the
	// grouped-nesting depth down the first-operand chain ("((a + b) + c)" opens with two).
	int leadingParens = 0;
	const DSLSymbol* leftmost = groupCloseSpan.symbol;
	while (leftmost->type == ST::Expression)
	{
		const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(leftmost->data);
		if (e.grouped)
			++leadingParens;
		leftmost = e.operands.front();
	}

	m_composeCoverStart = std::max(0, groupCloseSpan.startCol - 1); // fallback: at least the ')' itself
	if (m_cursorLine >= 0 && m_cursorLine < static_cast<int>(m_formatted.size()))
		for (const SyntaxSpan& s : m_formatted[m_cursorLine].spans)
			if (s.symbol == leftmost)
			{
				m_composeCoverStart = std::max(0, s.startCol - leadingParens);
				break;
			}
	m_composeCoverEnd = groupCloseSpan.endCol;
}

// See the declaration in ScriptEditor.ixx: the flow-control headers' counterpart of the declare/reassign
// widen -- Backspace peels a header's values back into the STAGED flow that authored them.
bool ScriptEditor::tryWidenFlowHeaderEdit()
{
	DSLCodeLine* line = (m_editChainExpr != nullptr) ? m_editChainExpr->line
		: (m_editSlot.parent != nullptr) ? m_editSlot.parent->line : nullptr;
	if (line == nullptr || line->head() == nullptr || line->head()->type != ST::FlowControl)
		return false;
	DSLSymbol* head = line->head();
	const DSLSymbol::FlowControl& fc = std::get<DSLSymbol::FlowControl>(head->data);

	// return <value> -> the ReturnValue stage.
	if (fc.control == DSLFlowControl::Return)
	{
		if (m_editChainExpr != nullptr || m_editSlot.kind != SlotRef::Kind::FlowControlCondition || m_editSlot.parent != head)
			return false;
		m_flowEditLine = line;
		enterChainStage(ComposeMode::ReturnValue);
		return true;
	}

	if (fc.control == DSLFlowControl::If || fc.control == DSLFlowControl::ElseIf || fc.control == DSLFlowControl::While)
	{
		// A bare (non-comparison) condition value re-picks from the left; a comparison's operands reopen at
		// the matching stage -- the RIGHT operand keeps the resolved left+comparator in the prefix, the LEFT
		// re-authors the whole condition (each stage feeds the next, so nothing later can be kept).
		m_logicalTerms.clear(); // widening re-stages a SINGLE term; a logical chain peels term-by-term first
		m_logicalOps.clear();
		if (m_editChainExpr == nullptr)
		{
			if (m_editSlot.kind != SlotRef::Kind::FlowControlCondition || m_editSlot.parent != head)
				return false;
			m_conditionControl = fc.control;
			m_flowEditLine = line;
			enterChainStage(ComposeMode::ConditionLeft);
			return true;
		}
		if (fc.condition != m_editChainExpr)
			return false;
		const DSLSymbol::Expression& cmp = std::get<DSLSymbol::Expression>(m_editChainExpr->data);
		if (cmp.operands.size() != 2 || cmp.operators.size() != 1 || !dslIsComparisonOperator(cmp.operators[0]))
			return false;
		m_conditionControl = fc.control;
		if (m_editOperandIndex == 0)
		{
			m_flowEditLine = line;
			enterChainStage(ComposeMode::ConditionLeft);
			return true;
		}
		PendingExprChain left;
		if (!chainFromSymbol(cmp.operands[0], left))
			return false; // a non-round-trippable left side can't re-stage -- leave the header alone
		m_conditionLeftChain = left;
		m_conditionOp = cmp.operators[0];
		m_flowEditLine = line;
		enterChainStage(ComposeMode::ConditionRight);
		return true;
	}

	if (fc.control != DSLFlowControl::For)
		return false;

	// A for-loop restores the ENTIRE staged state from its committed clauses first (any part that can't
	// round-trip refuses the widen), then reopens at the stage matching the edited span; commitForStatement's
	// re-edit path preserves the loop variable's symbol identity (see m_flowEditLoopVar).
	if (fc.forLoopVar == nullptr || fc.forCondition == nullptr || fc.forIncrement == nullptr
		|| fc.forCondition->type != ST::Expression || fc.forIncrement->type != ST::Expression)
		return false;
	const DSLSymbol::VariableDeclaration& loopDecl = std::get<DSLSymbol::VariableDeclaration>(fc.forLoopVar->data);
	const DSLType varType = std::get<DSLSymbol::TypeDeclaration>(loopDecl.typeSymbol->data).type;
	const DSLSymbol::Expression& cond = std::get<DSLSymbol::Expression>(fc.forCondition->data);
	const DSLSymbol::Expression& incr = std::get<DSLSymbol::Expression>(fc.forIncrement->data);
	if (cond.operands.size() != 2 || cond.operators.size() != 1 || !dslIsComparisonOperator(cond.operators[0])
		|| incr.operands.size() != 2 || incr.operators.size() != 1 || !dslIsAssignOperator(incr.operators[0]))
		return false;

	PendingExprChain initChain;
	std::string initRawText;
	if (isVectorType(varType) ? !vectorRawTextFromValue(loopDecl.initialValue, initRawText)
	                          : !chainFromSymbol(loopDecl.initialValue, initChain))
		return false;
	PendingExprChain conditionLeft, conditionValue, incrementValue;
	if (!chainFromSymbol(cond.operands[0], conditionLeft)
		|| !chainFromSymbol(cond.operands[1], conditionValue)
		|| !chainFromSymbol(incr.operands[1], incrementValue))
		return false;

	ComposeMode target;
	if (m_editChainExpr == nullptr)
	{
		if (m_editSlot.kind != SlotRef::Kind::VariableDeclarationInitialValue || m_editSlot.parent != fc.forLoopVar)
			return false;
		target = ComposeMode::ForVarValue;
	}
	else if (m_editChainExpr == fc.forCondition)
		target = (m_editOperandIndex == 0) ? ComposeMode::ForConditionLeft : ComposeMode::ForConditionValue;
	else if (m_editChainExpr == fc.forIncrement)
		target = ComposeMode::ForIncrementValue;
	else
		return false;

	m_forVarType = varType;
	m_forVarName = loopDecl.name;
	m_forVarInitRawText = initRawText;
	m_forVarInitChain = initChain;
	m_forConditionLeftChain = conditionLeft;
	m_forConditionOpCandidate = Candidate{ dslOperatorText(cond.operators[0]), Candidate::Kind::Comparator, nullptr, DSLType::Void, cond.operators[0] };
	m_forConditionValueChain = conditionValue;
	m_forIncrementOpCandidate = Candidate{ dslOperatorText(incr.operators[0]), Candidate::Kind::AssignOperator, nullptr, DSLType::Void, incr.operators[0] };
	m_forIncrementValueChain = incrementValue;
	m_flowEditLine = line;
	m_flowEditLoopVar = fc.forLoopVar;

	// The widened-to stage always starts EMPTY (its value was just backspaced away); everything else shows
	// restored in the prefix. Vector inits keep their raw-component entry.
	if (target == ComposeMode::ForVarValue && isVectorType(varType))
		enterCompose(target, forVarPrefix());
	else
		enterChainStage(target);
	return true;
}

// See the declaration in ScriptEditor.ixx: the staged Declare-function flow re-opened over an existing header.
void ScriptEditor::beginWidenFunctionHeader(DSLSymbol* funcSymbol)
{
	const DSLSymbol::FunctionDeclaration& f = std::get<DSLSymbol::FunctionDeclaration>(funcSymbol->data);
	m_pendingFunctionName = f.name;
	m_pendingReturnType = f.returnType; // round-trips through a re-confirm; re-pickable via the ')' + '-' stages
	m_pendingParamTypes.clear();
	m_pendingParamNames.clear();
	m_pendingParamRefs.clear();
	for (DSLSymbol* param : f.parameterVarDeclarations)
	{
		const DSLSymbol::VariableDeclaration& d = std::get<DSLSymbol::VariableDeclaration>(param->data);
		m_pendingParamTypes.push_back(std::get<DSLSymbol::TypeDeclaration>(d.typeSymbol->data).type);
		m_pendingParamNames.push_back(d.name);
		m_pendingParamRefs.push_back(d.isRef);
	}
	m_flowEditLine = funcSymbol->line;

	if (m_pendingParamNames.empty())
	{
		enterCompose(ComposeMode::FunctionDeclareName, "function ", m_pendingFunctionName);
		return;
	}
	// Pop the LAST parameter back into the box, mid-authoring style -- the exact state the flow was in right
	// before that parameter was finalized.
	m_pendingParamType = m_pendingParamTypes.back();
	m_pendingParamRef = m_pendingParamRefs.back();
	const std::string lastName = m_pendingParamNames.back();
	m_pendingParamTypes.pop_back();
	m_pendingParamNames.pop_back();
	m_pendingParamRefs.pop_back();
	enterCompose(ComposeMode::FunctionParamName, currentParamPrefix(), lastName);
}

// See the declaration in ScriptEditor.ixx: a call statement's counterpart of tryWidenFlowHeaderEdit.
bool ScriptEditor::tryWidenCallStatementEdit()
{
	if (m_editChainExpr != nullptr || m_editSlot.kind != SlotRef::Kind::CallArgumentValue || m_editSlot.parent == nullptr)
		return false;
	DSLSymbol* callSymbol = m_editSlot.parent;
	if (callSymbol->line == nullptr || callSymbol->line->head() != callSymbol)
		return false; // only call STATEMENTS re-stage; a call nested in some larger value has no staged flow
	const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(callSymbol->data);
	if (call.functionSymbol == nullptr)
		return false;
	DSLSymbol* receiverDecl = nullptr;
	std::string receiverPath;
	if (call.receiver != nullptr && !receiverChainToRoot(call.receiver, receiverDecl, receiverPath))
		return false;
	const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(call.functionSymbol->data);
	const int argIndex = m_editSlot.argIndex;
	if (argIndex < 0 || argIndex >= static_cast<int>(call.arguments.size())
		|| callee.parameterVarDeclarations.size() != call.arguments.size())
		return false; // vararg-style builtins (print) have no declared parameters to stage against

	// Arguments BEFORE the edited one restore as already-resolved (possibly compound/nested-call) chains; the
	// edited one (and everything after) re-authors forward, exactly like authoring the call fresh from there.
	std::vector<PendingExprChain> argChains;
	for (int i = 0; i < argIndex; ++i)
	{
		PendingExprChain chain;
		if (!chainFromSymbol(call.arguments[i].value, chain))
			return false;
		argChains.push_back(std::move(chain));
	}

	m_callStack.clear(); // safe: EditExpr (the only caller) never has a call already staging
	CallStage& stage = m_callStack.emplace_back();
	stage.func = call.functionSymbol;
	stage.receiver = receiverDecl;
	stage.receiverPath = std::move(receiverPath);
	stage.argChains = std::move(argChains);
	stage.returnMode = ComposeMode::None; // a STATEMENT call -- completion re-commits the line
	stage.savedExprStack.assign(1, ExprFrame{});

	m_flowEditLine = callSymbol->line;
	m_exprStack.assign(1, ExprFrame{}); // fresh compose for the argument being widened
	m_exprHasPendingGroup = false;
	enterCompose(ComposeMode::CallArgValue, callStagePrefix());
	return true;
}

bool ScriptEditor::tryWidenValueCallEdit()
{
	if (m_editChainExpr != nullptr || m_editSlot.kind != SlotRef::Kind::CallArgumentValue || m_editSlot.parent == nullptr)
		return false;
	DSLSymbol* callSymbol = m_editSlot.parent;
	if (callSymbol->line == nullptr || callSymbol->line->head() == callSymbol)
		return false; // a call STATEMENT's arguments take tryWidenCallStatementEdit instead
	const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(callSymbol->data);
	if (call.functionSymbol == nullptr)
		return false;
	const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(call.functionSymbol->data);
	const int argIndex = m_editSlot.argIndex;
	if (argIndex < 0 || argIndex >= static_cast<int>(call.arguments.size())
		|| callee.parameterVarDeclarations.size() != call.arguments.size())
		return false;

	DSLCodeLine& line = *callSymbol->line;
	DSLSymbol* head = line.head();

	// The call is itself an ARGUMENT of the line's call STATEMENT ("foo(test = vec3(1,|"): backspacing out of
	// its argument re-opens the OUTER statement's own staging at that argument -- delegate to
	// tryWidenCallStatementEdit's identical logic (restore earlier args, re-author forward) by pointing
	// m_editSlot at the outer call's matching argument. (Nesting deeper than this one level isn't supported yet.)
	if (head != nullptr && head->type == ST::FunctionCall)
	{
		const DSLSymbol::FunctionCall& outer = std::get<DSLSymbol::FunctionCall>(head->data);
		int outerArgIndex = -1;
		for (size_t i = 0; i < outer.arguments.size(); ++i)
			if (outer.arguments[i].value == callSymbol)
				outerArgIndex = static_cast<int>(i);
		if (outerArgIndex < 0)
			return false;
		m_editSlot = SlotRef{ SlotRef::Kind::CallArgumentValue, head, callSymbol->line, outerArgIndex };
		return tryWidenCallStatementEdit();
	}

	// Everything restorable resolves BEFORE any state mutates -- a refusal must leave no trace.
	DSLSymbol* receiverDecl = nullptr;
	std::string receiverPath;
	if (call.receiver != nullptr && !receiverChainToRoot(call.receiver, receiverDecl, receiverPath))
		return false;
	std::vector<PendingExprChain> argChains;
	for (int i = 0; i < argIndex; ++i)
	{
		PendingExprChain chain;
		if (!chainFromSymbol(call.arguments[i].value, chain))
			return false;
		argChains.push_back(std::move(chain));
	}

	// Which VALUE slot the call occupies decides the suspended context its staging returns into: a
	// declaration's whole initializer re-opens the redeclare flow, an assignment's right-hand side the
	// reassign-edit flow -- the exact contexts the plain whole-value Backspace widens into.
	ComposeMode returnMode = ComposeMode::None;
	if (head != nullptr && head->type == ST::VariableDeclaration
		&& std::get<DSLSymbol::VariableDeclaration>(head->data).initialValue == callSymbol)
	{
		m_redeclareTarget = head;
		m_pendingDeclareType = declaredTypeOf(head);
		m_pendingDeclareName = std::get<DSLSymbol::VariableDeclaration>(head->data).name;
		returnMode = ComposeMode::DeclareValue;
	}
	else if (head != nullptr && head->type == ST::Expression)
	{
		const DSLSymbol::Expression& assign = std::get<DSLSymbol::Expression>(head->data);
		DSLSymbol* targetRoot = nullptr;
		std::string targetPath;
		if (assign.operators.size() == 1 && dslIsAssignOperator(assign.operators[0])
			&& assign.operands.size() == 2 && assign.operands[1] == callSymbol
			&& receiverChainToRoot(assign.operands[0], targetRoot, targetPath))
		{
			m_reassignEditExpr = head;
			m_reassignTarget = targetRoot;
			m_reassignMemberPath = splitMemberPath(targetPath);
			returnMode = ComposeMode::ReassignValue;
		}
	}
	if (returnMode == ComposeMode::None)
		return false;

	m_callStack.clear(); // safe: EditExpr (the only caller) never has a call already staging
	CallStage& stage = m_callStack.emplace_back();
	stage.func = call.functionSymbol;
	stage.receiver = receiverDecl;
	stage.receiverPath = std::move(receiverPath);
	stage.argChains = std::move(argChains);
	stage.returnMode = returnMode;
	stage.outerLeadText = exprBasePrefixFor(returnMode); // "type name = " / "target op = " -- m_pendingDeclare*/m_reassign* are already set above
	stage.savedExprStack.assign(1, ExprFrame{}); // the resumed mode's own chain: empty -- the call is its sole term, being re-staged

	m_exprStack.assign(1, ExprFrame{}); // fresh compose for the argument being widened
	m_exprHasPendingGroup = false;
	enterCompose(ComposeMode::CallArgValue, callStagePrefix());
	return true;
}

// See the declaration in ScriptEditor.ixx: Backspace peels a comparison-typed VALUE back into its staged flow.
bool ScriptEditor::tryWidenComparisonValueEdit()
{
	if (m_editChainExpr == nullptr || m_editChainExpr->line == nullptr)
		return false;
	DSLSymbol* cmp = m_editChainExpr;
	const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(cmp->data);
	if (e.operands.size() != 2 || e.operators.size() != 1 || !dslIsComparisonOperator(e.operators[0]))
		return false;
	DSLCodeLine* line = cmp->line;
	DSLSymbol* head = line->head();
	if (head == nullptr)
		return false;

	// Everything restorable resolves BEFORE any state mutates -- a refusal must leave no trace.
	const bool reopenRight = (m_editOperandIndex == 1);
	PendingExprChain left;
	if (reopenRight && !chainFromSymbol(e.operands[0], left))
		return false; // a non-round-trippable left side can't re-stage

	if (head->type == ST::VariableDeclaration
		&& std::get<DSLSymbol::VariableDeclaration>(head->data).initialValue == cmp)
	{
		// A plain local declaration's initializer (a for-loop counter's line is headed by its FlowControl, so
		// it can never match here) -- restore the whole-line re-declare context, same as the declare widen.
		const DSLSymbol::VariableDeclaration& decl = std::get<DSLSymbol::VariableDeclaration>(head->data);
		m_redeclareTarget = head;
		m_pendingDeclareType = std::get<DSLSymbol::TypeDeclaration>(decl.typeSymbol->data).type;
		m_pendingDeclareName = decl.name;
		m_conditionValueReturnMode = ComposeMode::DeclareValue;
	}
	else if (head->type == ST::Expression)
	{
		// An assignment statement's right-hand side -- restore the widened-reassign context.
		const DSLSymbol::Expression& assign = std::get<DSLSymbol::Expression>(head->data);
		if (assign.operators.empty() || !dslIsAssignOperator(assign.operators[0])
			|| assign.operands.size() != 2 || assign.operands[1] != cmp
			|| assign.operands[0]->type != ST::VariableReference)
			return false;
		m_reassignEditExpr = head;
		m_reassignTarget = std::get<DSLSymbol::VariableReference>(assign.operands[0]->data).declaration;
		m_conditionValueReturnMode = ComposeMode::ReassignValue;
	}
	else
	{
		return false; // return values widen via tryWidenFlowHeaderEdit; other holders just cancel
	}

	m_logicalTerms.clear();
	m_logicalOps.clear();
	if (reopenRight)
	{
		m_conditionLeftChain = left;
		m_conditionOp = e.operators[0];
		enterChainStage(ComposeMode::ConditionRight);
	}
	else
	{
		enterChainStage(ComposeMode::ConditionLeft);
	}
	return true;
}

// EditExpr's confirm: builds the composed segment's symbols and splices them into the document per the state
// beginEditExprReplace/Insert captured. All shapes end the same way: head restored, orphans swept, cursor on
// the last term just composed.
void ScriptEditor::applyEditExpr(const std::vector<PendingExprTerm>& terms, const std::vector<DSLOperator>& ops, const std::string& rawVectorText)
{
	const SlotRef slot = m_editSlot;
	DSLSymbol* chainExpr = m_editChainExpr;
	const int operandIndex = m_editOperandIndex;
	const bool insert = m_editInsert;
	const DSLOperator leadOp = m_editLeadOp;
	DSLSymbol* anchor = m_editAnchorSymbol;
	const DSLType valueType = m_editValueType;
	cancelCompose();

	if (slot.line == nullptr)
		return;
	DSLCodeLine& line = *slot.line;
	DSLSymbol* originalHead = line.head();

	std::vector<DSLSymbol*> built;
	if (!rawVectorText.empty())
	{
		// A vector slot's comma components become one vecN literal (pre-validated by the confirm).
		DSLSymbol* vec = buildVectorLiteral(valueType, splitOnCommas(rawVectorText), line);
		if (vec == nullptr)
			return; // defensive -- buildVectorLiteral mutates nothing on failure
		built.push_back(vec);
	}
	else
	{
		built.reserve(terms.size());
		for (const PendingExprTerm& term : terms)
			built.push_back(buildExpressionTerm(term, line));
	}

	if (chainExpr != nullptr)
	{
		DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(chainExpr->data);
		if (insert)
		{
			// [.. anchor leadOp s0 op0 s1 ..] -- the segment slides in right after the anchor operand.
			e.operands.insert(e.operands.begin() + operandIndex + 1, built.begin(), built.end());
			std::vector<DSLOperator> newOps{ leadOp };
			newOps.insert(newOps.end(), ops.begin(), ops.end());
			e.operators.insert(e.operators.begin() + operandIndex, newOps.begin(), newOps.end());
		}
		else if (built.size() == 1)
		{
			e.operands[operandIndex] = built[0];
		}
		else if (e.operators.empty() || dslIsArithmeticOperator(e.operators[0]))
		{
			// Replacing one term of an arithmetic chain (or a one-operand group's lone value) with a
			// multi-term segment splices it in flat.
			e.operands[operandIndex] = built[0];
			e.operands.insert(e.operands.begin() + operandIndex + 1, built.begin() + 1, built.end());
			e.operators.insert(e.operators.begin() + operandIndex, ops.begin(), ops.end());
		}
		else
		{
			// A comparison/assignment side replaced by a compound value becomes ONE nested sub-chain operand --
			// structural Expressions stay exactly binary (see DSL.ixx).
			e.operands[operandIndex] = pushSymbol(line, ST::Expression, DSLSymbol::Expression{ built, ops });
		}
	}
	else if (insert)
	{
		// Wrap: the slot's occupant becomes the first operand of a fresh (ungrouped) chain in its place.
		std::vector<DSLSymbol*> operands{ anchor };
		operands.insert(operands.end(), built.begin(), built.end());
		std::vector<DSLOperator> operators{ leadOp };
		operators.insert(operators.end(), ops.begin(), ops.end());
		writeSlot(slot, pushSymbol(line, ST::Expression, DSLSymbol::Expression{ operands, operators }));
	}
	else
	{
		writeSlot(slot, built.size() == 1 ? built[0]
			: pushSymbol(line, ST::Expression, DSLSymbol::Expression{ built, ops }));
	}

	restoreHeadAndCollect(line, originalHead);
	selectExpressionTail(built.back());
}

// Enter (not composing): the ONLY way a new line appears. Inserts a blank statement placeholder right after
// the CURRENT line (whichever span within it is selected), one level deeper if that line opens a block
// (if/while/function -- so the natural next keystroke is the first line of a fresh body), else as a sibling.
// Sitting on a synthetic `end` marker instead inserts a new SIBLING right after the whole block that `end`
// closes -- i.e. after the end, continuing in the enclosing scope -- not one more statement inside it.
void ScriptEditor::insertLineAfterCursor()
{
	if (m_formatted.empty() || m_cursorLine < 0 || m_cursorLine >= static_cast<int>(m_formatted.size()))
		return;
	const SyntaxLine& curLine = m_formatted[m_cursorLine];

	if (curLine.sourceLine != nullptr)
	{
		DSLCodeLine& line = *curLine.sourceLine;
		const int newScopeLevel = Syntax::isBlockOpener(line.head()) ? line.scopeLevel + 1 : line.scopeLevel;
		m_pendingSelectTarget = seedStatementPlaceholder(insertLineAfter(line, newScopeLevel));
		return;
	}

	if (curLine.endOfSymbol == nullptr || curLine.endOfSymbol->line == nullptr)
		return; // shouldn't happen (every synthetic end line closes a real header), but be defensive

	DSLCodeLine& header = *curLine.endOfSymbol->line;
	const int headerIndex = dslLineIndex(m_document.file, &header);
	if (headerIndex < 0)
		return;

	// The block's LAST existing line (or the header itself, if its body is still empty) -- the new placeholder
	// goes right after that, at the HEADER's OWN level (a sibling following the closed block, i.e. right where
	// the synthetic `end` currently renders), not one level deeper.
	DSLCodeLine& lastBodyLine = *m_document.file.lines[dslBlockEnd(m_document.file, headerIndex) - 1];
	m_pendingSelectTarget = seedStatementPlaceholder(insertLineAfter(lastBodyLine, header.scopeLevel));
}

// Backspace on a blank statement placeholder (or any single-line statement's own deletion spot): removes
// `line` outright. The cursor lands at the END of the visible line above -- the spot typing would continue
// from -- via the formatted-line-index select (every caller deletes the line the cursor is sitting on, so the
// line above keeps its index across the re-format); deleting the topmost line lands on the new first line.
void ScriptEditor::deleteLine(DSLCodeLine& line)
{
	auto& lines = m_document.file.lines;
	const int index = dslLineIndex(m_document.file, &line);
	if (index < 0)
		return;

	lines.erase(lines.begin() + index);

	m_pendingSelectLineEnd = std::max(0, m_cursorLine - 1);
}

// Whether `headSymbol` (an If -- While never has one) has no attached elseif/else chain, or every branch in
// that chain is empty (isBlockBodyEmpty) -- gates deleteBlockKeepBody: a non-empty chained branch has nowhere
// safe to go (same reasoning as an else/elseif's own deletion needing to be empty), so the whole if is left
// alone rather than losing that content or leaving it dangling.
bool ScriptEditor::attachedElseChainEmpty(const DSLSymbol* headSymbol) const
{
	if (headSymbol == nullptr || headSymbol->line == nullptr)
		return false;
	const auto& lines = m_document.file.lines;

	const int headerIndex = dslLineIndex(m_document.file, headSymbol->line);
	if (headerIndex < 0)
		return false;

	const int headerScopeLevel = lines[headerIndex]->scopeLevel;
	// Skip the if's own body -- irrelevant here, deleteBlockKeepBody already keeps it.
	int i = dslBlockEnd(m_document.file, headerIndex);

	while (i < static_cast<int>(lines.size()) && lines[i]->scopeLevel == headerScopeLevel)
	{
		const DSLSymbol* branchHead = lines[i]->head();
		if (branchHead == nullptr || branchHead->type != ST::FlowControl)
			break;
		const DSLFlowControl control = std::get<DSLSymbol::FlowControl>(branchHead->data).control;
		if (control != DSLFlowControl::ElseIf && control != DSLFlowControl::Else)
			break; // not a continuation -- some unrelated sibling statement follows instead

		if (!isBlockBodyEmpty(branchHead))
			return false;

		i = dslBlockEnd(m_document.file, i); // past this branch's own body, onto the next chained header (if any)
	}
	return true;
}

// Backspace on an if/while's own keyword (caller already verified attachedElseChainEmpty): removes the header
// line (and, since `end` is synthetic, its closing marker disappears with it -- see DSL.ixx) while UN-NESTING
// the block's OWN body by exactly one scopeLevel, so its contents survive as plain siblings in the enclosing
// scope instead of being deleted too -- "remove this wrapper", not "delete everything inside it". Any attached
// elseif/else chain (already verified entirely empty) is consumed too -- there's nothing of theirs worth
// keeping, and leaving them behind would dangle a continuation with no "if" left to continue from. `headSymbol`
// must be the FlowControl symbol itself.
void ScriptEditor::deleteBlockKeepBody(DSLSymbol* headSymbol)
{
	if (headSymbol == nullptr || headSymbol->line == nullptr)
		return;
	DSLCodeLine& header = *headSymbol->line;
	auto& lines = m_document.file.lines;

	const int headerIndex = dslLineIndex(m_document.file, &header);
	if (headerIndex < 0)
		return;

	int bodyEnd = headerIndex + 1;
	for (; bodyEnd < static_cast<int>(lines.size()) && lines[bodyEnd]->scopeLevel > header.scopeLevel; ++bodyEnd)
		lines[bodyEnd]->scopeLevel -= 1;

	// Consume any attached elseif/else chain (each already verified empty) -- always erasing right at bodyEnd,
	// since removing one chained segment slides the next one (if any, e.g. a further elseif/else) into that
	// same position for the next iteration.
	while (bodyEnd < static_cast<int>(lines.size()) && lines[bodyEnd]->scopeLevel == header.scopeLevel)
	{
		const DSLSymbol* branchHead = lines[bodyEnd]->head();
		if (branchHead == nullptr || branchHead->type != ST::FlowControl)
			break;
		const DSLFlowControl control = std::get<DSLSymbol::FlowControl>(branchHead->data).control;
		if (control != DSLFlowControl::ElseIf && control != DSLFlowControl::Else)
			break;

		lines.erase(lines.begin() + bodyEnd, lines.begin() + dslBlockEnd(m_document.file, bodyEnd));
	}

	lines.erase(lines.begin() + headerIndex);

	// End of the visible line above the deleted header (the cursor sat on the header -- every caller's
	// contract), same landing rule as deleteLine: where typing/backspacing continues.
	m_pendingSelectLineEnd = std::max(0, m_cursorLine - 1);
}

// Whether the block `headSymbol` opens (a FunctionDeclaration, or an If/ElseIf/Else/While) has nothing worth
// keeping in its body: zero lines, or exactly one that's itself a blank statement placeholder (the shape
// commitFunctionDeclaration/insertLineAfterCursor always seed for a brand-new function/block). Anything more --
// real content, or even just two blank lines -- is NOT considered empty. Purely a scopeLevel scan, so it works
// the same regardless of what kind of header this is.
bool ScriptEditor::isBlockBodyEmpty(const DSLSymbol* headSymbol) const
{
	if (headSymbol == nullptr || headSymbol->line == nullptr)
		return false;

	const int headerIndex = dslLineIndex(m_document.file, headSymbol->line);
	if (headerIndex < 0)
		return false;

	const int bodyEnd = dslBlockEnd(m_document.file, headerIndex);
	if (bodyEnd == headerIndex + 1)
		return true; // no body lines at all
	return bodyEnd == headerIndex + 2 && isBlankStatementDSLLine(*m_document.file.lines[headerIndex + 1]);
}

// See the declaration in ScriptEditor.ixx: empty, or nothing but one `return` line.
bool ScriptEditor::isFunctionBodyDeletable(const DSLSymbol* headSymbol) const
{
	if (isBlockBodyEmpty(headSymbol))
		return true;
	if (headSymbol == nullptr || headSymbol->line == nullptr)
		return false;
	const int headerIndex = dslLineIndex(m_document.file, headSymbol->line);
	if (headerIndex < 0 || dslBlockEnd(m_document.file, headerIndex) != headerIndex + 2)
		return false;
	const DSLSymbol* bodyHead = m_document.file.lines[headerIndex + 1]->head();
	return bodyHead != nullptr && bodyHead->type == ST::FlowControl
		&& std::get<DSLSymbol::FlowControl>(bodyHead->data).control == DSLFlowControl::Return;
}

// See the declaration in ScriptEditor.ixx: is `line` the untouchable bottom return of a non-Void function.
bool ScriptEditor::isProtectedBottomReturn(const DSLCodeLine* line) const
{
	if (line == nullptr)
		return false;
	const DSLSymbol* head = line->head();
	if (head == nullptr || head->type != ST::FlowControl
		|| std::get<DSLSymbol::FlowControl>(head->data).control != DSLFlowControl::Return)
		return false;
	const int lineIndex = dslLineIndex(m_document.file, line);
	if (lineIndex < 0)
		return false;
	const int headerIndex = dslEnclosingFunctionHeader(m_document.file, lineIndex);
	if (headerIndex < 0)
		return false;
	const DSLCodeLine& headerLine = *m_document.file.lines[headerIndex];
	if (std::get<DSLSymbol::FunctionDeclaration>(headerLine.head()->data).returnType == DSLType::Void)
		return false;
	// The bottom return: physically last in the function AND at body level (a deeper return tucked inside a
	// trailing block sits before its own inner `end`s, not the function's).
	return lineIndex == dslBlockEnd(m_document.file, headerIndex) - 1
		&& line->scopeLevel == headerLine.scopeLevel + 1;
}

// See the declaration in ScriptEditor.ixx: the one place return-type changes reconcile the function's returns.
void ScriptEditor::applyFunctionReturnChange(DSLSymbol* funcSymbol, DSLType newReturnType)
{
	if (funcSymbol == nullptr || funcSymbol->line == nullptr)
		return;
	auto& lines = m_document.file.lines;
	const int headerIndex = dslLineIndex(m_document.file, funcSymbol->line);
	if (headerIndex < 0)
		return;

	// Every `return` line goes, at any nesting depth -- their values were typed against the OLD return type.
	// Backward walk: erasing never disturbs the indices still to visit.
	for (int i = dslBlockEnd(m_document.file, headerIndex) - 1; i > headerIndex; --i)
	{
		const DSLSymbol* head = lines[i]->head();
		if (head != nullptr && head->type == ST::FlowControl
			&& std::get<DSLSymbol::FlowControl>(head->data).control == DSLFlowControl::Return)
			lines.erase(lines.begin() + i);
	}

	if (newReturnType == DSLType::Void)
		return; // nothing to seed; the caller keeps its own cursor landing

	// Seed the new `return` slot at the very end of the body and open its compose there (next render).
	DSLCodeLine& returnLine = insertLineAfter(*lines[dslBlockEnd(m_document.file, headerIndex) - 1],
		funcSymbol->line->scopeLevel + 1);
	m_pendingSelectTarget = seedStatementPlaceholder(returnLine);
	m_pendingComposeReturnValue = true;
}

// Backspace on an EMPTY block header (a function, already checked uncalled too, or a for-loop -- caller has
// already checked isBlockBodyEmpty): removes the header AND its whole (blank) body AND its synthetic `end` in
// one range-erase -- unlike deleteBlockKeepBody, there's no un-nesting step since there's nothing worth keeping
// (a for-loop's body couldn't be safely kept anyway -- see the class comment).
void ScriptEditor::deleteEmptyBlock(DSLSymbol* headSymbol)
{
	if (headSymbol == nullptr || headSymbol->line == nullptr)
		return;
	auto& lines = m_document.file.lines;

	const int headerIndex = dslLineIndex(m_document.file, headSymbol->line);
	if (headerIndex < 0)
		return;

	lines.erase(lines.begin() + headerIndex, lines.begin() + dslBlockEnd(m_document.file, headerIndex));

	// Same end-of-previous-line landing as deleteLine/deleteBlockKeepBody (the cursor sat on the header).
	m_pendingSelectLineEnd = std::max(0, m_cursorLine - 1);
}

void ScriptEditor::handleKeyEvent(const SDL_Event& evt)
{
	if (evt.type != SDL_EVENT_KEY_DOWN)
		return;
	if (ImGui::GetIO().WantTextInput)
		return;
	if (!m_hasFocus || m_formatted.empty())
		return;

	const bool shift = (evt.key.mod & SDL_KMOD_SHIFT) != 0;
	const bool ctrl = (evt.key.mod & SDL_KMOD_CTRL) != 0;
	const bool composing = m_composeMode != ComposeMode::None;

	// Ctrl+S saves from anywhere, composing included -- a compose never touches the document, so what lands on
	// disk is exactly the committed state.
	if (ctrl && evt.key.scancode == SDL_SCANCODE_S)
	{
		saveDocument();
		return;
	}

	switch (static_cast<int>(evt.key.scancode))
	{
	case SDL_SCANCODE_LEFT:
		if (composing) cancelCompose(); else moveHorizontal(-1);
		return;
	case SDL_SCANCODE_RIGHT:
		if (composing) cancelCompose(); else moveHorizontal(+1);
		return;
	case SDL_SCANCODE_TAB:
		// M3: plain navigation while not composing; M7 may add "cycle argument placeholders" as a distinct mode.
		if (composing) cancelCompose(); else moveHorizontal(shift ? -1 : +1);
		return;
	case SDL_SCANCODE_HOME:
		if (composing) cancelCompose(); else moveHome();
		return;
	case SDL_SCANCODE_END:
		if (composing) cancelCompose(); else moveEnd();
		return;
	case SDL_SCANCODE_UP:
		if (hasCandidateList() && !m_candidates.empty())
			m_candidateSelected = (m_candidateSelected - 1 + static_cast<int>(m_candidates.size())) % static_cast<int>(m_candidates.size());
		else if (!composing)
			moveVertical(-1);
		return;
	case SDL_SCANCODE_DOWN:
		if (hasCandidateList() && !m_candidates.empty())
			m_candidateSelected = (m_candidateSelected + 1) % static_cast<int>(m_candidates.size());
		else if (!composing)
			moveVertical(+1);
		return;
	case SDL_SCANCODE_BACKSPACE:
		if (!composing)
		{
			const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
			const DSLFlowControl* selectedControl = (span != nullptr && span->symbol != nullptr && span->symbol->type == ST::FlowControl)
				? &std::get<DSLSymbol::FlowControl>(span->symbol->data).control : nullptr;
			// The function's own NAME span specifically -- its slot is LineHead, distinguishing it from the
			// SAME symbol's separate return-type span (Kind::FunctionReturnType), which must Backspace into
			// FunctionReturnType editing instead (handled by beginCompose, below), not this deletion path.
			const bool selectedFunctionHeader = span != nullptr && span->symbol != nullptr
				&& span->symbol->type == ST::FunctionDeclaration && span->slot.kind == SlotRef::Kind::LineHead;
			// The TYPE portion of a LOCAL variable declaration's own line (e.g. the "float" in `float test =
			// 1.0`) -- NOT a function parameter's type (embedded in its FunctionDeclaration header line, whose
			// own head symbol is the FunctionDeclaration, not this VariableDeclaration) or a for-loop's own
			// counter declaration (embedded in the `for` line, headed by its FlowControl). Selecting it and
			// backspacing deletes the whole declaration line.
			DSLSymbol* selectedDeclarationType = nullptr;
			if (span != nullptr && span->symbol != nullptr && span->symbol->type == ST::TypeDeclaration && span->symbol->line != nullptr)
			{
				DSLSymbol* ownerHead = span->symbol->line->head();
				if (ownerHead != nullptr && ownerHead->type == ST::VariableDeclaration
					&& std::get<DSLSymbol::VariableDeclaration>(ownerHead->data).typeSymbol == span->symbol)
					selectedDeclarationType = ownerHead;
			}
			// A bare assignment/compound-assign statement's own operator span, or a call statement's own name
			// span -- both LineHead (an Expression/FunctionCall used as someone else's operand/condition/argument
			// carries a DIFFERENT slot kind instead, see renderSymbol, so this never matches a nested usage).
			const bool selectedAssignmentStatement = span != nullptr && span->symbol != nullptr
				&& span->symbol->type == ST::Expression && span->slot.kind == SlotRef::Kind::LineHead;
			const bool selectedCallStatement = span != nullptr && span->symbol != nullptr
				&& span->symbol->type == ST::FunctionCall && span->slot.kind == SlotRef::Kind::LineHead;
			// In-place chain deletion targets: a CHAIN operator span (arithmetic or logical) deletes itself +
			// the operand after it; a chain operand whose adjacent operator is chain-class deletes as a pair
			// with it. Comparison/assign operators and their sides never qualify (a condition or assignment
			// can't be half-deleted), so those spans fall through to the statement-level rules / plain
			// re-editing below. A LineHead assignment's own '=' is operatorIndex 0 too but assign-class -- it
			// keeps hitting selectedAssignmentStatement, never these.
			const bool selectedChainOperator = span != nullptr && span->symbol != nullptr
				&& span->symbol->type == ST::Expression && span->operatorIndex >= 0
				&& dslIsChainOperator(std::get<DSLSymbol::Expression>(span->symbol->data).operators[span->operatorIndex]);
			bool selectedChainOperand = false;
			if (span != nullptr && span->slot.kind == SlotRef::Kind::ExpressionOperand)
			{
				const DSLSymbol::Expression& chain = std::get<DSLSymbol::Expression>(span->slot.parent->data);
				const int adjacentOp = (span->slot.argIndex > 0) ? span->slot.argIndex - 1 : 0;
				selectedChainOperand = adjacentOp < static_cast<int>(chain.operators.size())
					&& dslIsChainOperator(chain.operators[adjacentOp]);
			}

			if (selectedControl != nullptr && (*selectedControl == DSLFlowControl::If || *selectedControl == DSLFlowControl::While))
			{
				// Selected the if/while's own keyword -- remove the whole header (and its synthetic `end`),
				// keeping the body as plain siblings one scopeLevel shallower, instead of opening it to edit.
				// Only when any attached elseif/else chain is entirely empty too (While never has one, so this
				// is always true there) -- otherwise leave the whole if alone rather than lose that content.
				if (attachedElseChainEmpty(span->symbol))
					deleteBlockKeepBody(span->symbol);
			}
			else if (selectedControl != nullptr && (*selectedControl == DSLFlowControl::Else || *selectedControl == DSLFlowControl::ElseIf))
			{
				// Selected an else/elseif's own keyword -- only remove it when its OWN branch is empty (no
				// content to lose): a non-empty branch has no safe place for its statements to go (silently
				// merging them into the PRECEDING branch would reorder which condition guards which statements),
				// so it's left alone entirely, same as a non-empty function. When it IS empty, erasing just this
				// ONE header line is enough -- its (empty) body is already at the same scopeLevel as the branch
				// before it (an elseif/else continues the same open "if" on Syntax::format's stack rather than
				// opening a new one -- the synthetic `end` stays tied to the ORIGINAL if's head throughout the
				// whole chain, see DSL.ixx/ScriptLang.cpp), so there's no un-nesting/renumbering to do either way.
				if (isBlockBodyEmpty(span->symbol))
					deleteLine(*span->symbol->line);
			}
			else if (selectedFunctionHeader)
			{
				// A LOCKED entry-point function can only go away via its EXPORTS checkbox (toggleEntryFunction) --
				// never this way, regardless of body/reference state.
				if (isLockedEntryFunction(span->symbol))
					return;
				// Only remove an uncalled function whose body is empty (or nothing but its auto-seeded return)
				// this way -- otherwise leave it alone entirely (no fallback to beginCompose(), which would
				// otherwise let a stray keystroke replace the whole header via the statement candidate list).
				if (isFunctionBodyDeletable(span->symbol) && !AutoCompleteRules::isFunctionReferenced(span->symbol, m_document.file))
					deleteEmptyBlock(span->symbol);
			}
			else if (span != nullptr && span->slot.kind == SlotRef::Kind::FunctionReturnType)
			{
				// A LOCKED entry-point function's header (name/params/return type, all fixed by the real ABI --
				// see EntryPointDef) never re-opens for editing this way.
				if (isLockedEntryFunction(span->symbol))
					return;
				// Backspace at the header's END: a set return type clears first (one keystroke, like deleting
				// a chain term) -- but only while nothing calls the function (a caller's typing depends on
				// it); otherwise, and once blank, the staged Declare-function flow re-opens over the whole
				// header (beginWidenFunctionHeader) so further Backspace peels parameters, then the name,
				// then -- guarded -- the function itself. Typing on this span still composes a return type.
				DSLSymbol::FunctionDeclaration& f = std::get<DSLSymbol::FunctionDeclaration>(span->symbol->data);
				if (f.returnType != DSLType::Void && !AutoCompleteRules::isFunctionReferenced(span->symbol, m_document.file))
				{
					f.returnType = DSLType::Void;
					applyFunctionReturnChange(span->symbol, DSLType::Void); // a void function returns nothing -- its return lines go too
					m_pendingSelectLineEnd = m_cursorLine; // stay at the header's end (the now-blank span)
				}
				else
				{
					beginWidenFunctionHeader(span->symbol);
				}
			}
			else if (selectedControl != nullptr && *selectedControl == DSLFlowControl::For)
			{
				// Selected the for's own keyword -- only remove it (header + body + synthetic `end`) when its
				// body is empty: unlike if/while, a for-loop's body can't be safely kept/un-nested (statements
				// inside likely reference the loop variable, which would no longer exist once the header is gone).
				if (isBlockBodyEmpty(span->symbol))
					deleteEmptyBlock(span->symbol);
			}
			else if (selectedDeclarationType != nullptr)
			{
				// Only remove it when nothing else in this function still references it -- a still-used
				// declaration can't be removed out from under its uses.
				if (!AutoCompleteRules::isVariableReferenced(selectedDeclarationType, m_document.file))
					deleteLine(*selectedDeclarationType->line);
			}
			else if (span != nullptr && span->groupClose)
			{
				// Backspace on a group's ')' REOPENS the group mid-authoring (contents restored into the
				// compose box, paren open, last term ready to edit -- re-close and re-confirm as a whole);
				// continued backspacing peels its terms and finally removes the group itself. Contents that
				// can't round-trip into compose form (placeholders, member accesses) fall back to plain
				// replace-editing of the whole group instead.
				if (!beginReopenGroup(*span))
					beginCompose();
			}
			else if (selectedChainOperator)
			{
				deleteChainOperator(span->symbol, span->operatorIndex);
			}
			else if (selectedChainOperand)
			{
				deleteChainOperand(span->slot.parent, span->slot.argIndex);
			}
			else if ((selectedControl != nullptr && (*selectedControl == DSLFlowControl::Return || *selectedControl == DSLFlowControl::Break))
				|| selectedAssignmentStatement || selectedCallStatement)
			{
				// Selected a return/break, a bare assignment/compound-assign, or a call statement's own line --
				// just remove it; none of these open a block or declare a name, so there's nothing else to check
				// or preserve. The one exception: a non-Void function's BOTTOM return stays (its value remains
				// editable; only removing the function or its return type takes it away).
				if (span->symbol->line != nullptr && !isProtectedBottomReturn(span->symbol->line))
					deleteLine(*span->symbol->line);
			}
			else if (m_cursorLine >= 0 && m_cursorLine < static_cast<int>(m_formatted.size()) && isBlankStatementLine(m_formatted[m_cursorLine]))
			{
				// Selected a blank statement placeholder -- remove the line outright; there's nothing on it to edit.
				deleteLine(*m_formatted[m_cursorLine].sourceLine);
			}
			else
			{
				// Begins editing the currently-selected item, same as typing a printable character would --
				// just without adding one yet, so the FULL candidate list shows up front.
				beginCompose();
			}
		}
		else if (!m_pendingWord.empty())
		{
			// Ctrl+Backspace clears the whole typed word at once (renaming, new names, values -- anywhere
			// text is being composed); plain Backspace erases one character. Neither touches the staged
			// prefix -- stepping back through stages stays the plain-Backspace-on-empty ladder below.
			if (ctrl)
				m_pendingWord.clear();
			else
				m_pendingWord.pop_back();
			if (hasCandidateList())
				refreshCandidates();
		}
		else if (isChainComposeMode() || m_composeMode == ComposeMode::DeclareValue
			|| m_composeMode == ComposeMode::ReassignValue || m_composeMode == ComposeMode::EditExpr)
		{
			// (The vector-typed DeclareValue/ReassignValue/EditExpr variants aren't chain modes but still
			// step back through this ladder -- their isVector sub-branch and the widen terminals handle them.)
			// Undoes exactly ONE action at a time within the compound expression being composed (see the class
			// comment): the last operator+term, the last '(' if nothing's inside it yet, or reopening the
			// innermost just-closed ')' with ITS OWN last term restored for further editing -- before finally
			// stepping back out of value-composing entirely once nothing remains (DeclareValue -> re-edit the
			// name; ReassignValue/EditExpr -> cancel, leaving the document exactly as it was).
			const bool isVector = (m_composeMode == ComposeMode::DeclareValue && isVectorType(m_pendingDeclareType))
				|| (m_composeMode == ComposeMode::ReassignValue && isVectorType(reassignTargetType()));
			if (isVector)
			{
				// vec2/3/4: no expression chain involved (comma components only) -- step back out entirely
				// (a widened assignment re-edit steps out by clearing the line, same as its scalar path below).
				if (m_composeMode == ComposeMode::DeclareValue)
				{
					enterCompose(ComposeMode::DeclareName, std::string(dslTypeName(m_pendingDeclareType)) + " ", m_pendingDeclareName);
				}
				else if (m_reassignEditExpr != nullptr)
				{
					DSLCodeLine& line = *m_reassignEditExpr->line;
					cancelCompose();
					clearLineToBlankStatement(line);
				}
				else if (m_composeMode == ComposeMode::ReassignValue && m_reassignTarget != nullptr)
				{
					// New vector reassignment: same operator step-back as the scalar path below.
					enterCompose(ComposeMode::ReassignOp,
						std::get<DSLSymbol::VariableDeclaration>(m_reassignTarget->data).name + " ");
				}
				else
				{
					cancelCompose();
				}
			}
			else if (m_exprHasPendingGroup && !m_exprPendingGroup.isGroup)
			{
				// A resolved parameterized call un-resolves: its name returns to the box for editing; the
				// arguments drop (type '(' to stage them again).
				m_pendingWord = m_exprPendingGroup.candidate.label;
				m_exprHasPendingGroup = false;
			}
			else if (m_exprHasPendingGroup)
			{
				// Undo the ')' that just resolved this group: reopen it, restoring its OWN last term into
				// "being edited" (or, if that term was itself a group/resolved call, back into the pending
				// term) -- the operator (if any) leading into that term is left in place, since typing it was
				// a separate, earlier keystroke.
				ExprFrame reopened{ std::move(m_exprPendingGroup.groupTerms), std::move(m_exprPendingGroup.groupOps) };
				m_exprHasPendingGroup = false;
				PendingExprTerm lastTerm = std::move(reopened.terms.back());
				reopened.terms.pop_back();
				m_exprStack.push_back(std::move(reopened));
				restoreTermIntoBox(std::move(lastTerm));
			}
			else if (m_exprStack.size() > 1 && m_exprStack.back().terms.empty())
			{
				// This paren was just opened and nothing's been typed inside it yet -- undo the '(' itself.
				m_exprStack.pop_back();
			}
			else if (!m_exprStack.back().ops.empty())
			{
				// Undo the last operator: it and the term right before it were both committed in the SAME
				// keystroke (typing the operator), so both are undone together, restoring that term for
				// further editing.
				ExprFrame& frame = m_exprStack.back();
				PendingExprTerm lastTerm = std::move(frame.terms.back());
				frame.terms.pop_back();
				frame.ops.pop_back();
				restoreTermIntoBox(std::move(lastTerm));
			}
			else if (m_composeMode == ComposeMode::DeclareValue)
			{
				// Nothing chained/typed at all yet -- step back to re-edit the name.
				enterCompose(ComposeMode::DeclareName, std::string(dslTypeName(m_pendingDeclareType)) + " ", m_pendingDeclareName);
			}
			else if (m_composeMode == ComposeMode::EditExpr && !m_editInsert && m_editChainExpr == nullptr
				&& m_editSlot.kind == SlotRef::Kind::VariableDeclarationInitialValue
				&& m_editSlot.parent != nullptr && m_editSlot.parent->line != nullptr
				&& m_editSlot.parent->line->head() == m_editSlot.parent)
			{
				// Backspacing out of an empty WHOLE-initializer edit on a plain local declaration (its own
				// line's head -- not a for-loop counter, whose line is the for header) WIDENS the edit: the
				// staged Declare flow re-opens over the existing declaration at its value stage, compose box
				// covering the whole line ("float test = |"), so further Backspace peels into the name and
				// beyond -- see m_redeclareTarget's comment in the .ixx.
				m_redeclareTarget = m_editSlot.parent;
				const DSLSymbol::VariableDeclaration& decl = std::get<DSLSymbol::VariableDeclaration>(m_redeclareTarget->data);
				m_pendingDeclareType = std::get<DSLSymbol::TypeDeclaration>(decl.typeSymbol->data).type;
				m_pendingDeclareName = decl.name;
				enterChainStage(ComposeMode::DeclareValue);
			}
			else if (m_composeMode == ComposeMode::EditExpr && !m_editInsert && m_editChainExpr != nullptr
				&& m_editOperandIndex == 1 && m_editChainExpr->line != nullptr
				&& m_editChainExpr->line->head() == m_editChainExpr)
			{
				// Backspacing out of an empty right-hand-value edit on an assignment STATEMENT (the chain is
				// its own line's head -- never a for-clause or nested chain) widens the same way a
				// declaration's initializer does: the staged Reassign flow re-opens over the existing line,
				// showing its actual operator ("thing += |" / "test.x = |") -- member-assign targets restore
				// their dotted path too. See m_reassignEditExpr's comment in the .ixx.
				const DSLSymbol::Expression& assign = std::get<DSLSymbol::Expression>(m_editChainExpr->data);
				DSLSymbol* targetRoot = nullptr;
				std::string targetPath;
				if (!assign.operators.empty() && dslIsAssignOperator(assign.operators[0])
					&& receiverChainToRoot(assign.operands[0], targetRoot, targetPath))
				{
					m_reassignEditExpr = m_editChainExpr;
					m_reassignTarget = targetRoot;
					m_reassignMemberPath = splitMemberPath(targetPath);
					enterChainStage(ComposeMode::ReassignValue);
				}
				else
				{
					cancelCompose(); // a LineHead chain that isn't an assignment shouldn't exist -- bail cleanly
				}
			}
			else if (m_composeMode == ComposeMode::EditExpr && !m_editInsert && tryWidenFlowHeaderEdit())
			{
				// Widened into the matching staged flow (if/elseif/while condition, return value, or a
				// for-loop clause) -- the header re-authors from that point on and only changes when the flow
				// re-confirms in full; see tryWidenFlowHeaderEdit and m_flowEditLine.
			}
			else if (m_composeMode == ComposeMode::EditExpr && !m_editInsert && tryWidenCallStatementEdit())
			{
				// Same widening for a call STATEMENT's argument: the staged CallArgValue flow re-opens at that
				// argument with the earlier ones restored -- see tryWidenCallStatementEdit.
			}
			else if (m_composeMode == ComposeMode::EditExpr && !m_editInsert && tryWidenValueCallEdit())
			{
				// Same widening for a call VALUE's argument ("vec3 test = vec3(1,|"): the staging re-opens
				// with the owning declaration's/assignment's flow as the suspended return context, so the
				// peel continues out through the whole line -- see tryWidenValueCallEdit.
			}
			else if (m_composeMode == ComposeMode::EditExpr && !m_editInsert && tryWidenComparisonValueEdit())
			{
				// Same widening for a comparison-typed VALUE's operands ("bool test = i < 5"): the staged
				// comparison flow re-opens with the owning declaration's/assignment's context restored.
			}
			else if (m_composeMode == ComposeMode::EditExpr && !m_editInsert && m_editChainExpr != nullptr)
			{
				// A chain operand's edit backspaced past empty -- remove the operand itself (with its adjacent
				// chain operator, arithmetic or logical), continuing the same peeling the un-composed Backspace
				// does. This is also how a REOPENED group fully backspaced away finally deletes: its frame
				// empties, its '(' pops, and this step removes the group term from the surrounding chain. A
				// structural (comparison/assignment-side) operand can't be half-deleted, so that just cancels.
				DSLSymbol* chainExpr = m_editChainExpr;
				const int operandIndex = m_editOperandIndex;
				const DSLSymbol::Expression& chain = std::get<DSLSymbol::Expression>(chainExpr->data);
				const int adjacentOp = (operandIndex > 0) ? operandIndex - 1 : 0;
				const bool deletable = adjacentOp < static_cast<int>(chain.operators.size())
					&& dslIsChainOperator(chain.operators[adjacentOp]);
				cancelCompose();
				if (deletable)
					deleteChainOperand(chainExpr, operandIndex);
			}
			else if (m_composeMode == ComposeMode::ReassignValue && m_reassignEditExpr != nullptr)
			{
				// The widened re-edit backspaced past empty -- clear the whole assignment line back to a blank
				// statement. No reference guard needed (unlike a declaration): an assignment declares nothing.
				DSLCodeLine& line = *m_reassignEditExpr->line;
				cancelCompose();
				clearLineToBlankStatement(line);
			}
			else if (m_composeMode == ComposeMode::ReassignValue && m_reassignTarget != nullptr)
			{
				if (!m_reassignMemberPath.empty())
				{
					// A fresh member-assign steps back into its MemberSelect, the written member re-typed
					// ("test.x = |" -> "test.|x") -- the '.'-flow's own reverse.
					DSLSymbol* root = m_reassignTarget;
					std::vector<std::string> path = m_reassignMemberPath;
					m_reassignTarget = nullptr;
					m_reassignMemberPath.clear();
					const std::string last = path.back();
					path.pop_back();
					enterCompose(ComposeMode::FilterCandidates, "");
					enterMemberSelect(root);
					restoreMemberPath(joinedMemberPath(path));
					m_pendingWord = last;
					refreshCandidates();
				}
				else
				{
					// The NEW-statement flow steps back to re-pick the assignment operator, name kept.
					enterCompose(ComposeMode::ReassignOp,
						std::get<DSLSymbol::VariableDeclaration>(m_reassignTarget->data).name + " ");
				}
			}
			else if (m_composeMode == ComposeMode::ReturnValue && m_flowEditLine != nullptr
				&& !isProtectedBottomReturn(m_flowEditLine))
			{
				// Backspacing past a re-authored return's value removes the line -- EXCEPT a non-Void
				// function's bottom return, which falls through to a plain cancel instead (the original
				// return stays exactly as it was).
				DSLCodeLine& line = *m_flowEditLine;
				cancelCompose();
				deleteLine(line);
			}
			else if (m_composeMode == ComposeMode::ConditionRight)
			{
				// Step back to re-pick the comparator instead of losing the left side too (the prefix keeps
				// whichever context this condition serves -- flow keyword or value base -- plus any
				// accumulated &&/|| chain terms).
				enterCompose(ComposeMode::ConditionOp, stagedConditionPrefix() + chainDisplayText(m_conditionLeftChain) + " ");
			}
			else if (m_composeMode == ComposeMode::ConditionLeft && !m_logicalTerms.empty())
			{
				// Backspacing past an empty chain-term slot pops the PREVIOUS term back open for editing -- a
				// comparison reopens at its right side, a bare bool value straight into the box.
				const PendingLogicalTerm last = m_logicalTerms.back();
				m_logicalTerms.pop_back();
				m_logicalOps.pop_back();
				if (last.isComparison)
				{
					m_conditionLeftChain = last.left;
					m_conditionOp = last.comparator;
					enterCompose(ComposeMode::ConditionRight, "");
					restoreChainIntoCompose(last.right);
				}
				else
				{
					enterCompose(ComposeMode::ConditionLeft, "");
					restoreChainIntoCompose(last.left);
				}
			}
			else if (m_composeMode == ComposeMode::ConditionLeft && m_flowEditLine != nullptr)
			{
				// Backspacing past the whole re-authored condition = removing the wrapper itself, under
				// exactly the keyword's own Backspace rules: if/while need an empty attached else-chain (body
				// survives, un-nested); an elseif needs its own branch empty. A failing guard leaves the flow
				// at this stage -- guarded content refuses to vanish, same as everywhere else.
				DSLSymbol* head = m_flowEditLine->head();
				if (head != nullptr && head->type == ST::FlowControl)
				{
					if (std::get<DSLSymbol::FlowControl>(head->data).control == DSLFlowControl::ElseIf)
					{
						if (isBlockBodyEmpty(head))
						{
							DSLCodeLine& line = *m_flowEditLine;
							cancelCompose();
							deleteLine(line);
						}
					}
					else if (attachedElseChainEmpty(head))
					{
						cancelCompose();
						deleteBlockKeepBody(head);
					}
				}
			}
			else if (m_composeMode == ComposeMode::ForVarValue)
			{
				// (Non-vector -- the vector variant steps back in the outer ladder.) Re-edit the loop
				// variable's name instead of losing it entirely.
				enterCompose(ComposeMode::ForVarName, "for " + std::string(dslTypeName(m_forVarType)) + " ", m_forVarName);
			}
			else if (m_composeMode == ComposeMode::ForConditionLeft)
			{
				// Step back to re-edit the init clause, restored for further editing.
				if (isVectorType(m_forVarType))
					enterCompose(ComposeMode::ForVarValue, forVarPrefix(), m_forVarInitRawText);
				else
					enterChainStage(ComposeMode::ForVarValue, &m_forVarInitChain);
			}
			else if (m_composeMode == ComposeMode::ForConditionValue)
			{
				// Step back to re-pick the comparator instead of losing the condition's left side too.
				enterCompose(ComposeMode::ForConditionOp, forVarDeclPrefix() + ", " + chainDisplayText(m_forConditionLeftChain) + " ");
			}
			else if (m_composeMode == ComposeMode::ForIncrementValue)
			{
				// Step back to re-pick the increment operator instead of losing the whole condition too.
				enterCompose(ComposeMode::ForIncrementOp, forConditionPrefix() + ", " + m_forVarName + " ");
			}
			else if (m_composeMode == ComposeMode::CallArgValue)
			{
				// The CURRENT argument's own chain is fully empty (the generic peeling above already unwound
				// any operators/parens within it) -- step back one whole ARGUMENT (a fresh pick, restored as a
				// full chain for further editing, per the list-stage convention). Past the first argument, a
				// RE-authored call deletes its line (call statements are freely deletable, same as via their
				// name span); a fresh call just cancels/abandons -- nothing has touched the document at any point.
				CallStage& stage = m_callStack.back();
				if (!stage.argChains.empty())
				{
					PendingExprChain prev = std::move(stage.argChains.back());
					stage.argChains.pop_back();
					m_exprStack.assign(1, ExprFrame{});
					m_exprHasPendingGroup = false;
					enterCompose(ComposeMode::CallArgValue, "");
					restoreChainIntoCompose(prev);
					m_composePrefix = callStagePrefix() + exprComposePrefixFromStack();
					refreshCandidates();
				}
				else if (stage.returnMode != ComposeMode::None)
				{
					// A call VALUE with nothing confirmed yet abandons its staging entirely: pop it, resume
					// the SUSPENDED context (restoring ITS OWN m_exprStack), the callee's name re-typed there
					// -- a dot-call reopens its receiver's MemberSelect instead (the bare method name would
					// never re-resolve through the plain candidate lists).
					CallStage popped = std::move(m_callStack.back());
					m_callStack.pop_back();
					const ComposeMode back = popped.returnMode;
					m_exprStack = std::move(popped.savedExprStack);
					m_exprPendingGroup = std::move(popped.savedPendingGroup);
					m_exprHasPendingGroup = popped.savedHasPendingGroup;
					const std::string name = std::get<DSLSymbol::FunctionDeclaration>(popped.func->data).name;
					enterCompose(back, "", popped.receiver != nullptr ? std::string() : name);
					m_composePrefix = chainLeadTextFor(back) + exprComposePrefixFromStack();
					refreshCandidates();
					if (popped.receiver != nullptr)
					{
						enterMemberSelect(popped.receiver);
						restoreMemberPath(popped.receiverPath);
						m_pendingWord = name;
						refreshCandidates();
					}
				}
				else if (m_flowEditLine != nullptr)
				{
					DSLCodeLine& line = *m_flowEditLine;
					cancelCompose();
					deleteLine(line);
				}
				else if (stage.receiver != nullptr)
				{
					// A fresh dot-call STATEMENT abandons back into its receiver's MemberSelect, method name re-typed.
					DSLSymbol* receiver = stage.receiver;
					const std::string receiverPath = stage.receiverPath;
					const std::string name = std::get<DSLSymbol::FunctionDeclaration>(stage.func->data).name;
					cancelCompose();
					enterCompose(ComposeMode::FilterCandidates, "");
					enterMemberSelect(receiver);
					restoreMemberPath(receiverPath);
					m_pendingWord = name;
					refreshCandidates();
				}
				else
				{
					cancelCompose();
				}
			}
			else
			{
				// A fresh ReturnValue/ConditionLeft / an EditExpr that can't widen, nothing typed yet --
				// cancel like any other first-stage flow, leaving the document untouched.
				cancelCompose();
			}

			if (isChainComposeMode())
			{
				// chainLeadTextFor (not plain exprBasePrefix) since the CallArgValue branch above may have
				// left m_composeMode there -- exprBasePrefixFor has no case for it (see callStagePrefix).
				m_composePrefix = chainLeadTextFor(m_composeMode) + exprComposePrefixFromStack();
				refreshCandidates();
			}
		}
		else if (m_composeMode == ComposeMode::ConditionOp)
		{
			if (m_conditionValueReturnMode != ComposeMode::None && m_logicalTerms.empty())
			{
				// Back out of the comparison entirely: return to the value mode with the left side restored.
				const ComposeMode back = m_conditionValueReturnMode;
				m_conditionValueReturnMode = ComposeMode::None;
				enterChainStage(back, &m_conditionLeftChain);
			}
			else
			{
				// Step back to re-edit the current term's left side, restored.
				enterChainStage(ComposeMode::ConditionLeft, &m_conditionLeftChain);
			}
		}
		else if (m_composeMode == ComposeMode::FunctionParamName)
		{
			// Step back to re-pick this parameter's type instead of losing it entirely.
			enterCompose(ComposeMode::FunctionParamType, functionDeclarePrefix() + (m_pendingParamNames.empty() ? "" : ", "));
		}
		else if (m_composeMode == ComposeMode::FunctionDeclareDone)
		{
			// Undo the ')': reopen the parameter list, back in the add-another-or-close state.
			enterCompose(ComposeMode::FunctionParamType, functionDeclarePrefix() + (m_pendingParamNames.empty() ? "" : ", "));
		}
		else if (m_composeMode == ComposeMode::FunctionDeclareReturnType)
		{
			// Undo the '-': back to the closed list, arrow dropped.
			enterCompose(ComposeMode::FunctionDeclareDone, functionDeclarePrefix() + ")");
		}
		else if (m_composeMode == ComposeMode::FunctionParamType)
		{
			if (!m_pendingParamNames.empty())
			{
				// Step back into the PREVIOUS parameter's name instead of losing it.
				const std::string restoredName = m_pendingParamNames.back();
				m_pendingParamType = m_pendingParamTypes.back();
				m_pendingParamRef = m_pendingParamRefs.back();
				m_pendingParamNames.pop_back();
				m_pendingParamTypes.pop_back();
				m_pendingParamRefs.pop_back();
				enterCompose(ComposeMode::FunctionParamName, currentParamPrefix(), restoredName);
			}
			else
			{
				// No parameters accumulated yet -- step back to re-edit the function's own name.
				enterCompose(ComposeMode::FunctionDeclareName, "function ", m_pendingFunctionName);
			}
		}
		else if (m_composeMode == ComposeMode::ForVarName)
		{
			if (m_flowEditLine != nullptr)
			{
				// A re-edit keeps the loop variable's type fixed (see m_flowEditLoopVar), so there's no type
				// stage to step back into -- past the name, the next step is removing the loop itself, under
				// the keyword's own rule: only when its body is empty.
				DSLSymbol* head = m_flowEditLine->head();
				if (head != nullptr && isBlockBodyEmpty(head))
				{
					cancelCompose();
					deleteEmptyBlock(head);
				}
			}
			else
			{
				// No earlier free-typing stage to restore -- ForVarType is a candidate list, so just reset to it.
				enterCompose(ComposeMode::ForVarType, "for ");
			}
		}
		else if (m_composeMode == ComposeMode::ForVarValue)
		{
			// (Vector loop variables only -- scalar ForVarValue steps back inside the chain ladder above.)
			enterCompose(ComposeMode::ForVarName, "for " + std::string(dslTypeName(m_forVarType)) + " ", m_forVarName);
		}
		else if (m_composeMode == ComposeMode::ForConditionOp)
		{
			// Step back to re-edit the condition's left side, restored (usually the seeded loop variable).
			enterChainStage(ComposeMode::ForConditionLeft, &m_forConditionLeftChain);
		}
		else if (m_composeMode == ComposeMode::ForIncrementOp)
		{
			// Step back to re-edit the condition's bound, restored.
			enterChainStage(ComposeMode::ForConditionValue, &m_forConditionValueChain);
		}
		else if (m_composeMode == ComposeMode::CommentText)
		{
			// Fully backspaced away: an existing comment line deletes; a fresh one just cancels back to the
			// statement it was typed over.
			const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
			DSLCodeLine* commentLine = (span != nullptr && span->symbol != nullptr && span->symbol->type == ST::Comment)
				? span->symbol->line : nullptr;
			cancelCompose();
			if (commentLine != nullptr)
				deleteLine(*commentLine);
		}
		else if (m_composeMode == ComposeMode::ReassignOp)
		{
			// Step back into the statement compose with the target's name re-typed for further editing.
			const std::string name = (m_reassignTarget != nullptr)
				? std::get<DSLSymbol::VariableDeclaration>(m_reassignTarget->data).name : std::string();
			enterCompose(ComposeMode::FilterCandidates, "", name);
		}
		else if (m_composeMode == ComposeMode::FunctionDeclareName && m_flowEditLine != nullptr)
		{
			// The re-authored function's name has been fully backspaced away -- one step further removes the
			// function itself, under the keyword's own rules: empty-or-return-only body AND uncalled;
			// otherwise stay here.
			DSLSymbol* head = m_flowEditLine->head();
			if (head != nullptr && isFunctionBodyDeletable(head) && !AutoCompleteRules::isFunctionReferenced(head, m_document.file))
			{
				cancelCompose();
				deleteEmptyBlock(head);
			}
		}
		else if (m_composeMode == ComposeMode::MemberSelect)
		{
			if (!m_memberPath.empty())
			{
				// Peel one chain segment: "self.pos.|" steps back to "self.|pos", the segment re-typed.
				const std::string segment = m_memberPath.back();
				m_memberPath.pop_back();
				DSLType type = declaredTypeOf(m_memberReceiver);
				for (const std::string& walked : m_memberPath)
					type = resolveMemberType(type, walked);
				m_memberReceiverType = type;
				const std::string prefix = m_composePrefix.substr(0, m_composePrefix.size() - segment.size() - 1);
				enterCompose(ComposeMode::MemberSelect, prefix, segment); // member-select context survives (see enterMemberSelect)
			}
			else
			{
				// Backspacing past the '.' returns to the stage it was typed in, the object's name restored
				// as typed text (it re-matches its BindingObject/Variable candidate there).
				DSLSymbol* receiver = m_memberReceiver;
				const ComposeMode back = m_memberReturnMode;
				const std::string name = std::get<DSLSymbol::VariableDeclaration>(receiver->data).name;
				const std::string prefix = m_composePrefix.substr(0, m_composePrefix.size() - name.size() - 1);
				m_memberReceiver = nullptr;
				m_memberReturnMode = ComposeMode::None;
				enterCompose(back, prefix, name);
			}
		}
		else if (m_composeMode == ComposeMode::DeclareName && m_redeclareTarget != nullptr)
		{
			// The re-declare flow's name has been fully backspaced away -- one step further clears the WHOLE
			// declaration line back to a blank statement, but only when nothing references the variable (the
			// same guard as deleting it via its type span); a still-used declaration just stays at the name
			// stage, refusing to vanish out from under its uses.
			if (!AutoCompleteRules::isVariableReferenced(m_redeclareTarget, m_document.file))
			{
				DSLCodeLine& line = *m_redeclareTarget->line;
				cancelCompose(); // clears m_redeclareTarget -- the symbol it names is about to be destroyed
				clearLineToBlankStatement(line);
			}
		}
		else
		{
			// Nothing left to un-type and nowhere sensible to step back to -- back out entirely (nothing was
			// ever written to the document by any of these staged flows, so this always cancels cleanly).
			cancelCompose();
		}
		return;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
		if (composing)
			confirmCompose();
		else
			insertLineAfterCursor();
		return;
	case SDL_SCANCODE_ESCAPE:
		cancelCompose();
		return;
	default:
		break;
	}

	const char c = charFromKeycode(static_cast<int>(evt.key.key), shift);
	if (c == 0)
		return;

	// FREE-TEXT sub-states come before everything -- Space is content there, not a stage advance: a comment's
	// text, and an OPEN string literal (leading '"' not yet closed) inside any value compose.
	if (m_composeMode == ComposeMode::CommentText)
	{
		m_pendingWord += c;
		return;
	}
	if (composing && !m_pendingWord.empty() && m_pendingWord.front() == '"'
		&& (m_pendingWord.size() == 1 || m_pendingWord.back() != '"'))
	{
		m_pendingWord += c;
		if (hasCandidateList())
			refreshCandidates();
		return;
	}

	// '.' over a TYPED-and-matched binding object ("physics" + '.') dots into its member/function list -- any
	// stage that offers BindingObject candidates supports it (see refreshCandidates). Typed text only, so a
	// stray '.' can never resolve the highlighted default; elsewhere the character falls through (e.g. as a
	// float literal's decimal point).
	if (c == '.' && composing && !m_pendingWord.empty() && hasCandidateList())
	{
		const Candidate* picked = selectedCandidate();
		if (picked != nullptr && picked->kind == Candidate::Kind::BindingObject)
		{
			enterMemberSelect(picked->refSymbol);
			return;
		}
		// A struct-typed VARIABLE dots into its members/functions the same way ("v." -> x/y/z/length()...) --
		// as a value (Kind::Variable) or at a statement's start (Kind::Reassign, the member-assign lead-in).
		if (picked != nullptr && (picked->kind == Candidate::Kind::Variable || picked->kind == Candidate::Kind::Reassign)
			&& picked->refSymbol != nullptr
			&& m_composeMode != ComposeMode::MemberSelect && dslIsStructType(declaredTypeOf(picked->refSymbol)))
		{
			enterMemberSelect(picked->refSymbol);
			return;
		}
		// A chainable MEMBER matched mid-MemberSelect extends the chain ("self.pos." -> x/y/z/length()...,
		// "self.physics." -> applyImpulse()...).
		if (picked != nullptr && picked->kind == Candidate::Kind::Member
			&& m_composeMode == ComposeMode::MemberSelect && dslIsChainableType(picked->declareType))
		{
			m_memberPath.push_back(picked->label);
			m_memberReceiverType = picked->declareType;
			enterCompose(ComposeMode::MemberSelect, m_composePrefix + picked->label + ".");
			return;
		}
	}

	// '#' turns a statement slot into a comment line ("# ..."), committing on Enter like any statement.
	if (c == '#')
	{
		if (m_composeMode == ComposeMode::FilterCandidates && m_pendingWord.empty())
		{
			enterCompose(ComposeMode::CommentText, "# ");
			return;
		}
		if (!composing)
		{
			const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
			if (span != nullptr && span->slot.kind == SlotRef::Kind::LineHead)
				enterCompose(ComposeMode::CommentText, "# ");
		}
		return;
	}

	// '"' opens a string literal in any value compose (in-place edits included via beginCompose); the block
	// above then swallows every character until the closing quote.
	if (c == '"')
	{
		if (!composing)
			beginCompose(); // a value span opens its in-place edit; a non-editable span leaves mode None
		if (m_pendingWord.empty()
			&& (isChainComposeMode()
				|| (m_composeMode == ComposeMode::CallArgValue && !isVectorType(currentCallParamType()))))
		{
			m_pendingWord += c;
			if (hasCandidateList())
				refreshCandidates();
		}
		return;
	}

	if (c == ' ')
	{
		// Space advances stages WITHIN a line but never finishes one -- only Enter commits (see confirmCompose)
		// -- so a space typed while reaching for "&&", an operator, or just out of habit can't cut a line short.
		confirmCompose(/*allowCommit*/ false);
		return;
	}

	// Building a new function's parameter list free-types "type name" pairs -- ')' always closes the list right
	// now (finalizing the in-progress parameter first if it has a name), and ',' (only meaningful once a name
	// is being typed) finalizes the current parameter and starts the next one's type pick. Neither is a valid
	// identifier/compose character, so both are special-cased here rather than falling into the generic
	// filtering below and being silently dropped. Both modes are only ever entered via a prior confirm (never
	// beginCompose()'s first keystroke), so composing is already guaranteed true here.
	if (m_composeMode == ComposeMode::FunctionDeclareName && c == '(')
	{
		// '(' after the function's name reads as "start the parameter list" -- same as Space/Enter (the
		// confirm still refuses an empty or colliding name, exactly like it would there).
		confirmCompose();
		return;
	}
	if (m_composeMode == ComposeMode::FunctionParamType && c == ')')
	{
		enterCompose(ComposeMode::FunctionDeclareDone, functionDeclarePrefix() + ")");
		return;
	}
	if (m_composeMode == ComposeMode::FunctionDeclareDone)
	{
		if (c == '-')
		{
			// '-' starts appending the return syntax: "name(...) -> <type>" -- picking the type commits the
			// whole function with it (the '>' needn't be typed; the prefix supplies the full arrow). A
			// re-authored function that's called anywhere can't change its return type, so the stage doesn't
			// open there (commitFunctionRedeclare would refuse the change anyway).
			if (m_flowEditLine == nullptr || m_flowEditLine->head() == nullptr
				|| !AutoCompleteRules::isFunctionReferenced(m_flowEditLine->head(), m_document.file))
				enterCompose(ComposeMode::FunctionDeclareReturnType, functionDeclarePrefix() + ") -> ");
		}
		return; // nothing else means anything between the ')' and the commit/arrow
	}
	if (m_composeMode == ComposeMode::FunctionParamName)
	{
		if (c == ',')
		{
			if (m_pendingWord.empty() || isPendingParamNameTaken(m_pendingWord))
				return; // need a valid, non-duplicate name before starting the next parameter
			m_pendingParamTypes.push_back(m_pendingParamType);
			m_pendingParamNames.push_back(m_pendingWord);
			m_pendingParamRefs.push_back(m_pendingParamRef);
			enterCompose(ComposeMode::FunctionParamType, functionDeclarePrefix() + ", ");
			return;
		}
		if (c == ')')
		{
			if (!m_pendingWord.empty())
			{
				if (isPendingParamNameTaken(m_pendingWord))
					return;
				m_pendingParamTypes.push_back(m_pendingParamType);
				m_pendingParamNames.push_back(m_pendingWord);
				m_pendingParamRefs.push_back(m_pendingParamRef);
				m_pendingWord.clear();
			}
			enterCompose(ComposeMode::FunctionDeclareDone, functionDeclarePrefix() + ")");
			return;
		}
	}

	// '&'/'|' after a resolved condition term chains another with &&/||: at ConditionRight the whole comparison
	// commits as a chain term ("if i > 0 && |"); at ConditionLeft a BOOL-valued pick chains bare ("if canJump()
	// && |"). The flow loops back to ConditionLeft for the next term; Space/Enter still confirms the whole
	// chain wherever it's valid. Requires typed text matching the candidate, so the second '&' of a doubled
	// "&&" (empty box) is harmlessly ignored.
	if ((m_composeMode == ComposeMode::ConditionRight || m_composeMode == ComposeMode::ConditionLeft)
		&& (c == '&' || c == '|'))
	{
		const bool chainInput = !m_pendingWord.empty() || m_exprHasPendingGroup
			|| (!m_exprStack.empty() && !m_exprStack[0].terms.empty());
		if (!chainInput)
			return; // the doubled '&' of "&&" (or nothing typed yet) -- harmlessly ignored
		const DSLOperator logicalOp = (c == '&') ? DSLOperator::And : DSLOperator::Or;
		if (m_composeMode == ComposeMode::ConditionRight)
		{
			PendingExprChain right;
			if (captureComposedChain(right))
			{
				m_logicalTerms.push_back(PendingLogicalTerm{ true, m_conditionLeftChain, m_conditionOp, right });
				m_logicalOps.push_back(logicalOp);
				enterChainStage(ComposeMode::ConditionLeft);
			}
		}
		else if (composedChainPeekType() == DSLType::Bool) // type-checked BEFORE capturing -- a refusal must not consume
		{
			PendingExprChain bare;
			if (captureComposedChain(bare))
			{
				m_logicalTerms.push_back(PendingLogicalTerm{ false, bare, DSLOperator::Equal, PendingExprChain{} });
				m_logicalOps.push_back(logicalOp);
				enterChainStage(ComposeMode::ConditionLeft);
			}
		}
		return;
	}

	// Typing an assignment-operator character over a matched Reassign candidate in the statement compose turns
	// it into a reassignment with that operator: "i" then '+' reads as "i +=..." (the ReassignOp stage, the
	// typed character already filtering the six operators). Space-confirming the candidate instead authors a
	// plain `=` directly, as before.
	if (m_composeMode == ComposeMode::FilterCandidates && isAssignOperatorChar(c))
	{
		const Candidate* picked = selectedCandidate();
		if (picked != nullptr && picked->kind == Candidate::Kind::Reassign)
		{
			m_reassignTarget = picked->refSymbol;
			m_reassignOp = DSLOperator::Assign;
			enterCompose(ComposeMode::ReassignOp, picked->label + " ", std::string(1, c));
			return;
		}
	}

	// Typing a comparison character right after a condition's FIRST operand (if/elseif/while's ConditionLeft,
	// or a for-loop's init value -- the stage right before ITS comparator) resolves that operand and jumps
	// straight into picking the comparator, with the typed character already filtering it -- "height<" reads
	// as "height, then <" without an intervening Space. ConditionLeft advances DIRECTLY (never through
	// confirmCompose, whose bool-pick path would commit a bare bool -- '=' over a bool candidate must mean
	// "compare it": `b == ...`); a refusal (nothing matched, invalid components) drops the keystroke.
	if ((m_composeMode == ComposeMode::ConditionLeft || m_composeMode == ComposeMode::ForConditionLeft) && isOperatorChar(c))
	{
		// The left side (a possibly-compound chain, "i + 2") resolves and the comparator stage opens with the
		// typed character already filtering it. A capture that refuses (open paren, nothing typed) drops the key.
		PendingExprChain left;
		if (captureComposedChain(left))
		{
			if (m_composeMode == ComposeMode::ConditionLeft)
			{
				m_conditionLeftChain = left;
				enterCompose(ComposeMode::ConditionOp,
					stagedConditionPrefix() + chainDisplayText(left) + " ", std::string(1, c));
			}
			else
			{
				m_forConditionLeftChain = left;
				enterCompose(ComposeMode::ForConditionOp,
					forVarDeclPrefix() + ", " + chainDisplayText(left) + " ", std::string(1, c));
			}
		}
		return;
	}
	if (m_composeMode == ComposeMode::ForVarValue && isOperatorChar(c))
	{
		// The init clause resolves and the condition opens seeded with the loop variable as its left side,
		// the comparator character pre-typed -- "for int i = n-1 <" reads straight through.
		if (isVectorType(m_forVarType))
		{
			if (!vectorComponentsValid(m_forVarType, m_pendingWord))
				return;
			m_forVarInitRawText = m_pendingWord;
			m_forVarInitChain = PendingExprChain{};
		}
		else
		{
			PendingExprChain init;
			if (!captureComposedChain(init))
				return;
			m_forVarInitChain = init;
			m_forVarInitRawText.clear();
		}
		m_forConditionLeftChain = loopVarSeedChain();
		enterCompose(ComposeMode::ForConditionOp,
			forVarDeclPrefix() + ", " + chainDisplayText(m_forConditionLeftChain) + " ", std::string(1, c));
		return;
	}

	// ',' right after a for-loop clause's value confirms it and advances to the next clause -- "for int i = 0,"
	// then "i < 5," read straight through, no Space needed. Inside a VECTOR init the ',' keeps separating
	// components until the count is full, then flips back to meaning "next clause" (call-argument rule). A
	// confirm that refuses (nothing matched, invalid components) leaves the mode unchanged; the keystroke drops.
	if ((m_composeMode == ComposeMode::ForVarValue || m_composeMode == ComposeMode::ForConditionValue) && c == ',')
	{
		const bool componentComma = m_composeMode == ComposeMode::ForVarValue && isVectorType(m_forVarType)
			&& static_cast<int>(std::count(m_pendingWord.begin(), m_pendingWord.end(), ',')) < vectorComponentCount(m_forVarType) - 1;
		if (!componentComma)
		{
			confirmCompose();
			return;
		}
		// falls through -- appended as a component character by the vector filter below
	}

	// Staging a parameterized call's arguments: ',' or ')' at the OUTERMOST level of the CURRENT argument's own
	// expression confirms it (same as Space/Enter -- ')' on the last argument commits the whole call). While an
	// inner paren is still open WITHIN this argument's own expression (e.g. composing "(a + b)" as one
	// argument), both fall through instead -- ')' closes that paren via the generic chain-mode handling below,
	// a ',' there isn't valid syntax and simply gets dropped like anywhere else.
	if (m_composeMode == ComposeMode::CallArgValue && (c == ',' || c == ')') && m_exprStack.size() == 1)
	{
		confirmCompose();
		return;
	}

	// NOT composing: two in-place expression edits start from a bare keystroke on a committed span. An operator
	// character over an Expression's own OPERATOR span (matched to that operator's class, so `<` on a `+` still
	// falls through) begins replacing just that operator; an ARITHMETIC operator over any numeric-typed VALUE
	// span begins inserting a new term right after it (see beginEditExprInsert for the splice-vs-wrap shapes).
	if (!composing)
	{
		const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		if (span != nullptr && span->symbol != nullptr)
		{
			// ')' on the LAST operand of a parenthesized group steps the selection OUT to the group's own ')'
			// span -- "after the parens" -- so the natural next keystroke (an operator) continues the chain
			// past the group instead of inside it. Pressing ')' again steps out one more level when the group
			// itself sits last inside an enclosing group (the ')' span's slot is the group's own position, so
			// the same rule applies to it unchanged).
			if (c == ')' && span->slot.kind == SlotRef::Kind::ExpressionOperand)
			{
				const DSLSymbol::Expression& parentChain = std::get<DSLSymbol::Expression>(span->slot.parent->data);
				if (parentChain.grouped && span->slot.argIndex == static_cast<int>(parentChain.operands.size()) - 1)
				{
					m_pendingSelectTarget = span->slot.parent;
					m_pendingSelectGroupClose = true;
					return;
				}
			}
			if (span->operatorIndex >= 0 && span->symbol->type == ST::Expression)
			{
				const DSLOperator current = std::get<DSLSymbol::Expression>(span->symbol->data).operators[span->operatorIndex];
				const bool classChar = dslIsComparisonOperator(current) ? isOperatorChar(c)
					: dslIsAssignOperator(current) ? isAssignOperatorChar(c)
					: dslIsLogicalOperator(current) ? (c == '&' || c == '|')
					: isArithmeticOperatorChar(c);
				if (classChar)
				{
					m_replaceOpExpr = span->symbol;
					m_replaceOpIndex = span->operatorIndex;
					enterCompose(ComposeMode::ReplaceOperator, "", std::string(1, c));
					return;
				}
			}
			else if (c == '-' && span->slot.kind == SlotRef::Kind::FunctionReturnType)
			{
				// '-' on a committed header's return-type span starts the "-> type" pick, same as it does
				// right after the ')' in the staged flow -- the arrow comes from the compose prefix; the '-'
				// itself isn't text (typing a letter there always composed the type already; this makes the
				// arrow-shaped keystroke work too).
				beginCompose();
				return;
			}
			else if (isArithmeticOperatorChar(c)
				&& (span->slot.kind == SlotRef::Kind::FlowControlCondition || span->slot.kind == SlotRef::Kind::CallArgumentValue
					|| span->slot.kind == SlotRef::Kind::VariableDeclarationInitialValue || span->slot.kind == SlotRef::Kind::ExpressionOperand))
			{
				// Gated on the slot's element type: arithmetic on a Bool/String value (`canJump() + ...`)
				// makes no sense -- and vector values compose as comma components, not chains -- so those
				// keystrokes fall through and get dropped by the filters below.
				const DSLType type = AutoCompleteRules::expectedTypeForSlot(span->slot, m_document.file);
				if (type == DSLType::Int || type == DSLType::Float)
				{
					const SyntaxLine& syntaxLine = m_formatted[m_cursorLine];
					const std::string anchorText = syntaxLine.text.substr(span->startCol, span->endCol - span->startCol);
					beginEditExprInsert(*span, arithmeticOperatorFromChar(c), anchorText);
					return;
				}
			}
		}
	}

	// Composing a scalar DeclareValue/ReassignValue, or an in-place EditExpr segment -- an arbitrary-length
	// compound expression with parenthesized grouping (see the class comment). '(' (only at the very start of a
	// fresh term) opens a nested group; ')' (only if a paren is actually open) closes the innermost one;
	// +,-,*,/,% commit whatever term is currently resolved (a matched candidate, or a just-closed group) and
	// start composing the next one. None of these are compose/identifier/vector-component characters, so all
	// are intercepted here rather than falling into the generic filtering below where they'd just be silently
	// dropped. m_composePrefix is always fully rebuilt from m_exprStack after each of these
	// (exprComposePrefixFromStack), never hand-patched.
	if (isChainComposeMode())
	{
		// A matched parameterized-Function candidate resolves through argument staging before it can act as a
		// term: '(' (the natural call gesture), an operator, or ')' over it opens the sub-flow instead.
		if ((c == '(' || c == ')' || isArithmeticOperatorChar(c)) && tryBeginValueCallStaging())
			return;

		// A COMPARATOR typed over a matched candidate in a BOOL-typed value turns it into a comparison's left
		// operand ("bool b = i < 5"): the ConditionOp/Right stages take over with the typed character already
		// filtering the comparator, and their confirm commits the comparison AS the value
		// (m_conditionValueReturnMode -> commitComparisonValue). Whole-value only -- never mid-chain/grouped.
		// The bool comparator/'&&' VALUE entries below only apply to the value modes -- the condition stages
		// have their own comparator/logical handling in the earlier blocks, so they read as Void here.
		DSLType composedType = (m_composeMode == ComposeMode::DeclareValue) ? m_pendingDeclareType
			: (m_composeMode == ComposeMode::ReassignValue) ? reassignTargetType()
			: (m_composeMode == ComposeMode::EditExpr) ? m_editValueType
			: DSLType::Void;
		if (m_composeMode == ComposeMode::ReturnValue)
		{
			// a bool function's return supports the same comparator continuation ("return i < 5")
			const SyntaxSpan* span = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
			if (span != nullptr && span->slot.line != nullptr)
				composedType = AutoCompleteRules::enclosingFunctionReturnType(*span->slot.line, m_document.file);
		}
		if (isOperatorChar(c) && composedType == DSLType::Bool)
		{
			// The whole chain composed so far ("x + 1") becomes the comparison's LEFT side and the comparator
			// stage opens with the typed character filtering it -- the Condition* stages take over, committing
			// back through this value mode (m_conditionValueReturnMode).
			PendingExprChain left;
			if (captureComposedChain(left))
			{
				m_conditionValueReturnMode = m_composeMode;
				m_conditionLeftChain = left;
				m_logicalTerms.clear();
				m_logicalOps.clear();
				enterCompose(ComposeMode::ConditionOp,
					stagedConditionPrefix() + chainDisplayText(left) + " ", std::string(1, c));
			}
			return;
		}

		// '&'/'|' over a composed BOOL chain starts a logical chain with it as a bare first term
		// ("b = canJump() && |") -- same handover to the Condition* stages.
		if ((c == '&' || c == '|') && composedType == DSLType::Bool
			&& composedChainPeekType() == DSLType::Bool) // checked BEFORE capturing -- a refusal must not consume
		{
			PendingExprChain bare;
			if (captureComposedChain(bare))
			{
				m_conditionValueReturnMode = m_composeMode;
				m_logicalTerms.assign(1, PendingLogicalTerm{ false, bare, DSLOperator::Equal, PendingExprChain{} });
				m_logicalOps.assign(1, (c == '&') ? DSLOperator::And : DSLOperator::Or);
				enterChainStage(ComposeMode::ConditionLeft);
			}
			return;
		}

		// The term the typed operator/`)` acts on: a just-closed parenthesized group, or the matched candidate.
		const bool haveTerm = m_exprHasPendingGroup || selectedCandidate() != nullptr;
		auto takeCurrentTerm = [&]() -> PendingExprTerm
		{
			if (m_exprHasPendingGroup)
			{
				m_exprHasPendingGroup = false;
				return std::move(m_exprPendingGroup);
			}
			return PendingExprTerm{ false, *selectedCandidate(), {}, {} };
		};

		if (isArithmeticOperatorChar(c) && haveTerm)
		{
			m_exprStack.back().terms.push_back(takeCurrentTerm());
			m_exprStack.back().ops.push_back(arithmeticOperatorFromChar(c));
			m_pendingWord.clear();
			m_composePrefix = exprBasePrefix() + exprComposePrefixFromStack();
			refreshCandidates();
			return;
		}
		if (c == ')' && m_exprStack.size() > 1 && haveTerm)
		{
			m_exprStack.back().terms.push_back(takeCurrentTerm());
			ExprFrame closed = std::move(m_exprStack.back());
			m_exprStack.pop_back();
			m_exprPendingGroup = PendingExprTerm{ true, Candidate{}, std::move(closed.terms), std::move(closed.ops) };
			m_exprHasPendingGroup = true;
			m_pendingWord.clear();
			m_candidates.clear(); // nothing is being typed right after a ')' -- no list until an operator continues the chain
			m_composePrefix = exprBasePrefix() + exprComposePrefixFromStack();
			return;
		}
		if (c == '(' && m_pendingWord.empty() && !m_exprHasPendingGroup)
		{
			m_exprStack.push_back(ExprFrame{});
			m_candidates.clear();
			m_composePrefix = exprBasePrefix() + exprComposePrefixFromStack();
			refreshCandidates();
			return;
		}
		if (m_exprHasPendingGroup)
			return; // right after a group resolves, only an operator or an enclosing ')' means anything
	}

	// Renaming a VariableDeclaration is identifier-only too, but that mode only gets set by beginCompose()
	// itself -- for the very FIRST keystroke (composing not started yet), peek at what's selected so this
	// keystroke is judged by the SAME rule beginCompose() is about to apply, not the general one.
	bool willBeRename = false;
	if (!composing)
	{
		const SyntaxSpan* peekSpan = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		willBeRename = peekSpan != nullptr && peekSpan->symbol != nullptr
			&& (peekSpan->symbol->type == ST::VariableDeclaration
				|| (peekSpan->symbol->type == ST::FunctionDeclaration && peekSpan->slot.kind == SlotRef::Kind::LineHead));
	}

	// ConditionOp and vec2/3/4's DeclareValue only ever get entered via a prior confirm (never beginCompose()'s
	// first keystroke), so there's no "peek ahead" concern here the way Rename needs one below. Same for every
	// For* mode.
	if (m_composeMode == ComposeMode::ConditionOp || m_composeMode == ComposeMode::ForConditionOp)
	{
		if (!isOperatorChar(c))
		{
			// Anything non-comparator after TYPED operator text commits the matched comparator and starts the
			// right side with this character -- "n<x" reads straight through, no Space needed. An empty box
			// refuses (the highlighted default must never commit off a stray letter); the keystroke drops.
			if (m_pendingWord.empty())
				return;
			confirmCompose();
			if ((m_composeMode == ComposeMode::ConditionRight || m_composeMode == ComposeMode::ForConditionValue)
				&& isComposeChar(c))
			{
				m_pendingWord += c;
				refreshCandidates();
			}
			return;
		}
	}
	else if (m_composeMode == ComposeMode::ForIncrementOp)
	{
		if (!isAssignOperatorChar(c))
		{
			// Same continuation for the increment operator: "i+=n" commits the "+=" and starts the step.
			if (m_pendingWord.empty())
				return;
			confirmCompose();
			if (m_composeMode == ComposeMode::ForIncrementValue && isComposeChar(c))
			{
				m_pendingWord += c;
				refreshCandidates();
			}
			return;
		}
	}
	else if (m_composeMode == ComposeMode::ReassignOp)
	{
		if (!isAssignOperatorChar(c))
		{
			// Typing the VALUE right after the operator ("i+=1"): confirm the matched operator and let this
			// character start the value; nothing matched leaves the keystroke dropped.
			confirmCompose();
			if (m_composeMode == ComposeMode::ReassignValue && isComposeChar(c))
			{
				m_pendingWord += c;
				refreshCandidates();
			}
			return;
		}
	}
	else if (m_composeMode == ComposeMode::ReplaceOperator)
	{
		if (!isArithmeticOperatorChar(c) && !isOperatorChar(c) && c != '&' && c != '|')
			return; // the union of all four operator classes' characters -- anything else means nothing here
	}
	else if ((m_composeMode == ComposeMode::DeclareValue && isVectorType(m_pendingDeclareType))
		|| (m_composeMode == ComposeMode::ForVarValue && isVectorType(m_forVarType))
		|| (m_composeMode == ComposeMode::ReassignValue && isVectorType(reassignTargetType()))
		|| (m_composeMode == ComposeMode::EditExpr && isVectorType(m_editValueType))
		|| (m_composeMode == ComposeMode::CallArgValue && isVectorType(currentCallParamType())))
	{
		if (!isVectorComponentChar(c))
			return; // only digits/./-/, are meaningful while typing vector components
	}
	else if (m_composeMode == ComposeMode::DeclareName || m_composeMode == ComposeMode::Rename || willBeRename
		|| m_composeMode == ComposeMode::FunctionDeclareName || m_composeMode == ComposeMode::FunctionParamName
		|| m_composeMode == ComposeMode::ForVarName)
	{
		if (!isIdentifierChar(c, m_pendingWord.empty()))
			return; // not a valid (or valid-so-far) identifier character -- e.g. a leading digit, or '.'
	}
	else if (!isComposeChar(c))
	{
		return; // not a valid identifier/number character -- ignore rather than corrupt the composed text
	}

	if (!composing)
		beginCompose();
	if (m_composeMode != ComposeMode::None)
	{
		if (!m_pendingWord.empty() && m_pendingWord.front() == '"')
			return; // a CLOSED string literal takes no more characters -- confirm/operate on it, or Backspace in
		m_pendingWord += c;
		if (hasCandidateList())
			refreshCandidates();
	}
}

// The canonical VariableDeclaration/FunctionDeclaration `symbol` refers to: itself, if it already is one; the
// declaration a VariableReference points to; or the callee a FunctionCall targets. nullptr for anything else (a
// Constant, an Expression's operator, a FlowControl keyword, ...) -- those aren't "the same thing" anywhere
// else in the document, so there's nothing to highlight against.
DSLSymbol* ScriptEditor::referenceHighlightTarget(DSLSymbol* symbol) const
{
	if (symbol == nullptr)
		return nullptr;
	switch (symbol->type)
	{
	case ST::VariableDeclaration:
	case ST::FunctionDeclaration:
		return symbol;
	case ST::VariableReference:
		return std::get<DSLSymbol::VariableReference>(symbol->data).declaration;
	case ST::FunctionCall:
		return std::get<DSLSymbol::FunctionCall>(symbol->data).functionSymbol;
	default:
		return nullptr;
	}
}

namespace
{
	// Syntax palette (VS-dark-ish): flow keywords purple, declaration keywords blue, types teal, functions
	// yellow, variables light blue, numbers green, strings orange, operators/glue dim, placeholders dimmer.
	constexpr ImVec4 kColFlowKeyword{ 0.773f, 0.525f, 0.753f, 1.0f }; // if/elseif/else/while/for/return/break, `end`
	constexpr ImVec4 kColDeclKeyword{ 0.337f, 0.612f, 0.839f, 1.0f }; // "function"/"ref" glue words, true/false
	constexpr ImVec4 kColType{ 0.306f, 0.788f, 0.690f, 1.0f };
	constexpr ImVec4 kColFunction{ 0.863f, 0.863f, 0.667f, 1.0f };
	constexpr ImVec4 kColVariable{ 0.612f, 0.863f, 0.996f, 1.0f };
	constexpr ImVec4 kColNumber{ 0.710f, 0.808f, 0.659f, 1.0f };
	constexpr ImVec4 kColString{ 0.808f, 0.569f, 0.471f, 1.0f };
	constexpr ImVec4 kColComment{ 0.416f, 0.600f, 0.333f, 1.0f };
	constexpr ImVec4 kColPunct{ 0.65f, 0.65f, 0.65f, 1.0f };
	constexpr ImVec4 kColPlaceholder{ 0.48f, 0.48f, 0.48f, 1.0f };

	// `syntheticEnd` overrides everything: an `end` line's single span points at its block's HEADER symbol
	// (possibly a FunctionDeclaration), but the word itself is a flow keyword.
	ImVec4 syntaxSpanColor(const SyntaxSpan& span, bool syntheticEnd)
	{
		if (syntheticEnd || span.symbol == nullptr)
			return syntheticEnd ? kColFlowKeyword : kColPunct;
		switch (span.symbol->type)
		{
		case ST::FlowControl:
			return kColFlowKeyword;
		case ST::TypeDeclaration:
			return kColType;
		case ST::FunctionDeclaration: // the name span -- or the return-type span, whose text is a type name
			return span.slot.kind == SlotRef::Kind::FunctionReturnType ? kColType : kColFunction;
		case ST::FunctionCall:
			return kColFunction;
		case ST::VariableDeclaration:
		case ST::VariableReference:
		case ST::MemberAccess:
			return kColVariable;
		case ST::Constant:
		{
			const DSLType type = std::get<DSLSymbol::Constant>(span.symbol->data).type;
			return type == DSLType::String ? kColString : type == DSLType::Bool ? kColDeclKeyword : kColNumber;
		}
		case ST::Expression: // operator spans and a group's ')'
			return kColPunct;
		case ST::Comment:
			return kColComment;
		case ST::Placeholder:
			return kColPlaceholder;
		default:
			return kColPunct;
		}
	}

	// One colored run of text on the current visual line. `firstPiece` chains subsequent runs with
	// SameLine(0,0), so a whole line renders as adjacent items with zero spacing -- column math elsewhere
	// (highlights, click hit-testing) stays character-based and unaffected.
	void drawPiece(const char* begin, const char* end, const ImVec4& color, bool& firstPiece)
	{
		if (begin >= end)
			return;
		if (!firstPiece)
			ImGui::SameLine(0.0f, 0.0f);
		firstPiece = false;
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextUnformatted(begin, end);
		ImGui::PopStyleColor();
	}

	// Unspanned glue is punctuation ('=', parens, commas, "->", spacing) -- except the keyword words hiding in
	// it: "function " and "ref " render as glue, and deserve their keyword color.
	void drawGlue(const char* begin, const char* end, bool& firstPiece)
	{
		const char* p = begin;
		while (p < end)
		{
			if (std::isalpha(static_cast<unsigned char>(*p)))
			{
				const char* wordEnd = p;
				while (wordEnd < end && std::isalpha(static_cast<unsigned char>(*wordEnd)))
					++wordEnd;
				const std::string_view word(p, wordEnd);
				drawPiece(p, wordEnd, (word == "function" || word == "ref") ? kColDeclKeyword : kColPunct, firstPiece);
				p = wordEnd;
			}
			else
			{
				const char* runEnd = p;
				while (runEnd < end && !std::isalpha(static_cast<unsigned char>(*runEnd)))
					++runEnd;
				drawPiece(p, runEnd, kColPunct, firstPiece);
				p = runEnd;
			}
		}
	}

	// The column range [fromCol, toCol) of `line`, syntax-colored: spans in their symbol's color, the glue
	// between them via drawGlue. Always emits at least one item so the ImGui line advances and callers can
	// chain more pieces after it with SameLine.
	void drawHighlightedRange(const SyntaxLine& line, int fromCol, int toCol, bool& firstPiece)
	{
		const bool syntheticEnd = (line.sourceLine == nullptr);
		const char* text = line.text.data();
		int cursor = fromCol;
		for (const SyntaxSpan& span : line.spans)
		{
			if (span.endCol <= fromCol)
				continue;
			if (span.startCol >= toCol)
				break;
			const int glueEnd = std::clamp(span.startCol, fromCol, toCol);
			if (glueEnd > cursor)
				drawGlue(text + cursor, text + glueEnd, firstPiece);
			const int pieceBegin = std::max(span.startCol, fromCol);
			const int pieceEnd = std::min(span.endCol, toCol);
			drawPiece(text + pieceBegin, text + pieceEnd, syntaxSpanColor(span, syntheticEnd), firstPiece);
			cursor = std::max(cursor, pieceEnd);
		}
		if (toCol > cursor)
			drawGlue(text + cursor, text + toCol, firstPiece);
		if (firstPiece)
		{
			ImGui::TextUnformatted(""); // empty range (blank line / empty side of a compose) -- still an item
			firstPiece = false;
		}
	}
}

void ScriptEditor::renderTextArea()
{
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * m_fontScale);
	m_lineHeight = ImGui::GetTextLineHeightWithSpacing();

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::BeginChild("##se_view", avail, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

	ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsWindowHovered() && io.KeyCtrl && io.MouseWheel != 0.0f)
	{
		m_fontScale = std::clamp(m_fontScale + io.MouseWheel * 0.1f, 0.3f, 4.0f);
		io.MouseWheel = 0.0f;
	}

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	m_textOriginX = origin.x;
	m_textOriginY = origin.y;

	ImDrawList* drawList = ImGui::GetWindowDrawList();

	const bool composing = m_composeMode != ComposeMode::None;

	// Every OTHER occurrence of whatever variable/function is currently selected -- outlined in a distinct
	// color from the cursor's own fill+outline highlight (drawn per-line below). Suppressed while composing:
	// the underlying identity may be about to change.
	DSLSymbol* highlightTarget = nullptr;
	if (!composing)
	{
		const SyntaxSpan* selectedSpan = currentSpan(m_formatted, m_cursorLine, m_cursorSpan);
		if (selectedSpan != nullptr)
			highlightTarget = referenceHighlightTarget(selectedSpan->symbol);
	}

	// Fixed for this frame (font/scale don't change mid-loop) -- measured once instead of once per line.
	const float indentUnitWidth = ImGui::CalcTextSize("    ").x;

	for (int i = 0; i < static_cast<int>(m_formatted.size()); ++i)
	{
		const SyntaxLine& line = m_formatted[i];
		const ImVec2 lineOrigin = ImGui::GetCursorScreenPos();
		const float indent = static_cast<float>(line.scopeLevel) * indentUnitWidth;
		ImGui::Dummy(ImVec2(indent, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);

		if (highlightTarget != nullptr)
		{
			for (int s = 0; s < static_cast<int>(line.spans.size()); ++s)
			{
				if (i == m_cursorLine && s == m_cursorSpan)
					continue; // the active selection already has its own highlight
				const SyntaxSpan& hspan = line.spans[s];
				if (referenceHighlightTarget(hspan.symbol) != highlightTarget)
					continue;
				const float startX = ImGui::CalcTextSize(line.text.data(), line.text.data() + hspan.startCol).x;
				const float endX = ImGui::CalcTextSize(line.text.data(), line.text.data() + hspan.endCol).x;
				const ImVec2 rectMin(lineOrigin.x + indent + startX, lineOrigin.y);
				const ImVec2 rectMax(lineOrigin.x + indent + std::max(endX, startX + 6.0f), lineOrigin.y + ImGui::GetTextLineHeight());
				drawList->AddRect(rectMin, rectMax, ImGui::GetColorU32(ImVec4(0.30f, 0.70f, 1.0f, 0.85f)));
			}
		}

		const bool isCursorLine = (i == m_cursorLine) && !line.spans.empty();
		if (isCursorLine)
		{
			const int spanIdx = std::clamp(m_cursorSpan, 0, static_cast<int>(line.spans.size()) - 1);
			const SyntaxSpan& span = line.spans[spanIdx];
			// A staged re-declare's or widened reassignment's compose box covers the ENTIRE line, not just the
			// selected span -- those flows re-author the whole statement (see m_redeclareTarget /
			// m_reassignEditExpr), so nothing of the old line should show around the box. A reopened group's
			// box covers the group's full "(...)" column range instead (m_composeCover*, set by
			// beginReopenGroup); whole-line wins when a step-back transitions from one flow into the other.
			const bool wholeLineCompose = composing
				&& (m_redeclareTarget != nullptr || m_reassignEditExpr != nullptr || m_flowEditLine != nullptr);
			const bool coverCompose = composing && !wholeLineCompose && m_composeCoverStart >= 0;
			const int selStart = wholeLineCompose ? 0 : coverCompose ? m_composeCoverStart : span.startCol;
			const int selEnd = wholeLineCompose ? static_cast<int>(line.text.size()) : coverCompose ? m_composeCoverEnd : span.endCol;
			const float preWidth = ImGui::CalcTextSize(line.text.data(), line.text.data() + selStart).x;

			// While composing, the selected span's own text is replaced by the LIVE composed text (what's
			// been resolved so far, plus what's being typed right now) -- rendered inline, in the same
			// highlight rectangle, rather than a separate floating box (see AutoCompleteRules' candidate
			// list, still shown below via renderAutocompletePopup).
			const std::string composedText = composing ? (m_composePrefix + m_pendingWord + "|") : std::string();
			const float hiWidth = composing
				? std::max(6.0f, ImGui::CalcTextSize(composedText.c_str()).x)
				// A blank statement-placeholder line has a zero-width span (see Syntax's Placeholder
				// rendering) -- keep it visible instead of collapsing to an invisible sliver, same as a text
				// cursor on an empty line.
				: std::max(6.0f, ImGui::CalcTextSize(line.text.data() + selStart, line.text.data() + selEnd).x);

			const ImVec2 rectMin(lineOrigin.x + indent + preWidth, lineOrigin.y);
			const ImVec2 rectMax(lineOrigin.x + indent + preWidth + hiWidth, lineOrigin.y + ImGui::GetTextLineHeight());
			drawList->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(ImVec4(1.0f, 0.80f, 0.24f, 0.27f)));
			drawList->AddRect(rectMin, rectMax, ImGui::GetColorU32(ImVec4(1.0f, 0.80f, 0.24f, 0.78f)));
			m_cursorScreenX = rectMin.x;
			m_cursorScreenY = rectMin.y;

			if (composing)
			{
				// The surrounding committed text keeps its syntax colors; the live composed text stays the
				// default color -- in-progress input reads distinctly inside its highlight box.
				bool firstPiece = true;
				drawHighlightedRange(line, 0, selStart, firstPiece);
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::TextUnformatted(composedText.c_str());
				bool postFirst = false; // already mid-line -- the post pieces chain themselves (and an empty
				                        // post range must NOT leave a dangling SameLine)
				drawHighlightedRange(line, selEnd, static_cast<int>(line.text.size()), postFirst);
				continue;
			}
		}

		bool firstPiece = true;
		drawHighlightedRange(line, 0, static_cast<int>(line.text.size()), firstPiece);
	}

	// Click to select the nearest span on the clicked line (cancels any in-progress composition first, same
	// as keyboard navigation does).
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_formatted.empty())
	{
		if (composing)
			cancelCompose();

		const ImVec2 mouse = ImGui::GetMousePos();
		int clickedLine = static_cast<int>((mouse.y - m_textOriginY) / m_lineHeight);
		clickedLine = std::clamp(clickedLine, 0, static_cast<int>(m_formatted.size()) - 1);
		const SyntaxLine& line = m_formatted[clickedLine];
		const float indent = static_cast<float>(line.scopeLevel) * indentUnitWidth;
		const float relX = mouse.x - m_textOriginX - indent;

		int bestSpan = 0;
		float bestDist = std::numeric_limits<float>::max();
		for (int s = 0; s < static_cast<int>(line.spans.size()); ++s)
		{
			const SyntaxSpan& span = line.spans[s];
			const float startX = ImGui::CalcTextSize(line.text.data(), line.text.data() + span.startCol).x;
			const float endX   = ImGui::CalcTextSize(line.text.data(), line.text.data() + span.endCol).x;
			const float midX = (startX + endX) * 0.5f;
			const float dist = std::abs(relX - midX);
			if (dist < bestDist)
			{
				bestDist = dist;
				bestSpan = s;
			}
		}
		m_cursorLine = clickedLine;
		m_cursorSpan = bestSpan;
	}

	// Keep the selection in view.
	const float caretContentY = m_cursorLine * m_lineHeight;
	const float scrollY = ImGui::GetScrollY();
	const float childHeight = ImGui::GetWindowHeight();
	if (caretContentY < scrollY)
		ImGui::SetScrollY(caretContentY);
	else if (caretContentY + m_lineHeight > scrollY + childHeight)
		ImGui::SetScrollY(caretContentY + m_lineHeight - childHeight);

	ImGui::EndChild();
	ImGui::PopFont();

	renderAutocompletePopup();
}

// Just the candidate list, floated below the cursor's own highlight rectangle -- the composed text itself now
// renders inline in that rectangle (see renderTextArea), not in a separate box here. Nothing to show during
// DeclareName/Rename (free-typing an identifier, no candidates to filter against).
void ScriptEditor::renderAutocompletePopup()
{
	if (!hasCandidateList())
		return;

	ImGui::SetNextWindowPos(ImVec2(m_cursorScreenX, m_cursorScreenY + m_lineHeight));
	ImGui::SetNextWindowBgAlpha(0.97f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
	ImGui::Begin("##se_autocomplete", nullptr, flags);

	if (m_candidates.empty())
		ImGui::TextDisabled("(no matches)");
	for (int i = 0; i < static_cast<int>(m_candidates.size()); ++i)
	{
		const bool selected = i == m_candidateSelected;
		if (selected)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		ImGui::TextUnformatted(m_candidates[i].label.c_str());
		if (selected)
			ImGui::PopStyleColor();
	}
	ImGui::End();
}

bool ScriptEditor::isComponentMemberReferenced(DSLType memberType) const
{
	// Every use of a component member (self.physics, dotted into as a call receiver or a chained access) is a
	// MemberAccess stamped with that member's own type (see DSL.ixx's MemberAccess::type), owned flat by some
	// line (see DSL.ixx's ownership model) -- no need to walk back to which VariableDeclaration it hangs off.
	for (const std::unique_ptr<DSLCodeLine>& line : m_document.file.lines)
		for (const std::unique_ptr<DSLSymbol>& s : line->symbols)
			if (s->type == ST::MemberAccess && std::get<DSLSymbol::MemberAccess>(s->data).type == memberType)
				return true;
	return false;
}

bool ScriptEditor::isDataFieldReferenced(const std::string& name) const
{
	// A field's own MemberAccess ("self.data.<name>") is distinguished from any OTHER member sharing the same
	// spelling (e.g. self.pos vs. a field literally named "pos") by its RECEIVER's type -- only self.data's own
	// members ever resolve to DSLType::ScriptData.
	for (const std::unique_ptr<DSLCodeLine>& line : m_document.file.lines)
		for (const std::unique_ptr<DSLSymbol>& s : line->symbols)
			if (s->type == ST::MemberAccess)
			{
				const DSLSymbol::MemberAccess& m = std::get<DSLSymbol::MemberAccess>(s->data);
				if (m.memberName == name && dslValueType(m.receiver) == DSLType::ScriptData)
					return true;
			}
	return false;
}

DSLSymbol* ScriptEditor::findEntryFunctionHead(const char* name) const
{
	for (const std::unique_ptr<DSLCodeLine>& line : m_document.file.lines)
	{
		DSLSymbol* head = line->head();
		if (line->scopeLevel == 0 && head != nullptr && head->type == ST::FunctionDeclaration
			&& std::get<DSLSymbol::FunctionDeclaration>(head->data).name == name)
			return head;
	}
	return nullptr;
}

bool ScriptEditor::isEntryPointName(const std::string& name) const
{
	return m_bindings.entryPointFor(name) != nullptr;
}

bool ScriptEditor::isLockedEntryFunction(const DSLSymbol* funcDecl) const
{
	if (funcDecl == nullptr || funcDecl->type != ST::FunctionDeclaration)
		return false;
	return isEntryPointName(std::get<DSLSymbol::FunctionDeclaration>(funcDecl->data).name);
}

void ScriptEditor::toggleEntryFunction(const EntryPointDef& def, bool enable)
{
	DSLSymbol* existing = findEntryFunctionHead(def.name);
	if (enable)
	{
		if (existing != nullptr)
			return; // already present (shouldn't happen -- the checkbox already reads as checked)

		auto headerPtr = std::make_unique<DSLCodeLine>();
		headerPtr->scopeLevel = 0;
		DSLCodeLine& header = *headerPtr;
		m_document.file.lines.push_back(std::move(headerPtr));

		std::vector<DSLSymbol*> params;
		for (const EntryPointParam& param : def.dslParams)
		{
			DSLSymbol* typeSymbol = pushSymbol(header, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ param.type });
			params.push_back(pushSymbol(header, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ param.name, typeSymbol }));
		}
		pushSymbol(header, ST::FunctionDeclaration, DSLSymbol::FunctionDeclaration{ def.name, std::move(params), DSLType::Void });

		seedStatementPlaceholder(insertLineAfter(header, header.scopeLevel + 1));
		return;
	}

	// Disable: only when nothing would be lost -- same guard/refusal convention as un-requiring a component
	// still in use (renderSidebarPanel's checkboxes below).
	if (existing == nullptr)
		return;
	if (!isFunctionBodyDeletable(existing) || AutoCompleteRules::isFunctionReferenced(existing, m_document.file))
	{
		Log::warning(std::string("Can't remove '") + def.name + "' -- it has a body, or is still called elsewhere");
		return;
	}
	deleteEmptyBlock(existing);
}

// The bindings browser: an Entity section (self, its plain members, and one row per requirable component --
// physics/audio/force -- nested UNDER self, checkbox = the script requires it, since they're only ever reached
// as self.physics/self.audio/self.force) and an Engine section (the free functions). Auto-populated from the
// registry; read-only apart from the require checkboxes. Un-requiring a component the script still references
// is refused.
void ScriptEditor::renderSidebarPanel()
{
	ImGui::BeginChild("##dsl_sidebar", ImVec2(m_sidebarWidth, 0.0f), ImGuiChildFlags_None);

	bool firstSegment = true;
	auto seg = [&firstSegment](const ImVec4& color, const char* text)
	{
		if (!firstSegment)
			ImGui::SameLine(0.0f, 0.0f);
		ImGui::TextColored(color, "%s", text);
		firstSegment = false;
	};
	auto drawFunction = [&](const BindingFunc& func)
	{
		firstSegment = true;
		if (func.returnType != DSLType::Void)
		{
			seg(kColType, dslTypeName(func.returnType));
			seg(kColPunct, " ");
		}
		seg(kColFunction, func.name);
		seg(kColPunct, "(");
		for (size_t i = 0; i < func.params.size(); ++i)
		{
			if (i > 0)
				seg(kColPunct, ", ");
			seg(kColType, dslTypeName(func.params[i].type));
			seg(kColPunct, " ");
			seg(kColVariable, func.params[i].name);
		}
		seg(kColPunct, ")");
	};
	// Recursive (std::function, since it calls itself): a plain data member (including struct-typed ones like
	// "vec3 pos" -- their own shape is browsable separately, under TYPES) draws as "type name"; an ENGINE-OBJECT
	// member (self.physics and friends) draws as its own tree node instead -- checkbox-gated when
	// component-bound -- browsing the matching binding object's own members/functions, mirroring how the DSL
	// itself dots into it. Struct types are deliberately excluded here even though they're also "chainable"
	// (dslIsChainableType) -- objectFor only resolves binding OBJECTS, never structs (see ScriptBindings).
	std::function<void(const BindingObject&)> drawObjectContents = [&](const BindingObject& object)
	{
		for (const BindingMember& member : object.members)
		{
			if (dslIsEngineObjectType(member.type))
			{
				const BindingObject* nested = m_bindings.objectFor(member.type);
				if (nested == nullptr)
					continue;
				if (member.requiredComponent != DSLComponentKind::None)
				{
					bool checkboxValue = dslIsComponentRequired(m_document, member.requiredComponent);
					if (ImGui::Checkbox((std::string("##req_") + member.name).c_str(), &checkboxValue))
					{
						if (checkboxValue)
							m_document.requiredComponents.push_back(member.requiredComponent);
						else if (!isComponentMemberReferenced(member.type))
							std::erase(m_document.requiredComponents, member.requiredComponent);
						else
							Log::warning(std::string("Can't un-require '") + member.name + "' -- the script still references it");
					}
					ImGui::SameLine();
					if (!dslIsComponentRequired(m_document, member.requiredComponent))
					{
						ImGui::TextDisabled("%s %s", dslTypeName(member.type), member.name);
						continue;
					}
				}
				if (ImGui::TreeNode(member.name, "%s %s", dslTypeName(member.type), member.name))
				{
					drawObjectContents(*nested);
					ImGui::TreePop();
				}
				continue;
			}
			firstSegment = true;
			seg(kColType, dslTypeName(member.type));
			seg(kColPunct, " ");
			seg(kColVariable, member.name);
		}
		for (const BindingFunc& func : object.functions)
			drawFunction(func);
	};

	// EXPORTS: one checkbox per ScriptAPI.h entry point (see toggleEntryFunction) -- checked = a matching
	// top-level function currently exists in the document. Checking one on seeds it with its locked parameters;
	// unchecking removes it, refused while it has a real body or is still called (same pattern as the
	// require-component checkboxes below).
	ImGui::TextDisabled("EXPORTS");
	for (const EntryPointDef& def : m_bindings.entryPoints())
	{
		bool checked = findEntryFunctionHead(def.name) != nullptr;
		if (ImGui::Checkbox(def.name, &checked))
			toggleEntryFunction(def, checked);
	}

	ImGui::Spacing();
	ImGui::Separator();
	// SCRIPT DATA: this document's own persistent per-instance fields (self.data.<name>), authored here and
	// serialized as .dsl "//@@data <type> <name>" lines (ScriptLoader). Removing a field is refused (same
	// pattern as everything else in this panel) while the script still references it.
	ImGui::TextDisabled("SCRIPT DATA");
	for (size_t i = 0; i < m_document.dataFields.size(); ++i)
	{
		const DSLDataField& field = m_document.dataFields[i];
		ImGui::PushID(static_cast<int>(i));
		firstSegment = true;
		seg(kColType, dslTypeName(field.type));
		seg(kColPunct, " ");
		seg(kColVariable, field.name.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			if (!isDataFieldReferenced(field.name))
				m_document.dataFields.erase(m_document.dataFields.begin() + static_cast<std::ptrdiff_t>(i));
			else
				Log::warning("Can't remove '" + field.name + "' -- the script still references it");
		}
		ImGui::PopID();
	}
	{
		// The same declarable-type set typeKeywordCandidates offers (primitives + every registered struct).
		std::vector<DSLType> typeOptions = { DSLType::Int, DSLType::Float, DSLType::Bool, DSLType::String };
		for (size_t i = 0; i < m_bindings.structs().size(); ++i)
			typeOptions.push_back(dslStructType(static_cast<int>(i)));

		ImGui::SetNextItemWidth(80.0f);
		if (ImGui::BeginCombo("##dataFieldType", dslTypeName(m_pendingDataFieldType)))
		{
			for (DSLType t : typeOptions)
				if (ImGui::Selectable(dslTypeName(t), t == m_pendingDataFieldType))
					m_pendingDataFieldType = t;
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputText("##dataFieldName", m_pendingDataFieldName, sizeof(m_pendingDataFieldName));
		ImGui::SameLine();
		if (ImGui::SmallButton("Add"))
		{
			const std::string name = m_pendingDataFieldName;
			if (name.empty())
				Log::warning("Enter a name before adding a script data field");
			else if (AutoCompleteRules::isReservedWord(name))
				Log::warning("'" + name + "' is reserved");
			else if (dslFindDataField(m_document, name) != nullptr)
				Log::warning("'" + name + "' is already a script data field");
			else
			{
				m_document.dataFields.push_back(DSLDataField{ name, m_pendingDataFieldType });
				m_pendingDataFieldName[0] = '\0';
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("ENTITY");
	for (const BindingObject& object : m_bindings.objects())
	{
		if (object.name == nullptr || !object.sidebarTopLevel)
			continue;
		if (ImGui::TreeNode(object.name, "%s %s", dslTypeName(object.type), object.name))
		{
			drawObjectContents(object);
			ImGui::TreePop();
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("ENGINE");
	for (const BindingObject& object : m_bindings.objects())
		if (object.name == nullptr)
			drawObjectContents(object);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("TYPES");
	for (const BindingStruct& structDef : m_bindings.structs())
	{
		if (!ImGui::TreeNode(structDef.name))
			continue;
		// the constructor, then members, then member functions
		firstSegment = true;
		seg(kColType, structDef.name);
		seg(kColPunct, "(");
		for (size_t i = 0; i < structDef.constructorParams.size(); ++i)
		{
			if (i > 0)
				seg(kColPunct, ", ");
			seg(kColType, dslTypeName(structDef.constructorParams[i].type));
			seg(kColPunct, " ");
			seg(kColVariable, structDef.constructorParams[i].name);
		}
		seg(kColPunct, ")");
		for (const BindingMember& member : structDef.members)
		{
			firstSegment = true;
			seg(kColType, dslTypeName(member.type));
			seg(kColPunct, " ");
			seg(kColVariable, member.name);
		}
		for (const BindingFunc& func : structDef.functions)
			drawFunction(func);
		ImGui::TreePop();
	}

	ImGui::EndChild();
}

void ScriptEditor::render()
{
	if (!m_built)
	{
		buildExampleDocument();
		m_built = true;
	}

	// An empty document is ONE blank statement line, never zero lines -- there must always be a cursor stop
	// to type/compose on (deleting the last line otherwise leaves no way to enter anything again).
	if (m_document.file.lines.empty())
	{
		auto blankLine = std::make_unique<DSLCodeLine>();
		DSLCodeLine& line = *blankLine;
		m_document.file.lines.push_back(std::move(blankLine));
		seedStatementPlaceholder(line);
	}

	m_formatted = Syntax::format(m_document.file, m_compact);
	clampCursor();

	if (m_pendingSelectLineEnd >= 0 && !m_formatted.empty())
	{
		m_cursorLine = std::clamp(m_pendingSelectLineEnd, 0, static_cast<int>(m_formatted.size()) - 1);
		m_cursorSpan = std::max(0, static_cast<int>(m_formatted[m_cursorLine].spans.size()) - 1);
		m_pendingSelectLineEnd = -1;
	}

	if (m_pendingSelectTarget != nullptr)
	{
		bool found = false;
		for (int li = 0; li < static_cast<int>(m_formatted.size()) && !found; ++li)
			for (int si = 0; si < static_cast<int>(m_formatted[li].spans.size()) && !found; ++si)
				if (m_formatted[li].spans[si].symbol == m_pendingSelectTarget
					&& (m_pendingSelectOperatorIndex < 0 || m_formatted[li].spans[si].operatorIndex == m_pendingSelectOperatorIndex)
					&& m_formatted[li].spans[si].groupClose == m_pendingSelectGroupClose)
				{
					m_cursorLine = li;
					m_cursorSpan = si;
					found = true;
				}
		m_pendingSelectTarget = nullptr;
		m_pendingSelectOperatorIndex = -1;
		m_pendingSelectGroupClose = false;
	}

	if (m_pendingComposeReturnValue)
	{
		// The cursor just landed on a freshly seeded return slot (see applyFunctionReturnChange) -- open its
		// staged compose right away: "return |", the nudge that a function with a return type returns
		// something. Escape simply leaves the blank statement line.
		m_pendingComposeReturnValue = false;
		enterChainStage(ComposeMode::ReturnValue);
	}

	m_hasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	if (!m_hasFocus && m_composeMode != ComposeMode::None)
		cancelCompose(); // panel lost focus mid-composition -- don't leave an unresolved edit hanging

	renderSidebarPanel();
	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.28f, 0.28f, 0.28f, 0.60f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.50f, 0.50f, 0.80f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.50f, 0.50f, 0.50f, 1.00f));
	ImGui::Button("##dsl_sidebar_splitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		m_sidebarWidth += ImGui::GetIO().MouseDelta.x;
		m_sidebarWidth  = std::clamp(m_sidebarWidth, 80.0f, 600.0f);
	}
	if (ImGui::IsItemHovered())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	ImGui::BeginChild("##dsl_main", ImVec2(0.0f, 0.0f), false);

	// Save/Load toolbar. While the path field is being typed into, ImGui's WantTextInput keeps handleKeyEvent
	// out, so the document cursor and the field never fight over keys.
	ImGui::SetNextItemWidth(240.0f);
	ImGui::InputText("##dsl_path", m_pathBuf, sizeof(m_pathBuf));
	ImGui::SameLine();
	if (ImGui::Button("Save"))
		saveDocument();
	ImGui::SameLine();
	if (ImGui::Button("Load"))
		loadDocument();

	ImGui::Separator();

	renderTextArea();

	ImGui::EndChild();
}

void ScriptEditor::saveDocument()
{
	const std::string path = m_pathBuf;
	if (path.empty())
		return;
	m_document.filePath = path;
	if (ScriptLoader::save(m_document, path, Transpiler::transpile(m_document, m_bindings)))
		Log::info("Script saved to " + path);
	else
		Log::error("Failed to write " + path);
}

void ScriptEditor::loadDocument()
{
	const std::string path = m_pathBuf;
	if (path.empty())
		return;
	cancelCompose(); // resolve any in-flight edit against the OLD document before replacing it
	const ScriptLoader::LoadResult result = ScriptLoader::load(m_document, path, m_builtins, m_bindings);
	if (!result.success)
	{
		Log::error("Failed to load script: " + result.error);
		return;
	}
	m_document.filePath = path;
	// Every symbol pointer from the replaced document is dead -- reset all selection/landing state.
	m_cursorLine = 0;
	m_cursorSpan = 0;
	m_pendingSelectTarget = nullptr;
	m_pendingSelectOperatorIndex = -1;
	m_pendingSelectGroupClose = false;
	m_pendingSelectLineEnd = -1;
	m_pendingComposeReturnValue = false;
	Log::info("Script loaded from " + path);
}
