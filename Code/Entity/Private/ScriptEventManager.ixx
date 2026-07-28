export module Entity:ScriptEventManager;

import Core;
import Core.LPMultiMap;

import :ScriptContext;
import :Entity;
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

    // Mints a key for a never-seen name. Keys are minted when a script REGISTERS its entry names
    // (onScriptLoadedCallback, main thread); firing uses findEventKey instead, so no worker ever mutates
    // the map -- minting on a fire would only produce a key no script has a listener for.
    EventKey getEventKeyForName(const std::string& eventName)
    {
        {
            const std::shared_lock<std::shared_mutex> read(m_eventKeyMutex);
            if (auto it = m_eventNameKeyLookup.find(eventName); it != m_eventNameKeyLookup.end())
                return it->second;
        }
        const std::lock_guard<std::shared_mutex> write(m_eventKeyMutex);
        const auto [it, inserted] = m_eventNameKeyLookup.emplace(eventName, m_nextEventKey);
        if (inserted)
            ++m_nextEventKey;
        return it->second;
    }

    // Read-only lookup for the fire paths (reachable from worker threads). 0 = no script ever declared
    // this name, and 0 is never a live key.
    EventKey findEventKey(const std::string& eventName) const
    {
        const std::shared_lock<std::shared_mutex> read(m_eventKeyMutex);
        const auto it = m_eventNameKeyLookup.find(eventName);
        return it != m_eventNameKeyLookup.end() ? it->second : 0;
    }

    void fireEvent(EventKey key);

	void fireEvent(const std::string& eventName)
	{
		if (const EventKey key = findEventKey(eventName))
			fireEvent(key);
	}

	// Every add* below is called from script thunks, which run on worker threads during the parallel
	// entity pass; the drain is main-thread. The mutex is held only for the push.
    std::vector<EntityChange> takeEntityChanges()
    {
		std::vector<EntityChange> changes;
		const std::lock_guard<std::mutex> lock(m_entityChangeMutex);
		changes.swap(m_entityChanges);
		return changes;
    }

	inline void addDestroyRequest(EntityPtr&& entity)
	{
		const std::lock_guard<std::mutex> lock(m_entityChangeMutex);
		m_entityChanges.emplace_back(EntityChange::Delete{ std::move(entity) });
	}

	inline void addReparentRequest(EntityPtr&& entity, EntityPtr&& newParent)
	{
		const std::lock_guard<std::mutex> lock(m_entityChangeMutex);
		m_entityChanges.emplace_back(EntityChange::Reparent{ std::move(entity), std::move(newParent) });
	}

	inline void addSpawnRequest(std::string path, const glm::vec3& position)
	{
		const std::lock_guard<std::mutex> lock(m_entityChangeMutex);
		m_entityChanges.emplace_back(EntityChange::SpawnAtPosition{ std::move(path), position });
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

    std::vector<EntityChange> m_entityChanges;
    std::mutex m_entityChangeMutex;

    struct Entry
    {
		Entity* entity = nullptr;
		void* scriptData = nullptr;
    };

	std::unordered_map<std::string, EventKey> m_eventNameKeyLookup;
	EventKey m_nextEventKey = 1;
	mutable std::shared_mutex m_eventKeyMutex; // both guarded by this; only script load ever writes

	std::unordered_map<EventKey, std::vector<const ScriptModule*>> m_listenersByEvent;
	LPMultiMap<const ScriptModule*, Entry> m_listenersByScript;
};

export namespace Globals
{
    ScriptEventManager scriptEvents;
}

inline void ScriptEventManager::initialize()
{
    Globals::scriptHost.m_scriptLoadedCallback = [](const ScriptModule* script, const std::vector<std::string>& oldNames) { Globals::scriptEvents.onScriptLoadedCallback(script, oldNames); };
}