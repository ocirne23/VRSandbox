module Script;

import Core;
import :DSL;
import :ScriptLang;

const char* dslTypeName(DSLType type)
{
	// Struct types are dynamic -- their spelling lives in the registry (see DSLType::FirstStruct).
	if (const BindingStruct* structDef = Globals::scriptBindings.structFor(type); structDef != nullptr)
		return structDef->name;
	switch (type)
	{
	case DSLType::Void:             return "void";
	case DSLType::Int:              return "int";
	case DSLType::Float:            return "float";
	case DSLType::String:          return "string";
	case DSLType::Bool:             return "bool";
	case DSLType::Function:         return "function";
	case DSLType::World:            return "World";
	case DSLType::Entity:           return "Entity";
	case DSLType::PhysicsComponent: return "PhysicsComponent";
	case DSLType::AudioComponent:   return "AudioComponent";
	case DSLType::ForceComponent:   return "ForceComponent";
	default:                        return "?";
	}
}

namespace
{
	using ST = DSLSymbol::SymbolType;

	const char* flowControlKeyword(DSLFlowControl control)
	{
		switch (control)
		{
		case DSLFlowControl::If:     return "if";
		case DSLFlowControl::ElseIf: return "elseif";
		case DSLFlowControl::Else:   return "else";
		case DSLFlowControl::While:  return "while";
		case DSLFlowControl::For:    return "for";
		case DSLFlowControl::Return: return "return";
		case DSLFlowControl::Break:  return "break";
		default:                     return "?";
		}
	}

	const char* operatorText(DSLOperator op)
	{
		switch (op)
		{
		case DSLOperator::Assign:             return "=";
		case DSLOperator::AssignAdd:          return "+=";
		case DSLOperator::AssignSubtract:     return "-=";
		case DSLOperator::AssignMultiply:     return "*=";
		case DSLOperator::AssignDivide:       return "/=";
		case DSLOperator::AssignModulus:      return "%=";
		case DSLOperator::Add:                return "+";
		case DSLOperator::Subtract:           return "-";
		case DSLOperator::Multiply:           return "*";
		case DSLOperator::Divide:             return "/";
		case DSLOperator::Modulus:             return "%";
		case DSLOperator::Equal:              return "==";
		case DSLOperator::NotEqual:           return "!=";
		case DSLOperator::LessThan:           return "<";
		case DSLOperator::GreaterThan:        return ">";
		case DSLOperator::LessThanOrEqual:    return "<=";
		case DSLOperator::GreaterThanOrEqual: return ">=";
		case DSLOperator::And:                return "&&";
		case DSLOperator::Or:                 return "||";
		default:                              return "?";
		}
	}

	void appendSpan(DSLSymbol* symbol, const std::string& text, size_t startPos, std::vector<SyntaxSpan>& outSpans, SlotRef slot = {}, int operatorIndex = -1)
	{
		outSpans.push_back(SyntaxSpan{ symbol, static_cast<int>(startPos), static_cast<int>(text.size()), slot, operatorIndex });
	}

	// Recursive per-symbol renderer -- used both for a line's head symbol and for every nested sub-expression
	// (conditions, call arguments, operands, receivers). `isFunctionParamContext` is true only while rendering
	// a VariableDeclaration that is one of a FunctionDeclaration's own parameters (or a call's matching
	// argument type annotation), which is the one place `compact` hides a type. `slot` describes how to
	// replace THIS symbol wholesale (see SlotRef) -- it's attached only to the symbol's own top-level
	// identifying span; when recursing into a child, either a fresh slot is built (the three tracked
	// positions: FlowControl::condition, CallArgument::value, VariableDeclaration::initialValue) or `{}` is
	// passed (everywhere else -- not independently replaceable in M3).
	void renderSymbol(DSLSymbol* symbol, bool compact, bool isFunctionParamContext, const SlotRef& slot, std::string& outText, std::vector<SyntaxSpan>& outSpans)
	{
		if (symbol == nullptr)
			return;

		switch (symbol->type)
		{
		case ST::Constant:
		{
			const DSLSymbol::Constant& c = std::get<DSLSymbol::Constant>(symbol->data);
			const size_t start = outText.size();
			outText += (c.type == DSLType::String) ? ("\"" + c.value + "\"") : c.value;
			appendSpan(symbol, outText, start, outSpans, slot);
			break;
		}
		case ST::TypeDeclaration:
		{
			const DSLSymbol::TypeDeclaration& t = std::get<DSLSymbol::TypeDeclaration>(symbol->data);
			const size_t start = outText.size();
			outText += dslTypeName(t.type);
			appendSpan(symbol, outText, start, outSpans, slot);
			break;
		}
		case ST::VariableReference:
		{
			const DSLSymbol::VariableReference& r = std::get<DSLSymbol::VariableReference>(symbol->data);
			const DSLSymbol::VariableDeclaration& decl = std::get<DSLSymbol::VariableDeclaration>(r.declaration->data);
			const size_t start = outText.size();
			outText += decl.name;
			appendSpan(symbol, outText, start, outSpans, slot); // span -> the reference itself, not the declaration
			break;
		}
		case ST::VariableDeclaration:
		{
			const DSLSymbol::VariableDeclaration& v = std::get<DSLSymbol::VariableDeclaration>(symbol->data);
			if (v.isRef)
				outText += "ref ";
			if (!(compact && isFunctionParamContext))
			{
				renderSymbol(v.typeSymbol, compact, false, {}, outText, outSpans);
				outText += " ";
			}
			const size_t start = outText.size();
			outText += v.name;
			appendSpan(symbol, outText, start, outSpans, slot);
			if (v.initialValue != nullptr)
			{
				outText += " = ";
				renderSymbol(v.initialValue, compact, false,
					SlotRef{ SlotRef::Kind::VariableDeclarationInitialValue, symbol, symbol->line }, outText, outSpans);
			}
			break;
		}
		case ST::FunctionDeclaration:
		{
			const DSLSymbol::FunctionDeclaration& f = std::get<DSLSymbol::FunctionDeclaration>(symbol->data);
			outText += "function ";
			size_t start = outText.size();
			outText += f.name;
			appendSpan(symbol, outText, start, outSpans, slot);
			outText += "(";
			for (size_t i = 0; i < f.parameterVarDeclarations.size(); ++i)
			{
				if (i > 0) outText += ", ";
				renderSymbol(f.parameterVarDeclarations[i], compact, /*isFunctionParamContext*/ true, {}, outText, outSpans);
			}
			outText += ")";
			if (!compact)
			{
				// Always a selectable span (Kind::FunctionReturnType), even when Void -- rendered blank in that
				// case, exactly like a blank statement placeholder, so a return type can be added (or changed)
				// at any time, on a brand-new function or a pre-existing one alike (there's no dedicated
				// return-type symbol; this always resolves back to the SAME FunctionDeclaration).
				const SlotRef returnSlot{ SlotRef::Kind::FunctionReturnType, symbol, symbol->line };
				if (f.returnType != DSLType::Void)
					outText += " -> ";
				start = outText.size();
				if (f.returnType != DSLType::Void)
					outText += dslTypeName(f.returnType);
				appendSpan(symbol, outText, start, outSpans, returnSlot);
			}
			break;
		}
		case ST::FunctionCall:
		{
			const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(symbol->data);
			if (call.receiver != nullptr)
			{
				renderSymbol(call.receiver, compact, false, {}, outText, outSpans);
				outText += ".";
			}
			const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(call.functionSymbol->data);
			size_t start = outText.size();
			outText += callee.name;
			appendSpan(symbol, outText, start, outSpans, slot); // span -> the call site, not the callee's own declaration
			outText += "(";
			for (size_t i = 0; i < call.arguments.size(); ++i)
			{
				if (i > 0) outText += ", ";
				const DSLSymbol::CallArgument& arg = call.arguments[i];
				if (arg.parameter != nullptr)
				{
					const DSLSymbol::VariableDeclaration& param = std::get<DSLSymbol::VariableDeclaration>(arg.parameter->data);
					if (param.isRef)
						outText += "ref ";
					if (!compact)
					{
						renderSymbol(param.typeSymbol, compact, false, {}, outText, outSpans);
						outText += " ";
					}
					// The parameter NAME is a real span onto the callee's own parameter declaration -- so
					// occurrence highlighting connects call sites with the declaration and every body usage,
					// and renaming here renames the parameter itself (same symbol; user-declared callees only,
					// see ScriptEditor::beginCompose's builtin guard). The " = " stays glue.
					const size_t nameStart = outText.size();
					outText += param.name;
					appendSpan(arg.parameter, outText, nameStart, outSpans);
					outText += " = ";
				}
				renderSymbol(arg.value, compact, false,
					SlotRef{ SlotRef::Kind::CallArgumentValue, symbol, symbol->line, static_cast<int>(i) }, outText, outSpans);
			}
			outText += ")";
			break;
		}
		case ST::FlowControl:
		{
			const DSLSymbol::FlowControl& fc = std::get<DSLSymbol::FlowControl>(symbol->data);
			if (fc.control == DSLFlowControl::For)
			{
				// Unlike every other FlowControl kind, "for" previously had no span of its own at all (only its
				// three clauses were selectable) -- now spanned like the rest, so it's a legitimate cursor stop
				// (needed so Backspace can target the for's own keyword, see ScriptEditor::handleKeyEvent).
				const size_t start = outText.size();
				outText += "for";
				appendSpan(symbol, outText, start, outSpans, slot);
				outText += " ";
				renderSymbol(fc.forLoopVar, compact, false, {}, outText, outSpans); // e.g. "int counter = 0"
				outText += ", ";
				renderSymbol(fc.forCondition, compact, false, {}, outText, outSpans);
				outText += ", ";
				renderSymbol(fc.forIncrement, compact, false, {}, outText, outSpans);
				break;
			}
			const size_t start = outText.size();
			outText += flowControlKeyword(fc.control);
			appendSpan(symbol, outText, start, outSpans, slot);
			if (fc.condition != nullptr)
			{
				outText += " ";
				renderSymbol(fc.condition, compact, false,
					SlotRef{ SlotRef::Kind::FlowControlCondition, symbol, symbol->line }, outText, outSpans);
			}
			break;
		}
		case ST::Expression:
		{
			const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(symbol->data);

			// "ref" prefixes an assignment into a ref-parameter's reference (e.g. `ref appliedForce = toApply`
			// inside the function that owns it) -- distinct from a `ref` argument at a CALL site, handled above.
			if (!e.operators.empty() && dslIsAssignOperator(e.operators[0])
				&& !e.operands.empty() && e.operands[0]->type == ST::VariableReference)
			{
				const DSLSymbol::VariableReference& leftRef = std::get<DSLSymbol::VariableReference>(e.operands[0]->data);
				if (std::get<DSLSymbol::VariableDeclaration>(leftRef.declaration->data).isRef)
					outText += "ref ";
			}

			if (e.grouped)
				outText += "("; // unspanned glue -- grouping edits happen by deleting terms, never on the parens themselves
			for (size_t i = 0; i < e.operands.size(); ++i)
			{
				if (i > 0)
				{
					outText += " ";
					const size_t start = outText.size();
					outText += operatorText(e.operators[i - 1]);
					// The INCOMING slot (how this whole chain is replaced/deleted as a unit -- LineHead for an
					// assignment statement, FlowControlCondition for an if/while condition, ...) rides on the
					// FIRST operator span only, preserving the statement-level select/delete behaviors keyed on
					// it; operatorIndex additionally marks every operator span as an in-place replace target.
					appendSpan(symbol, outText, start, outSpans, (i == 1) ? slot : SlotRef{}, static_cast<int>(i - 1));
					outText += " ";
				}
				// The assignment TARGET (operand 0 under an assign-class operator) is not a value slot -- it
				// can't be swapped for a literal/call; changing what an assignment targets means deleting the
				// statement and re-authoring it (the staged Reassign flow).
				const bool assignTarget = (i == 0 && !e.operators.empty() && dslIsAssignOperator(e.operators[0]));
				renderSymbol(e.operands[i], compact, false,
					assignTarget ? SlotRef{} : SlotRef{ SlotRef::Kind::ExpressionOperand, symbol, symbol->line, static_cast<int>(i) },
					outText, outSpans);
			}
			if (e.grouped)
			{
				// The closing paren is its own cursor stop, selecting the GROUP as one unit (see
				// SyntaxSpan::groupClose) -- typing ')' on the group's last operand steps the selection out
				// here, ready to continue the chain past the parens (ScriptEditor::handleKeyEvent).
				const size_t start = outText.size();
				outText += ")";
				outSpans.push_back(SyntaxSpan{ symbol, static_cast<int>(start), static_cast<int>(outText.size()), slot, -1, true });
			}
			break;
		}
		case ST::MemberAccess:
		{
			const DSLSymbol::MemberAccess& m = std::get<DSLSymbol::MemberAccess>(symbol->data);
			renderSymbol(m.receiver, compact, false, {}, outText, outSpans);
			outText += ".";
			const size_t start = outText.size();
			outText += m.memberName;
			appendSpan(symbol, outText, start, outSpans, slot);
			break;
		}
		case ST::Comment:
		{
			const size_t start = outText.size();
			outText += "# " + std::get<DSLSymbol::Comment>(symbol->data).text;
			appendSpan(symbol, outText, start, outSpans, slot);
			break;
		}
		case ST::Placeholder:
		{
			const DSLSymbol::Placeholder& p = std::get<DSLSymbol::Placeholder>(symbol->data);
			const size_t start = outText.size();
			// A statement placeholder renders as an empty span -- a blank line, like an empty line in any text
			// editor (Enter, not an ever-visible marker, is what creates one -- see ScriptEditor). A VALUE
			// placeholder still needs a visible marker since it sits inside an otherwise-real line of code.
			if (p.expectedType != DSLType::Void)
				outText += "<" + std::string(dslTypeName(p.expectedType)) + ">";
			appendSpan(symbol, outText, start, outSpans, slot);
			break;
		}
		}
	}

	void renderLine(DSLCodeLine& line, bool compact, std::string& outText, std::vector<SyntaxSpan>& outSpans)
	{
		if (line.head() != nullptr)
			renderSymbol(line.head(), compact, false, SlotRef{ SlotRef::Kind::LineHead, nullptr, &line }, outText, outSpans);
	}

	bool isElseContinuation(const DSLSymbol* head)
	{
		if (head == nullptr || head->type != ST::FlowControl)
			return false;
		const DSLFlowControl control = std::get<DSLSymbol::FlowControl>(head->data).control;
		return control == DSLFlowControl::ElseIf || control == DSLFlowControl::Else;
	}

	SyntaxLine makeEndLine(int scopeLevel, DSLSymbol* headerSymbol)
	{
		SyntaxLine endLine;
		endLine.scopeLevel = scopeLevel;
		endLine.endOfSymbol = headerSymbol;
		endLine.text = "end";
		endLine.spans.push_back(SyntaxSpan{ headerSymbol, 0, 3, {} });
		return endLine;
	}

	// ---- AutoCompleteRules helpers ----

	bool matchesPrefix(const std::string& label, const std::string& prefix)
	{
		if (prefix.size() > label.size())
			return false;
		for (size_t i = 0; i < prefix.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(label[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
				return false;
		return true;
	}

	void addIfMatches(std::vector<Candidate>& out, Candidate candidate, const std::string& typedPrefix)
	{
		if (matchesPrefix(candidate.label, typedPrefix))
			out.push_back(std::move(candidate));
	}

	bool equalsCaseInsensitive(const std::string& a, const std::string& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return true;
	}

	// Moves a candidate whose label matches `typedPrefix` EXACTLY (not just as a prefix) to the front of the
	// list, preserving every other candidate's relative order -- so finishing a name to an exact match (e.g.
	// typing "test" when both "test" and "test2" are in scope) always surfaces it first, ready to confirm,
	// instead of wherever it happened to fall out of the underlying scan order.
	void sortExactMatchFirst(std::vector<Candidate>& candidates, const std::string& typedPrefix)
	{
		if (typedPrefix.empty())
			return;
		const auto it = std::find_if(candidates.begin(), candidates.end(),
			[&](const Candidate& c) { return equalsCaseInsensitive(c.label, typedPrefix); });
		if (it != candidates.end() && it != candidates.begin())
			std::rotate(candidates.begin(), it, it + 1);
	}

	// Whether `text` reads as a real Int/Float literal -- so "bla" is never offered (or accepted) as a numeric
	// value just because a variable/function of that name doesn't happen to exist either. An optional leading
	// sign, digits, and (Float only) at most one '.' with at least one digit somewhere.
	bool looksLikeIntLiteral(const std::string& text)
	{
		if (text.empty())
			return false;
		size_t i = (text[0] == '-' || text[0] == '+') ? 1 : 0;
		if (i >= text.size())
			return false;
		for (; i < text.size(); ++i)
			if (!std::isdigit(static_cast<unsigned char>(text[i])))
				return false;
		return true;
	}

	bool looksLikeFloatLiteral(const std::string& text)
	{
		if (text.empty())
			return false;
		size_t i = (text[0] == '-' || text[0] == '+') ? 1 : 0;
		bool sawDigit = false, sawDot = false;
		for (; i < text.size(); ++i)
		{
			if (std::isdigit(static_cast<unsigned char>(text[i]))) { sawDigit = true; continue; }
			if (text[i] == '.' && !sawDot) { sawDot = true; continue; }
			return false;
		}
		return sawDigit;
	}

	bool isValidLiteralTextImpl(DSLType type, const std::string& text)
	{
		// A string literal is QUOTED, exactly once: `"..."` -- an unquoted word in a string slot names a
		// variable/function, never a literal (the quotes are what disambiguate `s2 = s1` from `s2 = "s1"`).
		if (type == DSLType::String)
			return text.size() >= 2 && text.front() == '"' && text.back() == '"'
				&& text.find('"', 1) == text.size() - 1;
		if (type == DSLType::Int)
			return looksLikeIntLiteral(text);
		if (type == DSLType::Float)
			return looksLikeFloatLiteral(text);
		return false;
	}

	// Every VariableDeclaration reachable from `atLine`: sidebar bindings, this function's own parameters, and
	// locals declared earlier in the SAME function (walking file.lines backward to the enclosing
	// FunctionDeclaration header, whose own parameters are picked up there too). Cruder than real block scoping
	// (doesn't exclude sibling if/else branches) but enough for M3.
	std::vector<DSLSymbol*> inScopeVariables(const DSLCodeLine& atLine, const DSLScriptFile& file, const std::vector<std::unique_ptr<DSLSymbol>>& sidebar)
	{
		std::vector<DSLSymbol*> result;
		for (const std::unique_ptr<DSLSymbol>& s : sidebar)
			if (s->type == ST::VariableDeclaration)
				result.push_back(s.get());

		for (int i = dslLineIndex(file, &atLine); i >= 0; --i)
		{
			const DSLCodeLine& line = *file.lines[i];
			for (const std::unique_ptr<DSLSymbol>& s : line.symbols)
				if (s->type == ST::VariableDeclaration)
					result.push_back(s.get());
			if (line.head() != nullptr && line.head()->type == ST::FunctionDeclaration)
				break; // reached this function's own header (its params were just picked up above) -- stop
		}
		return result;
	}

	// Free-callable functions only: receiver-based builtins (physics.getMass(), world.rayCast(...) --
	// FunctionDeclaration::requiresReceiver) are excluded, since M3's Function candidate always inserts a
	// receiver-less call (see ScriptEditor::applyCandidate) -- offering one here would silently produce a
	// wrong, receiver-less call to a method that needs one. M5 formalizes inserting dot-calls properly.
	std::vector<DSLSymbol*> knownFunctions(const DSLScriptFile& file, const std::vector<std::unique_ptr<DSLSymbol>>& builtins)
	{
		std::vector<DSLSymbol*> result;
		for (const std::unique_ptr<DSLSymbol>& s : builtins)
			if (s->type == ST::FunctionDeclaration && !std::get<DSLSymbol::FunctionDeclaration>(s->data).requiresReceiver)
				result.push_back(s.get());
		for (const std::unique_ptr<DSLCodeLine>& line : file.lines)
			if (line->head() != nullptr && line->head()->type == ST::FunctionDeclaration)
				result.push_back(line->head());
		return result;
	}

	// The two candidate-list builders every "offer values here" branch shares -- one construction point for
	// Variable/Reassign/Function candidates instead of a copy of these loops per calling context. `accept`
	// filters by the variable's declared type / the function's return type (always-true lambda = "any").
	void addVariableCandidates(std::vector<Candidate>& out, const DSLCodeLine& atLine, const DSLScriptFile& file,
		const std::vector<std::unique_ptr<DSLSymbol>>& sidebar, const std::string& typedPrefix,
		Candidate::Kind kind, DSLSymbol* excludeVariable, auto&& accept)
	{
		for (DSLSymbol* var : inScopeVariables(atLine, file, sidebar))
		{
			if (var == excludeVariable)
				continue;
			const DSLSymbol::VariableDeclaration& v = std::get<DSLSymbol::VariableDeclaration>(var->data);
			const DSLType varType = std::get<DSLSymbol::TypeDeclaration>(v.typeSymbol->data).type;
			// Engine-object bindings (self/physics/...) are never plain values or assignment targets --
			// they're offered as Kind::BindingObject dot-into candidates instead (ScriptEditor appends those,
			// gated on the script's required-components set).
			if (dslIsEngineObjectType(varType))
				continue;
			if (!accept(varType))
				continue;
			Candidate c;
			c.label = v.name;
			c.kind = kind;
			c.refSymbol = var;
			addIfMatches(out, c, typedPrefix);
		}
	}

	// `includeParameterized` is false only where a call's arguments can't be staged before it lands -- the
	// editor stages arguments for call STATEMENTS and for call VALUES inside chain composes (ScriptEditor's
	// CallArgValue flow), but not, e.g., for another call's own argument slot (no nested staging), and
	// placeholder arguments never land in the document.
	void addFunctionCandidates(std::vector<Candidate>& out, const DSLScriptFile& file,
		const std::vector<std::unique_ptr<DSLSymbol>>& builtins, const std::string& typedPrefix,
		bool includeParameterized, auto&& accept)
	{
		for (DSLSymbol* func : knownFunctions(file, builtins))
		{
			const DSLSymbol::FunctionDeclaration& f = std::get<DSLSymbol::FunctionDeclaration>(func->data);
			if (!accept(f.returnType))
				continue;
			if (!includeParameterized && !f.parameterVarDeclarations.empty())
				continue;
			Candidate c;
			c.label = f.name;
			c.kind = Candidate::Kind::Function;
			c.refSymbol = func;
			addIfMatches(out, c, typedPrefix);
		}
	}
}

const char* dslOperatorText(DSLOperator op)
{
	return operatorText(op);
}

bool Syntax::isBlockOpener(const DSLSymbol* head)
{
	using ST = DSLSymbol::SymbolType;

	if (head == nullptr)
		return false;
	if (head->type == ST::FunctionDeclaration)
		return true;
	if (head->type == ST::FlowControl)
	{
		const DSLFlowControl control = std::get<DSLSymbol::FlowControl>(head->data).control;
		return control == DSLFlowControl::If || control == DSLFlowControl::ElseIf
			|| control == DSLFlowControl::Else || control == DSLFlowControl::While
			|| control == DSLFlowControl::For;
	}
	return false;
}

std::vector<SyntaxLine> Syntax::format(DSLScriptFile& file, bool compact)
{
	std::vector<SyntaxLine> out;

	struct OpenBlock { int scopeLevel; DSLSymbol* headerSymbol; };
	std::vector<OpenBlock> stack;

	for (std::unique_ptr<DSLCodeLine>& linePtr : file.lines)
	{
		DSLCodeLine& line = *linePtr;
		DSLSymbol* head = line.head();
		const bool continuation = isElseContinuation(head);

		// Close every open block at least as deep as this line, EXCEPT the one this line continues (an
		// ElseIf/Else continues the innermost open If at the SAME scopeLevel rather than closing it).
		while (!stack.empty() && stack.back().scopeLevel >= line.scopeLevel)
		{
			if (continuation && stack.back().scopeLevel == line.scopeLevel)
				break;
			out.push_back(makeEndLine(stack.back().scopeLevel, stack.back().headerSymbol));
			stack.pop_back();
		}

		SyntaxLine syntaxLine;
		syntaxLine.sourceLine = &line;
		syntaxLine.scopeLevel = line.scopeLevel;
		renderLine(line, compact, syntaxLine.text, syntaxLine.spans);
		out.push_back(std::move(syntaxLine));

		if (isBlockOpener(head) && !continuation)
			stack.push_back({ line.scopeLevel, head });
	}

	while (!stack.empty())
	{
		out.push_back(makeEndLine(stack.back().scopeLevel, stack.back().headerSymbol));
		stack.pop_back();
	}

	return out;
}

std::vector<Candidate> AutoCompleteRules::candidatesFor(DSLType expectedType, const DSLCodeLine& atLine, const DSLScriptFile& file,
	const std::vector<std::unique_ptr<DSLSymbol>>& sidebar, const std::vector<std::unique_ptr<DSLSymbol>>& builtins,
	const std::string& typedPrefix, DSLSymbol* excludeVariable, bool offerComparisonLeads)
{
	std::vector<Candidate> out;

	if (expectedType == DSLType::Void)
	{
		// Statement slot: control-flow keywords, "declare a new variable of type X" per known type, and
		// in-scope function names (a free call statement).
		addIfMatches(out, Candidate{ "if", Candidate::Kind::KeywordIf }, typedPrefix);
		addIfMatches(out, Candidate{ "while", Candidate::Kind::KeywordWhile }, typedPrefix);
		addIfMatches(out, Candidate{ "for", Candidate::Kind::KeywordFor }, typedPrefix);
		addIfMatches(out, Candidate{ "return", Candidate::Kind::KeywordReturn }, typedPrefix);
		addIfMatches(out, Candidate{ "break", Candidate::Kind::KeywordBreak }, typedPrefix);

		// else/elseif: only meaningful on a statement INSIDE an if/elseif branch, and only when it's the LAST
		// line of that branch -- statements below would silently stay in the current branch while the new one
		// opens after them, which never reads as intended. Confirming grows the chain with a new branch (see
		// ScriptEditor's KeywordElse/ElseIf handling); `else` additionally needs the chain to not already end
		// in one (a second else has no place to go).
		const int lineIndex = dslLineIndex(file, &atLine);
		int headerIndex = (lineIndex >= 0 && atLine.scopeLevel > 0) ? dslEnclosingBlockHeader(file, lineIndex) : -1;
		if (headerIndex >= 0 && lineIndex != dslBlockEnd(file, headerIndex) - 1)
			headerIndex = -1; // not the branch's last line -- something below would be stranded
		const DSLSymbol* branchHead = (headerIndex >= 0) ? file.lines[headerIndex]->head() : nullptr;
		if (branchHead != nullptr && branchHead->type == ST::FlowControl)
		{
			const DSLFlowControl branchControl = std::get<DSLSymbol::FlowControl>(branchHead->data).control;
			if (branchControl == DSLFlowControl::If || branchControl == DSLFlowControl::ElseIf)
			{
				addIfMatches(out, Candidate{ "elseif", Candidate::Kind::KeywordElseIf }, typedPrefix);
				bool chainHasElse = false;
				for (int i = headerIndex; !chainHasElse; )
				{
					const DSLFlowControl c = std::get<DSLSymbol::FlowControl>(file.lines[i]->head()->data).control;
					if (c == DSLFlowControl::Else)
						chainHasElse = true;
					const int next = dslBlockEnd(file, i);
					if (chainHasElse || next >= static_cast<int>(file.lines.size())
						|| file.lines[next]->scopeLevel != file.lines[headerIndex]->scopeLevel)
						break;
					const DSLSymbol* nextHead = file.lines[next]->head();
					if (nextHead == nullptr || nextHead->type != ST::FlowControl)
						break;
					const DSLFlowControl nc = std::get<DSLSymbol::FlowControl>(nextHead->data).control;
					if (nc != DSLFlowControl::ElseIf && nc != DSLFlowControl::Else)
						break;
					i = next;
				}
				if (!chainHasElse)
					addIfMatches(out, Candidate{ "else", Candidate::Kind::KeywordElse }, typedPrefix);
			}
		}

		// Declaring a new function is only well-formed at the top level (this DSL has no nested functions) --
		// atLine.scopeLevel == 0 is exactly "not inside any function body" (every scope-1+ line lives inside
		// some function's body, per the grammar; see DSL.ixx).
		if (atLine.scopeLevel == 0)
			addIfMatches(out, Candidate{ "function", Candidate::Kind::DeclareFunction }, typedPrefix);

		for (const Candidate& typeCandidate : typeKeywordCandidates(typedPrefix))
			out.push_back(typeCandidate);

		// Reassigning an EXISTING in-scope variable is also a statement candidate -- typing its name offers
		// "assign into it", the same way typing a function's name offers "call it" (any return type: a call
		// statement is free to ignore the result).
		const auto anyType = [](DSLType) { return true; };
		addVariableCandidates(out, atLine, file, sidebar, typedPrefix, Candidate::Kind::Reassign, nullptr, anyType);
		addFunctionCandidates(out, file, builtins, typedPrefix, /*includeParameterized*/ true, anyType);
		sortExactMatchFirst(out, typedPrefix);
		return out;
	}

	// Value slot of type `expectedType`.
	if (expectedType == DSLType::Bool)
	{
		addIfMatches(out, Candidate{ "true", Candidate::Kind::KeywordTrue }, typedPrefix);
		addIfMatches(out, Candidate{ "false", Candidate::Kind::KeywordFalse }, typedPrefix);
		if (offerComparisonLeads)
		{
			// Numeric leads for "bool b = i < 5" -- consumable only by a typed comparator (see the parameter
			// comment); no overlap with the Bool-typed candidates added below.
			const auto numeric = [](DSLType t) { return t == DSLType::Int || t == DSLType::Float; };
			addVariableCandidates(out, atLine, file, sidebar, typedPrefix, Candidate::Kind::Variable, excludeVariable, numeric);
			addFunctionCandidates(out, file, builtins, typedPrefix, /*includeParameterized*/ false, numeric);
		}
	}

	const auto matchesExpected = [&](DSLType type) { return type == expectedType; };
	addVariableCandidates(out, atLine, file, sidebar, typedPrefix, Candidate::Kind::Variable, excludeVariable, matchesExpected);
	addFunctionCandidates(out, file, builtins, typedPrefix, /*includeParameterized*/ true, matchesExpected);

	// Struct-typed variables that DON'T match the slot are still offered as dot-into WAYPOINTS ("test" in a
	// Float slot, toward "test.x") -- Kind::BindingObject, consumable only by '.'/confirm into MemberSelect,
	// never as the bare value (matching ones already appeared as plain Variable candidates above).
	addVariableCandidates(out, atLine, file, sidebar, typedPrefix, Candidate::Kind::BindingObject, excludeVariable,
		[&](DSLType type) { return dslIsStructType(type) && type != expectedType; });

	// Free-typed literal entry: only offered once what's typed actually reads as a value of the expected type
	// (any text is valid String content; Int/Float need looksLike*Literal to hold) -- "bla" must never become
	// a confirmable Float/Int constant just because no variable/function happens to be named that either.
	if (!typedPrefix.empty() && isValidLiteralTextImpl(expectedType, typedPrefix))
	{
		Candidate c;
		c.label = typedPrefix;
		c.kind = Candidate::Kind::Literal;
		c.declareType = expectedType;
		out.push_back(c);
	}

	sortExactMatchFirst(out, typedPrefix);
	return out;
}

bool AutoCompleteRules::isValidLiteralText(DSLType type, const std::string& text)
{
	return isValidLiteralTextImpl(type, text);
}

std::vector<Candidate> AutoCompleteRules::typeKeywordCandidates(const std::string& typedPrefix)
{
	std::vector<Candidate> out;
	auto add = [&](DSLType t)
	{
		Candidate c;
		c.label = dslTypeName(t);
		c.kind = Candidate::Kind::DeclareType;
		c.declareType = t;
		addIfMatches(out, c, typedPrefix);
	};
	for (DSLType t : { DSLType::Int, DSLType::Float, DSLType::Bool, DSLType::String })
		add(t);
	for (size_t i = 0; i < Globals::scriptBindings.structs().size(); ++i)
		add(dslStructType(static_cast<int>(i))); // every engine-defined struct is a declarable type
	sortExactMatchFirst(out, typedPrefix);
	return out;
}

bool AutoCompleteRules::isFunctionNameTaken(const std::string& name, const DSLScriptFile& file, const std::vector<std::unique_ptr<DSLSymbol>>& builtins,
	DSLSymbol* excludeFunction)
{
	using ST = DSLSymbol::SymbolType;

	for (const std::unique_ptr<DSLSymbol>& s : builtins)
		if (s->type == ST::FunctionDeclaration && std::get<DSLSymbol::FunctionDeclaration>(s->data).name == name)
			return true;
	for (const std::unique_ptr<DSLCodeLine>& line : file.lines)
		if (line->head() != nullptr && line->head() != excludeFunction && line->head()->type == ST::FunctionDeclaration
			&& std::get<DSLSymbol::FunctionDeclaration>(line->head()->data).name == name)
			return true;
	return false;
}

bool AutoCompleteRules::isVariableReferenced(const DSLSymbol* varDecl, const DSLScriptFile& file)
{
	using ST = DSLSymbol::SymbolType;

	if (varDecl == nullptr || varDecl->line == nullptr)
		return false;

	const int headerIndex = dslEnclosingFunctionHeader(file, dslLineIndex(file, varDecl->line));
	if (headerIndex < 0)
		return false;

	// Every symbol anywhere in the function's body is a peer in its owning line's flat `symbols` list (see
	// DSL.ixx's ownership model) -- no recursive tree-walk needed to find nested VariableReferences.
	const int blockEnd = dslBlockEnd(file, headerIndex);
	for (int i = headerIndex + 1; i < blockEnd; ++i)
		for (const std::unique_ptr<DSLSymbol>& s : file.lines[i]->symbols)
			if (s->type == ST::VariableReference && std::get<DSLSymbol::VariableReference>(s->data).declaration == varDecl)
				return true;

	return false;
}

bool AutoCompleteRules::isFunctionReferenced(const DSLSymbol* funcDecl, const DSLScriptFile& file)
{
	using ST = DSLSymbol::SymbolType;

	for (const std::unique_ptr<DSLCodeLine>& line : file.lines)
		for (const std::unique_ptr<DSLSymbol>& s : line->symbols)
			if (s->type == ST::FunctionCall && std::get<DSLSymbol::FunctionCall>(s->data).functionSymbol == funcDecl)
				return true;
	return false;
}

std::vector<Candidate> AutoCompleteRules::candidatesForAnyValue(const DSLCodeLine& atLine, const DSLScriptFile& file,
	const std::vector<std::unique_ptr<DSLSymbol>>& sidebar, const std::vector<std::unique_ptr<DSLSymbol>>& builtins,
	const std::string& typedPrefix, DSLSymbol* excludeVariable)
{
	std::vector<Candidate> out;
	addVariableCandidates(out, atLine, file, sidebar, typedPrefix, Candidate::Kind::Variable, excludeVariable, [](DSLType) { return true; });
	// A statement-only (Void-returning) call has nothing to compare, so those are excluded here.
	addFunctionCandidates(out, file, builtins, typedPrefix, /*includeParameterized*/ true, [](DSLType type) { return type != DSLType::Void; });
	sortExactMatchFirst(out, typedPrefix);
	return out;
}

namespace
{
	// The one construction path every fixed operator list shares -- labels always come from operatorText, so
	// candidate labels can never drift from how the operator renders.
	std::vector<Candidate> operatorCandidates(std::initializer_list<DSLOperator> operators, Candidate::Kind kind, const std::string& typedPrefix)
	{
		std::vector<Candidate> out;
		for (DSLOperator op : operators)
		{
			Candidate c;
			c.label = operatorText(op);
			c.kind = kind;
			c.op = op;
			addIfMatches(out, c, typedPrefix);
		}
		sortExactMatchFirst(out, typedPrefix);
		return out;
	}
}

std::vector<Candidate> AutoCompleteRules::receiverCandidates(const ScriptBindings& bindings, const DSL& document, DSLSymbol* receiverDecl,
	DSLType receiverType, DSLType expectedType, bool anyValue, const std::string& typedPrefix)
{
	std::vector<Candidate> out;

	// A receiver is a binding OBJECT (physics/self) or any STRUCT-typed value -- same candidate shapes either
	// way, sourced from the matching registry side.
	const std::vector<BindingFunc>* functions = nullptr;
	const std::vector<BindingMember>* members = nullptr;
	std::span<DSLSymbol* const> functionSymbols;
	if (const BindingObject* object = bindings.objectFor(receiverType); object != nullptr)
	{
		functions = &object->functions;
		members = &object->members;
		functionSymbols = bindings.functionSymbols(*object);
	}
	else if (const BindingStruct* structDef = bindings.structFor(receiverType); structDef != nullptr)
	{
		functions = &structDef->functions;
		members = &structDef->members;
		functionSymbols = bindings.structFunctionSymbols(receiverType);
	}
	else
		return out;

	const bool statementContext = expectedType == DSLType::Void && !anyValue;
	for (size_t i = 0; i < functions->size(); ++i)
	{
		const DSLType returnType = (*functions)[i].returnType;
		const bool accepted = statementContext
			|| (anyValue ? returnType != DSLType::Void : returnType == expectedType);
		if (!accepted)
			continue;
		Candidate c;
		c.label = (*functions)[i].name;
		c.kind = Candidate::Kind::Function;
		c.refSymbol = functionSymbols[i];
		c.receiver = receiverDecl;
		addIfMatches(out, c, typedPrefix);
	}
	for (const BindingMember& member : *members)
	{
		if (member.requiredComponent != DSLComponentKind::None && !dslIsComponentRequired(document, member.requiredComponent))
			continue;
		// Value contexts type-filter (chainable members always pass, as dot-into waypoints toward a matching
		// leaf); a STATEMENT context offers WRITABLE members (the lead-in of a member-assignment, `v.x = ...`)
		// plus chainable-but-unwritable ones (`self.physics`, a waypoint toward a Void-returning dot-call).
		const bool accepted = statementContext ? (member.writable || dslIsChainableType(member.type))
			: (anyValue || member.type == expectedType || dslIsChainableType(member.type));
		if (!accepted)
			continue;
		Candidate c;
		c.label = member.name;
		c.kind = Candidate::Kind::Member;
		c.refSymbol = receiverDecl; // the chain's ROOT -- the member itself has no symbol, only registry data
		c.declareType = member.type;
		c.memberWritable = member.writable;
		addIfMatches(out, c, typedPrefix);
	}

	sortExactMatchFirst(out, typedPrefix);
	return out;
}

std::vector<Candidate> AutoCompleteRules::comparisonOperatorCandidates(const std::string& typedPrefix)
{
	return operatorCandidates({ DSLOperator::Equal, DSLOperator::NotEqual, DSLOperator::LessThanOrEqual,
		DSLOperator::GreaterThanOrEqual, DSLOperator::LessThan, DSLOperator::GreaterThan },
		Candidate::Kind::Comparator, typedPrefix);
}

std::vector<Candidate> AutoCompleteRules::compoundAssignOperatorCandidates(const std::string& typedPrefix)
{
	return operatorCandidates({ DSLOperator::AssignAdd, DSLOperator::AssignSubtract, DSLOperator::AssignMultiply,
		DSLOperator::AssignDivide, DSLOperator::AssignModulus },
		Candidate::Kind::AssignOperator, typedPrefix);
}

std::vector<Candidate> AutoCompleteRules::arithmeticOperatorCandidates(const std::string& typedPrefix)
{
	return operatorCandidates({ DSLOperator::Add, DSLOperator::Subtract, DSLOperator::Multiply,
		DSLOperator::Divide, DSLOperator::Modulus },
		Candidate::Kind::ArithmeticOperator, typedPrefix);
}

std::vector<Candidate> AutoCompleteRules::logicalOperatorCandidates(const std::string& typedPrefix)
{
	return operatorCandidates({ DSLOperator::And, DSLOperator::Or }, Candidate::Kind::LogicalOperator, typedPrefix);
}

std::vector<Candidate> AutoCompleteRules::assignOperatorCandidates(const std::string& typedPrefix)
{
	return operatorCandidates({ DSLOperator::Assign, DSLOperator::AssignAdd, DSLOperator::AssignSubtract,
		DSLOperator::AssignMultiply, DSLOperator::AssignDivide, DSLOperator::AssignModulus },
		Candidate::Kind::AssignOperator, typedPrefix);
}

DSLType AutoCompleteRules::enclosingFunctionReturnType(const DSLCodeLine& atLine, const DSLScriptFile& file)
{
	const int headerIndex = dslEnclosingFunctionHeader(file, dslLineIndex(file, &atLine));
	if (headerIndex < 0)
		return DSLType::Void;
	return std::get<DSLSymbol::FunctionDeclaration>(file.lines[headerIndex]->head()->data).returnType;
}

DSLType AutoCompleteRules::expectedTypeForSlot(const SlotRef& slot, const DSLScriptFile& file)
{
	using ST = DSLSymbol::SymbolType;

	switch (slot.kind)
	{
	case SlotRef::Kind::LineHead:
		return DSLType::Void;

	case SlotRef::Kind::FlowControlCondition:
	{
		const DSLSymbol::FlowControl& fc = std::get<DSLSymbol::FlowControl>(slot.parent->data);
		if (fc.control == DSLFlowControl::Return)
			return slot.line != nullptr ? enclosingFunctionReturnType(*slot.line, file) : DSLType::Void;
		return DSLType::Bool; // If/ElseIf/While
	}

	case SlotRef::Kind::CallArgumentValue:
	{
		const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(slot.parent->data);
		if (slot.argIndex < 0 || slot.argIndex >= static_cast<int>(call.arguments.size()))
			return DSLType::Void;
		const DSLSymbol* paramSym = call.arguments[slot.argIndex].parameter;
		if (paramSym == nullptr)
		{
			// Positional argument (vec3(0, 1, 0), print(...)) -- fall back to the CALLEE's parameter list by
			// index, so e.g. a vector literal's components still edit as the Floats they are.
			const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(call.functionSymbol->data);
			if (slot.argIndex >= static_cast<int>(callee.parameterVarDeclarations.size()))
				return DSLType::Void; // vararg-style builtin (print) -- no declared parameter to derive from
			paramSym = callee.parameterVarDeclarations[slot.argIndex];
		}
		const DSLSymbol::VariableDeclaration& param = std::get<DSLSymbol::VariableDeclaration>(paramSym->data);
		return std::get<DSLSymbol::TypeDeclaration>(param.typeSymbol->data).type;
	}

	case SlotRef::Kind::VariableDeclarationInitialValue:
	{
		const DSLSymbol::VariableDeclaration& v = std::get<DSLSymbol::VariableDeclaration>(slot.parent->data);
		return std::get<DSLSymbol::TypeDeclaration>(v.typeSymbol->data).type;
	}

	case SlotRef::Kind::ExpressionOperand:
	{
		// The chain's element type: for an assignment every operand matches the TARGET's own type; for a
		// comparison or arithmetic chain, whatever any OTHER operand resolves to (all operands of one chain
		// share a type by construction -- skipping the operand being edited keeps its current occupant from
		// deciding its own replacement's type).
		const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(slot.parent->data);
		for (size_t i = 0; i < e.operands.size(); ++i)
		{
			if (static_cast<int>(i) == slot.argIndex)
				continue;
			if (const DSLType t = dslValueType(e.operands[i]); t != DSLType::Void)
				return t;
		}
		return dslValueType(slot.parent); // last resort -- includes the edited operand itself
	}

	default:
		return DSLType::Void;
	}
}

bool AutoCompleteRules::isNameInScope(const std::string& name, const DSLCodeLine& atLine, const DSLScriptFile& file,
	const std::vector<std::unique_ptr<DSLSymbol>>& sidebar, DSLSymbol* excludeVariable)
{
	using ST = DSLSymbol::SymbolType;

	// Sidebar bindings are visible everywhere.
	for (const std::unique_ptr<DSLSymbol>& s : sidebar)
		if (s->type == ST::VariableDeclaration && s.get() != excludeVariable
			&& std::get<DSLSymbol::VariableDeclaration>(s->data).name == name)
			return true;

	const int headerIndex = dslEnclosingFunctionHeader(file, dslLineIndex(file, &atLine));
	if (headerIndex < 0)
		return false;

	const DSLSymbol::FunctionDeclaration& header = std::get<DSLSymbol::FunctionDeclaration>(file.lines[headerIndex]->head()->data);
	for (DSLSymbol* param : header.parameterVarDeclarations)
		if (param != excludeVariable && std::get<DSLSymbol::VariableDeclaration>(param->data).name == name)
			return true;

	// Scan EVERY line belonging to this function -- deliberately not direction- or nesting-sensitive: a name
	// collides whether the other declaration comes before or after this point, and whether it sits at a
	// shallower or deeper scopeLevel (renaming an outer/earlier variable to match one declared later inside a
	// nested block must be blocked too, not just the reverse).
	const int blockEnd = dslBlockEnd(file, headerIndex);
	for (int i = headerIndex + 1; i < blockEnd; ++i)
		for (const std::unique_ptr<DSLSymbol>& s : file.lines[i]->symbols)
			if (s->type == ST::VariableDeclaration && s.get() != excludeVariable
				&& std::get<DSLSymbol::VariableDeclaration>(s->data).name == name)
				return true;

	return false;
}
