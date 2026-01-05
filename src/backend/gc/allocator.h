// Tyl Compiler - Custom Allocator Interface
// Allows users to specify alternative memory allocators
#ifndef TYL_ALLOCATOR_H
#define TYL_ALLOCATOR_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace tyl {

// Allocator function signatures
using AllocFn = void* (*)(size_t size, size_t alignment);
using FreeFn = void (*)(void* ptr, size_t size);
using ReallocFn = void* (*)(void* ptr, size_t oldSize, size_t newSize, size_t alignment);

// Allocator interface - can be implemented by users
struct Allocator {
    AllocFn alloc;          // Allocate memory
    FreeFn free;            // Free memory
    ReallocFn realloc;      // Reallocate memory (optional, can be nullptr)
    void* userData;         // User-provided context data
    const char* name;       // Allocator name for debugging
    
    // Default constructor - uses system allocator
    Allocator() : alloc(nullptr), free(nullptr), realloc(nullptr), 
                  userData(nullptr), name("default") {}
    
    // Constructor with custom functions
    Allocator(AllocFn a, FreeFn f, ReallocFn r = nullptr, 
              void* ud = nullptr, const char* n = "custom")
        : alloc(a), free(f), realloc(r), userData(ud), name(n) {}
};

// Allocator statistics
struct AllocatorStats {
    size_t totalAllocated;      // Total bytes currently allocated
    size_t totalAllocations;    // Number of allocations made
    size_t totalFrees;          // Number of frees made
    size_t peakUsage;           // Peak memory usage
    size_t currentObjects;      // Current number of live objects
};

// Built-in allocator types
enum class AllocatorType : uint8_t {
    SYSTEM = 0,         // Default system allocator (HeapAlloc/malloc)
    ARENA = 1,          // Arena/bump allocator (fast, no individual frees)
    POOL = 2,           // Pool allocator (fixed-size blocks)
    STACK = 3,          // Stack allocator (LIFO)
    CUSTOM = 4,         // User-provided custom allocator
};

// Arena allocator - fast bump allocation, frees all at once
class ArenaAllocator {
public:
    ArenaAllocator(size_t initialSize = 1024 * 1024);  // 1MB default
    ~ArenaAllocator();
    
    void* alloc(size_t size, size_t alignment = 8);
    void reset();  // Free all allocations at once
    
    size_t used() const { return offset_; }
    size_t capacity() const { return size_; }
    
    // Get as Allocator interface
    Allocator asAllocator();
    
private:
    uint8_t* buffer_;
    size_t size_;
    size_t offset_;
};

// Pool allocator - fixed-size block allocation
class PoolAllocator {
public:
    PoolAllocator(size_t blockSize, size_t blockCount);
    ~PoolAllocator();
    
    void* alloc();
    void free(void* ptr);
    
    size_t blockSize() const { return blockSize_; }
    size_t freeCount() const { return freeCount_; }
    
    // Get as Allocator interface
    Allocator asAllocator();
    
private:
    uint8_t* buffer_;
    void* freeList_;
    size_t blockSize_;
    size_t blockCount_;
    size_t freeCount_;
};

// Global allocator management
class AllocatorManager {
public:
    static AllocatorManager& instance();
    
    // Set the current allocator
    void setAllocator(const Allocator& alloc);
    void setAllocator(AllocatorType type);
    
    // Get the current allocator
    const Allocator& current() const { return current_; }
    
    // Get allocator by type
    const Allocator& getSystemAllocator() const { return systemAllocator_; }
    
    // Allocate/free using current allocator
    void* alloc(size_t size, size_t alignment = 8);
    void free(void* ptr, size_t size);
    void* realloc(void* ptr, size_t oldSize, size_t newSize, size_t alignment = 8);
    
    // Statistics
    const AllocatorStats& stats() const { return stats_; }
    void resetStats();
    
private:
    AllocatorManager();
    
    Allocator current_;
    Allocator systemAllocator_;
    AllocatorStats stats_;
};

// Runtime allocator functions (called from generated code)
extern "C" {
    // Set custom allocator functions
    void TYL_set_allocator(AllocFn alloc, FreeFn free, ReallocFn realloc, void* userData);
    
    // Reset to default system allocator
    void TYL_reset_allocator();
    
    // Allocate using current allocator
    void* TYL_alloc(size_t size);
    void* TYL_alloc_aligned(size_t size, size_t alignment);
    
    // Free using current allocator
    void TYL_free(void* ptr, size_t size);
    
    // Reallocate using current allocator
    void* TYL_realloc(void* ptr, size_t oldSize, size_t newSize);
    
    // Get allocator stats
    size_t TYL_allocator_total_allocated();
    size_t TYL_allocator_peak_usage();
}

} // namespace tyl

#endif // TYL_ALLOCATOR_H
