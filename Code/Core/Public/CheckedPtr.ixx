export module Core.CheckedPtr;

import Core;

export class RefCheckable;

struct RefCheckableHasher
{
	size_t operator()(const RefCheckable* obj) const
	{
		return std::hash<std::size_t>()(reinterpret_cast<std::size_t>(obj) & 0x0000'FFFF'FFFF'FFFF);
	}
};

struct RefCheckableEqual
{
	bool operator()(const RefCheckable* a, const RefCheckable* b) const
	{
		return (reinterpret_cast<size_t>(a) & 0x0000'FFFF'FFFF'FFFF) == (reinterpret_cast<size_t>(b) & 0x0000'FFFF'FFFF'FFFF);
	}
};

static std::unordered_set<const RefCheckable*, RefCheckableHasher, RefCheckableEqual> g_refCheckableSet;

static inline void registerRefCheckable(const RefCheckable* obj)
{
	auto ret = g_refCheckableSet.insert(obj);
	assert(ret.second && "RefCheckable object already registered!");
}

static inline void unregisterRefCheckable(const RefCheckable* obj)
{
	auto it = g_refCheckableSet.find(obj);
	assert(it != g_refCheckableSet.end() && "RefCheckable object not registered!");
	uint64 r = reinterpret_cast<uint64>(*it);
	assert(r <= 0x0000'FFFF'FFFF'FFFF && "RefCheckable object destroyed while still having references!");
	g_refCheckableSet.erase(it);
}

static inline void addRefCount(const RefCheckable* obj)
{
	auto it = g_refCheckableSet.find(obj);
	assert(it != g_refCheckableSet.end() && "RefCheckable object has already been destroyed!");
	uint64& r = reinterpret_cast<uint64&>(const_cast<const RefCheckable*&>(*it));
	assert(r < 0xFFFF'0000'0000'0000 && "RefCheckable reference count overflow!");
	r += 0x0001'0000'0000'0000;
}

static inline void removeRefCount(const RefCheckable* obj)
{
	auto it = g_refCheckableSet.find(obj);
	assert(it != g_refCheckableSet.end() && "RefCheckable object has already been destroyed!");
	uint64& r = reinterpret_cast<uint64&>(const_cast<const RefCheckable*&>(*it));
	assert(r >= 0x0001'0000'0000'0000 && "RefCheckable reference count underflow!");
	r -= 0x0001'0000'0000'0000;
}

static inline uint16 getRefCount(const RefCheckable* obj)
{
	auto it = g_refCheckableSet.find(obj);
	assert(it != g_refCheckableSet.end() && "RefCheckable object has already been destroyed!");
	uint64 r = reinterpret_cast<const uint64&>(const_cast<const RefCheckable*&>(*it));
	return uint16((r & 0xFFFF'0000'0000'0000) >> 48);
}

export class RefCheckable
{
public:

	RefCheckable()                                { registerRefCheckable(this); }
	RefCheckable(const RefCheckable& copy)        { registerRefCheckable(this); }
	RefCheckable(const RefCheckable&& move)       { registerRefCheckable(this); }
	RefCheckable& operator=(const RefCheckable&)  { return *this; }
	RefCheckable& operator=(const RefCheckable&&) { return *this; }
	~RefCheckable()                               { unregisterRefCheckable(this); }
};

export template <typename T>
concept IsRefCheckable = std::is_base_of<RefCheckable, T>::value;

export template<IsRefCheckable T>
class CheckedPtr
{
public:
	CheckedPtr()                       : m_ref(nullptr)    {}
	CheckedPtr(T& ref)                 : m_ref(&ref)       { addRefCount(m_ref); }
	CheckedPtr(T* ref)                 : m_ref(ref)        { addRefCount(m_ref); }
	CheckedPtr(const CheckedPtr& copy) : m_ref(copy.m_ref) { addRefCount(m_ref); }
	CheckedPtr(CheckedPtr&& move)      : m_ref(move.m_ref) { move.m_ref = nullptr; }
	~CheckedPtr()                                          { if (m_ref) removeRefCount(m_ref); }
	T& operator*() const                                   { return *m_ref; }
	T* operator->() const                                  { return m_ref; }

	CheckedPtr& operator=(const CheckedPtr& copy)
	{
		if (m_ref != nullptr && m_ref != copy.m_ref)
		{
			removeRefCount(m_ref);
		}
		m_ref = copy.m_ref;
		if (copy.m_ref != nullptr)
		{
			addRefCount(m_ref);
		}
		return *this;
	}

	CheckedPtr& operator=(CheckedPtr&& move)
	{
		if (m_ref != nullptr && m_ref != move.m_ref)
		{
			removeRefCount(m_ref);
		}
		m_ref = move.m_ref;
		move.m_ref = nullptr;
		return *this;
	}

private:
	T* m_ref;
};
/*
struct Test : RefCheckable
{
	int foo = 0;
	uint16 blah() { return getRefCount(this); }
};

export void testCheckedPtr()
{
	{
		Test t1;
		CheckedPtr<Test> ref1(t1);
		assert(getRefCount(&t1) == 1);
		{
			CheckedPtr<Test> ref2 = ref1;
			assert(getRefCount(&t1) == 2);
		}
		Test t2 = t1;
		CheckedPtr<Test> ref2(t2);
		assert(getRefCount(&t2) == 1);
		{
			CheckedPtr<Test> ref3(t2);
			assert(getRefCount(&t2) == 2);
		}

		{
			Test t3 = std::move(t2);
			{
				CheckedPtr<Test> ref3(t3);
				assert(getRefCount(&t3) == 1);
				CheckedPtr<Test> ref4 = std::move(ref3);
				assert(getRefCount(&t3) == 1);
			}
			assert(getRefCount(&t3) == 0);
		}

		//CheckedPtr<Test> ref4;
		//{
		//	Test t4;
		//	ref4 = t4;
		//}
	}
}*/