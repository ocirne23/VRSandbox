
#define EMPTY_ENTRY = 0xFFFFFFFFu;

struct Table
{
    uint tableSize;
    uint table[];
};

struct Value
{
    ivec4 values[];
};

uint getHashTableIdx(ivec3 p, uint tableSize) {
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n = n ^ (n >> 4u);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return uint(n % tableSize);
}

void insert(ivec3 pos, uint id)
{
    uint idx = getHashTableIdx(pos, tableSize);
    while (atomicCompSwap(table[idx], EMPTY_ENTRY, id) != EMPTY_ENTRY)
    {
        idx = (idx + 1) % tableSize;
    }
}

uint find(ivec3 pos, uint hash, inout uint idx)
{
    while (table[idx] != EMPTY_ENTRY)
    {
        if (values[idx] == ivec4(pos, hash))
        {
            return idx;
        }
        idx++;
    }
    return EMPTY_ENTRY;
}

void main()
{
    uint hash = getHashTableIdx(pos, tableSize);
    uint idx = hash;
    while(idx = find(in_pos, hash, idx) != EMPTY_ENTRY)
    {
        ivec4 val = values[idx];

    }
}