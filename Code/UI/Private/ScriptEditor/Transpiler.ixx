export module UI:Transpiler;

import Core;
import :DSL;
import :ScriptBindings;

// Emits the C++ a DSL document transpiles to: one generated per-script class (the user's ScriptEntity model)
// whose MEMBERS are the entity's own things -- `ctx` (the global engine surface), `self`, and one wrapper
// member per REQUIRED component -- and whose METHODS are the script's functions, so DSL
// `physics.applyImpulse(...)` reads as member-pattern access in the output. Per-function emit comes from the
// ScriptBindings registry's templates ("$r" = the receiver's class member, "$1..$n" = arguments in the
// callee's parameter order); user-function calls emit positionally (named/ref arguments reordered to match
// the declaration). Expression chains emit FLAT, exactly as authored -- C++'s own precedence supplies the
// "*, / over +, -" the DSL defers to emit time (see DSL.ixx) -- with parentheses only where the author
// grouped them.
//
// CODEGEN ONLY for now: the wrapper types (ScriptCtx/ScriptSelf/ScriptPhysics/...), the entry-point export
// shims, and the required-components mask are the engine-hookup step's work -- the generated text assumes
// those types exist and compiles against nothing yet.
export class Transpiler
{
public:
	static std::string transpile(const DSL& document, const ScriptBindings& bindings);
};
