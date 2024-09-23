export module Core.LinearBitAllocator;

import Core;

export template<bool ThreadSafe = false>
class LinearBitAllocator final
{
public:

    LinearBitAllocator(uint32 initialSize)
    {
        m_pBits = std::make_unique<uint64[]>((initialSize + 63) / 64);
    }
    ~LinearBitAllocator() {};
    LinearBitAllocator(const LinearBitAllocator&) = delete;

    int acquireRange(uint32 size)
    {

    }

    void releaseRange(int idx, uint32 size)
    {

    }

private:

    void setBitRange(int start, int end);
    void clearBitRange(int start, int end);

    std::unique_ptr<uint64[]> m_pBits;
    int m_lastAcquiredIdx = 0;
};