// Tyl Compiler - GC Runtime Header
// Lightweight garbage collection runtime for generated executables
#ifndef TYL_GC_RUNTIME_H
#define TYL_GC_RUNTIME_H

#include <cstdint>
#include <cstddef>

namespace tyl {
namespace gc_runtime {

// Object header layout (16 bytes, placed before user data)
// This is embedded in every GC-managed allocation
struct ObjectHeader {
    uint32_t size;          // Size of user data
    uint16_t type;          // Object type for tracing
    uint8_t  marked;        // Mark bit
    uint8_t  flags;         // Flags (pinned, etc.)
    ObjectHeader* next;     // Next in allocation list
};

// Object types
enum ObjectType : uint16_t {
    OBJ_RAW = 0,        // Raw bytes, no pointers
    OBJ_STRING = 1,     // String (no pointers)
    OBJ_LIST = 2,       // List with pointer elements
    OBJ_RECORD = 3,     // Record with pointer fields
    OBJ_CLOSURE = 4,    // Closure with captures
    OBJ_MAP = 5,        // Hash map
};

// GC state (global, embedded in data section)
struct GCState {
    ObjectHeader* allObjects;   // Head of allocation list
    size_t totalAllocated;      // Total bytes allocated
    size_t threshold;           // Collection threshold
    bool enabled;               // GC enabled flag
    void** stackBottom;         // Bottom of stack for scanning
};

// Initialize GC state
inline void gc_init(GCState* state, void** stackBottom) {
    state->allObjects = nullptr;
    state->totalAllocated = 0;
    state->threshold = 1024 * 1024;  // 1MB default
    state->enabled = true;
    state->stackBottom = stackBottom;
}

// Get header from user pointer
inline ObjectHeader* get_header(void* ptr) {
    return reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(ptr) - sizeof(ObjectHeader)
    );
}

// Get user pointer from header
inline void* get_user_ptr(ObjectHeader* header) {
    return reinterpret_cast<uint8_t*>(header) + sizeof(ObjectHeader);
}

} // namespace gc_runtime
} // namespace tyl

#endif // TYL_GC_RUNTIME_H
