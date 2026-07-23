export module Script:Transpiler;

import Core;
import :DSL;
import :ScriptBindings;

// Emits the C++ a DSL document transpiles to: one free, dllexported function per DSL function (SCRIPT_EXPORT,
// matching the node-graph system's generated .scr code) -- no wrapper class. `ctx`/`self` are auto-injected as
// every function's first two parameters, invisible to the DSL author at both the declaration and every call
// site, so DSL `self.thing`/`physics.applyImpulse(...)` and free engine calls (`print(...)`) transpile straight
// through the real ScriptContext ABI thunks (ctx->entitySetEnabled, ctx->physicsApplyImpulse, ...) instead of
// through any intermediate wrapper object. `scriptData` (void*) is auto-injected too -- the entry points' own
// per the real ABI, everyone else's 3rd parameter -- because self.data (the document's own persistent fields,
// DSL::dataFields, authored via the SCRIPT DATA sidebar section) needs it in scope wherever it's dotted into,
// not just from an entry point; a non-empty dataFields list additionally emits a real "struct ScriptData {
// ...fields...; };" up top plus ScriptDataSize()/REGISTER_SCRIPT_DATA_SIZE(), and self.data itself transpiles
// to a dereferenced cast of that parameter ("(*(ScriptData*)scriptData)", see ScriptBindings), so
// self.data.<field> reads through "." like any other value member. Per-function emit comes from the
// ScriptBindings registry's templates ("$r" = the receiver's own emitted expression, "$1..$n" = arguments in
// the callee's parameter order); user-function calls emit positionally (named/ref arguments reordered to match
// the declaration) with ctx/self/scriptData prepended to match the callee's own auto-injected parameters.
// Expression chains emit FLAT, exactly as authored -- C++'s own precedence supplies the "*, / over +, -" the
// DSL defers to emit time (see DSL.ixx) -- with parentheses only where the author grouped them.
//
// A DSL function named after one of the 5 ScriptAPI entry points (OnSpawn/OnDestroy/Update/OnEvent/
// OnPhysicsEvent -- see ScriptBindings' EntryPointDef, created/locked via ScriptEditor's EXPORTS sidebar
// toggles) transpiles to its EXACT real exported signature and gets its REGISTER_*() macro, so it IS a real,
// loadable ScriptHost entry point; every other DSL function keeps the generic ctx/self + declared-params shape.
// OnEvent additionally emits a zero-events ScriptEventCount/ScriptEventName stub, since the DSL has no way to
// author NAMED On Event entries yet. NOT YET DONE: the required-components assignment mask (a script should
// only bind to an entity that has every component it required) is separate, still not-yet-done follow-up work.
export class Transpiler
{
public:
	static std::string transpile(const DSL& document, const ScriptBindings& bindings);
};
