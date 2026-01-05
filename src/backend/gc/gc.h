// Tyl Compiler - Garbage Collector
// Mark-and-sweep garbage collector for automatic memory management
#ifndef TYL_GC_H
#define TYL_GC_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_set>

namespace tyl {

// Object header for GC-managed allocations
// Placed immediately before the user data
struct GCObjectHeader {
    uint32_t size;          // Size of user data (not including header)
    uint16_t type;          // Object type tag for tracing
    uint8_t  marked;        // Mark bit for mark-and-sweep
    uint8_t  flags;         // Additional flags (pinned, finalized, etc.)
    GCObjectHeader* next;   // Next object in allocation list
};

// Object type tags for tracing
enum class GCObjectType : uint16_t {
    RAW = 0,        // Raw bytes, no pointers to trace
    STRING = 1,     // String data (no pointers)
    LIST = 2,       // List: [count, capacity, ptr to elements...]
    RECORD = 3,     // Record: [field_count, field_ptrs...]
    CLOSURE = 4,    // Closure: [fn_ptr, capture_count, captured_vars...]
    ARRAY = 5,      // Array of pointers (needs tracing)
    BOX = 6,        // Single boxed value
};

// GC flags
enum GCFlags : uint8_t {
    GC_FLAG_NONE = 0,
    GC_FLAG_PINNED = 1,     // Don't move or collect
    GC_FLAG_WEAK = 2,       // Weak reference
    GC_FLAG_FINALIZE = 4,   // Has finalizer
};

// GC statistics
struct GCStats {
    size_t totalAllocated;      // Total bytes currently allocated
    size_t totalCollections;    // Number of collections performed
    size_t totalFreed;          // Total bytes freed across all collections
    size_t objectCount;         // Current number of live objects
    size_t lastCollectionFreed; // Bytes freed in last collection
};

// Garbage Collector class
class GarbageCollector {
public:
    GarbageCollector();
    ~GarbageCollector();
    
    // Initialize the GC with heap size
    void init(size_t initialHeapSize = 1024 * 1024);  // 1MB default
    
    // Allocate memory (returns pointer to user data, header is before it)
    void* alloc(size_t size, GCObjectType type = GCObjectType::RAW);
    
    // Allocate with specific alignment
    void* allocAligned(size_t size, size_t alignment, GCObjectType type = GCObjectType::RAW);
    
    // Register a root pointer (stack variable, global, etc.)
    void addRoot(void** root);
    void removeRoot(void** root);
    
    // Register a root range (for stack scanning)
    void addRootRange(void** start, void** end);
    void removeRootRange(void** start);
    
    // Manual collection trigger
    void collect();
    
    // Force a full collection
    void collectFull();
    
    // Get statistics
    const GCStats& getStats() const { return stats_; }
    
    // Set collection threshold (collect when allocated > threshold)
    void setThreshold(size_t bytes) { collectionThreshold_ = bytes; }
    
    // Pin/unpin an object (prevent collection)
    void pin(void* ptr);
    void unpin(void* ptr);
    
    // Check if pointer is managed by GC
    bool isManaged(void* ptr) const;
    
    // Get object header from user pointer
    static GCObjectHeader* getHeader(void* ptr);
    
    // Shutdown and free all memory
    void shutdown();

private:
    // Mark phase: trace from roots
    void mark();
    void markObject(GCObjectHeader* obj);
    void traceObject(GCObjectHeader* obj);
    
    // Sweep phase: free unmarked objects
    void sweep();
    
    // Check if we should collect
    bool shouldCollect() const;
    
    // Heap management
    uint8_t* heap_;
    size_t heapSize_;
    size_t heapUsed_;
    
    // Allocation list (all objects)
    GCObjectHeader* allObjects_;
    
    // Root sets
    std::unordered_set<void**> roots_;
    std::vector<std::pair<void**, void**>> rootRanges_;
    
    // Statistics
    GCStats stats_;
    
    // Collection threshold
    size_t collectionThreshold_;
    
    // Is GC initialized?
    bool initialized_;
};

// Global GC instance (initialized at runtime startup)
extern GarbageCollector* g_gc;

// Runtime GC functions (called from generated code)
extern "C" {
    // Allocate GC-managed memory
    void* TYL_gc_alloc(size_t size, uint16_t type);
    
    // Allocate a string
    void* TYL_gc_alloc_string(size_t len);
    
    // Allocate a list with initial capacity
    void* TYL_gc_alloc_list(size_t capacity);
    
    // Allocate a record with field count
    void* TYL_gc_alloc_record(size_t fieldCount);
    
    // Allocate a closure with capture count
    void* TYL_gc_alloc_closure(size_t captureCount);
    
    // Push a stack frame (for conservative stack scanning)
    void TYL_gc_push_frame(void** frameBase);
    void TYL_gc_pop_frame();
    
    // Trigger collection
    void TYL_gc_collect();
    
    // Initialize GC (called at program start)
    void TYL_gc_init();
    
    // Shutdown GC (called at program end)
    void TYL_gc_shutdown();
    
    // Write barrier (for generational GC - future)
    void TYL_gc_write_barrier(void* obj, void* field, void* newValue);
    
    // Custom allocator support
    // Set custom allocator functions for GC to use
    // alloc: function to allocate memory (size, alignment) -> ptr
    // free: function to free memory (ptr, size)
    // userData: optional user data passed to allocator
    void TYL_gc_set_allocator(void* (*alloc)(size_t, size_t), void (*free)(void*, size_t), void* userData);
    
    // Reset to default system allocator
    void TYL_gc_reset_allocator();
    
    // Get the user data passed to set_allocator
    void* TYL_gc_get_allocator_userdata();
}

} // namespace tyl

#endif // TYL_GC_H
