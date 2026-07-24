module Script;

import Core;
import :DSL;
import :ScriptBindings;

namespace
{
	using ST = DSLSymbol::SymbolType;

	DSLSymbol* addSymbol(std::vector<std::unique_ptr<DSLSymbol>>& container, ST type, DSLSymbol::Data data)
	{
		auto symbol = std::make_unique<DSLSymbol>();
		symbol->type = type;
		symbol->data = std::move(data);
		DSLSymbol* ptr = symbol.get();
		container.push_back(std::move(symbol));
		return ptr;
	}
}

DSLType ScriptBindings::registerStruct(BindingStruct def)
{
	const DSLType type = dslStructType(static_cast<int>(m_structDefs.size()));
	m_structDefs.push_back(std::move(def));
	return type;
}

void ScriptBindings::registerObject(BindingObject def)
{
	m_objectDefs.push_back(std::move(def));
}

DSLType ScriptBindings::registerComponentType(const char* memberName, const char* typeName, const char* memberEmit)
{
	const DSLType type = dslComponentType(static_cast<int>(m_componentTypeNames.size()));
	m_componentTypeNames.push_back(typeName);
	// self must already be registered by now (guaranteed by the caller -- see Entity's registerScriptDslBindings)
	// -- appended to IN PLACE, not re-registered, so every OTHER caller's earlier self.<member>s survive. The
	// member's own gate IS `type` itself (see DSL::requiredComponents) -- there's nothing else it could sensibly be.
	for (BindingObject& object : m_objectDefs)
		if (object.name != nullptr && std::string_view(object.name) == "self")
		{
			object.members.push_back({ memberName, type, memberEmit, /*writable*/ false, /*requiredComponent*/ type });
			break;
		}
	return type;
}

void ScriptBindings::registerEntryPoint(EntryPointDef def)
{
	m_entryPointDefs.push_back(std::move(def));
}

DSLType ScriptBindings::typeByName(const std::string& name) const
{
	for (size_t i = 0; i < m_structDefs.size(); ++i)
		if (name == m_structDefs[i].name)
			return dslStructType(static_cast<int>(i));
	return componentTypeByName(name);
}

DSLType ScriptBindings::componentTypeByName(const std::string& name) const
{
	for (size_t i = 0; i < m_componentTypeNames.size(); ++i)
		if (name == m_componentTypeNames[i])
			return dslComponentType(static_cast<int>(i));
	return DSLType::Void;
}

void ScriptBindings::build(std::vector<std::unique_ptr<DSLSymbol>>& sidebarOut, std::vector<std::unique_ptr<DSLSymbol>>& builtinsOut)
{
	if (!m_emits.empty())
	{
		assert(false && "ScriptBindings::build() called more than once");
		return;
	}

	// One FunctionDeclaration builtin per exposed function -- what the editor/loader resolve against. Shared
	// by binding-object functions, struct member functions (requiresReceiver=true), and struct constructors
	// (free positional calls named like the struct itself). `selfType` (struct rows only) resolves any
	// DSLType::ThisBinding in `returnType`/a param's type to the struct's own concrete type -- Void everywhere
	// else, where no BindingFunc ever spells ThisBinding.
	auto buildFunction = [&](const char* name, DSLType returnType, const std::vector<BindingParam>& params,
		const char* emit, bool requiresReceiver, bool isPositionalCall, DSLType selfType = DSLType::Void) -> DSLSymbol*
	{
		const auto resolveSelf = [selfType](DSLType t) { return t == DSLType::ThisBinding ? selfType : t; };
		std::vector<DSLSymbol*> built;
		for (const BindingParam& param : params)
		{
			DSLSymbol* paramType = addSymbol(builtinsOut, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ resolveSelf(param.type) });
			built.push_back(addSymbol(builtinsOut, ST::VariableDeclaration,
				DSLSymbol::VariableDeclaration{ param.name, paramType, nullptr, param.isRef }));
		}
		DSLSymbol* funcSymbol = addSymbol(builtinsOut, ST::FunctionDeclaration, DSLSymbol::FunctionDeclaration{
			name, std::move(built), resolveSelf(returnType), requiresReceiver, isPositionalCall });
		m_emits.emplace_back(funcSymbol, emit);
		return funcSymbol;
	};

	for (size_t i = 0; i < m_structDefs.size(); ++i)
	{
		const BindingStruct& def = m_structDefs[i];
		const DSLType selfType = dslStructType(static_cast<int>(i));
		BuiltStruct& built = m_builtStructs.emplace_back();
		built.def = &def;
		built.constructorFunc = buildFunction(def.name, selfType, def.constructorParams,
			def.constructorEmit, /*requiresReceiver*/ false, /*isPositionalCall*/ true, selfType);
		for (const BindingFunc& func : def.functions)
			built.functionSymbols.push_back(buildFunction(func.name, func.returnType, func.params, func.emit,
				/*requiresReceiver*/ true, func.isPositionalCall, selfType));
	}

	for (const BindingObject& object : m_objectDefs)
	{
		BuiltObject& built = m_built.emplace_back();
		built.def = &object;
		if (object.name != nullptr && object.sidebarTopLevel)
		{
			DSLSymbol* typeSymbol = addSymbol(sidebarOut, ST::TypeDeclaration, DSLSymbol::TypeDeclaration{ object.type });
			built.decl = addSymbol(sidebarOut, ST::VariableDeclaration, DSLSymbol::VariableDeclaration{ object.name, typeSymbol });
		}
		for (const BindingFunc& func : object.functions)
			built.functionSymbols.push_back(buildFunction(func.name, func.returnType, func.params, func.emit,
				/*requiresReceiver*/ object.name != nullptr, func.isPositionalCall));
	}
}

std::span<const BindingObject> ScriptBindings::objects() const
{
	return m_objectDefs;
}

const BindingObject* ScriptBindings::objectFor(DSLType type) const
{
	for (const BuiltObject& built : m_built)
		if (built.def->name != nullptr && built.def->type == type)
			return built.def;
	return nullptr;
}

DSLSymbol* ScriptBindings::objectDecl(const BindingObject& object) const
{
	for (const BuiltObject& built : m_built)
		if (built.def == &object)
			return built.decl;
	return nullptr;
}

const BindingObject* ScriptBindings::objectForDecl(const DSLSymbol* sidebarDecl) const
{
	for (const BuiltObject& built : m_built)
		if (built.decl != nullptr && built.decl == sidebarDecl)
			return built.def;
	return nullptr;
}

std::span<DSLSymbol* const> ScriptBindings::functionSymbols(const BindingObject& object) const
{
	for (const BuiltObject& built : m_built)
		if (built.def == &object)
			return built.functionSymbols;
	return {};
}

const BindingMember* ScriptBindings::findMember(DSLType receiverType, const std::string& name) const
{
	const std::vector<BindingMember>* members = nullptr;
	if (const BindingObject* object = objectFor(receiverType); object != nullptr)
		members = &object->members;
	else if (const BindingStruct* structDef = structFor(receiverType); structDef != nullptr)
		members = &structDef->members;
	if (members == nullptr)
		return nullptr;
	for (const BindingMember& member : *members)
		if (name == member.name)
			return &member;
	return nullptr;
}

const char* ScriptBindings::componentTypeName(DSLType type) const
{
	if (!dslIsComponentType(type))
		return nullptr;
	const int index = dslComponentTypeIndex(type);
	if (index < 0 || index >= static_cast<int>(m_componentTypeNames.size()))
		return nullptr;
	return m_componentTypeNames[index];
}

std::span<const BindingStruct> ScriptBindings::structs() const
{
	return m_structDefs;
}

const BindingStruct* ScriptBindings::structFor(DSLType type) const
{
	if (!dslIsStructType(type))
		return nullptr;
	const int index = dslStructIndex(type);
	if (index < 0 || index >= static_cast<int>(m_builtStructs.size()))
		return nullptr;
	return m_builtStructs[index].def;
}

std::span<DSLSymbol* const> ScriptBindings::structFunctionSymbols(DSLType type) const
{
	if (!dslIsStructType(type))
		return {};
	const int index = dslStructIndex(type);
	if (index < 0 || index >= static_cast<int>(m_builtStructs.size()))
		return {};
	return m_builtStructs[index].functionSymbols;
}

const char* ScriptBindings::emitFor(const DSLSymbol* funcDecl) const
{
	for (const auto& [symbol, emit] : m_emits)
		if (symbol == funcDecl)
			return emit;
	return nullptr;
}

std::span<const EntryPointDef> ScriptBindings::entryPoints() const
{
	return m_entryPointDefs;
}

const EntryPointDef* ScriptBindings::entryPointFor(const std::string& name) const
{
	for (const EntryPointDef& def : m_entryPointDefs)
		if (name == def.name)
			return &def;
	return nullptr;
}
