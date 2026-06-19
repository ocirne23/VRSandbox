export module Entity:Allocator;

import Core;

export class EntityAllocator final
{
public:

    ~EntityAllocator();

    [[nodiscard]] void* allocate(uint32 size);
    void deallocate(void* ptr, uint32 size);

    size_t getNumChunks() const { return m_chunks.size(); }

private:

    static constexpr uint32 ALIGNMENT = 16;
    static constexpr uint32 CHUNK_SIZE = 1u << 20; // 1 MB

    struct FreeBlock { FreeBlock* next; };

    void addChunk(uint32 minSize);

    std::vector<void*> m_chunks;                        // chunk bases, freed in the destructor
    uint8* m_cursor = nullptr;                          // bump pointer within the active chunk
    uint8* m_chunkEnd = nullptr;
    std::unordered_map<uint32, FreeBlock*> m_freeLists; // exact block size -> recycled blocks
};

export namespace Globals
{
    EntityAllocator entityAllocator;
}
