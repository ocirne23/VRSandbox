module;

#include <ScriptAPI.h>

export module Entity:ScriptCommands;

import Core;
import Core.LPMultiMap;

import :ScriptContext;
import Script;

// Deferred entity ownership changes requested by scripts, drained by App after the entity update pass
// (scripts run mid entity-tree-walk, so the root entity list can't be mutated inline).
//
// These queues store opaque void* rather than Entity*/EntityPtr: they are written from ScriptContext.cpp
// (which includes the ABI header's global forward-declared Entity) and read from App (the module-attached
// Entity). Those two Entity types don't merge under MSVC modules, so a shared Globals symbol whose mangled
// name embeds Entity would fail to link. void* keeps the symbol name Entity-free; pointer conversion is
// implicit for scriptDestroyRequests/scriptRootRemovals (raw, non-owning pointers used only to find-and-erase
// App's existing EntityPtr). scriptRootAdditions instead carries an owning reference across the boundary (a
// spawned entity, or one just detached to become root, has no other owner yet) -- ScriptContext.cpp heap-
// allocates an EntityPtr and pushes the box's address; App reclaims ownership with static_cast and deletes
// the box once it has moved the handle into its own entity list.
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
			reinterpret_cast<ScriptOnEventFn>(entry.script->onEvent)(&Globals::scriptContext, entry.entity, entry.idx, entry.scriptData);
		}
	}

private:

	friend class ScriptComponent;
    void registerListener(const ScriptModule* script, Entity* entity, void* scriptData)
    {
        for (const auto& [eventName, idx] : script->eventIndexes)
        {
            m_listeners.insert(eventName, { script, entity, scriptData, idx });
        }
    }

    void unregisterListener(const ScriptModule* script, Entity* entity)
    {
        for (const auto& [eventName, idx] : script->eventIndexes)
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
        const ScriptModule* script = nullptr;
		Entity* entity = nullptr;
		void* scriptData = nullptr;
        int idx;
    };

	LPMultiMap<std::string, Entry, std::hash<std::string>, std::equal_to<std::string>> m_listeners;
};

export namespace Globals
{
    ScriptEventManager scriptEvents;

    std::vector<void*> scriptDestroyRequests; // entities to remove entirely (see ScriptContext::destroyEntity)
    std::vector<void*> scriptRootRemovals;    // entities that gained a new parent this frame -> drop the stale root ref
    std::vector<void*> scriptRootAdditions;   // heap-boxed EntityPtr* for entities that (re)became root this frame
}
