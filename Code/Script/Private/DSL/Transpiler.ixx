export module Script:Transpiler;

import Core;
import :DSL;
import :ScriptBindings;

// Emits the C++ a DSL document transpiles to: one free, dllexported function per DSL function (SCRIPT_EXPORT,
// matching the node-graph system's generated .scr code) -- no wrapper class. `ctx`/`self` are auto-injected as
// every function's first two parameters, invisible to the DSL author at both the declaration and every call
// site, so DSL `self.thing`/`physics.applyImpulse(...)` and free engine calls (`print(...)`) transpile straight
// through the real ScriptContext ABI thunks (ctx->entitySetEnabled, ctx->physicsApplyImpulse, ...) instead of
// through any intermediate wrapper object. Per-function emit comes from the ScriptBindings registry's templates
// ("$r" = the receiver's own emitted expression, "$1..$n" = arguments in the callee's parameter order);
// user-function calls emit positionally (named/ref arguments reordered to match the declaration) with ctx/self
// prepended to match the callee's own auto-injected parameters. Expression chains emit FLAT, exactly as
// authored -- C++'s own precedence supplies the "*, / over +, -" the DSL defers to emit time (see DSL.ixx) --
// with parentheses only where the author grouped them.
//
// NOT YET DONE: wiring a DSL function up as a REAL ScriptHost entry point (renaming e.g. "update" to the
// exported "Update" symbol name, matching ScriptUpdateFn's exact signature, and emitting its REGISTER_*()
// macro) -- every DSL function transpiles the same way for now, so nothing is actually loadable by ScriptHost
// yet. The required-components assignment mask is separate, also not-yet-done follow-up work.
export class Transpiler
{
public:
	static std::string transpile(const DSL& document, const ScriptBindings& bindings);
};
