module UI;

import Core;
import :DSL;
import :ScriptBindings;
import :ScriptLang; // dslOperatorText -- the one shared operator spelling
import :Transpiler;

namespace
{
	using ST = DSLSymbol::SymbolType;

	const char* cppTypeName(DSLType type)
	{
		// Engine-defined structs carry their own C++ spelling in the registry.
		if (const BindingStruct* structDef = Globals::scriptBindings.structFor(type); structDef != nullptr)
			return structDef->cppName;
		switch (type)
		{
		case DSLType::Void:    return "void";
		case DSLType::Int:     return "int";
		case DSLType::Float:   return "float";
		case DSLType::Bool:    return "bool";
		case DSLType::String:  return "const char*";
		default:               return "/* unsupported DSLType */ void*"; // engine-object kinds never declare values
		}
	}

	// "physics" -> "ScriptPhysics": the generated class member's wrapper type (defined by the engine-hookup
	// step's shared script header, assumed here).
	std::string wrapperTypeName(const char* objectName)
	{
		std::string name = objectName;
		if (!name.empty())
			name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
		return "Script" + name;
	}

	const DSLSymbol::VariableDeclaration& declOf(const DSLSymbol* varDecl)
	{
		return std::get<DSLSymbol::VariableDeclaration>(varDecl->data);
	}

	DSLType declaredType(const DSLSymbol* varDecl)
	{
		return std::get<DSLSymbol::TypeDeclaration>(declOf(varDecl).typeSymbol->data).type;
	}

	struct Emitter
	{
		const DSL& document;
		const ScriptBindings& bindings;
		std::string out;
		int indent = 0;

		void emitLine(const std::string& text)
		{
			if (!text.empty())
				out.append(static_cast<size_t>(indent), '\t');
			out += text;
			out += '\n';
		}

		// ---- expressions ----

		// A Float constant needs a real C++ float literal: "1" -> "1.0f", "0.5" -> "0.5f", "1." -> "1.0f".
		static std::string floatLiteral(const std::string& value)
		{
			std::string text = value;
			if (text.find('.') == std::string::npos)
				text += ".0";
			else if (text.back() == '.')
				text += '0';
			return text + "f";
		}

		static std::string constantText(const DSLSymbol::Constant& c)
		{
			switch (c.type)
			{
			case DSLType::Float:  return floatLiteral(c.value);
			case DSLType::String: return "\"" + c.value + "\""; // content stored unquoted, no escapes in the DSL
			default:              return c.value;               // Int/Bool exactly as authored
			}
		}

		// "$r" -> the receiver's emitted expression, "$1".."$9" -> the argument in that CALLEE-parameter position.
		static std::string substituteTemplate(const char* emitTemplate, const std::string& receiverName,
			const std::vector<std::string>& args)
		{
			std::string result;
			for (const char* p = emitTemplate; *p != '\0'; ++p)
			{
				if (*p == '$' && p[1] == 'r')
				{
					result += receiverName;
					++p;
					continue;
				}
				if (*p == '$' && p[1] >= '1' && p[1] <= '9')
				{
					const size_t index = static_cast<size_t>(p[1] - '1');
					if (index < args.size())
						result += args[index];
					++p;
					continue;
				}
				result += *p;
			}
			return result;
		}

		std::string callText(const DSLSymbol* callSymbol)
		{
			const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(callSymbol->data);
			const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(call.functionSymbol->data);

			// Argument expressions in the CALLEE's parameter order -- named arguments reorder to their
			// parameter's position, positional ones keep their index (the DSL's order-independent named
			// matching becomes plain positional C++ here).
			std::vector<std::string> args(std::max(call.arguments.size(), callee.parameterVarDeclarations.size()));
			for (size_t i = 0; i < call.arguments.size(); ++i)
			{
				size_t index = i;
				if (call.arguments[i].parameter != nullptr)
					for (size_t p = 0; p < callee.parameterVarDeclarations.size(); ++p)
						if (callee.parameterVarDeclarations[p] == call.arguments[i].parameter)
						{
							index = p;
							break;
						}
				if (index < args.size())
					args[index] = expressionText(call.arguments[i].value);
			}

			if (const char* emitTemplate = bindings.emitFor(call.functionSymbol); emitTemplate != nullptr)
			{
				// Vararg builtins (print: no declared parameters) pass every argument where "$1" sits.
				if (callee.parameterVarDeclarations.empty() && args.size() > 1)
				{
					std::string joined;
					for (size_t i = 0; i < args.size(); ++i)
					{
						if (i > 0)
							joined += ", ";
						joined += args[i];
					}
					args.assign(1, std::move(joined));
				}
				// The receiver is its own emitted EXPRESSION (a class member like "physics", or a chained
				// member access like "self.pos"), so emit templates compose textually.
				const std::string receiverName = (call.receiver != nullptr) ? expressionText(call.receiver) : std::string();
				return substituteTemplate(emitTemplate, receiverName, args);
			}

			// A user function: a plain method call on the generated class (receivers never occur).
			std::string text = callee.name + "(";
			for (size_t i = 0; i < args.size(); ++i)
			{
				if (i > 0)
					text += ", ";
				text += args[i];
			}
			return text + ")";
		}

		std::string memberText(const DSLSymbol* symbol)
		{
			const DSLSymbol::MemberAccess& m = std::get<DSLSymbol::MemberAccess>(symbol->data);
			const std::string receiverText = expressionText(m.receiver); // recursion makes chains compose ("self.pos" -> ".x")
			if (const BindingMember* member = bindings.findMember(dslValueType(m.receiver), m.memberName); member != nullptr)
				return substituteTemplate(member->emit, receiverText, {});
			return receiverText + "." + m.memberName; // defensive -- authored/loaded members always exist in the registry
		}

		std::string expressionText(const DSLSymbol* symbol)
		{
			if (symbol == nullptr)
				return "0";
			switch (symbol->type)
			{
			case ST::Constant:
				return constantText(std::get<DSLSymbol::Constant>(symbol->data));
			case ST::VariableReference:
				return declOf(std::get<DSLSymbol::VariableReference>(symbol->data).declaration).name;
			case ST::FunctionCall:
				return callText(symbol);
			case ST::MemberAccess:
				return memberText(symbol);
			case ST::Expression:
			{
				// FLAT, exactly as authored: C++'s own precedence supplies the "*, / over +, -" the DSL defers
				// to emit time (DSL.ixx); parens appear only where the author grouped.
				const DSLSymbol::Expression& e = std::get<DSLSymbol::Expression>(symbol->data);
				std::string text;
				for (size_t i = 0; i < e.operands.size(); ++i)
				{
					if (i > 0)
					{
						text += ' ';
						text += dslOperatorText(e.operators[i - 1]);
						text += ' ';
					}
					text += expressionText(e.operands[i]);
				}
				return e.grouped ? "(" + text + ")" : text;
			}
			case ST::Placeholder:
			{
				// Committed documents never hold value placeholders (the editor's no-placeholder rule) -- emit
				// a type default anyway rather than broken text if one ever slips through.
				const DSLType expected = std::get<DSLSymbol::Placeholder>(symbol->data).expectedType;
				if (const BindingStruct* structDef = bindings.structFor(expected); structDef != nullptr)
					return std::string(structDef->cppName) + "()";
				switch (expected)
				{
				case DSLType::Float:  return "0.0f";
				case DSLType::Bool:   return "false";
				case DSLType::String: return "\"\"";
				default:              return "0";
				}
			}
			default:
				return "0"; // no other symbol kind is a value
			}
		}

		// "float applied = 0.0f" (no trailing ';') -- local declarations and the for-loop's first clause.
		std::string declarationText(const DSLSymbol* declSymbol)
		{
			const DSLSymbol::VariableDeclaration& v = declOf(declSymbol);
			std::string text = std::string(cppTypeName(declaredType(declSymbol))) + " " + v.name;
			if (v.initialValue != nullptr)
				text += " = " + expressionText(v.initialValue);
			return text;
		}

		// ---- statements / blocks ----

		void openBlock()
		{
			emitLine("{");
			++indent;
		}

		void closeBlock()
		{
			--indent;
			emitLine("}");
		}

		// One function: header line + every body line up to (not including) `bodyEnd`. Blocks mirror
		// Syntax::format's scopeLevel derivation -- an elseif/else continues its chain's open block.
		void emitFunction(int headerIndex, int bodyEnd)
		{
			const DSLSymbol::FunctionDeclaration& func =
				std::get<DSLSymbol::FunctionDeclaration>(document.file.lines[headerIndex]->head()->data);

			std::string signature = std::string(cppTypeName(func.returnType)) + " " + func.name + "(";
			for (size_t i = 0; i < func.parameterVarDeclarations.size(); ++i)
			{
				if (i > 0)
					signature += ", ";
				const DSLSymbol::VariableDeclaration& param = declOf(func.parameterVarDeclarations[i]);
				signature += cppTypeName(declaredType(func.parameterVarDeclarations[i]));
				signature += param.isRef ? "& " : " "; // `ref` out-parameters become C++ references
				signature += param.name;
			}
			signature += ")";
			emitLine(signature);
			openBlock();

			std::vector<int> openScopes; // scopeLevel of each currently-open nested block's header
			for (int i = headerIndex + 1; i < bodyEnd; ++i)
			{
				const DSLCodeLine& line = *document.file.lines[i];
				const DSLSymbol* head = line.head();
				const DSLSymbol::FlowControl* flow = (head != nullptr && head->type == ST::FlowControl)
					? &std::get<DSLSymbol::FlowControl>(head->data) : nullptr;
				const bool continuation = flow != nullptr
					&& (flow->control == DSLFlowControl::ElseIf || flow->control == DSLFlowControl::Else);

				while (!openScopes.empty() && openScopes.back() >= line.scopeLevel)
				{
					if (continuation && openScopes.back() == line.scopeLevel)
						break;
					closeBlock();
					openScopes.pop_back();
				}

				emitStatement(line, head, flow, openScopes);
			}
			while (!openScopes.empty())
			{
				closeBlock();
				openScopes.pop_back();
			}

			closeBlock();
		}

		void emitStatement(const DSLCodeLine& line, const DSLSymbol* head, const DSLSymbol::FlowControl* flow,
			std::vector<int>& openScopes)
		{
			if (head == nullptr || head->type == ST::Placeholder)
			{
				emitLine(""); // a blank statement slot stays a blank line
				return;
			}
			switch (head->type)
			{
			case ST::Comment:
				emitLine("// " + std::get<DSLSymbol::Comment>(head->data).text);
				return;
			case ST::VariableDeclaration:
				emitLine(declarationText(head) + ";");
				return;
			case ST::Expression: // an assignment/compound-assign statement -- same flat-chain emit as any value
				emitLine(expressionText(head) + ";");
				return;
			case ST::FunctionCall:
				emitLine(callText(head) + ";");
				return;
			case ST::FlowControl:
				break;
			default:
				emitLine("// unsupported statement");
				return;
			}

			switch (flow->control)
			{
			case DSLFlowControl::If:
			case DSLFlowControl::While:
				emitLine(std::string(flow->control == DSLFlowControl::If ? "if" : "while")
					+ " (" + expressionText(flow->condition) + ")");
				openBlock();
				openScopes.push_back(line.scopeLevel);
				return;
			case DSLFlowControl::ElseIf:
			case DSLFlowControl::Else:
				// The chain's previous branch closes here; the open-block entry survives -- one `end` (a single
				// scopeLevel step-down) closes the whole chain, exactly like the formatter reads it.
				closeBlock();
				emitLine(flow->control == DSLFlowControl::Else ? "else"
					: "else if (" + expressionText(flow->condition) + ")");
				openBlock();
				return;
			case DSLFlowControl::For:
				emitLine("for (" + declarationText(flow->forLoopVar) + "; "
					+ expressionText(flow->forCondition) + "; " + expressionText(flow->forIncrement) + ")");
				openBlock();
				openScopes.push_back(line.scopeLevel);
				return;
			case DSLFlowControl::Return:
				emitLine(flow->condition != nullptr ? "return " + expressionText(flow->condition) + ";" : "return;");
				return;
			case DSLFlowControl::Break:
				emitLine("break;");
				return;
			default:
				return;
			}
		}
	};

	// "Scripts/jumpPad.dsl" -> "jumpPad", sanitized to an identifier ("script"-prefixed when empty/leading-digit).
	std::string classStem(const std::string& filePath)
	{
		size_t start = filePath.find_last_of("/\\");
		start = (start == std::string::npos) ? 0 : start + 1;
		size_t end = filePath.rfind('.');
		if (end == std::string::npos || end < start)
			end = filePath.size();
		std::string stem;
		for (size_t i = start; i < end; ++i)
		{
			const char c = filePath[i];
			stem += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') ? c : '_';
		}
		if (stem.empty() || std::isdigit(static_cast<unsigned char>(stem[0])) != 0)
			stem = "script" + stem;
		return stem;
	}
}

std::string Transpiler::transpile(const DSL& document, const ScriptBindings& bindings)
{
	Emitter emitter{ document, bindings };

	emitter.emitLine("// Generated by the DSL transpiler -- do not edit (the source is the //@@dsl block below).");
	emitter.emitLine("struct Script_" + classStem(document.filePath));
	emitter.openBlock();

	// The entity's things as MEMBERS: the global engine surface, the entity itself, and one wrapper per
	// REQUIRED component (never null, per the require guarantee -- see DSL::requiredComponents). The wrapper
	// types and their construction are the engine-hookup step's work.
	emitter.emitLine("ScriptCtx ctx;");
	for (const BindingObject& object : bindings.objects())
	{
		if (object.name == nullptr)
			continue;
		if (object.requiredComponent != DSLComponentKind::None
			&& !dslIsComponentRequired(document, object.requiredComponent))
			continue;
		emitter.emitLine(wrapperTypeName(object.name) + " " + object.name + ";");
	}

	for (int i = 0; i < static_cast<int>(document.file.lines.size()); ++i)
	{
		const DSLSymbol* head = document.file.lines[i]->head();
		if (head == nullptr || head->type != ST::FunctionDeclaration)
			continue;
		emitter.emitLine("");
		const int bodyEnd = dslBlockEnd(document.file, i);
		emitter.emitFunction(i, bodyEnd);
		i = bodyEnd - 1;
	}

	--emitter.indent;
	emitter.emitLine("};");
	return emitter.out;
}
