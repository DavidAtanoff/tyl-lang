// Tyl Compiler - Garbage Collector Implementation
// Mark-and-sweep garbage collector with proper runtime integration
// Supports custom allocators for flexible memory management

#include "gc.h"
#include "allocator.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace tyl {

// Global GC instance
GarbageCollector* g_gc = nullptr;

// GC enabled flag (can be toggled at runtime)
static bool gcEnabled = true;

// Custom allocator function pointers (nullptr = use default)
static AllocFn g_customAlloc = nullptr;
static FreeFn g_customFree = nullptr;
static void* g_customAllocUserData = nullptr;

// Stack frame tracking for conservative scanning
static thread_local std::vector<void**> stackFrames;

GarbageCollector::GarbageCollector() 
    : heap_(nullptr), heapSize_(0), heapUsed_(0), 
      allObjects_(nullptr), collectionThreshold_(512 * 1024),
      initialized_(false) {
    stats_ = {0, 0, 0, 0, 0};
}

GarbageCollector::~GarbageCollector() {
    shutdown();
}

void GarbageCollector::init(size_t initialHeapSize) {
    if (initialized_) return;
    
    heapSize_ = initialHeapSize;
    heap_ = static_cast<uint8_t*>(std::malloc(heapSize_));
    if (!heap_) {
        heapSize_ = 256 * 1024;
        heap_ = static_cast<uint8_t*>(std::malloc(heapSize_));
    }
    
    heapUsed_ = 0;
    allObjects_ = nullptr;
    initialized_ = true;
}

void GarbageCollector::shutdown() {
    if (!initialized_) return;
    
    // Free all objects
    GCObjectHeader* obj = allObjects_;
    while (obj) {
        GCObjectHeader* next = obj->next;
        std::free(obj);
        obj = next;
    }
    
    allObjects_ = nullptr;
    
    if (heap_) {
        std::free(heap_);
        heap_ = nullptr;
    }
    
    roots_.clear();
    rootRanges_.clear();
    initialized_ = false;
}

void* GarbageCollector::alloc(size_t size, GCObjectType type) {
    if (!initialized_) init();
    
    // Check if we should collect
    if (gcEnabled && shouldCollect()) {
        collect();
    }
    
    // Allocate header + user data
    size_t totalSize = sizeof(GCObjectHeader) + size;
    totalSize = (totalSize + 7) & ~7;  // Align to 8 bytes
    
    GCObjectHeader* header = nullptr;
    
    // Use custom allocator if set, otherwise use system malloc
    if (g_customAlloc) {
        header = static_cast<GCObjectHeader*>(g_customAlloc(totalSize, 8));
        if (header) std::memset(header, 0, totalSize);
    } else {
        header = static_cast<GCObjectHeader*>(std::malloc(totalSize));
    }
    
    if (!header) {
        // Try collecting and retry
        collectFull();
        if (g_customAlloc) {
            header = static_cast<GCObjectHeader*>(g_customAlloc(totalSize, 8));
            if (header) std::memset(header, 0, totalSize);
        } else {
            header = static_cast<GCObjectHeader*>(std::malloc(totalSize));
        }
        if (!header) {
            return nullptr;  // Out of memory
        }
    }
    
    // Initialize header
    header->size = static_cast<uint32_t>(size);
    header->type = static_cast<uint16_t>(type);
    header->marked = 0;
    header->flags = GC_FLAG_NONE;
    
    // Add to allocation list
    header->next = allObjects_;
    allObjects_ = header;
    
    // Update stats
    stats_.totalAllocated += size;
    stats_.objectCount++;
    
    // Return pointer to user data (after header)
    void* userPtr = reinterpret_cast<uint8_t*>(header) + sizeof(GCObjectHeader);
    std::memset(userPtr, 0, size);  // Zero-initialize
    
    return userPtr;
}

void* GarbageCollector::allocAligned(size_t size, size_t alignment, GCObjectType type) {
    (void)alignment;
    return alloc(size, type);
}

void GarbageCollector::addRoot(void** root) {
    roots_.insert(root);
}

void GarbageCollector::removeRoot(void** root) {
    roots_.erase(root);
}

void GarbageCollector::addRootRange(void** start, void** end) {
    rootRanges_.emplace_back(start, end);
}

void GarbageCollector::removeRootRange(void** start) {
    rootRanges_.erase(
        std::remove_if(rootRanges_.begin(), rootRanges_.end(),
            [start](const auto& range) { return range.first == start; }),
        rootRanges_.end()
    );
}

GCObjectHeader* GarbageCollector::getHeader(void* ptr) {
    if (!ptr) return nullptr;
    return reinterpret_cast<GCObjectHeader*>(
        static_cast<uint8_t*>(ptr) - sizeof(GCObjectHeader)
    );
}

bool GarbageCollector::isManaged(void* ptr) const {
    if (!ptr) return false;
    
    GCObjectHeader* header = getHeader(ptr);
    
    // Walk the allocation list to verify
    GCObjectHeader* obj = allObjects_;
    while (obj) {
        if (obj == header) return true;
        obj = obj->next;
    }
    return false;
}

void GarbageCollector::pin(void* ptr) {
    if (!ptr) return;
    GCObjectHeader* header = getHeader(ptr);
    header->flags |= GC_FLAG_PINNED;
}

void GarbageCollector::unpin(void* ptr) {
    if (!ptr) return;
    GCObjectHeader* header = getHeader(ptr);
    header->flags &= ~GC_FLAG_PINNED;
}

bool GarbageCollector::shouldCollect() const {
    return stats_.totalAllocated > collectionThreshold_;
}

void GarbageCollector::collect() {
    if (!gcEnabled) return;
    mark();
    sweep();
    stats_.totalCollections++;
}

void GarbageCollector::collectFull() {
    bool wasEnabled = gcEnabled;
    gcEnabled = true;
    collect();
    gcEnabled = wasEnabled;
}

void GarbageCollector::mark() {
    // Clear all marks
    GCObjectHeader* obj = allObjects_;
    while (obj) {
        obj->marked = 0;
        obj = obj->next;
    }
    
    // Mark from explicit roots
    for (void** root : roots_) {
        if (*root && isManaged(*root)) {
            markObject(getHeader(*root));
        }
    }
    
    // Mark from root ranges (conservative stack scanning)
    for (const auto& range : rootRanges_) {
        for (void** ptr = range.first; ptr < range.second; ptr++) {
            if (*ptr && isManaged(*ptr)) {
                markObject(getHeader(*ptr));
            }
        }
    }
    
    // Mark from stack frames
    for (void** frame : stackFrames) {
        if (frame && *frame && isManaged(*frame)) {
            markObject(getHeader(*frame));
        }
    }
}

void GarbageCollector::markObject(GCObjectHeader* obj) {
    if (!obj || obj->marked) return;
    
    obj->marked = 1;
    traceObject(obj);
}

void GarbageCollector::traceObject(GCObjectHeader* obj) {
    void* userData = reinterpret_cast<uint8_t*>(obj) + sizeof(GCObjectHeader);
    
    switch (static_cast<GCObjectType>(obj->type)) {
        case GCObjectType::RAW:
        case GCObjectType::STRING:
            break;
            
        case GCObjectType::LIST: {
            int64_t* listData = static_cast<int64_t*>(userData);
            int64_t count = listData[0];
            void** elements = reinterpret_cast<void**>(&listData[2]);
            
            for (int64_t i = 0; i < count; i++) {
                if (elements[i] && isManaged(elements[i])) {
                    markObject(getHeader(elements[i]));
                }
            }
            break;
        }
        
        case GCObjectType::RECORD: {
            int64_t* recData = static_cast<int64_t*>(userData);
            int64_t fieldCount = recData[0];
            void** fields = reinterpret_cast<void**>(&recData[1]);
            
            for (int64_t i = 0; i < fieldCount; i++) {
                if (fields[i] && isManaged(fields[i])) {
                    markObject(getHeader(fields[i]));
                }
            }
            break;
        }
        
        case GCObjectType::CLOSURE: {
            void** closureData = static_cast<void**>(userData);
            int64_t captureCount = reinterpret_cast<int64_t>(closureData[1]);
            void** captures = &closureData[2];
            
            for (int64_t i = 0; i < captureCount; i++) {
                if (captures[i] && isManaged(captures[i])) {
                    markObject(getHeader(captures[i]));
                }
            }
            break;
        }
        
        case GCObjectType::ARRAY: {
            size_t count = obj->size / sizeof(void*);
            void** ptrs = static_cast<void**>(userData);
            
            for (size_t i = 0; i < count; i++) {
                if (ptrs[i] && isManaged(ptrs[i])) {
                    markObject(getHeader(ptrs[i]));
                }
            }
            break;
        }
        
        case GCObjectType::BOX: {
            void** boxed = static_cast<void**>(userData);
            if (*boxed && isManaged(*boxed)) {
                markObject(getHeader(*boxed));
            }
            break;
        }
    }
}

void GarbageCollector::sweep() {
    GCObjectHeader** objPtr = &allObjects_;
    size_t freedBytes = 0;
    size_t freedCount = 0;
    
    while (*objPtr) {
        GCObjectHeader* obj = *objPtr;
        
        if (!obj->marked && !(obj->flags & GC_FLAG_PINNED)) {
            *objPtr = obj->next;
            freedBytes += obj->size;
            freedCount++;
            
            // Use custom free if set, otherwise use system free
            size_t totalSize = sizeof(GCObjectHeader) + obj->size;
            totalSize = (totalSize + 7) & ~7;
            if (g_customFree) {
                g_customFree(obj, totalSize);
            } else {
                std::free(obj);
            }
        } else {
            obj->marked = 0;
            objPtr = &obj->next;
        }
    }
    
    stats_.totalAllocated -= freedBytes;
    stats_.totalFreed += freedBytes;
    stats_.objectCount -= freedCount;
    stats_.lastCollectionFreed = freedBytes;
}

// C API implementations
extern "C" {

void* TYL_gc_alloc(size_t size, uint16_t type) {
    if (!g_gc) TYL_gc_init();
    return g_gc->alloc(size, static_cast<GCObjectType>(type));
}

void* TYL_gc_alloc_string(size_t len) {
    return TYL_gc_alloc(len + 1, static_cast<uint16_t>(GCObjectType::STRING));
}

void* TYL_gc_alloc_list(size_t capacity) {
    size_t size = 16 + capacity * 8;
    void* ptr = TYL_gc_alloc(size, static_cast<uint16_t>(GCObjectType::LIST));
    if (ptr) {
        int64_t* data = static_cast<int64_t*>(ptr);
        data[0] = 0;
        data[1] = capacity;
    }
    return ptr;
}

void* TYL_gc_alloc_record(size_t fieldCount) {
    size_t size = 8 + fieldCount * 8;
    void* ptr = TYL_gc_alloc(size, static_cast<uint16_t>(GCObjectType::RECORD));
    if (ptr) {
        int64_t* data = static_cast<int64_t*>(ptr);
        data[0] = fieldCount;
    }
    return ptr;
}

void* TYL_gc_alloc_closure(size_t captureCount) {
    size_t size = 16 + captureCount * 8;
    void* ptr = TYL_gc_alloc(size, static_cast<uint16_t>(GCObjectType::CLOSURE));
    if (ptr) {
        int64_t* data = static_cast<int64_t*>(ptr);
        data[0] = 0;
        data[1] = captureCount;
    }
    return ptr;
}

void TYL_gc_push_frame(void** frameBase) {
    stackFrames.push_back(frameBase);
}

void TYL_gc_pop_frame() {
    if (!stackFrames.empty()) {
        stackFrames.pop_back();
    }
}

void TYL_gc_collect() {
    if (g_gc) g_gc->collect();
}

void TYL_gc_init() {
    if (!g_gc) {
        g_gc = new GarbageCollector();
        g_gc->init();
    }
}

void TYL_gc_shutdown() {
    if (g_gc) {
        g_gc->shutdown();
        delete g_gc;
        g_gc = nullptr;
    }
}

void TYL_gc_enable() {
    gcEnabled = true;
}

void TYL_gc_disable() {
    gcEnabled = false;
}

size_t TYL_gc_stats() {
    if (g_gc) {
        return g_gc->getStats().totalAllocated;
    }
    return 0;
}

void TYL_gc_write_barrier(void* obj, void* field, void* newValue) {
    (void)obj;
    (void)field;
    (void)newValue;
}

// Custom allocator API
void TYL_gc_set_allocator(AllocFn alloc, FreeFn free, void* userData) {
    g_customAlloc = alloc;
    g_customFree = free;
    g_customAllocUserData = userData;
}

void TYL_gc_reset_allocator() {
    g_customAlloc = nullptr;
    g_customFree = nullptr;
    g_customAllocUserData = nullptr;
}

void* TYL_gc_get_allocator_userdata() {
    return g_customAllocUserData;
}

} // extern "C"

} // namespace tyl
