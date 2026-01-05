// Tyl Compiler - Type Checker Core
// Core methods, type utilities, diagnostics

#include "checker_base.h"
#include <regex>
#include <functional>

namespace tyl {

TypeChecker::TypeChecker() : currentType_(nullptr), expectedReturn_(nullptr) {
    // Register built-in functions
    registerBuiltins();
}

void TypeChecker::registerBuiltins() {
    auto& reg = TypeRegistry::instance();
    
    // print/println - variadic, returns void
    auto printFn = std::make_shared<FunctionType>();
    printFn->isVariadic = true;
    printFn->returnType = reg.voidType();
    symbols_.define(Symbol("print", SymbolKind::FUNCTION, printFn));
    symbols_.define(Symbol("println", SymbolKind::FUNCTION, printFn));
    
    // len(x) -> int
    auto lenFn = std::make_shared<FunctionType>();
    lenFn->params.push_back({"x", reg.anyType()});
    lenFn->returnType = reg.intType();
    symbols_.define(Symbol("len", SymbolKind::FUNCTION, lenFn));
    
    // str(x) -> string
    auto strFn = std::make_shared<FunctionType>();
    strFn->params.push_back({"x", reg.anyType()});
    strFn->returnType = reg.stringType();
    symbols_.define(Symbol("str", SymbolKind::FUNCTION, strFn));
    
    // int(x) -> int - Convert string/float/bool to int
    auto intFn = std::make_shared<FunctionType>();
    intFn->params.push_back({"x", reg.anyType()});
    intFn->returnType = reg.intType();
    symbols_.define(Symbol("int", SymbolKind::FUNCTION, intFn));
    
    // float(x) -> float - Convert string/int/bool to float
    auto floatFn = std::make_shared<FunctionType>();
    floatFn->params.push_back({"x", reg.anyType()});
    floatFn->returnType = reg.floatType();
    symbols_.define(Symbol("float", SymbolKind::FUNCTION, floatFn));
    
    // bool(x) -> bool - Convert int/string to bool
    auto boolFn = std::make_shared<FunctionType>();
    boolFn->params.push_back({"x", reg.anyType()});
    boolFn->returnType = reg.boolType();
    symbols_.define(Symbol("bool", SymbolKind::FUNCTION, boolFn));
    
    // upper(s) -> string
    auto upperFn = std::make_shared<FunctionType>();
    upperFn->params.push_back({"s", reg.stringType()});
    upperFn->returnType = reg.stringType();
    symbols_.define(Symbol("upper", SymbolKind::FUNCTION, upperFn));
    
    // lower(s) -> string
    auto lowerFn = std::make_shared<FunctionType>();
    lowerFn->params.push_back({"s", reg.stringType()});
    lowerFn->returnType = reg.stringType();
    symbols_.define(Symbol("lower", SymbolKind::FUNCTION, lowerFn));
    
    // trim(s) -> string
    auto trimFn = std::make_shared<FunctionType>();
    trimFn->params.push_back({"s", reg.stringType()});
    trimFn->returnType = reg.stringType();
    symbols_.define(Symbol("trim", SymbolKind::FUNCTION, trimFn));
    
    // starts_with(s, prefix) -> bool
    auto startsWithFn = std::make_shared<FunctionType>();
    startsWithFn->params.push_back({"s", reg.stringType()});
    startsWithFn->params.push_back({"prefix", reg.stringType()});
    startsWithFn->returnType = reg.boolType();
    symbols_.define(Symbol("starts_with", SymbolKind::FUNCTION, startsWithFn));
    
    // ends_with(s, suffix) -> bool
    auto endsWithFn = std::make_shared<FunctionType>();
    endsWithFn->params.push_back({"s", reg.stringType()});
    endsWithFn->params.push_back({"suffix", reg.stringType()});
    endsWithFn->returnType = reg.boolType();
    symbols_.define(Symbol("ends_with", SymbolKind::FUNCTION, endsWithFn));
    
    // substring(s, start, len?) -> string
    auto substringFn = std::make_shared<FunctionType>();
    substringFn->params.push_back({"s", reg.stringType()});
    substringFn->params.push_back({"start", reg.intType()});
    substringFn->params.push_back({"len", reg.intType()});
    substringFn->isVariadic = true;  // len is optional
    substringFn->returnType = reg.stringType();
    symbols_.define(Symbol("substring", SymbolKind::FUNCTION, substringFn));
    
    // replace(s, old, new) -> string
    auto replaceFn = std::make_shared<FunctionType>();
    replaceFn->params.push_back({"s", reg.stringType()});
    replaceFn->params.push_back({"old", reg.stringType()});
    replaceFn->params.push_back({"new_str", reg.stringType()});
    replaceFn->returnType = reg.stringType();
    symbols_.define(Symbol("replace", SymbolKind::FUNCTION, replaceFn));
    
    // index_of(s, substr) -> int (-1 if not found)
    auto indexOfFn = std::make_shared<FunctionType>();
    indexOfFn->params.push_back({"s", reg.stringType()});
    indexOfFn->params.push_back({"substr", reg.stringType()});
    indexOfFn->returnType = reg.intType();
    symbols_.define(Symbol("index_of", SymbolKind::FUNCTION, indexOfFn));
    
    // split(s, delimiter) -> list[string]
    auto splitFn = std::make_shared<FunctionType>();
    splitFn->params.push_back({"s", reg.stringType()});
    splitFn->params.push_back({"delimiter", reg.stringType()});
    splitFn->returnType = reg.listType(reg.stringType());
    symbols_.define(Symbol("split", SymbolKind::FUNCTION, splitFn));
    
    // join(list, delimiter) -> string
    auto joinFn = std::make_shared<FunctionType>();
    joinFn->params.push_back({"list", reg.anyType()});
    joinFn->params.push_back({"delimiter", reg.stringType()});
    joinFn->returnType = reg.stringType();
    symbols_.define(Symbol("join", SymbolKind::FUNCTION, joinFn));
    
    // contains(s, sub) -> bool
    auto containsFn = std::make_shared<FunctionType>();
    containsFn->params.push_back({"s", reg.stringType()});
    containsFn->params.push_back({"sub", reg.stringType()});
    containsFn->returnType = reg.boolType();
    symbols_.define(Symbol("contains", SymbolKind::FUNCTION, containsFn));
    
    // range(n) or range(start, end) -> list[int]
    auto rangeFn = std::make_shared<FunctionType>();
    rangeFn->params.push_back({"n", reg.intType()});
    rangeFn->isVariadic = true;  // Can take 1-3 args
    rangeFn->returnType = reg.listType(reg.intType());
    symbols_.define(Symbol("range", SymbolKind::FUNCTION, rangeFn));
    
    // push(list, elem) -> list
    auto pushFn = std::make_shared<FunctionType>();
    pushFn->params.push_back({"list", reg.anyType()});
    pushFn->params.push_back({"elem", reg.anyType()});
    pushFn->returnType = reg.anyType();
    symbols_.define(Symbol("push", SymbolKind::FUNCTION, pushFn));
    
    // platform() -> string
    auto platformFn = std::make_shared<FunctionType>();
    platformFn->returnType = reg.stringType();
    symbols_.define(Symbol("platform", SymbolKind::FUNCTION, platformFn));
    
    // arch() -> string
    auto archFn = std::make_shared<FunctionType>();
    archFn->returnType = reg.stringType();
    symbols_.define(Symbol("arch", SymbolKind::FUNCTION, archFn));
    
    // hostname() -> string
    auto hostnameFn = std::make_shared<FunctionType>();
    hostnameFn->returnType = reg.stringType();
    symbols_.define(Symbol("hostname", SymbolKind::FUNCTION, hostnameFn));
    
    // username() -> string
    auto usernameFn = std::make_shared<FunctionType>();
    usernameFn->returnType = reg.stringType();
    symbols_.define(Symbol("username", SymbolKind::FUNCTION, usernameFn));
    
    // cpu_count() -> int
    auto cpuCountFn = std::make_shared<FunctionType>();
    cpuCountFn->returnType = reg.intType();
    symbols_.define(Symbol("cpu_count", SymbolKind::FUNCTION, cpuCountFn));
    
    // sleep(ms) -> void
    auto sleepFn = std::make_shared<FunctionType>();
    sleepFn->params.push_back({"ms", reg.intType()});
    sleepFn->returnType = reg.voidType();
    symbols_.define(Symbol("sleep", SymbolKind::FUNCTION, sleepFn));
    
    // now() -> int (seconds)
    auto nowFn = std::make_shared<FunctionType>();
    nowFn->returnType = reg.intType();
    symbols_.define(Symbol("now", SymbolKind::FUNCTION, nowFn));
    
    // now_ms() -> int (milliseconds)
    auto nowMsFn = std::make_shared<FunctionType>();
    nowMsFn->returnType = reg.intType();
    symbols_.define(Symbol("now_ms", SymbolKind::FUNCTION, nowMsFn));
    
    // year(), month(), day(), hour(), minute(), second() -> int
    auto timeFn = std::make_shared<FunctionType>();
    timeFn->returnType = reg.intType();
    symbols_.define(Symbol("year", SymbolKind::FUNCTION, timeFn));
    symbols_.define(Symbol("month", SymbolKind::FUNCTION, std::make_shared<FunctionType>(*timeFn)));
    symbols_.define(Symbol("day", SymbolKind::FUNCTION, std::make_shared<FunctionType>(*timeFn)));
    symbols_.define(Symbol("hour", SymbolKind::FUNCTION, std::make_shared<FunctionType>(*timeFn)));
    symbols_.define(Symbol("minute", SymbolKind::FUNCTION, std::make_shared<FunctionType>(*timeFn)));
    symbols_.define(Symbol("second", SymbolKind::FUNCTION, std::make_shared<FunctionType>(*timeFn)));
    
    // ===== Basic Math Builtins =====
    // abs(x) -> int
    auto absFn = std::make_shared<FunctionType>();
    absFn->params.push_back({"x", reg.intType()});
    absFn->returnType = reg.intType();
    symbols_.define(Symbol("abs", SymbolKind::FUNCTION, absFn));
    
    // min(a, b) -> int
    auto minFn = std::make_shared<FunctionType>();
    minFn->params.push_back({"a", reg.intType()});
    minFn->params.push_back({"b", reg.intType()});
    minFn->returnType = reg.intType();
    symbols_.define(Symbol("min", SymbolKind::FUNCTION, minFn));
    
    // max(a, b) -> int
    auto maxFn = std::make_shared<FunctionType>();
    maxFn->params.push_back({"a", reg.intType()});
    maxFn->params.push_back({"b", reg.intType()});
    maxFn->returnType = reg.intType();
    symbols_.define(Symbol("max", SymbolKind::FUNCTION, maxFn));
    
    // sqrt(x) -> float
    auto sqrtFn = std::make_shared<FunctionType>();
    sqrtFn->params.push_back({"x", reg.floatType()});
    sqrtFn->returnType = reg.floatType();
    symbols_.define(Symbol("sqrt", SymbolKind::FUNCTION, sqrtFn));
    
    // floor(x) -> int
    auto floorFn = std::make_shared<FunctionType>();
    floorFn->params.push_back({"x", reg.floatType()});
    floorFn->returnType = reg.intType();
    symbols_.define(Symbol("floor", SymbolKind::FUNCTION, floorFn));
    
    // ceil(x) -> int
    auto ceilFn = std::make_shared<FunctionType>();
    ceilFn->params.push_back({"x", reg.floatType()});
    ceilFn->returnType = reg.intType();
    symbols_.define(Symbol("ceil", SymbolKind::FUNCTION, ceilFn));
    
    // round(x) -> int
    auto roundFn = std::make_shared<FunctionType>();
    roundFn->params.push_back({"x", reg.floatType()});
    roundFn->returnType = reg.intType();
    symbols_.define(Symbol("round", SymbolKind::FUNCTION, roundFn));
    
    // pow(base, exp) -> float
    auto powFn = std::make_shared<FunctionType>();
    powFn->params.push_back({"base", reg.floatType()});
    powFn->params.push_back({"exp", reg.floatType()});
    powFn->returnType = reg.floatType();
    symbols_.define(Symbol("pow", SymbolKind::FUNCTION, powFn));
    
    // Result type functions
    // Ok(value) -> Result (encoded as int with LSB=1)
    auto okFn = std::make_shared<FunctionType>();
    okFn->params.push_back({"value", reg.anyType()});
    okFn->returnType = reg.intType();  // Result is encoded as int
    symbols_.define(Symbol("Ok", SymbolKind::FUNCTION, okFn));
    
    // Err(value) -> Result (encoded as int with LSB=0)
    auto errFn = std::make_shared<FunctionType>();
    errFn->params.push_back({"value", reg.anyType()});
    errFn->returnType = reg.intType();
    symbols_.define(Symbol("Err", SymbolKind::FUNCTION, errFn));
    
    // is_ok(result) -> bool
    auto isOkFn = std::make_shared<FunctionType>();
    isOkFn->params.push_back({"result", reg.anyType()});
    isOkFn->returnType = reg.boolType();
    symbols_.define(Symbol("is_ok", SymbolKind::FUNCTION, isOkFn));
    
    // is_err(result) -> bool
    auto isErrFn = std::make_shared<FunctionType>();
    isErrFn->params.push_back({"result", reg.anyType()});
    isErrFn->returnType = reg.boolType();
    symbols_.define(Symbol("is_err", SymbolKind::FUNCTION, isErrFn));
    
    // unwrap(result) -> any (extracts value from Ok)
    auto unwrapFn = std::make_shared<FunctionType>();
    unwrapFn->params.push_back({"result", reg.anyType()});
    unwrapFn->returnType = reg.anyType();
    symbols_.define(Symbol("unwrap", SymbolKind::FUNCTION, unwrapFn));
    
    // unwrap_or(result, default) -> any
    auto unwrapOrFn = std::make_shared<FunctionType>();
    unwrapOrFn->params.push_back({"result", reg.anyType()});
    unwrapOrFn->params.push_back({"default", reg.anyType()});
    unwrapOrFn->returnType = reg.anyType();
    symbols_.define(Symbol("unwrap_or", SymbolKind::FUNCTION, unwrapOrFn));
    
    // File I/O functions
    // open(filename, mode?) -> int (file handle, -1 on error)
    auto openFn = std::make_shared<FunctionType>();
    openFn->params.push_back({"filename", reg.stringType()});
    openFn->params.push_back({"mode", reg.stringType()});  // Optional: "r", "w", "a", "rw"
    openFn->isVariadic = true;  // mode is optional
    openFn->returnType = reg.intType();
    symbols_.define(Symbol("open", SymbolKind::FUNCTION, openFn));
    
    // read(handle, size) -> string
    auto readFn = std::make_shared<FunctionType>();
    readFn->params.push_back({"handle", reg.intType()});
    readFn->params.push_back({"size", reg.intType()});
    readFn->returnType = reg.stringType();
    symbols_.define(Symbol("read", SymbolKind::FUNCTION, readFn));
    
    // write(handle, data) -> int (bytes written)
    auto writeFn = std::make_shared<FunctionType>();
    writeFn->params.push_back({"handle", reg.intType()});
    writeFn->params.push_back({"data", reg.stringType()});
    writeFn->returnType = reg.intType();
    symbols_.define(Symbol("write", SymbolKind::FUNCTION, writeFn));
    
    // close(handle) -> int (0 on success)
    auto closeFn = std::make_shared<FunctionType>();
    closeFn->params.push_back({"handle", reg.intType()});
    closeFn->returnType = reg.intType();
    symbols_.define(Symbol("close", SymbolKind::FUNCTION, closeFn));
    
    // file_size(handle) -> int
    auto fileSizeFn = std::make_shared<FunctionType>();
    fileSizeFn->params.push_back({"handle", reg.intType()});
    fileSizeFn->returnType = reg.intType();
    symbols_.define(Symbol("file_size", SymbolKind::FUNCTION, fileSizeFn));
    
    // Garbage Collection functions
    // gc_collect() -> void - Force garbage collection
    auto gcCollectFn = std::make_shared<FunctionType>();
    gcCollectFn->returnType = reg.voidType();
    symbols_.define(Symbol("gc_collect", SymbolKind::FUNCTION, gcCollectFn));
    
    // gc_disable() -> void - Disable automatic GC
    auto gcDisableFn = std::make_shared<FunctionType>();
    gcDisableFn->returnType = reg.voidType();
    symbols_.define(Symbol("gc_disable", SymbolKind::FUNCTION, gcDisableFn));
    
    // gc_enable() -> void - Enable automatic GC
    auto gcEnableFn = std::make_shared<FunctionType>();
    gcEnableFn->returnType = reg.voidType();
    symbols_.define(Symbol("gc_enable", SymbolKind::FUNCTION, gcEnableFn));
    
    // gc_stats() -> int - Get GC statistics (allocated bytes)
    auto gcStatsFn = std::make_shared<FunctionType>();
    gcStatsFn->returnType = reg.intType();
    symbols_.define(Symbol("gc_stats", SymbolKind::FUNCTION, gcStatsFn));
    
    // gc_threshold() -> int or gc_threshold(bytes) -> void - Get or set collection threshold
    auto gcThresholdGetFn = std::make_shared<FunctionType>();
    gcThresholdGetFn->returnType = reg.intType();
    symbols_.define(Symbol("gc_threshold", SymbolKind::FUNCTION, gcThresholdGetFn));
    
    // gc_count() -> int - Get number of collections performed
    auto gcCountFn = std::make_shared<FunctionType>();
    gcCountFn->returnType = reg.intType();
    symbols_.define(Symbol("gc_count", SymbolKind::FUNCTION, gcCountFn));
    
    // Manual memory management (for unsafe blocks)
    // alloc(size) -> ptr - Allocate raw memory (not GC managed)
    auto allocFn = std::make_shared<FunctionType>();
    allocFn->params.push_back({"size", reg.intType()});
    allocFn->returnType = reg.intType();  // Returns pointer as int
    symbols_.define(Symbol("alloc", SymbolKind::FUNCTION, allocFn));
    
    // free(ptr) -> void - Free raw memory
    auto freeFn = std::make_shared<FunctionType>();
    freeFn->params.push_back({"ptr", reg.intType()});
    freeFn->returnType = reg.voidType();
    symbols_.define(Symbol("free", SymbolKind::FUNCTION, freeFn));
    
    // stackalloc(size) -> ptr - Allocate memory on the stack (requires unsafe)
    auto stackallocFn = std::make_shared<FunctionType>();
    stackallocFn->params.push_back({"size", reg.intType()});
    stackallocFn->returnType = reg.intType();  // Returns pointer as int
    symbols_.define(Symbol("stackalloc", SymbolKind::FUNCTION, stackallocFn));
    
    // placement_new(ptr, value) -> ptr - Construct value at specific address (requires unsafe)
    auto placementNewFn = std::make_shared<FunctionType>();
    placementNewFn->params.push_back({"ptr", reg.intType()});
    placementNewFn->params.push_back({"value", reg.anyType()});
    placementNewFn->returnType = reg.intType();  // Returns the same pointer
    symbols_.define(Symbol("placement_new", SymbolKind::FUNCTION, placementNewFn));
    
    // gc_pin(ptr) -> void - Pin GC object to prevent collection/movement (requires unsafe)
    auto gcPinFn = std::make_shared<FunctionType>();
    gcPinFn->params.push_back({"ptr", reg.intType()});
    gcPinFn->returnType = reg.voidType();
    symbols_.define(Symbol("gc_pin", SymbolKind::FUNCTION, gcPinFn));
    
    // gc_unpin(ptr) -> void - Unpin GC object (requires unsafe)
    auto gcUnpinFn = std::make_shared<FunctionType>();
    gcUnpinFn->params.push_back({"ptr", reg.intType()});
    gcUnpinFn->returnType = reg.voidType();
    symbols_.define(Symbol("gc_unpin", SymbolKind::FUNCTION, gcUnpinFn));
    
    // gc_add_root(ptr) -> void - Register external pointer as GC root (requires unsafe)
    auto gcAddRootFn = std::make_shared<FunctionType>();
    gcAddRootFn->params.push_back({"ptr", reg.intType()});
    gcAddRootFn->returnType = reg.voidType();
    symbols_.define(Symbol("gc_add_root", SymbolKind::FUNCTION, gcAddRootFn));
    
    // gc_remove_root(ptr) -> void - Unregister external pointer as GC root (requires unsafe)
    auto gcRemoveRootFn = std::make_shared<FunctionType>();
    gcRemoveRootFn->params.push_back({"ptr", reg.intType()});
    gcRemoveRootFn->returnType = reg.voidType();
    symbols_.define(Symbol("gc_remove_root", SymbolKind::FUNCTION, gcRemoveRootFn));
    
    // Custom allocator functions
    // set_allocator(alloc_fn, free_fn) -> void - Set custom allocator (requires unsafe)
    auto setAllocatorFn = std::make_shared<FunctionType>();
    setAllocatorFn->params.push_back({"alloc_fn", reg.intType()});  // Function pointer
    setAllocatorFn->params.push_back({"free_fn", reg.intType()});   // Function pointer
    setAllocatorFn->returnType = reg.voidType();
    symbols_.define(Symbol("set_allocator", SymbolKind::FUNCTION, setAllocatorFn));
    
    // reset_allocator() -> void - Reset to default system allocator
    auto resetAllocatorFn = std::make_shared<FunctionType>();
    resetAllocatorFn->returnType = reg.voidType();
    symbols_.define(Symbol("reset_allocator", SymbolKind::FUNCTION, resetAllocatorFn));
    
    // allocator_stats() -> int - Get total bytes allocated by current allocator
    auto allocatorStatsFn = std::make_shared<FunctionType>();
    allocatorStatsFn->returnType = reg.intType();
    symbols_.define(Symbol("allocator_stats", SymbolKind::FUNCTION, allocatorStatsFn));
    
    // allocator_peak() -> int - Get peak memory usage
    auto allocatorPeakFn = std::make_shared<FunctionType>();
    allocatorPeakFn->returnType = reg.intType();
    symbols_.define(Symbol("allocator_peak", SymbolKind::FUNCTION, allocatorPeakFn));
    
    // Type introspection functions
    // sizeof(T) -> int - Get byte size of type
    auto sizeofFn = std::make_shared<FunctionType>();
    sizeofFn->params.push_back({"type", reg.anyType()});  // Type name passed as identifier
    sizeofFn->returnType = reg.intType();
    symbols_.define(Symbol("sizeof", SymbolKind::FUNCTION, sizeofFn));
    
    // alignof(T) -> int - Get alignment requirement of type
    auto alignofFn = std::make_shared<FunctionType>();
    alignofFn->params.push_back({"type", reg.anyType()});  // Type name passed as identifier
    alignofFn->returnType = reg.intType();
    symbols_.define(Symbol("alignof", SymbolKind::FUNCTION, alignofFn));
    
    // offsetof(Record, field) -> int - Get byte offset of field in record
    auto offsetofFn = std::make_shared<FunctionType>();
    offsetofFn->params.push_back({"record", reg.anyType()});  // Record type name
    offsetofFn->params.push_back({"field", reg.anyType()});   // Field name
    offsetofFn->returnType = reg.intType();
    symbols_.define(Symbol("offsetof", SymbolKind::FUNCTION, offsetofFn));
    
    // Memory intrinsics (require unsafe block)
    // memcpy(dst, src, n) -> ptr - Fast memory copy (non-overlapping)
    auto memcpyFn = std::make_shared<FunctionType>();
    memcpyFn->params.push_back({"dst", reg.intType()});   // Destination pointer
    memcpyFn->params.push_back({"src", reg.intType()});   // Source pointer
    memcpyFn->params.push_back({"n", reg.intType()});     // Number of bytes
    memcpyFn->returnType = reg.intType();  // Returns dst pointer
    symbols_.define(Symbol("memcpy", SymbolKind::FUNCTION, memcpyFn));
    
    // memset(ptr, val, n) -> ptr - Fast memory fill
    auto memsetFn = std::make_shared<FunctionType>();
    memsetFn->params.push_back({"ptr", reg.intType()});   // Destination pointer
    memsetFn->params.push_back({"val", reg.intType()});   // Value to set (byte)
    memsetFn->params.push_back({"n", reg.intType()});     // Number of bytes
    memsetFn->returnType = reg.intType();  // Returns ptr
    symbols_.define(Symbol("memset", SymbolKind::FUNCTION, memsetFn));
    
    // memmove(dst, src, n) -> ptr - Overlapping memory copy
    auto memmoveFn = std::make_shared<FunctionType>();
    memmoveFn->params.push_back({"dst", reg.intType()});  // Destination pointer
    memmoveFn->params.push_back({"src", reg.intType()});  // Source pointer
    memmoveFn->params.push_back({"n", reg.intType()});    // Number of bytes
    memmoveFn->returnType = reg.intType();  // Returns dst pointer
    symbols_.define(Symbol("memmove", SymbolKind::FUNCTION, memmoveFn));
    
    // memcmp(a, b, n) -> int - Memory comparison
    auto memcmpFn = std::make_shared<FunctionType>();
    memcmpFn->params.push_back({"a", reg.intType()});     // First pointer
    memcmpFn->params.push_back({"b", reg.intType()});     // Second pointer
    memcmpFn->params.push_back({"n", reg.intType()});     // Number of bytes
    memcmpFn->returnType = reg.intType();  // Returns <0, 0, or >0
    symbols_.define(Symbol("memcmp", SymbolKind::FUNCTION, memcmpFn));
    
    // ===== Extended String Builtins =====
    // ltrim(s) -> string - Remove leading whitespace
    auto ltrimFn = std::make_shared<FunctionType>();
    ltrimFn->params.push_back({"s", reg.stringType()});
    ltrimFn->returnType = reg.stringType();
    symbols_.define(Symbol("ltrim", SymbolKind::FUNCTION, ltrimFn));
    
    // rtrim(s) -> string - Remove trailing whitespace
    auto rtrimFn = std::make_shared<FunctionType>();
    rtrimFn->params.push_back({"s", reg.stringType()});
    rtrimFn->returnType = reg.stringType();
    symbols_.define(Symbol("rtrim", SymbolKind::FUNCTION, rtrimFn));
    
    // char_at(s, index) -> string - Get character at index
    auto charAtFn = std::make_shared<FunctionType>();
    charAtFn->params.push_back({"s", reg.stringType()});
    charAtFn->params.push_back({"index", reg.intType()});
    charAtFn->returnType = reg.stringType();
    symbols_.define(Symbol("char_at", SymbolKind::FUNCTION, charAtFn));
    
    // repeat(s, n) -> string - Repeat string n times
    auto repeatFn = std::make_shared<FunctionType>();
    repeatFn->params.push_back({"s", reg.stringType()});
    repeatFn->params.push_back({"n", reg.intType()});
    repeatFn->returnType = reg.stringType();
    symbols_.define(Symbol("repeat", SymbolKind::FUNCTION, repeatFn));
    
    // reverse_str(s) -> string - Reverse string
    auto reverseStrFn = std::make_shared<FunctionType>();
    reverseStrFn->params.push_back({"s", reg.stringType()});
    reverseStrFn->returnType = reg.stringType();
    symbols_.define(Symbol("reverse_str", SymbolKind::FUNCTION, reverseStrFn));
    
    // is_digit(s) -> bool - Check if all characters are digits
    auto isDigitFn = std::make_shared<FunctionType>();
    isDigitFn->params.push_back({"s", reg.stringType()});
    isDigitFn->returnType = reg.boolType();
    symbols_.define(Symbol("is_digit", SymbolKind::FUNCTION, isDigitFn));
    
    // is_alpha(s) -> bool - Check if all characters are alphabetic
    auto isAlphaFn = std::make_shared<FunctionType>();
    isAlphaFn->params.push_back({"s", reg.stringType()});
    isAlphaFn->returnType = reg.boolType();
    symbols_.define(Symbol("is_alpha", SymbolKind::FUNCTION, isAlphaFn));
    
    // ord(s) -> int - Get ASCII code of first character
    auto ordFn = std::make_shared<FunctionType>();
    ordFn->params.push_back({"s", reg.stringType()});
    ordFn->returnType = reg.intType();
    symbols_.define(Symbol("ord", SymbolKind::FUNCTION, ordFn));
    
    // chr(n) -> string - Convert ASCII code to character
    auto chrFn = std::make_shared<FunctionType>();
    chrFn->params.push_back({"n", reg.intType()});
    chrFn->returnType = reg.stringType();
    symbols_.define(Symbol("chr", SymbolKind::FUNCTION, chrFn));
    
    // last_index_of(s, substr) -> int - Find last occurrence
    auto lastIndexOfFn = std::make_shared<FunctionType>();
    lastIndexOfFn->params.push_back({"s", reg.stringType()});
    lastIndexOfFn->params.push_back({"substr", reg.stringType()});
    lastIndexOfFn->returnType = reg.intType();
    symbols_.define(Symbol("last_index_of", SymbolKind::FUNCTION, lastIndexOfFn));
    
    // ===== Extended Math Builtins =====
    // sin(x) -> float
    auto sinFn = std::make_shared<FunctionType>();
    sinFn->params.push_back({"x", reg.floatType()});
    sinFn->returnType = reg.floatType();
    symbols_.define(Symbol("sin", SymbolKind::FUNCTION, sinFn));
    
    // cos(x) -> float
    auto cosFn = std::make_shared<FunctionType>();
    cosFn->params.push_back({"x", reg.floatType()});
    cosFn->returnType = reg.floatType();
    symbols_.define(Symbol("cos", SymbolKind::FUNCTION, cosFn));
    
    // tan(x) -> float
    auto tanFn = std::make_shared<FunctionType>();
    tanFn->params.push_back({"x", reg.floatType()});
    tanFn->returnType = reg.floatType();
    symbols_.define(Symbol("tan", SymbolKind::FUNCTION, tanFn));
    
    // exp(x) -> float
    auto expFn = std::make_shared<FunctionType>();
    expFn->params.push_back({"x", reg.floatType()});
    expFn->returnType = reg.floatType();
    symbols_.define(Symbol("exp", SymbolKind::FUNCTION, expFn));
    
    // log(x) -> float
    auto logFn = std::make_shared<FunctionType>();
    logFn->params.push_back({"x", reg.floatType()});
    logFn->returnType = reg.floatType();
    symbols_.define(Symbol("log", SymbolKind::FUNCTION, logFn));
    
    // trunc(x) -> int - Truncate towards zero
    auto truncFn = std::make_shared<FunctionType>();
    truncFn->params.push_back({"x", reg.floatType()});
    truncFn->returnType = reg.intType();
    symbols_.define(Symbol("trunc", SymbolKind::FUNCTION, truncFn));
    
    // sign(x) -> int - Return -1, 0, or 1
    auto signFn = std::make_shared<FunctionType>();
    signFn->params.push_back({"x", reg.intType()});
    signFn->returnType = reg.intType();
    symbols_.define(Symbol("sign", SymbolKind::FUNCTION, signFn));
    
    // clamp(x, min, max) -> int
    auto clampFn = std::make_shared<FunctionType>();
    clampFn->params.push_back({"x", reg.intType()});
    clampFn->params.push_back({"min", reg.intType()});
    clampFn->params.push_back({"max", reg.intType()});
    clampFn->returnType = reg.intType();
    symbols_.define(Symbol("clamp", SymbolKind::FUNCTION, clampFn));
    
    // lerp(a, b, t) -> float - Linear interpolation
    auto lerpFn = std::make_shared<FunctionType>();
    lerpFn->params.push_back({"a", reg.floatType()});
    lerpFn->params.push_back({"b", reg.floatType()});
    lerpFn->params.push_back({"t", reg.floatType()});
    lerpFn->returnType = reg.floatType();
    symbols_.define(Symbol("lerp", SymbolKind::FUNCTION, lerpFn));
    
    // gcd(a, b) -> int - Greatest common divisor
    auto gcdFn = std::make_shared<FunctionType>();
    gcdFn->params.push_back({"a", reg.intType()});
    gcdFn->params.push_back({"b", reg.intType()});
    gcdFn->returnType = reg.intType();
    symbols_.define(Symbol("gcd", SymbolKind::FUNCTION, gcdFn));
    
    // lcm(a, b) -> int - Least common multiple
    auto lcmFn = std::make_shared<FunctionType>();
    lcmFn->params.push_back({"a", reg.intType()});
    lcmFn->params.push_back({"b", reg.intType()});
    lcmFn->returnType = reg.intType();
    symbols_.define(Symbol("lcm", SymbolKind::FUNCTION, lcmFn));
    
    // factorial(n) -> int
    auto factorialFn = std::make_shared<FunctionType>();
    factorialFn->params.push_back({"n", reg.intType()});
    factorialFn->returnType = reg.intType();
    symbols_.define(Symbol("factorial", SymbolKind::FUNCTION, factorialFn));
    
    // fib(n) -> int - Fibonacci number
    auto fibFn = std::make_shared<FunctionType>();
    fibFn->params.push_back({"n", reg.intType()});
    fibFn->returnType = reg.intType();
    symbols_.define(Symbol("fib", SymbolKind::FUNCTION, fibFn));
    
    // random() -> int - Random number
    auto randomFn = std::make_shared<FunctionType>();
    randomFn->returnType = reg.intType();
    symbols_.define(Symbol("random", SymbolKind::FUNCTION, randomFn));
    
    // is_nan(x) -> bool
    auto isNanFn = std::make_shared<FunctionType>();
    isNanFn->params.push_back({"x", reg.floatType()});
    isNanFn->returnType = reg.boolType();
    symbols_.define(Symbol("is_nan", SymbolKind::FUNCTION, isNanFn));
    
    // is_inf(x) -> bool
    auto isInfFn = std::make_shared<FunctionType>();
    isInfFn->params.push_back({"x", reg.floatType()});
    isInfFn->returnType = reg.boolType();
    symbols_.define(Symbol("is_inf", SymbolKind::FUNCTION, isInfFn));
    
    // ===== Extended List Builtins =====
    // first(list) -> any - Get first element
    auto firstFn = std::make_shared<FunctionType>();
    firstFn->params.push_back({"list", reg.anyType()});
    firstFn->returnType = reg.anyType();
    symbols_.define(Symbol("first", SymbolKind::FUNCTION, firstFn));
    
    // last(list) -> any - Get last element
    auto lastFn = std::make_shared<FunctionType>();
    lastFn->params.push_back({"list", reg.anyType()});
    lastFn->returnType = reg.anyType();
    symbols_.define(Symbol("last", SymbolKind::FUNCTION, lastFn));
    
    // get(list, index) -> any - Get element at index
    auto getFn = std::make_shared<FunctionType>();
    getFn->params.push_back({"list", reg.anyType()});
    getFn->params.push_back({"index", reg.intType()});
    getFn->returnType = reg.anyType();
    symbols_.define(Symbol("get", SymbolKind::FUNCTION, getFn));
    
    // reverse(list) -> list - Reverse list
    auto reverseFn = std::make_shared<FunctionType>();
    reverseFn->params.push_back({"list", reg.anyType()});
    reverseFn->returnType = reg.anyType();
    symbols_.define(Symbol("reverse", SymbolKind::FUNCTION, reverseFn));
    
    // index(list, elem) -> int - Find element index (-1 if not found)
    auto indexFn = std::make_shared<FunctionType>();
    indexFn->params.push_back({"list", reg.anyType()});
    indexFn->params.push_back({"elem", reg.anyType()});
    indexFn->returnType = reg.intType();
    symbols_.define(Symbol("index", SymbolKind::FUNCTION, indexFn));
    
    // includes(list, elem) -> bool - Check if list contains element
    auto includesFn = std::make_shared<FunctionType>();
    includesFn->params.push_back({"list", reg.anyType()});
    includesFn->params.push_back({"elem", reg.anyType()});
    includesFn->returnType = reg.boolType();
    symbols_.define(Symbol("includes", SymbolKind::FUNCTION, includesFn));
    
    // take(list, n) -> list - Take first n elements
    auto takeFn = std::make_shared<FunctionType>();
    takeFn->params.push_back({"list", reg.anyType()});
    takeFn->params.push_back({"n", reg.intType()});
    takeFn->returnType = reg.anyType();
    symbols_.define(Symbol("take", SymbolKind::FUNCTION, takeFn));
    
    // drop(list, n) -> list - Drop first n elements
    auto dropFn = std::make_shared<FunctionType>();
    dropFn->params.push_back({"list", reg.anyType()});
    dropFn->params.push_back({"n", reg.intType()});
    dropFn->returnType = reg.anyType();
    symbols_.define(Symbol("drop", SymbolKind::FUNCTION, dropFn));
    
    // min_of(list) -> any - Get minimum element
    auto minOfFn = std::make_shared<FunctionType>();
    minOfFn->params.push_back({"list", reg.anyType()});
    minOfFn->returnType = reg.anyType();
    symbols_.define(Symbol("min_of", SymbolKind::FUNCTION, minOfFn));
    
    // max_of(list) -> any - Get maximum element
    auto maxOfFn = std::make_shared<FunctionType>();
    maxOfFn->params.push_back({"list", reg.anyType()});
    maxOfFn->returnType = reg.anyType();
    symbols_.define(Symbol("max_of", SymbolKind::FUNCTION, maxOfFn));
    
    // ===== Extended Time Builtins =====
    // now_us() -> int - Current time in microseconds
    auto nowUsFn = std::make_shared<FunctionType>();
    nowUsFn->returnType = reg.intType();
    symbols_.define(Symbol("now_us", SymbolKind::FUNCTION, nowUsFn));
    
    // weekday() -> int - Day of week (0=Sunday)
    auto weekdayFn = std::make_shared<FunctionType>();
    weekdayFn->returnType = reg.intType();
    symbols_.define(Symbol("weekday", SymbolKind::FUNCTION, weekdayFn));
    
    // day_of_year() -> int - Day of year (1-366)
    auto dayOfYearFn = std::make_shared<FunctionType>();
    dayOfYearFn->returnType = reg.intType();
    symbols_.define(Symbol("day_of_year", SymbolKind::FUNCTION, dayOfYearFn));
    
    // make_time(year, month, day, hour, min, sec) -> int - Create timestamp
    auto makeTimeFn = std::make_shared<FunctionType>();
    makeTimeFn->params.push_back({"year", reg.intType()});
    makeTimeFn->params.push_back({"month", reg.intType()});
    makeTimeFn->params.push_back({"day", reg.intType()});
    makeTimeFn->params.push_back({"hour", reg.intType()});
    makeTimeFn->params.push_back({"min", reg.intType()});
    makeTimeFn->params.push_back({"sec", reg.intType()});
    makeTimeFn->returnType = reg.intType();
    symbols_.define(Symbol("make_time", SymbolKind::FUNCTION, makeTimeFn));
    
    // add_days(timestamp, days) -> int
    auto addDaysFn = std::make_shared<FunctionType>();
    addDaysFn->params.push_back({"timestamp", reg.intType()});
    addDaysFn->params.push_back({"days", reg.intType()});
    addDaysFn->returnType = reg.intType();
    symbols_.define(Symbol("add_days", SymbolKind::FUNCTION, addDaysFn));
    
    // add_hours(timestamp, hours) -> int
    auto addHoursFn = std::make_shared<FunctionType>();
    addHoursFn->params.push_back({"timestamp", reg.intType()});
    addHoursFn->params.push_back({"hours", reg.intType()});
    addHoursFn->returnType = reg.intType();
    symbols_.define(Symbol("add_hours", SymbolKind::FUNCTION, addHoursFn));
    
    // diff_days(t1, t2) -> int - Difference in days
    auto diffDaysFn = std::make_shared<FunctionType>();
    diffDaysFn->params.push_back({"t1", reg.intType()});
    diffDaysFn->params.push_back({"t2", reg.intType()});
    diffDaysFn->returnType = reg.intType();
    symbols_.define(Symbol("diff_days", SymbolKind::FUNCTION, diffDaysFn));
    
    // is_leap_year(year) -> bool
    auto isLeapYearFn = std::make_shared<FunctionType>();
    isLeapYearFn->params.push_back({"year", reg.intType()});
    isLeapYearFn->returnType = reg.boolType();
    symbols_.define(Symbol("is_leap_year", SymbolKind::FUNCTION, isLeapYearFn));
    
    // ===== Extended System Builtins =====
    // env(name) -> string - Get environment variable
    auto envFn = std::make_shared<FunctionType>();
    envFn->params.push_back({"name", reg.stringType()});
    envFn->returnType = reg.stringType();
    symbols_.define(Symbol("env", SymbolKind::FUNCTION, envFn));
    
    // set_env(name, value) -> bool - Set environment variable
    auto setEnvFn = std::make_shared<FunctionType>();
    setEnvFn->params.push_back({"name", reg.stringType()});
    setEnvFn->params.push_back({"value", reg.stringType()});
    setEnvFn->returnType = reg.boolType();
    symbols_.define(Symbol("set_env", SymbolKind::FUNCTION, setEnvFn));
    
    // home_dir() -> string - Get user home directory
    auto homeDirFn = std::make_shared<FunctionType>();
    homeDirFn->returnType = reg.stringType();
    symbols_.define(Symbol("home_dir", SymbolKind::FUNCTION, homeDirFn));
    
    // temp_dir() -> string - Get temp directory
    auto tempDirFn = std::make_shared<FunctionType>();
    tempDirFn->returnType = reg.stringType();
    symbols_.define(Symbol("temp_dir", SymbolKind::FUNCTION, tempDirFn));
    
    // assert(condition, message?) -> void
    auto assertFn = std::make_shared<FunctionType>();
    assertFn->params.push_back({"condition", reg.boolType()});
    assertFn->params.push_back({"message", reg.stringType()});
    assertFn->isVariadic = true;  // message is optional
    assertFn->returnType = reg.voidType();
    symbols_.define(Symbol("assert", SymbolKind::FUNCTION, assertFn));
    
    // panic(message) -> void - Terminate with error
    auto panicFn = std::make_shared<FunctionType>();
    panicFn->params.push_back({"message", reg.stringType()});
    panicFn->returnType = reg.voidType();
    symbols_.define(Symbol("panic", SymbolKind::FUNCTION, panicFn));
    
    // debug(value) -> void - Print debug info
    auto debugFn = std::make_shared<FunctionType>();
    debugFn->params.push_back({"value", reg.anyType()});
    debugFn->returnType = reg.voidType();
    symbols_.define(Symbol("debug", SymbolKind::FUNCTION, debugFn));
    
    // system(command) -> int - Execute shell command
    auto systemFn = std::make_shared<FunctionType>();
    systemFn->params.push_back({"command", reg.stringType()});
    systemFn->returnType = reg.intType();
    symbols_.define(Symbol("system", SymbolKind::FUNCTION, systemFn));
    
    // ===== Complex Number Builtins =====
    // complex(real, imag) -> c128 - Create complex number
    auto complexFn = std::make_shared<FunctionType>();
    complexFn->params.push_back({"real", reg.floatType()});
    complexFn->params.push_back({"imag", reg.floatType()});
    complexFn->returnType = reg.complex128Type();
    symbols_.define(Symbol("complex", SymbolKind::FUNCTION, complexFn));
    
    // real(z) -> float - Get real part of complex number
    auto realFn = std::make_shared<FunctionType>();
    realFn->params.push_back({"z", reg.complex128Type()});
    realFn->returnType = reg.floatType();
    symbols_.define(Symbol("real", SymbolKind::FUNCTION, realFn));
    
    // imag(z) -> float - Get imaginary part of complex number
    auto imagFn = std::make_shared<FunctionType>();
    imagFn->params.push_back({"z", reg.complex128Type()});
    imagFn->returnType = reg.floatType();
    symbols_.define(Symbol("imag", SymbolKind::FUNCTION, imagFn));
    
    // ===== BigInt Builtins =====
    // bigint(value) -> BigInt - Create BigInt from int
    auto bigintFn = std::make_shared<FunctionType>();
    bigintFn->params.push_back({"value", reg.intType()});
    bigintFn->returnType = reg.bigIntType();
    symbols_.define(Symbol("bigint", SymbolKind::FUNCTION, bigintFn));
    
    // bigint_add(a, b) -> BigInt
    auto bigintAddFn = std::make_shared<FunctionType>();
    bigintAddFn->params.push_back({"a", reg.bigIntType()});
    bigintAddFn->params.push_back({"b", reg.bigIntType()});
    bigintAddFn->returnType = reg.bigIntType();
    symbols_.define(Symbol("bigint_add", SymbolKind::FUNCTION, bigintAddFn));
    
    // bigint_to_int(b) -> int
    auto bigintToIntFn = std::make_shared<FunctionType>();
    bigintToIntFn->params.push_back({"b", reg.bigIntType()});
    bigintToIntFn->returnType = reg.intType();
    symbols_.define(Symbol("bigint_to_int", SymbolKind::FUNCTION, bigintToIntFn));
    
    // ===== Rational Builtins =====
    // rational(num, denom) -> Rational
    auto rationalFn = std::make_shared<FunctionType>();
    rationalFn->params.push_back({"num", reg.intType()});
    rationalFn->params.push_back({"denom", reg.intType()});
    rationalFn->returnType = reg.rationalType();
    symbols_.define(Symbol("rational", SymbolKind::FUNCTION, rationalFn));
    
    // rational_add(a, b) -> Rational
    auto rationalAddFn = std::make_shared<FunctionType>();
    rationalAddFn->params.push_back({"a", reg.rationalType()});
    rationalAddFn->params.push_back({"b", reg.rationalType()});
    rationalAddFn->returnType = reg.rationalType();
    symbols_.define(Symbol("rational_add", SymbolKind::FUNCTION, rationalAddFn));
    
    // rational_to_float(r) -> float
    auto rationalToFloatFn = std::make_shared<FunctionType>();
    rationalToFloatFn->params.push_back({"r", reg.rationalType()});
    rationalToFloatFn->returnType = reg.floatType();
    symbols_.define(Symbol("rational_to_float", SymbolKind::FUNCTION, rationalToFloatFn));
    
    // ===== Fixed-Point Builtins =====
    // fixed(value) -> Fixed - Create fixed-point from float/int
    auto fixedFn = std::make_shared<FunctionType>();
    fixedFn->params.push_back({"value", reg.anyType()});
    fixedFn->returnType = reg.intType();  // Fixed is stored as int64
    symbols_.define(Symbol("fixed", SymbolKind::FUNCTION, fixedFn));
    
    // fixed_add(a, b) -> Fixed
    auto fixedAddFn = std::make_shared<FunctionType>();
    fixedAddFn->params.push_back({"a", reg.intType()});
    fixedAddFn->params.push_back({"b", reg.intType()});
    fixedAddFn->returnType = reg.intType();
    symbols_.define(Symbol("fixed_add", SymbolKind::FUNCTION, fixedAddFn));
    
    // fixed_sub(a, b) -> Fixed
    auto fixedSubFn = std::make_shared<FunctionType>();
    fixedSubFn->params.push_back({"a", reg.intType()});
    fixedSubFn->params.push_back({"b", reg.intType()});
    fixedSubFn->returnType = reg.intType();
    symbols_.define(Symbol("fixed_sub", SymbolKind::FUNCTION, fixedSubFn));
    
    // fixed_mul(a, b) -> Fixed
    auto fixedMulFn = std::make_shared<FunctionType>();
    fixedMulFn->params.push_back({"a", reg.intType()});
    fixedMulFn->params.push_back({"b", reg.intType()});
    fixedMulFn->returnType = reg.intType();
    symbols_.define(Symbol("fixed_mul", SymbolKind::FUNCTION, fixedMulFn));
    
    // fixed_to_float(f) -> float
    auto fixedToFloatFn = std::make_shared<FunctionType>();
    fixedToFloatFn->params.push_back({"f", reg.intType()});
    fixedToFloatFn->returnType = reg.floatType();
    symbols_.define(Symbol("fixed_to_float", SymbolKind::FUNCTION, fixedToFloatFn));
    
    // ===== Vec3 Builtins =====
    // vec3(x, y, z) -> Vec3
    auto vec3Fn = std::make_shared<FunctionType>();
    vec3Fn->params.push_back({"x", reg.floatType()});
    vec3Fn->params.push_back({"y", reg.floatType()});
    vec3Fn->params.push_back({"z", reg.floatType()});
    vec3Fn->returnType = reg.anyType();  // Returns pointer to Vec3
    symbols_.define(Symbol("vec3", SymbolKind::FUNCTION, vec3Fn));
    
    // vec3_add(a, b) -> Vec3
    auto vec3AddFn = std::make_shared<FunctionType>();
    vec3AddFn->params.push_back({"a", reg.anyType()});
    vec3AddFn->params.push_back({"b", reg.anyType()});
    vec3AddFn->returnType = reg.anyType();
    symbols_.define(Symbol("vec3_add", SymbolKind::FUNCTION, vec3AddFn));
    
    // vec3_dot(a, b) -> float
    auto vec3DotFn = std::make_shared<FunctionType>();
    vec3DotFn->params.push_back({"a", reg.anyType()});
    vec3DotFn->params.push_back({"b", reg.anyType()});
    vec3DotFn->returnType = reg.floatType();
    symbols_.define(Symbol("vec3_dot", SymbolKind::FUNCTION, vec3DotFn));
    
    // vec3_length(v) -> float
    auto vec3LengthFn = std::make_shared<FunctionType>();
    vec3LengthFn->params.push_back({"v", reg.anyType()});
    vec3LengthFn->returnType = reg.floatType();
    symbols_.define(Symbol("vec3_length", SymbolKind::FUNCTION, vec3LengthFn));
}

bool TypeChecker::check(Program& program) {
    diagnostics_.clear();
    exprTypes_.clear();
    currentTypeParams_.clear();
    currentTypeParamNames_.clear();
    program.accept(*this);
    return !hasErrors();
}

bool TypeChecker::hasErrors() const {
    for (auto& d : diagnostics_) {
        if (d.level == TypeDiagnostic::Level::ERROR) return true;
    }
    return false;
}

TypePtr TypeChecker::getType(Expression* expr) {
    auto it = exprTypes_.find(expr);
    return it != exprTypes_.end() ? it->second : TypeRegistry::instance().unknownType();
}

TypePtr TypeChecker::inferType(Expression* expr) {
    expr->accept(*this);
    exprTypes_[expr] = currentType_;
    return currentType_;
}

void TypeChecker::error(const std::string& msg, const SourceLocation& loc) {
    diagnostics_.emplace_back(TypeDiagnostic::Level::ERROR, msg, loc);
}

void TypeChecker::warning(const std::string& msg, const SourceLocation& loc) {
    diagnostics_.emplace_back(TypeDiagnostic::Level::WARNING, msg, loc);
}

void TypeChecker::note(const std::string& msg, const SourceLocation& loc) {
    diagnostics_.emplace_back(TypeDiagnostic::Level::NOTE, msg, loc);
}

TypePtr TypeChecker::parseTypeAnnotation(const std::string& str) {
    if (str.empty()) return TypeRegistry::instance().unknownType();
    
    // Check for generic type syntax: Name[T, U, ...]
    auto genericType = parseGenericType(str);
    if (genericType) return genericType;
    
    // Check if it's a type parameter in scope
    auto typeParam = resolveTypeParam(str);
    if (typeParam) return typeParam;
    
    return TypeRegistry::instance().fromString(str);
}

TypePtr TypeChecker::parseGenericType(const std::string& str) {
    auto& reg = TypeRegistry::instance();
    
    // Match pattern: Name[Type1, Type2, ...]
    size_t bracketPos = str.find('[');
    if (bracketPos == std::string::npos) return nullptr;
    
    std::string baseName = str.substr(0, bracketPos);
    
    // If baseName is empty, this is list syntax [T] not generic syntax Name[T]
    // Let TypeRegistry::fromString handle it
    if (baseName.empty()) return nullptr;
    
    // If baseName starts with & or *, this is a reference/pointer to a list/generic
    // Let TypeRegistry::fromString handle it (e.g., &[int], *[int])
    if (!baseName.empty() && (baseName[0] == '&' || baseName[0] == '*')) return nullptr;
    
    size_t endBracket = str.rfind(']');
    if (endBracket == std::string::npos || endBracket <= bracketPos) return nullptr;
    
    std::string argsStr = str.substr(bracketPos + 1, endBracket - bracketPos - 1);
    
    // Parse type arguments (simple comma-separated for now)
    std::vector<TypePtr> typeArgs;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i <= argsStr.size(); i++) {
        if (i == argsStr.size() || (argsStr[i] == ',' && depth == 0)) {
            std::string arg = argsStr.substr(start, i - start);
            // Trim whitespace
            size_t first = arg.find_first_not_of(" \t");
            size_t last = arg.find_last_not_of(" \t");
            if (first != std::string::npos) {
                arg = arg.substr(first, last - first + 1);
            }
            if (!arg.empty()) {
                typeArgs.push_back(parseTypeAnnotation(arg));
            }
            start = i + 1;
        } else if (argsStr[i] == '[') {
            depth++;
        } else if (argsStr[i] == ']') {
            depth--;
        }
    }
    
    // Check for built-in generic types
    if (baseName == "List" || baseName == "list") {
        if (typeArgs.size() == 1) {
            return reg.listType(typeArgs[0]);
        }
    } else if (baseName == "Map" || baseName == "map") {
        if (typeArgs.size() == 2) {
            return reg.mapType(typeArgs[0], typeArgs[1]);
        }
    } else if (baseName == "Result") {
        // Result[T, E] - for now treat as generic
        return reg.genericType(baseName, typeArgs);
    } else if (baseName == "chan" || baseName == "Chan" || baseName == "Channel") {
        if (typeArgs.size() >= 1) {
            int64_t bufSize = 0;  // Default unbuffered
            return reg.channelType(typeArgs[0], bufSize);
        }
    } else if (baseName == "Mutex") {
        if (typeArgs.size() == 1) {
            return reg.mutexType(typeArgs[0]);
        }
    } else if (baseName == "RWLock") {
        if (typeArgs.size() == 1) {
            return reg.rwlockType(typeArgs[0]);
        }
    } else if (baseName == "Atomic") {
        if (typeArgs.size() == 1) {
            return reg.atomicType(typeArgs[0]);
        }
    }
    
    // Look up user-defined generic type
    TypePtr baseType = reg.lookupType(baseName);
    if (baseType) {
        return reg.instantiateGeneric(baseType, typeArgs);
    }
    
    // Return as unresolved generic
    return reg.genericType(baseName, typeArgs);
}

TypePtr TypeChecker::resolveTypeParam(const std::string& name) {
    // Check if name is a type parameter in current scope
    auto it = currentTypeParams_.find(name);
    if (it != currentTypeParams_.end()) {
        return it->second;
    }
    
    // Check if it's in the list of type param names (unbound)
    for (const auto& paramName : currentTypeParamNames_) {
        if (paramName == name) {
            return TypeRegistry::instance().typeParamType(name);
        }
    }
    
    return nullptr;
}

bool TypeChecker::checkTraitBounds(TypePtr type, const std::vector<std::string>& bounds, const SourceLocation& loc) {
    auto& reg = TypeRegistry::instance();
    
    for (const auto& bound : bounds) {
        if (!reg.typeImplementsTrait(type, bound)) {
            error("Type '" + type->toString() + "' does not implement trait '" + bound + "'", loc);
            return false;
        }
    }
    return true;
}

TypePtr TypeChecker::instantiateGenericFunction(FunctionType* fnType, const std::vector<TypePtr>& typeArgs, const SourceLocation& loc) {
    auto& reg = TypeRegistry::instance();
    
    if (fnType->typeParams.size() != typeArgs.size()) {
        error("Wrong number of type arguments: expected " + std::to_string(fnType->typeParams.size()) +
              ", got " + std::to_string(typeArgs.size()), loc);
        return reg.errorType();
    }
    
    // Build substitution map
    std::unordered_map<std::string, TypePtr> substitutions;
    for (size_t i = 0; i < fnType->typeParams.size(); i++) {
        substitutions[fnType->typeParams[i]] = typeArgs[i];
    }
    
    // Create instantiated function type
    auto newFn = std::make_shared<FunctionType>();
    for (const auto& param : fnType->params) {
        newFn->params.push_back({param.first, reg.substituteTypeParams(param.second, substitutions)});
    }
    newFn->returnType = reg.substituteTypeParams(fnType->returnType, substitutions);
    newFn->isVariadic = fnType->isVariadic;
    
    return newFn;
}

void TypeChecker::checkTraitImpl(const std::string& traitName, const std::string& typeName,
                                  const std::vector<std::unique_ptr<FnDecl>>& methods, const SourceLocation& loc) {
    auto& reg = TypeRegistry::instance();
    
    TraitPtr trait = reg.lookupTrait(traitName);
    if (!trait) {
        error("Unknown trait '" + traitName + "'", loc);
        return;
    }
    
    // Collect all required methods (including from super traits)
    std::vector<std::pair<std::string, const TraitMethod*>> requiredMethods;
    
    // Add methods from this trait
    for (const auto& traitMethod : trait->methods) {
        requiredMethods.push_back({traitName, &traitMethod});
    }
    
    // Add methods from super traits (recursively)
    std::function<void(const std::string&)> collectSuperMethods = [&](const std::string& superName) {
        TraitPtr superTrait = reg.lookupTrait(superName);
        if (!superTrait) return;
        
        for (const auto& method : superTrait->methods) {
            requiredMethods.push_back({superName, &method});
        }
        
        // Recurse into super traits of super traits
        for (const auto& superSuper : superTrait->superTraits) {
            collectSuperMethods(superSuper);
        }
    };
    
    for (const auto& superTrait : trait->superTraits) {
        collectSuperMethods(superTrait);
    }
    
    // Check that all required methods are implemented
    for (const auto& [fromTrait, traitMethod] : requiredMethods) {
        if (traitMethod->hasDefaultImpl) continue;  // Has default, not required
        
        bool found = false;
        for (const auto& implMethod : methods) {
            if (implMethod->name == traitMethod->name) {
                found = true;
                
                // Check method signature matches
                auto implFnType = std::make_shared<FunctionType>();
                for (const auto& p : implMethod->params) {
                    implFnType->params.push_back({p.first, parseTypeAnnotation(p.second)});
                }
                implFnType->returnType = parseTypeAnnotation(implMethod->returnType);
                
                // Compare signatures (simplified - just check param count and return type)
                if (implFnType->params.size() != traitMethod->signature->params.size()) {
                    error("Method '" + traitMethod->name + "' has wrong number of parameters", implMethod->location);
                }
                
                break;
            }
        }
        
        if (!found) {
            std::string errorMsg = "Missing implementation of method '" + traitMethod->name + "'";
            if (fromTrait != traitName) {
                errorMsg += " (required by super trait '" + fromTrait + "')";
            }
            errorMsg += " for trait '" + traitName + "'";
            error(errorMsg, loc);
        }
    }
    
    // Register the implementation
    TraitImpl impl;
    impl.traitName = traitName;
    impl.typeName = typeName;
    for (const auto& method : methods) {
        auto fnType = std::make_shared<FunctionType>();
        for (const auto& p : method->params) {
            fnType->params.push_back({p.first, parseTypeAnnotation(p.second)});
        }
        fnType->returnType = parseTypeAnnotation(method->returnType);
        impl.methods[method->name] = fnType;
    }
    reg.registerTraitImpl(impl);
}

TypePtr TypeChecker::unify(TypePtr a, TypePtr b, const SourceLocation& loc) {
    auto& reg = TypeRegistry::instance();
    if (a->kind == TypeKind::UNKNOWN) return b;
    if (b->kind == TypeKind::UNKNOWN) return a;
    if (a->kind == TypeKind::ANY || b->kind == TypeKind::ANY) return reg.anyType();
    if (a->equals(b.get())) return a;
    
    // Handle type parameter unification
    if (a->kind == TypeKind::TYPE_PARAM) {
        auto* tp = static_cast<TypeParamType*>(a.get());
        // Check bounds
        if (!tp->bounds.empty() && !reg.checkTraitBounds(b, tp->bounds)) {
            error("Type '" + b->toString() + "' does not satisfy bounds of '" + tp->name + "'", loc);
            return reg.errorType();
        }
        return b;
    }
    if (b->kind == TypeKind::TYPE_PARAM) {
        auto* tp = static_cast<TypeParamType*>(b.get());
        if (!tp->bounds.empty() && !reg.checkTraitBounds(a, tp->bounds)) {
            error("Type '" + a->toString() + "' does not satisfy bounds of '" + tp->name + "'", loc);
            return reg.errorType();
        }
        return a;
    }
    
    if (a->isNumeric() && b->isNumeric()) {
        if (a->isFloat() || b->isFloat()) return reg.floatType();
        if (a->size() >= b->size()) return a;
        return b;
    }
    error("Cannot unify types '" + a->toString() + "' and '" + b->toString() + "'", loc);
    return reg.errorType();
}

void TypeChecker::checkUnusedVariables(Scope* scope) {
    if (!scope) return;
    
    for (auto& [name, sym] : scope->symbolsMut()) {
        // Skip functions, types, modules, etc.
        if (sym.kind != SymbolKind::VARIABLE && sym.kind != SymbolKind::PARAMETER) {
            continue;
        }
        
        // Skip if already used
        if (sym.isUsed) continue;
        
        // Skip special variables (loop variables, destructuring temps, etc.)
        if (name.empty() || name[0] == '$' || name == "_") continue;
        
        // Skip parameters that start with underscore (intentionally unused)
        if (name.size() > 1 && name[0] == '_') continue;
        
        // Generate warning
        if (sym.kind == SymbolKind::PARAMETER) {
            warning("Unused parameter '" + name + "'", sym.location);
        } else {
            warning("Unused variable '" + name + "'", sym.location);
        }
    }
}

// ===== Ownership and Borrow Checking =====

void TypeChecker::checkOwnership(Expression* expr, bool isMove) {
    if (!borrowCheckEnabled_) return;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        if (isMove) {
            auto err = ownership_.recordMove(id->name, id->location);
            if (err) {
                emitOwnershipError(*err, id->location);
            }
        } else {
            auto err = ownership_.checkUsable(id->name, id->location);
            if (err) {
                emitOwnershipError(*err, id->location);
            }
        }
    }
}

void TypeChecker::checkBorrow(Expression* expr, bool isMutable) {
    if (!borrowCheckEnabled_) return;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        auto err = ownership_.checkCanBorrow(id->name, isMutable, id->location);
        if (err) {
            emitOwnershipError(*err, id->location);
        }
    }
}

ParamMode TypeChecker::parseParamMode(const std::string& typeName) {
    if (typeName.empty()) return ParamMode::OWNED;
    
    // Check for borrow prefixes
    if (typeName.size() >= 5 && typeName.substr(0, 5) == "&mut ") {
        return ParamMode::BORROW_MUT;
    }
    if (typeName.size() >= 1 && typeName[0] == '&') {
        return ParamMode::BORROW;
    }
    
    // Check if it's a Copy type
    std::string baseType = stripBorrowPrefix(typeName);
    if (isCopyType(baseType)) {
        return ParamMode::COPY;
    }
    
    return ParamMode::OWNED;
}

std::string TypeChecker::stripBorrowPrefix(const std::string& typeName) {
    if (typeName.size() >= 5 && typeName.substr(0, 5) == "&mut ") {
        return typeName.substr(5);
    }
    if (typeName.size() >= 1 && typeName[0] == '&') {
        size_t start = 1;
        while (start < typeName.size() && typeName[start] == ' ') start++;
        return typeName.substr(start);
    }
    return typeName;
}

void TypeChecker::emitOwnershipError(const std::string& msg, const SourceLocation& loc) {
    error(msg, loc);
}

} // namespace tyl
