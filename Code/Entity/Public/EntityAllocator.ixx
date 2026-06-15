export module Entity.Allocator;

import Core;

// Pre-allocates entity memory in large chunks and sub-allocates variable-sized, 16-byte-aligned
// blocks for individual entities. Freed blocks are recycled through per-size free lists; the chunks
// themselves are returned to the engine allocator when the EntityAllocator is destroyed. Not
// thread-safe: entity creation/destruction is expected on a single thread (only the EntityPtr
// refcount is atomic).
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

    // Recycled blocks thread an intrusive list through their own (now-unused) storage.
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
