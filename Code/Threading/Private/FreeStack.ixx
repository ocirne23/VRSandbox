export module Threading:FreeStack;

import Core;

// Lock-free LIFO freelist over a fixed index range (Treiber stack, 32-bit ABA tag in the head).
// Chosen over a FIFO ring for the recycling pools because a preempted thread mid-operation can
// never make it LOOK empty to others (a ring has claimed-but-unpublished cells that stall the
// cursor behind them): pop fails only when the freelist is truly empty, and LIFO hands back the
// cache-hottest slot. Index links live in an internal array, payload types stay untouched; the
// racy link read of a concurrently repopped node is benign - the tag makes its CAS fail.
export class TaggedIndexStack final
{
public:

    static constexpr uint32 Invalid = 0xffffffffu;

    void initialize(uint32 capacity)
    {
        m_next = std::make_unique<std::atomic<uint32>[]>(capacity);
        m_head.store(uint64(Invalid), std::memory_order_relaxed);
    }

    void push(uint32 index)
    {
        uint64 head = m_head.load(std::memory_order_relaxed);
        for (;;)
        {
            m_next[index].store(uint32(head), std::memory_order_relaxed);
            const uint64 newHead = ((head + 0x1'0000'0000ull) & 0xffffffff'00000000ull) | index;
            if (m_head.compare_exchange_weak(head, newHead, std::memory_order_release, std::memory_order_relaxed))
                return;
        }
    }

    bool pop(uint32& outIndex)
    {
        uint64 head = m_head.load(std::memory_order_acquire);
        for (;;)
        {
            const uint32 index = uint32(head);
            if (index == Invalid)
                return false;
            const uint32 next = m_next[index].load(std::memory_order_relaxed);
            const uint64 newHead = ((head + 0x1'0000'0000ull) & 0xffffffff'00000000ull) | next;
            if (m_head.compare_exchange_weak(head, newHead, std::memory_order_acquire, std::memory_order_acquire))
            {
                outIndex = index;
                return true;
            }
        }
    }

    bool wasEmpty() const { return uint32(m_head.load(std::memory_order_relaxed)) == Invalid; }

private:

    std::unique_ptr<std::atomic<uint32>[]> m_next;
    alignas(64) std::atomic<uint64> m_head = uint64(Invalid);
};
