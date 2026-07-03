export module Entity:ScriptEventManager;

import Core;
import Core.LPMultiMap;

import :ScriptContext;
import Script;

class Entity;
export class ScriptEventManager
{
public:

    // Registers the script-(re)load hook on the ScriptHost. Call once at startup from main, AFTER both
    // Globals singletons exist. This can't live in the constructor: scriptEvents (Entity) and scriptHost
    // (Script) are globals in different modules with no defined construction order, so scriptHost's own
    // construction can run later and reset m_scriptLoadedCallback back to its nullptr default, silently
    // dropping the registration.
    void initialize();

    using EventKey = uint32;

    EventKey getEventKeyForName(const std::string& eventName)
    {
        auto it = m_eventNameKeyLookup.find(eventName);
        if (it == m_eventNameKeyLookup.end())
        {
            m_eventNameKeyLookup[eventName] = m_nextEventKey;
            return m_nextEventKey++;
        }
        return it->second;
    }

    void fireEvent(EventKey key);

	void fireEvent(const std::string& eventName)
	{
		auto it = m_eventNameKeyLookup.find(eventName);
		if (it == m_eventNameKeyLookup.end())
			return;
		fireEvent(it->second);
	}

private:

    void onScriptLoadedCallback(const ScriptModule* script, const std::vector<std::string>& oldNames);

	friend class ScriptComponent;
    void registerListener(const ScriptModule* script, Entity* entity, void* scriptData)
    {
        m_listenersByScript.insert(script, { entity, scriptData });
    }

    void unregisterListener(const ScriptModule* script, Entity* entity)
    {
        auto range = m_listenersByScript.equalRange(script);
        for (auto it = range.begin(); it != range.end();)
        {
            if (it->second.entity == entity)
            {
                m_listenersByScript.eraseOne(it);
                break;
            }
            else
                ++it;
        }
    }

private:

    struct Entry
    {
		Entity* entity = nullptr;
		void* scriptData = nullptr;
    };

	std::unordered_map<std::string, EventKey> m_eventNameKeyLookup;
	EventKey m_nextEventKey = 1;

	std::unordered_map<EventKey, std::vector<const ScriptModule*>> m_listenersByEvent;
	LPMultiMap<const ScriptModule*, Entry> m_listenersByScript;
};

export namespace Globals
{
    ScriptEventManager scriptEvents;

    std::vector<void*> scriptDestroyRequests; // entities to remove entirely (see ScriptContext::destroyEntity)
    std::vector<void*> scriptRootRemovals;    // entities that gained a new parent this frame -> drop the stale root ref
    std::vector<void*> scriptRootAdditions;   // heap-boxed EntityPtr* for entities that (re)became root this frame
}

inline void ScriptEventManager::initialize()
{
    Globals::scriptHost.m_scriptLoadedCallback = [](const ScriptModule* script, const std::vector<std::string>& oldNames) { Globals::scriptEvents.onScriptLoadedCallback(script, oldNames); };
}