export module UI:ScriptLoader;

import Core;
import :DSL;

// Save/load for the DSL editor's native on-disk format (.dsl). The format IS the editor's EXPANDED view:
// save() writes the exact text Syntax::format renders (fully typed), one line per SyntaxLine, each prefixed
// "//@" and bracketed by "//@@dsl <version>" ... "//@@end" markers -- the whole DSL block reads as C++
// comments, leaving the rest of the file free to hold the transpiled C++ itself later (one dual-purpose file:
// everything that is not valid C++ stays commented out; the "@" keeps DSL lines distinguishable from the
// generated code's own ordinary "//" comments, and the markers double it since a DSL `end` line serializes as
// "//@end"). load() is the exact inverse: it finds the marker block, strips the "//@" prefixes, and parses the
// expanded-view text back into DSLSymbol structures -- names resolve against
// the document's own functions (two passes, so forward calls work), its sidebar bindings, and the editor's
// builtin registry. References are stored by NAME, never by index, which is what keeps old files loadable as
// the builtin/sidebar lists evolve.
//
// Failure policy: an editor-saved file always parses (the editor cannot author an invalid document), so any
// parse or resolution error means hand-editing or version drift -- load() then REFUSES the whole file with a
// line-numbered error instead of constructing a partial document (the editor's no-placeholder rule applied to
// loading: nothing is ever guessed or half-built). After a successful parse the reconstructed document is
// re-rendered and compared line-by-line against what was loaded; a mismatch still loads (the structure is
// valid) but logs a warning per differing line -- the usual cause is hand-edited formatting, which the next
// save normalizes.
export class ScriptLoader
{
public:
	struct LoadResult
	{
		bool success = false;
		std::string error; // human-readable, "<path>(<line>): <what>" -- empty on success
	};

	// Writes document.file as a "//"-commented expanded-view DSL block. False = the file couldn't be opened
	// for writing. Sidebar bindings and builtins are fixed editor context, deliberately NOT serialized -- a
	// future version adds "//@bind" lines when M5 makes the sidebar user-editable (old files without them just
	// keep getting the default sidebar, so that stays backward compatible).
	static bool save(DSL& document, const std::string& path);

	// Parses a file save() wrote and REPLACES document.file.lines on success; on failure the document is left
	// untouched. document.sidebar and `builtins` are the name-resolution context (they must be the same fixed
	// sets the editor composes against, or references would resolve to nothing).
	static LoadResult load(DSL& document, const std::string& path, const std::vector<std::unique_ptr<DSLSymbol>>& builtins);
};
