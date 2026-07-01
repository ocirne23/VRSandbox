module;

#include <ScriptAPI.h>

export module Entity:ScriptCommands;

import Core;
import Core.glm;
import Core.LPMultiMap;

import :ScriptContext;
import Script;

// Deferred entity create/destroy requested by scripts, drained by App after the entity update pass
// (scripts run mid entity-tree-walk, so the entity list can't be mutated inline).
//
// The destroy queue stores opaque void* rather than Entity*: it is written from ScriptContext.cpp (which
// includes the ABI header's global forward-declared Entity) and read from App (the module-attached Entity).
// Those two Entity types don't merge under MSVC modules, so a shared Globals symbol whose mangled name
// embeds Entity would fail to link. void* keeps the symbol name Entity-free; pointer conversion is implicit.
export struct ScriptSpawnRequest
{
    std::string assetPath;
    glm::vec3   position;
};

class Entity;
export class ScriptEventManager
{
public:

	void fireEvent(const std::string& eventName)
	{
		auto range = m_listeners.equalRange(eventName);
		for (auto it = range.begin(); it != range.end(); ++it)
		{
			const Entry& entry = it->second;
			reinterpret_cast<ScriptOnEventFn>(entry.onEvent)(&Globals::scriptContext, entry.entity, entry.idx, entry.scriptData);
		}
	}

private:

	friend class ScriptComponent;
    void registerListener(const ScriptModule* module, Entity* entity, void* scriptData)
    {
        for (const auto& [eventName, idx] : module->eventIndexes)
        {
            m_listeners.insert(eventName, { module->onEvent, entity, scriptData, idx });
        }
    }

    void unregisterListener(const ScriptModule* module, Entity* entity)
    {
        for (const auto& [eventName, idx] : module->eventIndexes)
        {
            auto range = m_listeners.equalRange(eventName);
            for (auto it = range.begin(); it != range.end();)
            {
                if (it->second.entity == entity)
                {
                    m_listeners.eraseOne(it);
                    break;
                }
                else
                    ++it;
            }
        }
    }

private:

    struct Entry
    {
		void* onEvent = nullptr;
		Entity* entity = nullptr;
		void* scriptData = nullptr;
        int idx;
    };

	LPMultiMap<std::string, Entry, std::hash<std::string>, std::equal_to<std::string>> m_listeners;
};

export namespace Globals
{
    ScriptEventManager scriptEvents;

    std::vector<ScriptSpawnRequest> scriptSpawnRequests;
    std::vector<void*>              scriptDestroyRequests;
}
