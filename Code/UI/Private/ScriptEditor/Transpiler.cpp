module UI;

import Core;
import :DSL;
import :Transpiler;

namespace
{
	struct EntryPointInfo
	{
		std::string exportName;
		std::string registerMacro;
	};

	// M1's fixed, tiny set of recognized entry-point names (matching the example syntax's `function update(...)`
	// convention exactly) -- richer name resolution/aliasing across the rest of ScriptAPI.h's entry points
	// (OnEvent/OnPhysicsEvent, case-insensitive matching, ...) is M6 work.
	bool findEntryPoint(const std::string& name, EntryPointInfo& outInfo)
	{
		if (name == "update")    { outInfo = { "Update",    "REGISTER_UPDATE()" };     return true; }
		if (name == "onSpawn")   { outInfo = { "OnSpawn",   "REGISTER_ON_SPAWN()" };   return true; }
		if (name == "onDestroy") { outInfo = { "OnDestroy", "REGISTER_ON_DESTROY()" }; return true; }
		return false;
	}

	const char* cppTypeName(DSLType type)
	{
		switch (type)
		{
		case DSLType::Void:    return "void";
		case DSLType::Int:     return "int";
		case DSLType::Float:   return "float";
		case DSLType::Bool:    return "bool";
		case DSLType::String:  return "const char*";
		case DSLType::Vector2: return "glm::vec2";
		case DSLType::Vector3: return "glm::vec3";
		case DSLType::Vector4: return "glm::vec4";
		default:               return "/* unsupported DSLType */ void*";
		}
	}

	// M1's only recognized statement shape: a builtin, receiver-less `print(<one string literal>)` ->
	// ctx->log(...). Multi-arg {}-style interpolation (-> ctx->logf/std::format) is M6 work; anything else emits
	// a marker comment rather than guessing, since M1 deliberately doesn't cover the rest of the grammar yet.
	std::string emitStatement(const DSLSymbol& head)
	{
		if (head.type == DSLSymbol::SymbolType::Comment)
			return "\t// " + std::get<DSLSymbol::Comment>(head.data).text;

		if (head.type != DSLSymbol::SymbolType::FunctionCall)
			return "\t// TODO(DSL M1): unsupported statement kind";

		const DSLSymbol::FunctionCall& call = std::get<DSLSymbol::FunctionCall>(head.data);
		const DSLSymbol::FunctionDeclaration& callee = std::get<DSLSymbol::FunctionDeclaration>(call.functionSymbol->data);

		if (callee.name == "print" && call.receiver == nullptr && call.arguments.size() == 1
			&& call.arguments[0].value->type == DSLSymbol::SymbolType::Constant)
		{
			const DSLSymbol::Constant& literal = std::get<DSLSymbol::Constant>(call.arguments[0].value->data);
			if (literal.type == DSLType::String)
				return "\tctx->log(\"" + literal.value + "\");";
		}

		return "\t// TODO(DSL M1): unsupported call (only a single-string-literal print(...) is handled so far)";
	}
}

std::vector<std::string> Transpiler::transpile(const std::vector<std::unique_ptr<DSLCodeLine>>& lines)
{
	std::vector<std::string> out;

	for (size_t i = 0; i < lines.size(); ++i)
	{
		const DSLCodeLine& headerLine = *lines[i];

		// A function-declaration header line's PRIMARY symbol is its last one (DSLCodeLine::head, per DSL.ixx's
		// post-order storage convention -- the header's own parameter declarations are earlier peers on the line).
		const DSLSymbol* headSymbol = headerLine.head();
		if (headSymbol == nullptr || headSymbol->type != DSLSymbol::SymbolType::FunctionDeclaration)
			continue;

		const DSLSymbol::FunctionDeclaration& func = std::get<DSLSymbol::FunctionDeclaration>(headSymbol->data);

		EntryPointInfo entryPoint;
		if (!findEntryPoint(func.name, entryPoint))
			continue; // M6: non-entry-point helper functions (Graph.scr-style static free functions)

		std::string signature = "SCRIPT_EXPORT void " + entryPoint.exportName + "(const ScriptContext* ctx, Entity* self";
		for (const DSLSymbol* paramSymbol : func.parameterVarDeclarations)
		{
			const DSLSymbol::VariableDeclaration& param = std::get<DSLSymbol::VariableDeclaration>(paramSymbol->data);
			const DSLSymbol::TypeDeclaration& paramType = std::get<DSLSymbol::TypeDeclaration>(param.typeSymbol->data);
			signature += std::string(", ") + cppTypeName(paramType.type) + " " + param.name;
		}
		signature += ", void* scriptData)";

		out.push_back(signature);
		out.push_back("{");

		// Body = every following line strictly nested under this header (scopeLevel > header's), i.e. up to
		// (not including) the next line back at the header's own scopeLevel or shallower.
		size_t bodyEnd = i + 1;
		for (; bodyEnd < lines.size() && lines[bodyEnd]->scopeLevel > headerLine.scopeLevel; ++bodyEnd)
		{
			const DSLCodeLine& bodyLine = *lines[bodyEnd];
			if (bodyLine.head() != nullptr)
				out.push_back(emitStatement(*bodyLine.head()));
		}

		out.push_back("}");
		out.push_back("");
		out.push_back(entryPoint.registerMacro);
		out.push_back("");

		i = bodyEnd - 1; // loop's ++i resumes scanning exactly at bodyEnd
	}

	return out;
}
