module Force;

import Core;
import Core.glm;
import Core.Tweaks;
import RendererVK;
import :System;

using namespace RendererVKLayout;

// CPU mirror of the shader lobe (force_field.inc.glsl): directional reach shaping. focus 0 = exact
// sphere; higher focus grows the forward reach x(1+f) and shrinks side/back, with the blend exponent
// sharpening so high focus reads as a spear. C1 in cosAngle (exponent >= 2), so surfaces stay smooth.
static float forceLobe(float cosAngle, float focus)
{
    const float side = 1.0f / (1.0f + 0.5f * focus);
    const float forward = 1.0f + focus;
    return glm::mix(side, forward, std::pow(std::max(cosAngle, 0.0f), 2.0f + focus));
}

static uint32 packDebugColor(const glm::vec3& c)
{
    const glm::vec3 s = glm::clamp(c, 0.0f, 1.0f) * 255.0f;
    return (uint32)s.x | ((uint32)s.y << 8) | ((uint32)s.z << 16) | 0xFF000000u;
}

// ---- ForceEmitter handle ----

ForceEmitter& ForceEmitter::operator=(ForceEmitter&& move) noexcept
{
    if (this != &move)
    {
        destroy();
        m_handle = move.m_handle;
        move.m_handle = 0;
    }
    return *this;
}

void ForceEmitter::destroy()
{
    if (m_handle != 0)
    {
        Globals::forceSystem.destroyEmitter(m_handle);
        m_handle = 0;
    }
}

void ForceEmitter::setTransform(const glm::vec3& pos, const glm::vec3& direction)
{
    if (ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
    {
        inst->pos = pos;
        inst->dir = direction;
    }
}

void ForceEmitter::setPosition(const glm::vec3& pos)
{
    if (ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
        inst->pos = pos;
}

void ForceEmitter::setOutput(float output)
{
    if (ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
        inst->output = output;
}

void ForceEmitter::setReach(float reach)
{
    if (ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
        inst->reach = reach;
}

void ForceEmitter::setFocus(float focus)
{
    if (ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
        inst->focus = focus;
}

void ForceEmitter::setTeam(uint32 team)
{
    if (ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
        inst->team = glm::min(team, MAX_FORCE_TEAMS - 1);
}

glm::vec3 ForceEmitter::getAppliedForce() const
{
    if (const ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
        return inst->appliedForce;
    return glm::vec3(0.0f);
}

float ForceEmitter::getPressure() const
{
    if (const ForceSystem::EmitterInstance* inst = Globals::forceSystem.resolveEmitter(m_handle))
        return inst->pressure;
    return 0.0f;
}

// ---- ForceQuery handle ----

ForceQuery& ForceQuery::operator=(ForceQuery&& move) noexcept
{
    if (this != &move)
    {
        destroy();
        m_handle = move.m_handle;
        move.m_handle = 0;
    }
    return *this;
}

void ForceQuery::destroy()
{
    if (m_handle != 0)
    {
        Globals::forceSystem.destroyQuery(m_handle);
        m_handle = 0;
    }
}

void ForceQuery::setPosition(const glm::vec3& pos)
{
    if (ForceSystem::QueryInstance* inst = Globals::forceSystem.resolveQuery(m_handle))
        inst->pos = pos;
}

ForceQuery::Result ForceQuery::getResult() const
{
    if (ForceSystem::QueryInstance* inst = Globals::forceSystem.resolveQuery(m_handle))
        return inst->result;
    return Result{};
}

// ---- ForceSystem ----

void ForceSystem::initialize()
{
    Tweak::boolean("Force", "Enabled", &m_params.enabled);
    Tweak::floatVar("Force", "Iso threshold", &m_params.isoThreshold, 0.01f, 2.0f);
    Tweak::intVar("Force", "March steps", &m_params.marchSteps, 8, 128);
    Tweak::floatVar("Force", "Big reach threshold (m)", &m_params.bigReachThreshold, 8.0f, 512.0f, 1.0f);
    Tweak::boolean("Force", "Use grid", &m_params.useGrid); // off = brute force (A-B correctness check)
    Tweak::floatVar("Force", "Force gain", &m_params.forceGain, 0.0f, 10.0f);
    Tweak::floatVar("Force/Shell", "Alpha", &m_params.shellAlpha, 0.0f, 1.0f);
    Tweak::floatVar("Force/Shell", "Interior alpha", &m_params.interiorAlpha, 0.0f, 1.0f);
    Tweak::floatVar("Force/Shell", "Backface alpha", &m_params.backfaceAlpha, 0.0f, 1.0f);
    Tweak::floatVar("Force/Shell", "Rim power", &m_params.rimPower, 0.5f, 8.0f);
    Tweak::floatVar("Force/Shell", "Rim intensity", &m_params.rimIntensity, 0.0f, 8.0f);
    Tweak::floatVar("Force/Glow", "Contact intensity", &m_params.contactGlowIntensity, 0.0f, 16.0f);
    Tweak::floatVar("Force/Glow", "Contact width", &m_params.contactGlowWidth, 0.01f, 1.0f);
    Tweak::floatVar("Force/Glow", "Geometry distance (m)", &m_params.geoGlowDistance, 0.0f, 4.0f);
    Tweak::floatVar("Force/Pattern", "Scale (1/m)", &m_params.patternScale, 0.01f, 8.0f);
    Tweak::floatVar("Force/Pattern", "Scroll speed", &m_params.patternSpeed, 0.0f, 4.0f);
    Tweak::floatVar("Force/Pattern", "Intensity", &m_params.patternIntensity, 0.0f, 4.0f);
    static const char* teamNames[MAX_FORCE_TEAMS] = { "Team 0", "Team 1", "Team 2", "Team 3", "Team 4", "Team 5", "Team 6", "Team 7" };
    for (uint32 i = 0; i < MAX_FORCE_TEAMS; ++i)
        Tweak::color3("Force/Teams", teamNames[i], &m_params.teamColors[i]);
    Tweak::boolean("Force/Debug", "Draw emitters", &m_debugDraw);
    Tweak::boolean("Force/Debug", "Draw queries", &m_debugDrawQueries);
}

static ForceEmitterGpu buildEmitterGpu(const glm::vec3& pos, const glm::vec3& dir, float output,
    float reach, float focus, uint32 team, float phaseSeed)
{
    ForceEmitterGpu gpu;
    gpu.posReach = glm::vec4(pos, glm::max(reach, 1e-3f));
    const glm::vec3 d = glm::dot(dir, dir) > 1e-6f ? glm::normalize(dir) : glm::vec3(0.0f, 1.0f, 0.0f);
    gpu.dirFocus = glm::vec4(d, glm::max(focus, 0.0f));
    gpu.outputParams = glm::vec4(glm::max(output, 0.0f), 1.0f, phaseSeed, 0.0f);
    gpu.teamFlags = glm::uvec4(glm::min(team, MAX_FORCE_TEAMS - 1), FORCE_FLAG_ACTIVE, 0u, 0u);
    return gpu;
}

ForceEmitter ForceSystem::createEmitter(uint32 team, const glm::vec3& pos, const glm::vec3& direction,
    float output, float reach, float focus)
{
    const uint32 slot = Globals::rendererVK.createForceEmitter(
        buildEmitterGpu(pos, direction, output, reach, focus, team, 0.0f));
    if (slot == UINT32_MAX)
    {
        printf("ForceSystem: out of force emitter slots (%u live)\n", m_numLiveEmitters);
        return ForceEmitter();
    }
    uint32 idx;
    if (!m_freeEmitters.empty())
    {
        idx = m_freeEmitters.back();
        m_freeEmitters.pop_back();
    }
    else
    {
        m_emitters.emplace_back();
        idx = (uint32)m_emitters.size() - 1;
    }
    EmitterInstance& inst = m_emitters[idx];
    inst = EmitterInstance{};
    inst.generation = m_generationCounter++;
    if (m_generationCounter == 0)
        m_generationCounter = 1;
    inst.rendererSlot = slot;
    inst.team = glm::min(team, MAX_FORCE_TEAMS - 1);
    inst.output = output;
    inst.reach = reach;
    inst.focus = focus;
    inst.pos = pos;
    inst.dir = direction;
    inst.phaseSeed = 0.0f; // per-emitter pattern phase removed: any per-emitter shading term prints
                           // a seam where merged shells switch owners (the pattern is world-anchored)
    ++m_numLiveEmitters;
    return ForceEmitter(((uint64)inst.generation << 32) | idx);
}

ForceQuery ForceSystem::createQuery(const glm::vec3& pos)
{
    const uint32 slot = Globals::rendererVK.createForceQuerySlot();
    if (slot == UINT32_MAX)
    {
        printf("ForceSystem: out of force query slots\n");
        return ForceQuery();
    }
    uint32 idx;
    if (!m_freeQueries.empty())
    {
        idx = m_freeQueries.back();
        m_freeQueries.pop_back();
    }
    else
    {
        m_queries.emplace_back();
        idx = (uint32)m_queries.size() - 1;
    }
    QueryInstance& inst = m_queries[idx];
    inst = QueryInstance{};
    inst.generation = m_generationCounter++;
    if (m_generationCounter == 0)
        m_generationCounter = 1;
    inst.rendererSlot = slot;
    inst.pos = pos;
    return ForceQuery(((uint64)inst.generation << 32) | idx);
}

ForceSystem::EmitterInstance* ForceSystem::resolveEmitter(uint64 handle)
{
    const uint32 idx = (uint32)handle;
    const uint32 gen = (uint32)(handle >> 32);
    if (gen != 0 && idx < m_emitters.size() && m_emitters[idx].generation == gen)
        return &m_emitters[idx];
    return nullptr;
}

const ForceSystem::EmitterInstance* ForceSystem::resolveEmitter(uint64 handle) const
{
    return const_cast<ForceSystem*>(this)->resolveEmitter(handle);
}

ForceSystem::QueryInstance* ForceSystem::resolveQuery(uint64 handle)
{
    const uint32 idx = (uint32)handle;
    const uint32 gen = (uint32)(handle >> 32);
    if (gen != 0 && idx < m_queries.size() && m_queries[idx].generation == gen)
        return &m_queries[idx];
    return nullptr;
}

void ForceSystem::destroyEmitter(uint64 handle)
{
    if (EmitterInstance* inst = resolveEmitter(handle))
    {
        Globals::rendererVK.destroyForceEmitter(inst->rendererSlot);
        inst->generation = 0;
        m_freeEmitters.push_back((uint32)handle);
        --m_numLiveEmitters;
    }
}

void ForceSystem::destroyQuery(uint64 handle)
{
    if (QueryInstance* inst = resolveQuery(handle))
    {
        Globals::rendererVK.destroyForceQuerySlot(inst->rendererSlot);
        inst->generation = 0;
        m_freeQueries.push_back((uint32)handle);
    }
}

void ForceSystem::update(Renderer& renderer, float)
{
    renderer.setForceFieldParams(m_params);
    for (EmitterInstance& inst : m_emitters)
    {
        if (inst.generation == 0)
            continue;
        renderer.updateForceEmitter(inst.rendererSlot,
            buildEmitterGpu(inst.pos, inst.dir, inst.output, inst.reach, inst.focus, inst.team, inst.phaseSeed));
        // Latch the GPU force readback (slot-indexed, ~2 frames old; zero until the first lands).
        const glm::vec4 readback = renderer.getForceEmitterReadback(inst.rendererSlot);
        inst.appliedForce = glm::vec3(readback);
        inst.pressure = readback.w;
        if (m_debugDraw)
            debugDrawEmitter(renderer, inst);
    }
    for (QueryInstance& query : m_queries)
    {
        if (query.generation == 0)
            continue;
        renderer.setForceQuery(query.rendererSlot, query.pos);
        const RendererVKLayout::ForceQueryResult result = renderer.getForceQueryReadback(query.rendererSlot);
        query.result.valid = result.frameStamp != 0u;
        query.result.inside = result.owningTeam < MAX_FORCE_TEAMS;
        query.result.owningTeam = query.result.inside ? result.owningTeam : 0u;
        query.result.ownField = result.ownField;
        query.result.opposingField = result.bestOpposingField;
        if (m_debugDrawQueries)
        {
            const uint32 color = query.result.inside
                ? packDebugColor(m_params.teamColors[query.result.owningTeam]) : 0xFF404040u;
            const float s = 0.25f;
            renderer.addDebugLine(query.pos - glm::vec3(s, 0, 0), query.pos + glm::vec3(s, 0, 0), color);
            renderer.addDebugLine(query.pos - glm::vec3(0, s, 0), query.pos + glm::vec3(0, s, 0), color);
            renderer.addDebugLine(query.pos - glm::vec3(0, 0, s), query.pos + glm::vec3(0, 0, s), color);
        }
    }
}

// Draws the emitter's UNCONTESTED iso surface (what its bubble looks like alone): two longitudinal
// rings through the direction axis + the equatorial ring, all following the focus lobe, plus a
// direction tick. Deformation against other bubbles only exists in the field evaluation — this is
// the authoring view of reach/output/focus, not the equilibrium surface.
void ForceSystem::debugDrawEmitter(Renderer& renderer, const EmitterInstance& inst) const
{
    const glm::vec3 dir = glm::dot(inst.dir, inst.dir) > 1e-6f ? glm::normalize(inst.dir) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 ref = std::abs(dir.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(dir, ref));
    const glm::vec3 up = glm::cross(right, dir);
    const uint32 color = packDebugColor(m_params.teamColors[glm::min(inst.team, MAX_FORCE_TEAMS - 1)]);

    // Isolated-bubble radius: field output*(1-u^2)^2 = iso  =>  u = sqrt(1 - sqrt(iso/output)).
    const float isoFrac = inst.output > m_params.isoThreshold
        ? std::sqrt(1.0f - std::sqrt(m_params.isoThreshold / inst.output)) : 0.0f;
    const auto isoRadius = [&](float cosAngle) {
        return glm::max(inst.reach, 1e-3f) * forceLobe(cosAngle, inst.focus) * isoFrac;
    };

    constexpr int SEG = 32;
    constexpr float STEP = 6.2831853f / SEG;
    for (const glm::vec3& planeAxis : { right, up })
    {
        glm::vec3 prev = inst.pos + dir * isoRadius(1.0f);
        for (int i = 1; i <= SEG; ++i)
        {
            const float theta = STEP * i;
            const float c = std::cos(theta);
            const glm::vec3 p = inst.pos + (dir * c + planeAxis * std::sin(theta)) * isoRadius(c);
            renderer.addDebugLine(prev, p, color);
            prev = p;
        }
    }
    const float equator = isoRadius(0.0f);
    glm::vec3 prev = inst.pos + right * equator;
    for (int i = 1; i <= SEG; ++i)
    {
        const float phi = STEP * i;
        const glm::vec3 p = inst.pos + (right * std::cos(phi) + up * std::sin(phi)) * equator;
        renderer.addDebugLine(prev, p, color);
        prev = p;
    }
    renderer.addDebugLine(inst.pos, inst.pos + dir * glm::max(isoRadius(1.0f) * 1.15f, 0.25f), color);
}
