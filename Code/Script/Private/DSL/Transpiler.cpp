module Script;

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
				// The receiver is its own emitted EXPRESSION (a handle-fetch call like
				// "ctx->entityGetPhysicsComponent(self)", or a chained member access like "self.pos"), so emit
				// templates compose textually.
				const std::string receiverName = (call.receiver != nullptr) ? expressionText(call.receiver) : std::string();
				return substituteTemplate(emitTemplate, receiverName, args);
			}

			// A user function: ctx/self are auto-threaded through exactly like the callee's own signature (see
			// emitFunction) -- invisible to the DSL author at both the declaration and every call site.
			std::string text = callee.name + "(ctx, self";
			for (const std::string& arg : args)
				text += ", " + arg;
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

			// Every generated function is a free, dllexported function (like the node system's generated .scr
			// code) -- no wrapper class. `ctx`/`self` are auto-injected as the first two parameters, invisible
			// to the DSL author, so self.thing/thunk-function calls resolve without one (see the ScriptBindings
			// emit templates, all written in terms of these two names) and so user-function calls (callText)
			// can thread them straight through too.
			std::string signature = std::string("SCRIPT_EXPORT ") + cppTypeName(func.returnType) + " " + func.name
				+ "(const ScriptContext* ctx, Entity* self";
			for (size_t i = 0; i < func.parameterVarDeclarations.size(); ++i)
			{
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

}

std::string Transpiler::transpile(const DSL& document, const ScriptBindings& bindings)
{
	Emitter emitter{ document, bindings };

	emitter.emitLine("// Generated by the DSL transpiler -- do not edit (the source is the //@@dsl block below).");

	// One free, dllexported function per DSL function, at file scope -- no wrapper class (see emitFunction).
	// A real hookup into ScriptHost (the exported entry-point names Update/OnSpawn/... and their REGISTER_*()
	// macros) is separate, not-yet-done follow-up work; every DSL function transpiles uniformly for now.
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

	return emitter.out;
}
