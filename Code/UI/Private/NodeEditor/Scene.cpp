module UI.NodeEditor.Scene;

import Core;
import Core.imgui;
import UI.imgui_node_editor;
import UI.NodeEditor.Node;
import UI.NodeEditor.Link;
import UI.NodeEditor.NodeDef;

using namespace NodeEditor;

namespace
{
    // ---- graph traversal helpers (links store input pin in Id1, output pin in Id2) ----

    Pin* sourceOfInput(const std::vector<std::unique_ptr<Link>>& links, const Pin* inputPin)
    {
        for (const auto& link : links)
            if (link->getInputId().AsPointer<Pin>() == inputPin)
                return link->getOutputId().AsPointer<Pin>();
        return nullptr;
    }

    Pin* targetOfOutput(const std::vector<std::unique_ptr<Link>>& links, const Pin* outputPin)
    {
        for (const auto& link : links)
            if (link->getOutputId().AsPointer<Pin>() == outputPin)
                return link->getInputId().AsPointer<Pin>();
        return nullptr;
    }

    int indexOfPin(const std::vector<std::unique_ptr<Pin>>& pins, const Pin* pin)
    {
        for (int i = 0; i < (int)pins.size(); ++i)
            if (pins[i].get() == pin)
                return i;
        return -1;
    }

    // The static `expr` template of a node's output pin (empty if none). May carry a HOIST marker.
    const std::string& outputExprOf(const Pin* outPin)
    {
        static const std::string empty;
        const NodeDef* def = findNodeDef(outPin->node->getTypeId());
        if (!def) return empty;
        const int outIdx = indexOfPin(outPin->node->getOutputPins(), outPin);
        return (outIdx >= 0 && outIdx < (int)def->outputs.size()) ? def->outputs[outIdx].expr : empty;
    }

    // Can these two pins be linked? Exec only joins exec; the output's read/write capability must cover what
    // the input requires; an unresolved wildcard accepts any value type, otherwise the concrete types match.
    bool pinsCompatible(const Pin* input, const Pin* output)
    {
        if (input->dataType == EDataType::Exec || output->dataType == EDataType::Exec)
            return input->dataType == EDataType::Exec && output->dataType == EDataType::Exec;

        // Mutability: e.g. a Writable input (writes to its source) can't take a read-only rvalue output.
        if ((mutableBits(input->mutability) & ~mutableBits(output->mutability)) != 0)
            return false;

        const bool inputWild = input->typeGroup != 0 && input->dataType == EDataType::Wildcard;
        const bool outputWild = output->typeGroup != 0 && output->dataType == EDataType::Wildcard;
        if (inputWild || outputWild)
            return true;
        return input->dataType == output->dataType;
    }

    // ---- code generation ----
    //
    // Exec traversal and data resolution keep SEPARATE recursion stacks: a node that sits in the exec
    // flow can still feed its data output into a downstream node (e.g. Conditional), which is not a cycle.
    //
    // Common-subexpression elimination: per exec statement, any data output pin that would otherwise be
    // expanded 2+ times (e.g. a Get Entity output read for both .x and .z) is hoisted into a `const auto`
    // local declared just before the statement, and consumers reference that local.

    using HoistMap = std::map<const Pin*, std::string>; // output pin -> local variable name

    struct Codegen
    {
        const std::vector<std::unique_ptr<Link>>& links;
        int tempCounter = 0;
        std::string         globalDecls;    // HOIST variable declarations, emitted once at the top of the function
        std::set<const Pin*> globalDeclared; // pins already added to globalDecls (dedups multi-use variables)
    };

    std::string emitDataExpr(Codegen& cg, const Pin* inputPin, std::set<const Node*>& dataStack, const HoistMap& hoist);
    std::string emitExecChain(Codegen& cg, Node* node, std::set<const Node*>& execStack);
    std::string expandOutput(Codegen& cg, const Pin* outPin, std::set<const Node*>& dataStack, const HoistMap& hoist);

    // @ -> the node's selected dropdown-property code token.
    void appendEnumToken(Codegen& cg, std::string& out, Node* node)
    {
        const NodeDef* def = findNodeDef(node->getTypeId());
        if (def->enumTokens.empty())
		{
			out += std::to_string(node->getNodeIdx());
			return;
		}
        const int sel = node->getEnumSelection();
        if (def && sel >= 0 && sel < (int)def->enumTokens.size())
            out += def->enumTokens[sel];
    }

    std::string inputExpr(Codegen& cg, Node* node, int idx, std::set<const Node*>& dataStack, const HoistMap& hoist)
    {
        const auto& inputs = node->getInputPins();
        return (idx >= 0 && idx < (int)inputs.size()) ? emitDataExpr(cg, inputs[idx].get(), dataStack, hoist) : "0";
    }

    // Value-expression template ($k = data input k, @ = enum token). Data resolution threads dataStack.
    std::string substituteData(Codegen& cg, const std::string& tmpl, Node* node, std::set<const Node*>& dataStack, const HoistMap& hoist)
    {
        std::string out;
        for (size_t i = 0; i < tmpl.size();)
        {
            const char c = tmpl[i];
            if (c == '$' && i + 1 < tmpl.size() && tmpl[i + 1] >= '0' && tmpl[i + 1] <= '9')
            {
                int idx = 0; size_t j = i + 1;
                while (j < tmpl.size() && tmpl[j] >= '0' && tmpl[j] <= '9') { idx = idx * 10 + (tmpl[j] - '0'); ++j; }
                out += inputExpr(cg, node, idx, dataStack, hoist);
                i = j;
            }
            else if (c == '@') { appendEnumToken(cg, out, node); ++i; }
            else { out += c; ++i; }
        }
        return out;
    }

    // Statement template ($k = data input k via a fresh data recursion, #k = exec continuation k, @ = enum).
    std::string substituteExec(Codegen& cg, const std::string& tmpl, Node* node, std::set<const Node*>& execStack, const HoistMap& hoist)
    {
        std::string out;
        for (size_t i = 0; i < tmpl.size();)
        {
            const char c = tmpl[i];
            if ((c == '$' || c == '#') && i + 1 < tmpl.size() && tmpl[i + 1] >= '0' && tmpl[i + 1] <= '9')
            {
                int idx = 0; size_t j = i + 1;
                while (j < tmpl.size() && tmpl[j] >= '0' && tmpl[j] <= '9') { idx = idx * 10 + (tmpl[j] - '0'); ++j; }
                if (c == '$')
                {
                    std::set<const Node*> dataStack; // data resolution is independent of the exec path
                    out += inputExpr(cg, node, idx, dataStack, hoist);
                }
                else
                {
                    const auto& outputs = node->getOutputPins();
                    Pin* tgt = (idx >= 0 && idx < (int)outputs.size()) ? targetOfOutput(cg.links, outputs[idx].get()) : nullptr;
                    out += tgt ? emitExecChain(cg, tgt->node, execStack) : std::string();
                }
                i = j;
            }
            else if (c == '@') { appendEnumToken(cg, out, node); ++i; }
            // ?k{block}: emit the block (with $-substitution) only if input pin k is connected.
            else if (c == '?' && i + 1 < tmpl.size() && tmpl[i + 1] >= '0' && tmpl[i + 1] <= '9')
            {
                int idx = 0; size_t j = i + 1;
                while (j < tmpl.size() && tmpl[j] >= '0' && tmpl[j] <= '9') { idx = idx * 10 + (tmpl[j] - '0'); ++j; }
                if (j < tmpl.size() && tmpl[j] == '{')
                {
                    int depth = 1; size_t k = j + 1;
                    for (; k < tmpl.size() && depth > 0; ++k)
                    {
                        if (tmpl[k] == '{') ++depth;
                        else if (tmpl[k] == '}') { if (--depth == 0) break; }
                    }
                    const std::string block = tmpl.substr(j + 1, k - (j + 1));
                    const auto& inputs = node->getInputPins();
                    const Pin* in = (idx >= 0 && idx < (int)inputs.size()) ? inputs[idx].get() : nullptr;
                    if (in && sourceOfInput(cg.links, in))
                        out += substituteExec(cg, block, node, execStack, hoist);
                    i = (k < tmpl.size()) ? k + 1 : k;
                }
                else { out += c; ++i; }
            }
            else { out += c; ++i; }
        }
        return out;
    }

    // Picks the value template for an output pin and substitutes it (per-output expr, else node emit/dataEmit).
    std::string expandOutput(Codegen& cg, const Pin* outPin, std::set<const Node*>& dataStack, const HoistMap& hoist)
    {
        Node* node = outPin->node;
        const NodeDef* def = findNodeDef(node->getTypeId());
        if (!def || dataStack.count(node))
            return "0";

        const int outIdx = indexOfPin(node->getOutputPins(), outPin);
        std::string tmpl;
        if (outIdx >= 0 && outIdx < (int)def->outputs.size() && !def->outputs[outIdx].expr.empty())
        {
            const std::string& e = def->outputs[outIdx].expr;
            const auto hp = e.find(HOIST);
            tmpl = (hp == std::string::npos) ? e : e.substr(hp + 1); // inline-expand only the reference part
        }
        else if (def->isExec)
            tmpl = def->dataEmit;
        else
            tmpl = def->emit;
        if (tmpl.empty())
            return "0";

        dataStack.insert(node);
        std::string result = substituteData(cg, tmpl, node, dataStack, hoist);
        dataStack.erase(node);
        return result;
    }

    std::string emitDataExpr(Codegen& cg, const Pin* inputPin, std::set<const Node*>& dataStack, const HoistMap& hoist)
    {
        Pin* src = sourceOfInput(cg.links, inputPin);
        if (!src)
            return inputPin->defaultValue.empty() ? std::string("0") : inputPin->defaultValue;
        if (auto it = hoist.find(src); it != hoist.end())
            return it->second; // already computed into a local
        return expandOutput(cg, src, dataStack, hoist);
    }

    // Dry run mirroring emitDataExpr: tallies how many times each data output pin would be expanded inline.
    void countExpansions(Codegen& cg, const Pin* inputPin, std::map<const Pin*, int>& counts, std::set<const Node*>& path)
    {
        Pin* src = sourceOfInput(cg.links, inputPin);
        if (!src) return;
        Node* node = src->node;
        if (!findNodeDef(node->getTypeId())) return;
        counts[src]++;
        if (path.count(node)) return; // cycle guard
        path.insert(node);
        for (const auto& pin : node->getInputPins())
            if (pin->dataType != EDataType::Exec)
                countExpansions(cg, pin.get(), counts, path);
        path.erase(node);
    }

    // Emits `const auto <local> = <expr>;` for a hoisted output pin, declaring its hoisted dependencies first.
    void declareHoist(Codegen& cg, const Pin* outPin, const HoistMap& hoist, std::set<const Pin*>& declared, std::string& decls)
    {
        if (declared.count(outPin)) return;
        declared.insert(outPin);
        for (const auto& pin : outPin->node->getInputPins())
        {
            if (pin->dataType == EDataType::Exec) continue;
            if (Pin* s = sourceOfInput(cg.links, pin.get()); s && hoist.count(s))
                declareHoist(cg, s, hoist, declared, decls);
        }
        std::set<const Node*> dataStack;
        const std::string& oexpr = outputExprOf(outPin);
        const auto hp = oexpr.find(HOIST);
        if (hp != std::string::npos)
        {
            // A variable declaration: emit it once at function scope (not before each consuming statement), so
            // a variable used across sibling statements / inside and after a loop is declared a single time.
            if (cg.globalDeclared.insert(outPin).second)
                cg.globalDecls += substituteData(cg, oexpr.substr(0, hp), outPin->node, dataStack, hoist);
        }
        else
            decls += "const auto " + hoist.at(outPin) + " = " + expandOutput(cg, outPin, dataStack, hoist) + ";\n";
    }

    std::string emitExecChain(Codegen& cg, Node* node, std::set<const Node*>& execStack)
    {
        if (!node) return std::string();
        const NodeDef* def = findNodeDef(node->getTypeId());
        if (!def || execStack.count(node)) return std::string();

        execStack.insert(node);

        // Hoist data outputs this statement would evaluate 2+ times into locals declared before it.
        std::map<const Pin*, int> counts;
        {
            std::set<const Node*> path;
            for (const auto& pin : node->getInputPins())
                if (pin->dataType != EDataType::Exec)
                    countExpansions(cg, pin.get(), counts, path);
        }
        // Hoist a data output into a local when it is expanded 2+ times (CSE), OR when its expr carries a
        // HOIST marker (a forced standalone declaration, e.g. a Var node) — then the local is the reference part.
        HoistMap hoist;
        for (const auto& [pin, count] : counts)
        {
            const std::string& oexpr = outputExprOf(pin);
            const auto hp = oexpr.find(HOIST);
            if (hp != std::string::npos)
            {
                std::set<const Node*> ds;
                hoist[pin] = substituteData(cg, oexpr.substr(hp + 1), pin->node, ds, hoist); // reference, e.g. "f3"
            }
            else if (count >= 2)
                hoist[pin] = "t" + std::to_string(cg.tempCounter++);
        }

        std::string decls;
        {
            std::set<const Pin*> declared;
            for (const auto& [pin, name] : hoist)
                declareHoist(cg, pin, hoist, declared, decls);
        }

        std::string out = decls + substituteExec(cg, def->emit, node, execStack, hoist);
        execStack.erase(node);
        return out;
    }

    // Convert the indent-control markers emitted by block nodes into real leading whitespace: \x01 raises
    // the indent level for subsequent lines, \x02 lowers it (the markers themselves are stripped). Each line
    // is prefixed with one pad per active level, so nested Branch/ForLoop bodies step in correctly.
    std::string applyIndent(const std::string& body, const char* pad)
    {
        std::string out;
        int depth = 0;
        bool atLineStart = true;
        for (char c : body)
        {
            if (c == INDENT_UP)   { ++depth; continue; }
            if (c == INDENT_DOWN) { if (depth > 0) --depth; continue; }
            if (atLineStart && c != '\n')
            {
                for (int i = 0; i < depth; ++i) out += pad;
                atLineStart = false;
            }
            out += c;
            if (c == '\n') atLineStart = true;
        }
        return out;
    }

    std::string indentLines(const std::string& body, const char* pad)
    {
        std::string out = pad;
        for (char c : body)
        {
            out += c;
            if (c == '\n') out += pad;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back(); // drop the trailing pad before the closing brace
        return out;
    }
}

Scene::~Scene()
{
}

void Scene::initialize()
{
    ed::Config config;
    config.SettingsFile = nullptr;
    m_nodeEditorContext = ed::CreateEditor(&config);
    ed::SetCurrentEditor(m_nodeEditorContext);

    if (!loadFromFile(m_scriptPath))
    {
        newGraph();
        saveToFile(m_scriptPath);
    }
}

Node& Scene::createNode()
{
    return *m_nodes.emplace_back(std::make_unique<Node>((uint32)m_nodes.size()));
}

Node& Scene::addNodeOfType(const std::string& typeId, ImVec2 pos)
{
    Node& node = createNode();
    if (const NodeDef* def = findNodeDef(typeId))
        node.initFromDef(pos, *def);
    if (m_nodeEditorContext)
        ed::SetNodePosition(node.getId(), pos); // so generateCpp can read positions before the first render
    return node;
}

int Scene::indexOfNode(const Node* node) const
{
    for (int i = 0; i < (int)m_nodes.size(); ++i)
        if (m_nodes[i].get() == node)
            return i;
    return -1;
}

Node* Scene::findEntry() const
{
    for (const auto& node : m_nodes)
        if (node->getTypeId() == "EventUpdate")
            return node.get();
    return nullptr;
}

void Scene::connectNodes(Node* from, int outIdx, Node* to, int inIdx)
{
    if (!from || !to) return;
    const auto& outputs = from->getOutputPins();
    const auto& inputs = to->getInputPins();
    if (outIdx < 0 || outIdx >= (int)outputs.size() || inIdx < 0 || inIdx >= (int)inputs.size())
        return;

    Pin* outPin = outputs[outIdx].get();
    Pin* inPin = inputs[inIdx].get();
    inPin->numConnections++;
    outPin->numConnections++;
    m_links.emplace_back(std::make_unique<Link>())->initialize(ed::PinId(inPin), ed::PinId(outPin));
}

void Scene::resolveNodeTypes(Node* node)
{
    if (!node) return;

    std::set<int> groups;
    for (const auto& pin : node->getInputPins())  if (pin->typeGroup != 0) groups.insert(pin->typeGroup);
    for (const auto& pin : node->getOutputPins()) if (pin->typeGroup != 0) groups.insert(pin->typeGroup);

    for (int group : groups)
    {
        // The group's concrete type is whatever a connected pin in it links to.
        EDataType resolved = EDataType::Wildcard;
        for (const auto& pin : node->getInputPins())
            if (pin->typeGroup == group)
                if (Pin* src = sourceOfInput(m_links, pin.get()); src && src->dataType != EDataType::Wildcard)
                    resolved = src->dataType;
        for (const auto& pin : node->getOutputPins())
            if (pin->typeGroup == group)
                for (const auto& link : m_links)
                    if (link->getOutputId().AsPointer<Pin>() == pin.get())
                        if (Pin* dst = link->getInputId().AsPointer<Pin>(); dst && dst->dataType != EDataType::Wildcard)
                            resolved = dst->dataType;

        auto apply = [&](Pin& pin)
        {
            if (pin.dataType == resolved) return;
            pin.dataType = resolved;
            pin.color = dataTypeColor(resolved);
            pin.shape = resolved == EDataType::Exec ? EPinShape_Flow
                      : pin.mutability != EMutableType::Readable ? EPinShape_Square // keep writable pins square
                      : EPinShape_Circle;
            if (pin.type == EPinType_Input && pin.numConnections == 0)
                pin.defaultValue = defaultValueForType(resolved);
        };
        for (const auto& pin : node->getInputPins())  if (pin->typeGroup == group) apply(*pin);
        for (const auto& pin : node->getOutputPins()) if (pin->typeGroup == group) apply(*pin);
    }
}

void Scene::removeNode(ed::NodeId nodeId)
{
    Node* node = nodeId.AsPointer<Node>();
    if (!node) return;

    // Drop any links touching this node's pins, tracking the neighbours so their wildcard types re-resolve.
    std::set<Node*> affected;
    std::erase_if(m_links, [&](const std::unique_ptr<Link>& link)
    {
        Pin* in = link->getInputId().AsPointer<Pin>();
        Pin* out = link->getOutputId().AsPointer<Pin>();
        if (in && in->node == node) { if (out) { out->numConnections--; affected.insert(out->node); } return true; }
        if (out && out->node == node) { if (in) { in->numConnections--; affected.insert(in->node); } return true; }
        return false;
    });

    std::erase_if(m_nodes, [&](const std::unique_ptr<Node>& n) { return n.get() == node; });

    for (Node* neighbour : affected)
        resolveNodeTypes(neighbour);
}

void Scene::newGraph()
{
    m_links.clear();
    m_nodes.clear();
    Node& entry = addNodeOfType("EventUpdate", ImVec2(40.0f, 60.0f));
    Node& spawn = addNodeOfType("SpawnPointLight", ImVec2(340.0f, 60.0f));
    connectNodes(&entry, 0, &spawn, 0);
    m_firstFrame = true;
    m_hasBaseline = false;
}

std::string Scene::generateCpp()
{
    if (m_nodeEditorContext)
        ed::SetCurrentEditor(m_nodeEditorContext);

    std::string code;
    code += "#include \"ScriptAPI.h\"\n";
    code += "SCRIPT_EXPORT void ScriptUpdate(const ScriptContext* ctx, Entity* self, float dt)\n{\n";

    if (Node* entry = findEntry())
    {
        Codegen cg{ m_links };
        std::set<const Node*> execStack;
        const std::string chain = emitExecChain(cg, entry, execStack); // also collects cg.globalDecls
        const std::string body = applyIndent(cg.globalDecls + chain, "    "); // variable decls first, at function scope
        if (!body.empty())
            code += indentLines(body, "    ");
    }

    code += "}\n\n";
    code += serializeGraph();
    return code;
}

std::string Scene::serializeGraph()
{
    std::string s = "//@graph 1\n";

    for (int i = 0; i < (int)m_nodes.size(); ++i)
    {
        const Node* node = m_nodes[i].get();
        const ImVec2 pos = m_nodeEditorContext ? ed::GetNodePosition(node->getId()) : ImVec2(0.0f, 0.0f);
        s += "//@node " + std::to_string(i) + " " + node->getTypeId() + " " +
             std::to_string((int)pos.x) + " " + std::to_string((int)pos.y) + "\n";
    }

    for (int i = 0; i < (int)m_nodes.size(); ++i)
    {
        const auto& inputs = m_nodes[i]->getInputPins();
        for (int j = 0; j < (int)inputs.size(); ++j)
            if (inputs[j]->dataType != EDataType::Exec)
                s += "//@pin " + std::to_string(i) + " " + std::to_string(j) + " " + inputs[j]->defaultValue + "\n";
    }

    for (int i = 0; i < (int)m_nodes.size(); ++i)
        if (m_nodes[i]->getEnumSelection() != 0)
            s += "//@enum " + std::to_string(i) + " " + std::to_string(m_nodes[i]->getEnumSelection()) + "\n";

    for (const auto& link : m_links)
    {
        Pin* out = link->getOutputId().AsPointer<Pin>();
        Pin* in = link->getInputId().AsPointer<Pin>();
        if (!out || !in) continue;
        const int fromIdx = indexOfNode(out->node);
        const int toIdx = indexOfNode(in->node);
        const int fo = indexOfPin(out->node->getOutputPins(), out);
        const int ti = indexOfPin(in->node->getInputPins(), in);
        if (fromIdx < 0 || toIdx < 0 || fo < 0 || ti < 0) continue;
        s += "//@link " + std::to_string(fromIdx) + " " + std::to_string(fo) + " " +
             std::to_string(toIdx) + " " + std::to_string(ti) + "\n";
    }

    return s;
}

bool Scene::saveToFile(const std::string& path)
{
    const std::string code = generateCpp();
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(code.data(), code.size());
    m_hasBaseline = false; // re-baseline against the just-saved state next frame
    return file.good();
}

namespace
{
    struct LineReader
    {
        const std::string& s;
        size_t pos = 0;
        void skipWs() { while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos; }
        std::string token() { skipWs(); size_t start = pos; while (pos < s.size() && s[pos] != ' ' && s[pos] != '\t') ++pos; return s.substr(start, pos - start); }
        std::string rest() { skipWs(); std::string r = s.substr(pos); while (!r.empty() && (r.back() == '\r' || r.back() == '\n')) r.pop_back(); return r; }
    };

    int toInt(const std::string& s)
    {
        int sign = 1; size_t i = 0;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) { sign = s[i] == '-' ? -1 : 1; ++i; }
        int v = 0;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (s[i] - '0');
        return v * sign;
    }
}

bool Scene::loadFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
        lines.push_back(line);

    m_links.clear();
    m_nodes.clear();
    std::vector<Node*> byIndex;

    // pass 1: nodes
    for (const std::string& ln : lines)
    {
        LineReader r{ ln };
        if (r.token() != "//@node") continue;
        const int idx = toInt(r.token());
        const std::string typeId = r.token();
        const int x = toInt(r.token());
        const int y = toInt(r.token());
        if (!findNodeDef(typeId)) continue;
        Node& n = addNodeOfType(typeId, ImVec2((float)x, (float)y));
        if ((int)byIndex.size() <= idx) byIndex.resize(idx + 1, nullptr);
        byIndex[idx] = &n;
    }

    // pass 2: links (before defaults, so wildcard groups can resolve from them first)
    for (const std::string& ln : lines)
    {
        LineReader r{ ln };
        if (r.token() != "//@link") continue;
        const int a = toInt(r.token());
        const int b = toInt(r.token());
        const int c = toInt(r.token());
        const int d = toInt(r.token());
        Node* from = (a >= 0 && a < (int)byIndex.size()) ? byIndex[a] : nullptr;
        Node* to = (c >= 0 && c < (int)byIndex.size()) ? byIndex[c] : nullptr;
        connectNodes(from, b, to, d);
    }

    // pass 3: resolve wildcard types from the now-restored links (may reset unconnected defaults)
    for (Node* node : byIndex)
        resolveNodeTypes(node);

    // pass 4: input pin defaults (after resolution, so saved literals win over type defaults)
    for (const std::string& ln : lines)
    {
        LineReader r{ ln };
        if (r.token() != "//@pin") continue;
        const int ni = toInt(r.token());
        const int pi = toInt(r.token());
        const std::string lit = r.rest();
        if (ni < 0 || ni >= (int)byIndex.size() || !byIndex[ni]) continue;
        const auto& inputs = byIndex[ni]->getInputPins();
        if (pi >= 0 && pi < (int)inputs.size())
            inputs[pi]->defaultValue = lit;
    }

    // pass 5: dropdown-property selections
    for (const std::string& ln : lines)
    {
        LineReader r{ ln };
        if (r.token() != "//@enum") continue;
        const int ni = toInt(r.token());
        const int sel = toInt(r.token());
        if (ni >= 0 && ni < (int)byIndex.size() && byIndex[ni])
            byIndex[ni]->setEnumSelection(sel);
    }

    m_firstFrame = true;
    m_hasBaseline = false; // re-baseline against the freshly loaded graph once it renders
    return !m_nodes.empty();
}

void Scene::update(double deltaSec)
{
    ed::SetCurrentEditor(m_nodeEditorContext);
    ed::Begin("Script Editor", ImVec2(0.0, 0.0f));
    ed::GetStyle().NodePadding = ImVec4(8, 8, 8, 8);

    for (auto& node : m_nodes)
        node->update(deltaSec, m_firstFrame);

    processInteractions();

    for (auto& link : m_links)
        link->update(deltaSec, m_firstFrame);

    // Right-click the canvas to add a node from the palette.
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu())
    {
        m_pendingAddPos = ed::ScreenToCanvas(ImGui::GetMousePos());
        ImGui::OpenPopup("AddNodePopup");
    }
    if (ImGui::BeginPopup("AddNodePopup"))
    {
        // Group node defs by category (first-seen order), then split the categories across two balanced
        // columns so the menu doesn't grow into one very tall list.
        struct Category { std::string name; std::vector<const NodeDef*> defs; };
        std::vector<Category> categories;
        for (const NodeDef& def : nodeRegistry())
        {
            if (categories.empty() || categories.back().name != def.category)
                categories.push_back({ def.category, {} });
            categories.back().defs.push_back(&def);
        }

        int totalRows = 0; // each category costs its items + one header row
        for (const Category& c : categories) totalRows += (int)c.defs.size() + 1;
        size_t split = categories.size();
        for (size_t i = 0, running = 0; i < categories.size(); ++i)
        {
            running += categories[i].defs.size() + 1;
            if ((int)running * 2 >= totalRows) { split = i + 1; break; }
        }

        // Fixed, content-derived column width. Without this the popup auto-resizes to its content while the
        // full-width SeparatorText/MenuItems size to the (growing) window — a feedback loop that runs away.
        float colWidth = 0.0f;
        for (const Category& c : categories)
        {
            const float h = ImGui::CalcTextSize(c.name.c_str()).x;
            if (h > colWidth) colWidth = h;
            for (const NodeDef* def : c.defs)
            {
                const float w = ImGui::CalcTextSize(def->displayName.c_str()).x;
                if (w > colWidth) colWidth = w;
            }
        }
        const ImGuiStyle& style = ImGui::GetStyle();
        colWidth += style.FramePadding.x * 2.0f + style.CellPadding.x * 2.0f + style.ItemSpacing.x + 6.0f;

        auto renderRange = [&](size_t begin, size_t end)
        {
            for (size_t i = begin; i < end; ++i)
            {
                ImGui::SeparatorText(categories[i].name.c_str());
                for (const NodeDef* def : categories[i].defs)
                    if (ImGui::MenuItem(def->displayName.c_str()))
                        addNodeOfType(def->typeId, m_pendingAddPos);
            }
        };

        if (ImGui::BeginTable("##addnodecols", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX))
        {
            ImGui::TableSetupColumn("##a", ImGuiTableColumnFlags_WidthFixed, colWidth);
            ImGui::TableSetupColumn("##b", ImGuiTableColumnFlags_WidthFixed, colWidth);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            renderRange(0, split);
            ImGui::TableSetColumnIndex(1);
            renderRange(split, categories.size());
            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
    ed::Resume();

    if (m_firstFrame)
        ed::NavigateToContent(0.0f);

    ed::End();

    // Capture the clean-state baseline once the graph has rendered (node positions are now valid).
    if (!m_hasBaseline)
    {
        m_baselineState = serializeGraph();
        m_hasBaseline = true;
    }

    m_firstFrame = false;
}

bool Scene::isDirty()
{
    if (!m_hasBaseline)
        return false; // baseline not captured yet (just loaded) — treat as clean until it settles
    if (m_nodeEditorContext)
        ed::SetCurrentEditor(m_nodeEditorContext);
    return serializeGraph() != m_baselineState;
}

void Scene::processInteractions()
{
    if (ed::BeginCreate())
    {
        ed::PinId pin1Id, pin2Id;
        if (ed::QueryNewLink(&pin1Id, &pin2Id))
        {
            if (pin1Id && pin2Id && pin1Id != pin2Id)
            {
                Pin* pin1 = pin1Id.AsPointer<Pin>();
                Pin* pin2 = pin2Id.AsPointer<Pin>();
                if (pin1->type == EPinType_Output)
                    std::swap(pin1, pin2);

                if (pin1->type == EPinType_Input && pin2->type == EPinType_Output &&
                    pin1->numConnections == 0 && pinsCompatible(pin1, pin2))
                {
                    if (ed::AcceptNewItem())
                    {
                        pin1->numConnections++;
                        pin2->numConnections++;
                        m_links.emplace_back(std::make_unique<Link>())->initialize(ed::PinId(pin1), ed::PinId(pin2));
                        resolveNodeTypes(pin1->node); // a new connection may resolve a wildcard group
                        resolveNodeTypes(pin2->node);
                    }
                }
                else
                {
                    ed::RejectNewItem();
                }
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete())
    {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId))
        {
            if (ed::AcceptDeletedItem())
            {
                for (auto it = m_links.begin(); it != m_links.end(); ++it)
                {
                    if ((*it)->getId() == deletedLinkId)
                    {
                        Pin* pin1 = (*it)->getInputId().AsPointer<Pin>();
                        Pin* pin2 = (*it)->getOutputId().AsPointer<Pin>();
                        if (pin1) pin1->numConnections--;
                        if (pin2) pin2->numConnections--;
                        Node* node1 = pin1 ? pin1->node : nullptr;
                        Node* node2 = pin2 ? pin2->node : nullptr;
                        m_links.erase(it);
                        resolveNodeTypes(node1); // a removed connection may revert a wildcard group
                        resolveNodeTypes(node2);
                        break;
                    }
                }
            }
        }

        ed::NodeId deletedNodeId;
        while (ed::QueryDeletedNode(&deletedNodeId))
            if (ed::AcceptDeletedItem())
                removeNode(deletedNodeId);
    }
    ed::EndDelete();
}
