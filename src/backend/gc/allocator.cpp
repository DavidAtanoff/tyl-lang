// Tyl Compiler - Custom Allocator Implementation
#include "allocator.h"
#include <cstdlib>
#include <cstring>

// Avoid Windows macro conflicts
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace tyl {

// ============================================================================
// System Allocator Implementation
// ============================================================================

static void* systemAlloc(size_t size, size_t alignment) {
#ifdef _WIN32
    // Use HeapAlloc on Windows
    HANDLE heap = GetProcessHeap();
    void* ptr = HeapAlloc(heap, HEAP_ZERO_MEMORY, size);
    return ptr;
#else
    // Use aligned_alloc on other platforms
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    void* ptr = aligned_alloc(alignment, (size + alignment - 1) & ~(alignment - 1));
    if (ptr) std::memset(ptr, 0, size);
    return ptr;
#endif
}

static void systemFree(void* ptr, size_t size) {
    (void)size;  // Size not needed for system free
#ifdef _WIN32
    HANDLE heap = GetProcessHeap();
    HeapFree(heap, 0, ptr);
#else
    std::free(ptr);
#endif
}

static void* systemRealloc(void* ptr, size_t oldSize, size_t newSize, size_t alignment) {
    (void)alignment;  // System realloc doesn't support alignment
    (void)oldSize;
#ifdef _WIN32
    HANDLE heap = GetProcessHeap();
    return HeapReAlloc(heap, HEAP_ZERO_MEMORY, ptr, newSize);
#else
    return std::realloc(ptr, newSize);
#endif
}

// ============================================================================
// Arena Allocator Implementation
// ============================================================================

ArenaAllocator::ArenaAllocator(size_t initialSize) 
    : size_(initialSize), offset_(0) {
    buffer_ = static_cast<uint8_t*>(systemAlloc(initialSize, 16));
}

ArenaAllocator::~ArenaAllocator() {
    if (buffer_) {
        systemFree(buffer_, size_);
    }
}

void* ArenaAllocator::alloc(size_t size, size_t alignment) {
    // Align the current offset
    size_t aligned = (offset_ + alignment - 1) & ~(alignment - 1);
    
    if (aligned + size > size_) {
        // Out of space - could grow here, but for simplicity return nullptr
        return nullptr;
    }
    
    void* ptr = buffer_ + aligned;
    offset_ = aligned + size;
    return ptr;
}

void ArenaAllocator::reset() {
    offset_ = 0;
    std::memset(buffer_, 0, size_);
}

static void* arenaAllocWrapper(size_t size, size_t alignment) {
    // This would need to access the arena instance - simplified for now
    return nullptr;
}

static void arenaFreeWrapper(void* ptr, size_t size) {
    // Arena doesn't free individual allocations
    (void)ptr;
    (void)size;
}

Allocator ArenaAllocator::asAllocator() {
    return Allocator(arenaAllocWrapper, arenaFreeWrapper, nullptr, this, "arena");
}

// ============================================================================
// Pool Allocator Implementation
// ============================================================================

PoolAllocator::PoolAllocator(size_t blockSize, size_t blockCount)
    : blockSize_(blockSize), blockCount_(blockCount), freeCount_(blockCount) {
    
    // Ensure block size is at least pointer-sized for free list
    if (blockSize_ < sizeof(void*)) {
        blockSize_ = sizeof(void*);
    }
    
    // Allocate the pool
    buffer_ = static_cast<uint8_t*>(systemAlloc(blockSize_ * blockCount_, 16));
    
    // Initialize free list
    freeList_ = buffer_;
    for (size_t i = 0; i < blockCount_ - 1; i++) {
        void** block = reinterpret_cast<void**>(buffer_ + i * blockSize_);
        *block = buffer_ + (i + 1) * blockSize_;
    }
    // Last block points to nullptr
    void** lastBlock = reinterpret_cast<void**>(buffer_ + (blockCount_ - 1) * blockSize_);
    *lastBlock = nullptr;
}

PoolAllocator::~PoolAllocator() {
    if (buffer_) {
        systemFree(buffer_, blockSize_ * blockCount_);
    }
}

void* PoolAllocator::alloc() {
    if (!freeList_) return nullptr;
    
    void* block = freeList_;
    freeList_ = *static_cast<void**>(freeList_);
    freeCount_--;
    
    std::memset(block, 0, blockSize_);
    return block;
}

void PoolAllocator::free(void* ptr) {
    if (!ptr) return;
    
    // Add back to free list
    *static_cast<void**>(ptr) = freeList_;
    freeList_ = ptr;
    freeCount_++;
}

static void* poolAllocWrapper(size_t size, size_t alignment) {
    (void)size;
    (void)alignment;
    return nullptr;
}

static void poolFreeWrapper(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
}

Allocator PoolAllocator::asAllocator() {
    return Allocator(poolAllocWrapper, poolFreeWrapper, nullptr, this, "pool");
}

// ============================================================================
// Allocator Manager Implementation
// ============================================================================

AllocatorManager& AllocatorManager::instance() {
    static AllocatorManager instance;
    return instance;
}

AllocatorManager::AllocatorManager() {
    // Initialize system allocator
    systemAllocator_ = Allocator(systemAlloc, systemFree, systemRealloc, nullptr, "system");
    current_ = systemAllocator_;
    
    // Initialize stats
    std::memset(&stats_, 0, sizeof(stats_));
}

void AllocatorManager::setAllocator(const Allocator& alloc) {
    current_ = alloc;
}

void AllocatorManager::setAllocator(AllocatorType type) {
    switch (type) {
        case AllocatorType::SYSTEM:
            current_ = systemAllocator_;
            break;
        case AllocatorType::ARENA:
        case AllocatorType::POOL:
        case AllocatorType::STACK:
        case AllocatorType::CUSTOM:
            // These require user setup
            break;
    }
}

void* AllocatorManager::alloc(size_t size, size_t alignment) {
    void* ptr = nullptr;
    
    if (current_.alloc) {
        ptr = current_.alloc(size, alignment);
    } else {
        ptr = systemAlloc(size, alignment);
    }
    
    if (ptr) {
        stats_.totalAllocated += size;
        stats_.totalAllocations++;
        stats_.currentObjects++;
        if (stats_.totalAllocated > stats_.peakUsage) {
            stats_.peakUsage = stats_.totalAllocated;
        }
    }
    
    return ptr;
}

void AllocatorManager::free(void* ptr, size_t size) {
    if (!ptr) return;
    
    if (current_.free) {
        current_.free(ptr, size);
    } else {
        systemFree(ptr, size);
    }
    
    stats_.totalAllocated -= size;
    stats_.totalFrees++;
    stats_.currentObjects--;
}

void* AllocatorManager::realloc(void* ptr, size_t oldSize, size_t newSize, size_t alignment) {
    if (current_.realloc) {
        void* newPtr = current_.realloc(ptr, oldSize, newSize, alignment);
        if (newPtr) {
            stats_.totalAllocated += (newSize - oldSize);
            if (stats_.totalAllocated > stats_.peakUsage) {
                stats_.peakUsage = stats_.totalAllocated;
            }
        }
        return newPtr;
    }
    
    // Fallback: alloc + copy + free
    void* newPtr = alloc(newSize, alignment);
    if (newPtr && ptr) {
        size_t copySize = oldSize < newSize ? oldSize : newSize;
        std::memcpy(newPtr, ptr, copySize);
        free(ptr, oldSize);
    }
    return newPtr;
}

void AllocatorManager::resetStats() {
    std::memset(&stats_, 0, sizeof(stats_));
}

// ============================================================================
// C Runtime Functions (called from generated code)
// ============================================================================

// Global allocator state for runtime
static Allocator g_runtimeAllocator = {nullptr, nullptr, nullptr, nullptr, "default"};
static AllocatorStats g_runtimeStats = {0, 0, 0, 0, 0};

extern "C" {

void TYL_set_allocator(AllocFn alloc, FreeFn free, ReallocFn realloc, void* userData) {
    g_runtimeAllocator.alloc = alloc;
    g_runtimeAllocator.free = free;
    g_runtimeAllocator.realloc = realloc;
    g_runtimeAllocator.userData = userData;
    g_runtimeAllocator.name = "custom";
}

void TYL_reset_allocator() {
    g_runtimeAllocator.alloc = nullptr;
    g_runtimeAllocator.free = nullptr;
    g_runtimeAllocator.realloc = nullptr;
    g_runtimeAllocator.userData = nullptr;
    g_runtimeAllocator.name = "default";
}

void* TYL_alloc(size_t size) {
    return TYL_alloc_aligned(size, 8);
}

void* TYL_alloc_aligned(size_t size, size_t alignment) {
    void* ptr = nullptr;
    
    if (g_runtimeAllocator.alloc) {
        ptr = g_runtimeAllocator.alloc(size, alignment);
    } else {
        ptr = systemAlloc(size, alignment);
    }
    
    if (ptr) {
        g_runtimeStats.totalAllocated += size;
        g_runtimeStats.totalAllocations++;
        g_runtimeStats.currentObjects++;
        if (g_runtimeStats.totalAllocated > g_runtimeStats.peakUsage) {
            g_runtimeStats.peakUsage = g_runtimeStats.totalAllocated;
        }
    }
    
    return ptr;
}

void TYL_free(void* ptr, size_t size) {
    if (!ptr) return;
    
    if (g_runtimeAllocator.free) {
        g_runtimeAllocator.free(ptr, size);
    } else {
        systemFree(ptr, size);
    }
    
    g_runtimeStats.totalAllocated -= size;
    g_runtimeStats.totalFrees++;
    g_runtimeStats.currentObjects--;
}

void* TYL_realloc(void* ptr, size_t oldSize, size_t newSize) {
    if (g_runtimeAllocator.realloc) {
        void* newPtr = g_runtimeAllocator.realloc(ptr, oldSize, newSize, 8);
        if (newPtr) {
            g_runtimeStats.totalAllocated += (newSize - oldSize);
            if (g_runtimeStats.totalAllocated > g_runtimeStats.peakUsage) {
                g_runtimeStats.peakUsage = g_runtimeStats.totalAllocated;
            }
        }
        return newPtr;
    }
    
    // Fallback
    void* newPtr = TYL_alloc(newSize);
    if (newPtr && ptr) {
        size_t copySize = oldSize < newSize ? oldSize : newSize;
        std::memcpy(newPtr, ptr, copySize);
        TYL_free(ptr, oldSize);
    }
    return newPtr;
}

size_t TYL_allocator_total_allocated() {
    return g_runtimeStats.totalAllocated;
}

size_t TYL_allocator_peak_usage() {
    return g_runtimeStats.peakUsage;
}

}  // extern "C"

} // namespace tyl
