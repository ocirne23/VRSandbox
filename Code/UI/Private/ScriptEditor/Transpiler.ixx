export module UI:Transpiler;

import Core;
import :DSL;

// Emits body-only C++ (no #include/#define -- ScriptHost force-includes ScriptAPI.h) from a flat DSLCodeLine
// sequence, matching ScriptAPI.h's ABI exactly: one SCRIPT_EXPORT function + REGISTER_*() per recognized entry
// point. M1 slice only: recognizes `function update/onSpawn/onDestroy(...)` headers and a single-string-literal
// `print(...)` call statement in their bodies -- everything else (helper functions, full expression/control-flow
// grammar) is M3/M5/M6 work, per the milestone plan.
export class Transpiler
{
public:
	static std::vector<std::string> transpile(const std::vector<std::unique_ptr<DSLCodeLine>>& lines);
};