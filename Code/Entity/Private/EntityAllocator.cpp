module Entity.Allocator;

import Core;
import Core.Allocator;

static uint32 alignUp(uint32 value, uint32 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

EntityAllocator::~EntityAllocator()
{
    for (void* chunk : m_chunks)
        Globals::allocator.deallocate(chunk);
    m_chunks.clear();
}

void EntityAllocator::addChunk(uint32 minSize)
{
    const uint32 chunkSize = minSize > CHUNK_SIZE ? minSize : CHUNK_SIZE;
    void* chunk = Globals::allocator.allocate(chunkSize); // 16-byte aligned
    m_chunks.push_back(chunk);
    m_cursor = static_cast<uint8*>(chunk);
    m_chunkEnd = m_cursor + chunkSize;
}

void* EntityAllocator::allocate(uint32 size)
{
    size = alignUp(size, ALIGNMENT);

    if (auto it = m_freeLists.find(size); it != m_freeLists.end() && it->second)
    {
        FreeBlock* block = it->second;
        it->second = block->next;
        return block;
    }

    if (m_cursor + size > m_chunkEnd) // also covers the very first allocation (cursor/end both null)
        addChunk(size);

    void* ptr = m_cursor;
    m_cursor += size;
    return ptr;
}

void EntityAllocator::deallocate(void* ptr, uint32 size)
{
    size = alignUp(size, ALIGNMENT);
    FreeBlock* block = static_cast<FreeBlock*>(ptr);
    block->next = m_freeLists[size];
    m_freeLists[size] = block;
}
