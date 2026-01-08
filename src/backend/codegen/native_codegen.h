// Tyl Compiler - Native Code Generator
#ifndef TYL_NATIVE_CODEGEN_H
#define TYL_NATIVE_CODEGEN_H

#include "frontend/ast/ast.h"
#include "backend/x64/x64_assembler.h"
#include "backend/x64/pe_generator.h"
#include "backend/object/object_file.h"
#include "backend/codegen/register_allocator.h"
#include "backend/codegen/global_register_allocator.h"
#include "backend/gc/gc.h"
#include "semantic/generics/monomorphizer.h"
#include "semantic/ctfe/ctfe_interpreter.h"
#include <map>
#include <set>

namespace tyl {

// Optimization level for code generation (LLVM/Clang compatible)
enum class CodeGenOptLevel {
    O0,    // No optimization - debug friendly, no inlining
    O1,    // Basic optimization - shared runtime routines
    O2,    // Standard optimization - selective inlining
    O3,    // Aggressive optimization - more inlining, speed over size
    Os,    // Optimize for size - shared routines, minimal inlining
    Oz,    // Aggressive size - maximum code sharing
    Ofast  // Maximum optimization - full inlining, unsafe opts
};

class NativeCodeGen : public ASTVisitor {
public:
    NativeCodeGen();
    bool compile(Program& program, const std::string& outputFile);
    bool compileToObject(Program& program, const std::string& outputFile);  // Compile to .obj/.o file
    
    // Set optimization level
    void setOptLevel(CodeGenOptLevel level) { optLevel_ = level; }
    CodeGenOptLevel optLevel() const { return optLevel_; }
    
    // Dump generated assembly (for debugging)
    void dumpAssembly(std::ostream& out) const;
    
    // Get the assembler for inspection
    const X64Assembler& getAssembler() const { return asm_; }
    
private:
    X64Assembler asm_;
    PEGenerator pe_;
    std::map<std::string, int32_t> locals;
    std::map<std::string, uint32_t> dataOffsets;
    int32_t stackOffset = 0;
    int labelCounter = 0;
    bool inFunction = false;
    int currentArgCount = 0;
    std::map<std::string, uint32_t> stringOffsets;
    uint32_t itoaBufferRVA_ = 0;
    std::map<std::string, int64_t> constVars;
    std::map<std::string, std::string> constStrVars;
    std::map<std::string, std::vector<int64_t>> constListVars;  // Track constant list values
    std::map<std::string, size_t> listSizes;  // Track list sizes
    std::set<std::string> listVars;  // Track variables that hold list values (runtime)
    
    // Float support
    std::set<std::string> floatVars;           // Variables that are floats
    std::map<std::string, double> constFloatVars;  // Constant float values
    uint32_t negZeroRVA_ = 0;                  // RVA for -0.0 constant (for negation)
    bool lastExprWasFloat_ = false;            // Track if last expression result is float
    bool lastExprWasComplex_ = false;          // Track if last expression result is complex
    
    // Comprehensive variable type tracking for 'is' type checks
    std::map<std::string, std::string> varTypes_;  // Variable name -> type name (int, float, str, bool, record name, etc.)
    
    // Loop context for break/continue
    struct LoopLabels {
        std::string label;          // Optional label for named loops
        std::string continueLabel;  // Jump here for continue
        std::string breakLabel;     // Jump here for break
    };
    std::vector<LoopLabels> loopStack;
    
    // Stack frame optimization - allocate stack once per function
    bool useOptimizedStackFrame_ = true;       // Enable stack frame optimization
    int32_t functionStackSize_ = 0;            // Total stack size for current function
    bool stackAllocated_ = false;              // Whether stack is already allocated
    
    // Register allocation
    RegisterAllocator regAlloc_;               // Register allocator instance
    bool useRegisterAllocation_ = true;        // Enable register allocation
    std::map<std::string, VarRegister> varRegisters_;  // Variable -> register mapping for current function
    
    // Global register allocation (top-level)
    GlobalRegisterAllocator globalRegAlloc_;   // Global register allocator
    bool useGlobalRegisterAllocation_ = true;  // Enable global register allocation
    std::map<std::string, VarRegister> globalVarRegisters_;  // Global variable -> register mapping
    
    // Leaf function optimization
    bool isLeafFunction_ = false;              // Current function is a leaf (no calls)
    bool useLeafOptimization_ = true;          // Enable leaf function optimization
    
    // Stdout handle caching - avoid redundant GetStdHandle calls
    bool stdoutHandleCached_ = false;          // Whether stdout handle is cached in RDI
    bool useStdoutCaching_ = true;             // Enable stdout handle caching
    
    // Optimization level
    CodeGenOptLevel optLevel_ = CodeGenOptLevel::O2;  // Default to O2
    
    // Shared runtime routines (for O1/O2 - reduces code size)
    bool runtimeRoutinesEmitted_ = false;      // Whether runtime routines have been emitted
    std::string itoaRoutineLabel_;             // Label for shared itoa routine
    std::string ftoaRoutineLabel_;             // Label for shared ftoa routine
    std::string printIntRoutineLabel_;         // Label for shared print_int routine
    
    std::string newLabel(const std::string& prefix = "L");
    uint32_t addString(const std::string& str);
    uint32_t addFloatConstant(double value);    // Add float constant to data section
    void allocLocal(const std::string& name);
    void emitPrintInt(int32_t localOffset);
    void emitPrintString(uint32_t dataOffset);
    void emitPrintNewline();
    void emitItoa();
    void emitPrintRuntimeValue();
    void emitPrintFloat();                      // Print float from xmm0
    void emitFtoa();                            // Float to ASCII conversion
    
    // Shared runtime routines (for O1/O2 code size optimization)
    void emitRuntimeRoutines();                 // Emit shared runtime routines at end of code
    void emitItoaCall();                        // Call shared itoa routine (O1/O2)
    void emitFtoaCall();                        // Call shared ftoa routine (O1/O2)
    void emitPrintIntCall();                    // Call shared print_int routine (O1/O2)
    bool shouldInlineItoa() const;              // Check if itoa should be inlined based on opt level
    bool shouldInlineFtoa() const;              // Check if ftoa should be inlined based on opt level
    
    bool tryEvalConstant(Expression* expr, int64_t& outValue);
    bool tryEvalConstantFloat(Expression* expr, double& outValue);  // Evaluate float constants
    bool tryEvalConstantString(Expression* expr, std::string& outValue);
    bool tryEvalComptimeCall(Expression* expr, int64_t& outValue);  // Evaluate compile-time function calls
    void emitPrintExpr(Expression* expr);  // Helper to print a single expression
    bool isFloatExpression(Expression* expr);  // Check if expression is float type
    bool isStringReturningExpr(Expression* expr);  // Check if expression returns a string pointer
    void emitPrintStringPtr();  // Print string from pointer in rax (calculates length at runtime)
    void emitPrintStrView();    // Print str_view from pointer in rax (ptr at [rax], len at [rax+8])
    void emitWriteConsole(uint32_t strRVA, size_t len);  // Emit WriteConsoleA with cached stdout handle
    void emitWriteConsoleBuffer();  // Emit WriteConsoleA for buffer in rdx with length in r8, uses cached handle
    
    // Type checking helpers for extended numeric types
    static bool isFloatTypeName(const std::string& typeName);    // Check if type is any float (f16, f32, f64, f128, float)
    static bool isComplexTypeName(const std::string& typeName);  // Check if type is complex (c64, c128)
    
    // Stack frame optimization helpers
    int32_t calculateFunctionStackSize(Statement* body);  // Pre-scan to calculate stack needs
    int32_t calculateExprStackSize(Expression* expr);     // Calculate stack needs for expression
    void emitCallWithOptimizedStack(uint32_t importRVA);  // Emit call without stack adjustment
    void emitCallRelWithOptimizedStack(const std::string& label);  // Emit relative call
    
    // Dead code elimination helper - check if statement ends with terminator
    bool endsWithTerminator(Statement* stmt);  // Returns true if stmt ends with return/break/continue
    
    // Register allocation helpers
    void emitLoadVarToRax(const std::string& name);       // Load variable to RAX (from register or stack)
    void emitStoreRaxToVar(const std::string& name);      // Store RAX to variable (to register or stack)
    void emitSaveCalleeSavedRegs();                       // Save used callee-saved registers
    void emitRestoreCalleeSavedRegs();                    // Restore used callee-saved registers
    void emitMoveParamToVar(int paramIndex, const std::string& name, const std::string& type);  // Move param register to variable location
    
    // Leaf function optimization helpers
    bool checkIsLeafFunction(Statement* body);            // Check if function makes no calls
    bool statementHasCall(Statement* stmt);               // Check if statement contains a call
    bool expressionHasCall(Expression* expr);             // Check if expression contains a call
    
    // Closure capture analysis
    void collectCapturedVariables(Expression* expr, const std::set<std::string>& params, std::set<std::string>& captured);
    void collectCapturedVariablesStmt(Statement* stmt, const std::set<std::string>& params, std::set<std::string>& captured);
    
    // Module support
    std::string currentModule_;                           // Current module name (empty if top-level)
    std::map<std::string, std::vector<std::string>> moduleFunctions_;  // Module -> function names
    
    // Extern/FFI support
    std::map<std::string, uint32_t> externFunctions_;     // Extern function name -> import RVA
    
    // Trait/vtable support
    struct TraitInfo {
        std::string name;
        std::vector<std::string> methodNames;             // Method names in order
        std::vector<std::string> superTraits;             // Super traits (inheritance)
    };
    struct ImplInfo {
        std::string traitName;
        std::string typeName;
        std::map<std::string, std::string> methodLabels;  // Method name -> label
    };
    std::map<std::string, TraitInfo> traits_;             // Trait name -> info
    std::map<std::string, ImplInfo> impls_;               // "trait:type" -> impl info
    std::map<std::string, uint32_t> vtables_;             // "trait:type" -> vtable RVA
    std::map<std::string, std::vector<uint32_t>> vtableFixups_;  // "trait:type" -> list of fixup offsets
    
    // Trait dispatch helpers
    void finalizeVtables();                               // Generate vtables with actual function pointers
    void emitTraitMethodCall(const std::string& traitName, const std::string& methodName, 
                             int argCount);               // Emit dynamic dispatch call
    int getMethodIndex(const std::string& traitName, const std::string& methodName);  // Get method index in vtable
    std::string resolveTraitMethod(const std::string& typeName, const std::string& traitName, 
                                   const std::string& methodName);  // Resolve to concrete method label
    
    // Garbage collection support
    bool useGC_ = true;                                    // Enable GC for allocations
    bool gcInitEmitted_ = false;                           // Whether GC init code has been emitted
    uint32_t gcDataRVA_ = 0;                               // RVA of GC data section globals
    std::string gcCollectLabel_;                           // Label for GC collection routine
    
    // Generics / Monomorphization support
    Monomorphizer monomorphizer_;                          // Tracks generic instantiations
    std::unordered_map<std::string, FnDecl*> genericFunctions_;    // Generic function declarations
    std::unordered_map<std::string, RecordDecl*> genericRecords_;  // Generic record declarations
    std::vector<std::unique_ptr<FnDecl>> specializedFunctions_;    // Specialized function copies
    std::vector<std::unique_ptr<RecordDecl>> specializedRecords_;  // Specialized record copies
    
    // All user-defined function names (for UFCS lookup)
    std::set<std::string> allFunctionNames_;                       // All function names including inlined ones
    
    // Refinement type information
    struct RefinementTypeInfo {
        std::string name;                                  // Type alias name (e.g., "Positive")
        std::string baseType;                              // Base type (e.g., "int")
        Expression* constraint;                            // Constraint expression (e.g., _ > 0)
    };
    std::map<std::string, RefinementTypeInfo> refinementTypes_;    // Type name -> refinement info
    std::map<std::string, std::string> varRefinementTypes_;        // Variable name -> refinement type name
    
    // Dependent type information (types that depend on values)
    struct DependentTypeParam {
        std::string name;                                  // Parameter name (e.g., "T" or "N")
        std::string kind;                                  // "type" for type params, or type name for value params
        bool isValue;                                      // true if this is a value parameter
    };
    struct DependentTypeInfo {
        std::string name;                                  // Type alias name (e.g., "Vector")
        std::string baseType;                              // Base type (e.g., "[T; N]")
        std::vector<DependentTypeParam> params;            // Type and value parameters
    };
    std::map<std::string, DependentTypeInfo> dependentTypes_;      // Type name -> dependent type info
    
    // Record type information for field access
    struct RecordTypeInfo {
        std::string name;
        std::vector<std::string> fieldNames;               // Field names in order
        std::vector<std::string> fieldTypes;               // Field types
        std::vector<int32_t> fieldOffsets;                 // Cached field offsets (computed once)
        std::vector<int> fieldBitWidths;                   // Bitfield widths (0 = not a bitfield)
        std::vector<int> fieldBitOffsets;                  // Bit offset within storage unit for bitfields
        int32_t totalSize = 0;                             // Total record size in bytes
        bool reprC = false;                                // C-compatible layout
        bool reprPacked = false;                           // No padding
        int reprAlign = 0;                                 // Explicit alignment
        bool isUnion = false;                              // Union type (all fields at offset 0)
        bool offsetsComputed = false;                      // Whether offsets have been computed
        bool hasBitfields = false;                         // Whether record has any bitfields
    };
    std::map<std::string, RecordTypeInfo> recordTypes_;    // Record name -> type info
    std::map<std::string, std::string> varRecordTypes_;    // Variable name -> record type name
    
    // Runtime type identification (RTTI) for 'is' type checks
    std::map<std::string, uint64_t> typeIds_;              // Type name -> unique type ID
    uint64_t nextTypeId_ = 1;                              // Next type ID to assign (0 = unknown)
    
    // Fixed-size array type tracking
    struct FixedArrayInfo {
        std::string elementType;                           // Element type (e.g., "int", "[int; 3]")
        size_t size;                                       // Number of elements
        int32_t elementSize;                               // Size of each element in bytes
    };
    std::map<std::string, FixedArrayInfo> varFixedArrayTypes_;  // Variable name -> fixed array info
    
    // Function pointer type tracking
    std::set<std::string> fnPtrVars_;                      // Variables that hold function pointers
    std::set<std::string> closureVars_;                    // Variables that hold closures (lambdas)
    
    // Callback/trampoline support for passing Flex functions to C
    struct CallbackInfo {
        std::string flexFnName;                            // Name of the Flex function
        std::string trampolineLabel;                       // Label for the trampoline wrapper
        CallingConvention callingConv;                     // Calling convention for the callback
        std::vector<std::string> paramTypes;               // Parameter types
        std::string returnType;                            // Return type
    };
    std::map<std::string, CallbackInfo> callbacks_;        // Function name -> callback info
    std::map<std::string, uint32_t> callbackTrampolines_;  // Trampoline label -> code RVA
    
    // Function calling convention tracking
    std::map<std::string, CallingConvention> fnCallingConvs_;  // Function name -> calling convention
    
    // Function export/visibility attributes
    struct FnAttributes {
        bool isExport = false;    // #[export] - export from DLL
        bool isHidden = false;    // #[hidden] - not visible outside module
        bool isWeak = false;      // #[weak] - can be overridden
    };
    std::map<std::string, FnAttributes> fnAttributes_;  // Function name -> attributes
    
    // Channel support
    struct ChannelInfo {
        std::string elementType;                               // Element type being sent/received
        size_t bufferSize;                                     // Buffer capacity (0 = unbuffered)
        int32_t elementSize;                                   // Size of each element in bytes
    };
    std::map<std::string, ChannelInfo> varChannelTypes_;       // Variable name -> channel info
    
    // Atomic type tracking
    struct AtomicInfo {
        std::string elementType;                               // Element type (e.g., "int")
        int32_t elementSize;                                   // Size of element in bytes
    };
    std::map<std::string, AtomicInfo> varAtomicTypes_;         // Variable name -> atomic info
    
    // Smart pointer type tracking
    struct SmartPtrInfo {
        std::string elementType;                               // Element type (e.g., "int")
        int32_t elementSize;                                   // Size of element in bytes
        enum class Kind { Box, Rc, Arc, Weak, Cell, RefCell } kind;
        bool isAtomic = false;                                 // For Weak: true if from Arc
    };
    std::map<std::string, SmartPtrInfo> varSmartPtrTypes_;     // Variable name -> smart pointer info
    
    // Algebraic Effects runtime support
    // Effect handler stack entry - pushed when entering a handle block
    struct EffectHandlerEntry {
        std::string effectName;                                // Effect being handled (e.g., "Console")
        std::string opName;                                    // Operation being handled (e.g., "log")
        std::string handlerLabel;                              // Label to jump to for this handler
        std::string resumeLabel;                               // Label to resume after handler
        std::vector<std::string> paramNames;                   // Parameter names for the handler
        bool hasResume;                                        // Whether handler has resume parameter
    };
    std::vector<EffectHandlerEntry> effectHandlerStack_;       // Runtime handler stack (compile-time tracking)
    int effectHandlerDepth_ = 0;                               // Current handler nesting depth
    std::string currentResumeLabel_;                           // Label for current resume point
    std::string currentHandlerEndLabel_;                       // Label for end of current handler block
    uint32_t effectHandlerStackRVA_ = 0;                       // RVA of global handler stack pointer
    bool effectRuntimeInitialized_ = false;                    // Whether effect runtime is initialized
    
    // Effect runtime helper methods
    void emitEffectRuntimeInit();                              // Initialize effect handler stack
    void emitPushEffectHandler(const std::string& effectName, const std::string& opName,
                               const std::string& handlerLabel, bool hasResume);
    void emitPopEffectHandler();                               // Pop handler from stack
    void emitLookupEffectHandler(const std::string& effectName, const std::string& opName);
    void emitEffectDispatch(const std::string& effectName, const std::string& opName, size_t numArgs);
    
    // Borrow parameter tracking for auto-dereference on return
    std::map<std::string, std::string> borrowParams_;          // Parameter name -> base type (e.g., "x" -> "int" for &int)
    std::string currentFnReturnType_;                          // Return type of current function
    
    // CTFE (Compile-Time Function Evaluation) support
    CTFEInterpreter ctfe_;                                     // CTFE interpreter instance
    std::set<std::string> comptimeFunctions_;                  // Names of comptime functions (don't emit code for these)
    bool tryEvalComptimeFnCall(CallExpr* call, int64_t& outValue);  // Try to evaluate comptime function call
    
    // Channel helper methods
    void emitChannelCreate(size_t bufferSize, int32_t elementSize);  // Create a new channel
    void emitChannelSend();                                          // Send value to channel (channel in RAX, value in RCX)
    void emitChannelRecv();                                          // Receive value from channel (channel in RAX, result in RAX)
    void emitChannelClose();                                         // Close a channel (channel in RAX)
    
    // Mutex helper methods
    void emitMutexCreate(int32_t elementSize);                       // Create a new mutex
    void emitMutexLock();                                            // Lock mutex (mutex in RAX)
    void emitMutexUnlock();                                          // Unlock mutex (mutex in RAX)
    
    // RWLock helper methods
    void emitRWLockCreate(int32_t elementSize);                      // Create a new RWLock
    void emitRWLockReadLock();                                       // Acquire read lock (rwlock in RAX)
    void emitRWLockWriteLock();                                      // Acquire write lock (rwlock in RAX)
    void emitRWLockUnlock();                                         // Release lock (rwlock in RAX)
    
    // Condition variable helper methods
    void emitCondCreate();                                           // Create a new condition variable
    void emitCondWait();                                             // Wait on condition (cond in RAX, mutex in RCX)
    void emitCondSignal();                                           // Signal one waiter (cond in RAX)
    void emitCondBroadcast();                                        // Signal all waiters (cond in RAX)
    
    // Semaphore helper methods
    void emitSemaphoreCreate(int64_t initialCount, int64_t maxCount); // Create a new semaphore
    void emitSemaphoreAcquire();                                     // Acquire semaphore (sem in RAX)
    void emitSemaphoreRelease();                                     // Release semaphore (sem in RAX)
    void emitSemaphoreTryAcquire();                                  // Try to acquire (sem in RAX, result in RAX)
    
    // Atomic integer helper methods
    void emitAtomicCreate(int64_t initialValue);                     // Create a new atomic (result in RAX)
    void emitAtomicLoad(MemoryOrder order = MemoryOrder::SeqCst);    // Load value (atomic in RAX, result in RAX)
    void emitAtomicStore(MemoryOrder order = MemoryOrder::SeqCst);   // Store value (atomic in RAX, value in RCX)
    void emitAtomicSwap(MemoryOrder order = MemoryOrder::SeqCst);    // Swap value (atomic in RAX, new value in RCX, old value in RAX)
    void emitAtomicCas(MemoryOrder successOrder = MemoryOrder::SeqCst, MemoryOrder failureOrder = MemoryOrder::SeqCst);  // CAS (atomic in RAX, expected in RCX, desired in RDX, returns 1 if success)
    void emitAtomicAdd(MemoryOrder order = MemoryOrder::SeqCst);     // Fetch-and-add (atomic in RAX, value in RCX, returns old value in RAX)
    void emitAtomicSub(MemoryOrder order = MemoryOrder::SeqCst);     // Fetch-and-sub (atomic in RAX, value in RCX, returns old value in RAX)
    void emitAtomicAnd(MemoryOrder order = MemoryOrder::SeqCst);     // Fetch-and-and (atomic in RAX, value in RCX, returns old value in RAX)
    void emitAtomicOr(MemoryOrder order = MemoryOrder::SeqCst);      // Fetch-and-or (atomic in RAX, value in RCX, returns old value in RAX)
    void emitAtomicXor(MemoryOrder order = MemoryOrder::SeqCst);     // Fetch-and-xor (atomic in RAX, value in RCX, returns old value in RAX)
    void emitMemoryFence(MemoryOrder order);                         // Emit memory fence for ordering
    
    // Future/Promise helper methods (codegen_expr_advanced_concurrency.cpp)
    void emitFutureCreate(int32_t elementSize);                      // Create a new future
    void emitFutureGet();                                            // Get value from future (blocks until ready)
    void emitFutureSet();                                            // Set value on future (future in RAX, value in RCX)
    void emitFutureIsReady();                                        // Check if future is ready (future in RAX, result in RAX)
    
    // Thread Pool helper methods (codegen_expr_advanced_concurrency.cpp)
    void emitThreadPoolCreate(int64_t numWorkers);                   // Create a new thread pool
    void emitThreadPoolSubmit();                                     // Submit task to pool (pool in RAX, task in RCX)
    void emitThreadPoolShutdown();                                   // Shutdown thread pool (pool in RAX)
    
    // Timeout helper methods (codegen_expr_advanced_concurrency.cpp)
    void emitChannelRecvTimeout(int64_t timeoutMs);                  // Receive with timeout (channel in RAX)
    
    // Cancellation helper methods (codegen_expr_advanced_concurrency.cpp)
    void emitCancelTokenCreate();                                    // Create a new cancel token
    void emitCancel();                                               // Cancel a token (token in RAX)
    void emitIsCancelled();                                          // Check if cancelled (token in RAX, result in RAX)
    
    // Async Runtime helper methods (codegen_expr_advanced_concurrency.cpp)
    void emitAsyncRuntimeInit(int64_t numWorkers);                   // Initialize async runtime
    void emitAsyncRuntimeRun();                                      // Run the event loop
    void emitAsyncRuntimeShutdown();                                 // Shutdown the runtime
    void emitAsyncSpawn();                                           // Spawn async task (task fn in RAX)
    void emitAsyncSleep(int64_t durationMs);                         // Sleep for duration
    void emitAsyncYield();                                           // Yield to other tasks
    
    // Smart pointer helper methods (codegen_expr_smart_ptr.cpp)
    void emitBoxDeref();                                             // Dereference Box (box ptr in RAX, value in RAX)
    void emitRcDeref();                                              // Dereference Rc (rc ptr in RAX, value in RAX)
    void emitArcDeref();                                             // Dereference Arc (arc ptr in RAX, value in RAX)
    void emitRcClone();                                              // Clone Rc (rc ptr in RAX, returns same ptr with incremented refcount)
    void emitArcClone();                                             // Clone Arc (arc ptr in RAX, returns same ptr with atomic increment)
    void emitWeakUpgrade();                                          // Upgrade Weak to Rc/Arc (weak ptr in RAX, returns Rc/Arc or nil)
    void emitWeakDowngrade(bool isAtomic);                           // Downgrade Rc/Arc to Weak (rc/arc ptr in RAX, returns weak ptr)
    void emitCellGet();                                              // Get Cell value (cell ptr in RAX, value in RAX)
    void emitCellSet();                                              // Set Cell value (cell ptr in RAX, value in RCX)
    void emitRefCellBorrow();                                        // Borrow RefCell (refcell ptr in RAX, returns ptr to value)
    void emitRefCellBorrowMut();                                     // Mutable borrow RefCell (refcell ptr in RAX, returns ptr to value)
    void emitRefCellRelease();                                       // Release RefCell borrow (refcell ptr in RAX)
    void emitBoxDrop();                                              // Drop Box (box ptr in RAX)
    void emitRcDrop();                                               // Drop Rc (rc ptr in RAX, decrements refcount, frees if 0)
    void emitArcDrop();                                              // Drop Arc (arc ptr in RAX, atomic decrement, frees if 0)
    
    // Callback/trampoline helpers
    void emitCallbackTrampoline(const std::string& fnName, const CallbackInfo& info);
    uint32_t getCallbackAddress(const std::string& fnName);  // Get address of callback wrapper
    void collectCallbackFunctions(Program& program);         // Scan for functions that need callbacks
    
    // Record layout helpers
    int32_t getTypeSize(const std::string& typeName);      // Get size of a type in bytes
    int32_t getTypeAlignment(const std::string& typeName); // Get alignment of a type in bytes
    void computeRecordLayout(RecordTypeInfo& info);        // Compute field offsets for a record
    int32_t getRecordFieldOffset(const std::string& recordName, int fieldIndex);  // Get field offset
    int32_t getRecordSize(const std::string& recordName);  // Get total record size
    
    // Struct-by-value helpers for FFI
    bool isSmallStruct(const std::string& typeName);       // Check if struct fits in registers (<=16 bytes)
    void emitStructByValuePass(const std::string& typeName, int argIndex);  // Pass struct in registers
    void emitStructByValueReturn(const std::string& typeName);  // Return struct in registers
    void emitLoadStructToRegs(const std::string& typeName);    // Load struct from RAX ptr to RCX:RDX
    void emitStoreRegsToStruct(const std::string& typeName);   // Store RCX:RDX to struct at RAX ptr
    
    // Bitfield helpers
    void emitBitfieldRead(const std::string& recordName, int fieldIndex);   // Read bitfield value
    void emitBitfieldWrite(const std::string& recordName, int fieldIndex);  // Write bitfield value
    
    // Modular expression helpers (codegen_expr_assign.cpp)
    void emitIndexAssignment(IndexExpr* indexExpr, AssignExpr& node);
    void emitMapIndexAssignment(IndexExpr* indexExpr, StringLiteral* strKey);
    void emitFixedArrayIndexAssignment(IndexExpr* indexExpr, const FixedArrayInfo& info);
    
    // Modular expression helpers (codegen_expr_index.cpp)
    void emitMapIndexAccess(IndexExpr& node, StringLiteral* strKey);
    void emitFixedArrayIndexAccess(IndexExpr& node, const FixedArrayInfo& info);
    void emitStringSlice(IndexExpr& node, Expression* startExpr, Expression* endExpr, bool inclusive);
    bool getNestedFixedArrayInfo(IndexExpr* indexExpr, FixedArrayInfo& outInfo);
    
    // Modular statement helpers (codegen_stmt_vardecl.cpp)
    void emitUninitializedVarDecl(VarDecl& node);
    void emitFixedArrayDecl(VarDecl& node);
    
    // Modular statement helpers (codegen_stmt_assign.cpp)
    void emitIdentifierAssign(Identifier* id, AssignStmt& node, bool isFloat, 
                              bool valueIsConst, bool valueIsSmall, int64_t constVal);
    void emitRegisterAssign(VarRegister reg, AssignStmt& node, bool isFloat,
                            bool valueIsConst, bool valueIsSmall, int64_t constVal);
    void emitFloatCompoundAssign(int32_t offset, TokenType op);
    void emitIntCompoundAssign(int32_t offset, TokenType op);
    void emitDerefAssign(DerefExpr* deref, AssignStmt& node);
    void emitIndexAssign(IndexExpr* indexExpr, AssignStmt& node);
    void emitFixedArrayAssign(IndexExpr* indexExpr, AssignStmt& node, const FixedArrayInfo& info);
    void emitMemberAssign(MemberExpr* member, AssignStmt& node);
    
    // Refinement type helpers
    void emitRefinementCheck(const RefinementTypeInfo& info, SourceLocation loc);
    bool tryEvalRefinementConstraint(const RefinementTypeInfo& info, int64_t value);  // Compile-time constraint check
    
    // Modular builtin helpers (codegen_call_builtins_system.cpp)
    void emitSystemExit(CallExpr& node);
    void emitSystemSleep(CallExpr& node);
    void emitSystemPlatform(CallExpr& node);
    void emitSystemArch(CallExpr& node);
    void emitSystemHostname(CallExpr& node);
    void emitSystemUsername(CallExpr& node);
    void emitSystemCpuCount(CallExpr& node);
    void emitTimeNow(CallExpr& node);
    void emitTimeNowMs(CallExpr& node);
    void emitTimeYear(CallExpr& node);
    void emitTimeMonth(CallExpr& node);
    void emitTimeDay(CallExpr& node);
    void emitTimeHour(CallExpr& node);
    void emitTimeMinute(CallExpr& node);
    void emitTimeSecond(CallExpr& node);
    void emitGetLocalTimeField(int32_t fieldOffset);
    
    // Modular builtin helpers (codegen_call_builtins_string.cpp)
    void emitStringLen(CallExpr& node);
    void emitStringUpper(CallExpr& node);
    void emitStringLower(CallExpr& node);
    void emitStringTrim(CallExpr& node);
    void emitStringStartsWith(CallExpr& node);
    void emitStringEndsWith(CallExpr& node);
    void emitStringSubstring(CallExpr& node);
    void emitStringReplace(CallExpr& node);
    void emitStringSplit(CallExpr& node);
    void emitStringJoin(CallExpr& node);
    void emitStringIndexOf(CallExpr& node);
    
    // Modular builtin helpers (codegen_call_builtins_io.cpp)
    void emitPrint(CallExpr& node, bool newline);
    void emitRead(CallExpr& node);
    void emitFileOpen(CallExpr& node);
    void emitFileRead(CallExpr& node);
    void emitFileWrite(CallExpr& node);
    void emitFileClose(CallExpr& node);
    void emitFileSize(CallExpr& node);
    
    // Modular builtin helpers (codegen_call_builtins_math.cpp)
    void emitMathAbs(CallExpr& node);
    void emitMathMin(CallExpr& node);
    void emitMathMax(CallExpr& node);
    void emitMathSqrt(CallExpr& node);
    void emitMathFloor(CallExpr& node);
    void emitMathCeil(CallExpr& node);
    void emitMathRound(CallExpr& node);
    void emitMathPow(CallExpr& node);
    
    // Modular builtin helpers (codegen_call_builtins_conv.cpp)
    void emitConvInt(CallExpr& node);
    void emitConvFloat(CallExpr& node);
    void emitConvStr(CallExpr& node);
    void emitConvBool(CallExpr& node);
    void emitConvType(CallExpr& node);
    
    // Modular builtin helpers (codegen_call_builtins_list.cpp)
    void emitListPush(CallExpr& node);
    void emitListPop(CallExpr& node);
    void emitListContains(CallExpr& node);
    void emitRange(CallExpr& node);
    
    // Modular builtin helpers (codegen_call_builtins_result.cpp)
    void emitResultOk(CallExpr& node);
    void emitResultErr(CallExpr& node);
    void emitResultIsOk(CallExpr& node);
    void emitResultIsErr(CallExpr& node);
    void emitResultUnwrap(CallExpr& node);
    void emitResultUnwrapOr(CallExpr& node);
    
    // Modular builtin helpers (codegen_call_builtins_memory.cpp)
    void emitMemAlloc(CallExpr& node);
    void emitMemFree(CallExpr& node);
    void emitMemStackAlloc(CallExpr& node);
    void emitMemSizeof(CallExpr& node);
    void emitMemAlignof(CallExpr& node);
    void emitMemOffsetof(CallExpr& node);
    void emitMemPlacementNew(CallExpr& node);
    void emitMemcpy(CallExpr& node);
    void emitMemset(CallExpr& node);
    void emitMemmove(CallExpr& node);
    void emitMemcmp(CallExpr& node);
    
    // Modular builtin helpers (codegen_call_builtins_gc.cpp)
    void emitGCCollect(CallExpr& node);
    void emitGCStats(CallExpr& node);
    void emitGCCount(CallExpr& node);
    void emitGCPin(CallExpr& node);
    void emitGCUnpin(CallExpr& node);
    void emitGCAddRoot(CallExpr& node);
    void emitGCRemoveRoot(CallExpr& node);
    void emitSetAllocator(CallExpr& node);
    void emitResetAllocator(CallExpr& node);
    void emitAllocatorStats(CallExpr& node);
    void emitAllocatorPeak(CallExpr& node);
    
    // Extended string builtins (call/codegen_call_builtins_string_ext.cpp)
    void emitStringLtrim(CallExpr& node);
    void emitStringRtrim(CallExpr& node);
    void emitStringCharAt(CallExpr& node);
    void emitStringRepeat(CallExpr& node);
    void emitStringReverse(CallExpr& node);
    void emitStringIsDigit(CallExpr& node);
    void emitStringIsAlpha(CallExpr& node);
    void emitStringOrd(CallExpr& node);
    void emitStringChr(CallExpr& node);
    void emitStringLastIndexOf(CallExpr& node);
    
    // Extended math builtins (call/codegen_call_builtins_math_ext.cpp)
    void emitMathSin(CallExpr& node);
    void emitMathCos(CallExpr& node);
    void emitMathTan(CallExpr& node);
    void emitMathExp(CallExpr& node);
    void emitMathLog(CallExpr& node);
    void emitMathTrunc(CallExpr& node);
    void emitMathSign(CallExpr& node);
    void emitMathClamp(CallExpr& node);
    void emitMathLerp(CallExpr& node);
    void emitMathGcd(CallExpr& node);
    void emitMathLcm(CallExpr& node);
    void emitMathFactorial(CallExpr& node);
    void emitMathFib(CallExpr& node);
    void emitMathRandom(CallExpr& node);
    void emitMathIsNan(CallExpr& node);
    void emitMathIsInf(CallExpr& node);
    
    // Complex number builtins (call/codegen_call_builtins_complex.cpp)
    void emitComplexCreate(CallExpr& node);
    void emitComplexReal(CallExpr& node);
    void emitComplexImag(CallExpr& node);
    
    // Extended numeric type builtins (call/codegen_call_builtins_numeric.cpp)
    void emitBigIntNew(CallExpr& node);
    void emitBigIntAdd(CallExpr& node);
    void emitBigIntToInt(CallExpr& node);
    void emitRationalNew(CallExpr& node);
    void emitRationalAdd(CallExpr& node);
    void emitRationalToFloat(CallExpr& node);
    void emitFixedNew(CallExpr& node);
    void emitFixedAdd(CallExpr& node);
    void emitFixedSub(CallExpr& node);
    void emitFixedMul(CallExpr& node);
    void emitFixedToFloat(CallExpr& node);
    void emitVec3New(CallExpr& node);
    void emitVec3Add(CallExpr& node);
    void emitVec3Dot(CallExpr& node);
    void emitVec3Length(CallExpr& node);
    
    // Extended list builtins (call/codegen_call_builtins_list_ext.cpp)
    void emitListFirst(CallExpr& node);
    void emitListLast(CallExpr& node);
    void emitListGet(CallExpr& node);
    void emitListReverse(CallExpr& node);
    void emitListIndex(CallExpr& node);
    void emitListIncludes(CallExpr& node);
    void emitListTake(CallExpr& node);
    void emitListDrop(CallExpr& node);
    void emitListMinOf(CallExpr& node);
    void emitListMaxOf(CallExpr& node);
    
    // Extended time builtins (call/codegen_call_builtins_time_ext.cpp)
    void emitTimeNowUs(CallExpr& node);
    void emitTimeWeekday(CallExpr& node);
    void emitTimeDayOfYear(CallExpr& node);
    void emitTimeMakeTime(CallExpr& node);
    void emitTimeAddDays(CallExpr& node);
    void emitTimeAddHours(CallExpr& node);
    void emitTimeDiffDays(CallExpr& node);
    void emitTimeIsLeapYear(CallExpr& node);
    
    // Extended system builtins (call/codegen_call_builtins_system_ext.cpp)
    void emitSystemEnv(CallExpr& node);
    void emitSystemSetEnv(CallExpr& node);
    void emitSystemHomeDir(CallExpr& node);
    void emitSystemTempDir(CallExpr& node);
    void emitSystemAssert(CallExpr& node);
    void emitSystemPanic(CallExpr& node);
    void emitSystemDebug(CallExpr& node);
    void emitSystemCommand(CallExpr& node);
    
    // Generics helper methods
    void collectGenericInstantiations(Program& program);   // Collect all generic instantiations
    void emitSpecializedFunctions();                       // Emit code for specialized functions
    std::string resolveGenericCall(const std::string& fnName, const std::vector<TypePtr>& typeArgs);
    
    // GC helper methods
    void emitGCInit();                                     // Emit GC initialization at program start
    void emitGCShutdown();                                 // Emit GC shutdown at program end
    void emitGCAlloc(size_t size, GCObjectType type);      // Emit GC allocation call
    void emitGCAllocList(size_t capacity);                 // Emit list allocation via GC
    void emitGCAllocRecord(size_t fieldCount, uint64_t typeId = 0);  // Emit record allocation via GC (typeId for RTTI)
    void emitGCAllocClosure(size_t captureCount);          // Emit closure allocation via GC
    void emitGCAllocString(size_t len);                    // Emit string allocation via GC
    void emitGCAllocMap(size_t capacity);                  // Emit map allocation via GC
    void emitGCAllocMapEntry();                            // Emit map entry allocation via GC
    void emitGCAllocRaw(size_t size);                      // Emit raw allocation via GC
    void emitGCPushFrame();                                // Emit stack frame push for GC
    void emitGCPopFrame();                                 // Emit stack frame pop for GC
    void emitGCCollectRoutine();                           // Emit the GC collection routine (mark-and-sweep)
    
    // Ownership system helpers
    void emitListClone();                                  // Deep copy a list (RAX = source, returns new list in RAX)
    void emitConstListClone(size_t count);                 // Deep copy a constant list (RAX = source data ptr, count = element count)
    void emitRecordClone(const std::string& typeName);     // Deep copy a record (RAX = source, returns new record in RAX)
    
    void visit(IntegerLiteral& node) override;
    void visit(FloatLiteral& node) override;
    void visit(StringLiteral& node) override;
    void visit(CharLiteral& node) override;
    void visit(ByteStringLiteral& node) override;
    void visit(InterpolatedString& node) override;
    void visit(BoolLiteral& node) override;
    void visit(NilLiteral& node) override;
    void visit(Identifier& node) override;
    void visit(BinaryExpr& node) override;
    void visit(UnaryExpr& node) override;
    void visit(CallExpr& node) override;
    void visit(MemberExpr& node) override;
    void visit(IndexExpr& node) override;
    void visit(ListExpr& node) override;
    void visit(RecordExpr& node) override;
    
    // Call dispatch helpers (codegen_call_dispatch.cpp)
    void emitStandardFunctionCall(CallExpr& node, const std::string& callTarget);
    void emitFloatFunctionCall(CallExpr& node, const std::string& callTarget);
    void emitFunctionPointerCall(CallExpr& node, const std::string& varName);
    void emitClosureCall(CallExpr& node);

    void visit(MapExpr& node) override;
    void visit(RangeExpr& node) override;
    void visit(LambdaExpr& node) override;
    void visit(TernaryExpr& node) override;
    void visit(ListCompExpr& node) override;
    void visit(AddressOfExpr& node) override;
    void visit(BorrowExpr& node) override;
    void visit(DerefExpr& node) override;
    void visit(NewExpr& node) override;
    void visit(CastExpr& node) override;
    void visit(AwaitExpr& node) override;
    void visit(SpawnExpr& node) override;
    void visit(DSLBlock& node) override;
    void visit(AssignExpr& node) override;
    void visit(PropagateExpr& node) override;
    void visit(ChanSendExpr& node) override;
    void visit(ChanRecvExpr& node) override;
    void visit(MakeChanExpr& node) override;
    void visit(MakeMutexExpr& node) override;
    void visit(MakeRWLockExpr& node) override;
    void visit(MakeCondExpr& node) override;
    void visit(MakeSemaphoreExpr& node) override;
    void visit(MutexLockExpr& node) override;
    void visit(MutexUnlockExpr& node) override;
    void visit(RWLockReadExpr& node) override;
    void visit(RWLockWriteExpr& node) override;
    void visit(RWLockUnlockExpr& node) override;
    void visit(CondWaitExpr& node) override;
    void visit(CondSignalExpr& node) override;
    void visit(CondBroadcastExpr& node) override;
    void visit(SemAcquireExpr& node) override;
    void visit(SemReleaseExpr& node) override;
    void visit(SemTryAcquireExpr& node) override;
    void visit(MakeAtomicExpr& node) override;
    void visit(AtomicLoadExpr& node) override;
    void visit(AtomicStoreExpr& node) override;
    void visit(AtomicSwapExpr& node) override;
    void visit(AtomicCasExpr& node) override;
    void visit(AtomicAddExpr& node) override;
    void visit(AtomicSubExpr& node) override;
    void visit(AtomicAndExpr& node) override;
    void visit(AtomicOrExpr& node) override;
    void visit(AtomicXorExpr& node) override;
    // Smart Pointer expressions
    void visit(MakeBoxExpr& node) override;
    void visit(MakeRcExpr& node) override;
    void visit(MakeArcExpr& node) override;
    void visit(MakeWeakExpr& node) override;
    void visit(MakeCellExpr& node) override;
    void visit(MakeRefCellExpr& node) override;
    // Advanced Concurrency - Future/Promise
    void visit(MakeFutureExpr& node) override;
    void visit(FutureGetExpr& node) override;
    void visit(FutureSetExpr& node) override;
    void visit(FutureIsReadyExpr& node) override;
    // Advanced Concurrency - Thread Pool
    void visit(MakeThreadPoolExpr& node) override;
    void visit(ThreadPoolSubmitExpr& node) override;
    void visit(ThreadPoolShutdownExpr& node) override;
    // Advanced Concurrency - Select
    void visit(SelectExpr& node) override;
    // Advanced Concurrency - Timeout
    void visit(TimeoutExpr& node) override;
    void visit(ChanRecvTimeoutExpr& node) override;
    void visit(ChanSendTimeoutExpr& node) override;
    // Advanced Concurrency - Cancellation
    void visit(MakeCancelTokenExpr& node) override;
    void visit(CancelExpr& node) override;
    void visit(IsCancelledExpr& node) override;
    // Async Runtime - Event Loop and Task Management
    void visit(AsyncRuntimeInitExpr& node) override;
    void visit(AsyncRuntimeRunExpr& node) override;
    void visit(AsyncRuntimeShutdownExpr& node) override;
    void visit(AsyncSpawnExpr& node) override;
    void visit(AsyncSleepExpr& node) override;
    void visit(AsyncYieldExpr& node) override;
    void visit(ExprStmt& node) override;
    void visit(VarDecl& node) override;
    void visit(DestructuringDecl& node) override;
    void visit(AssignStmt& node) override;
    void visit(Block& node) override;
    void visit(IfStmt& node) override;
    void visit(WhileStmt& node) override;
    void visit(ForStmt& node) override;
    void visit(MatchStmt& node) override;
    void visit(ReturnStmt& node) override;
    void visit(BreakStmt& node) override;
    void visit(ContinueStmt& node) override;
    void visit(TryStmt& node) override;
    void visit(FnDecl& node) override;
    void visit(RecordDecl& node) override;
    void visit(UnionDecl& node) override;
    void visit(EnumDecl& node) override;
    void visit(TypeAlias& node) override;
    void visit(TraitDecl& node) override;
    void visit(ImplBlock& node) override;
    void visit(ConceptDecl& node) override;  // Concepts are compile-time only, no codegen needed
    void visit(UnsafeBlock& node) override;
    void visit(ImportStmt& node) override;
    void visit(ExternDecl& node) override;
    void visit(MacroDecl& node) override;
    void visit(SyntaxMacroDecl& node) override;
    void visit(LayerDecl& node) override;
    void visit(UseStmt& node) override;
    void visit(ModuleDecl& node) override;
    void visit(DeleteStmt& node) override;
    void visit(LockStmt& node) override;
    void visit(AsmStmt& node) override;
    // Syntax Redesign - New Expression Visitors
    void visit(PlaceholderExpr& node) override;
    void visit(InclusiveRangeExpr& node) override;
    void visit(SafeNavExpr& node) override;
    void visit(TypeCheckExpr& node) override;
    // Syntax Redesign - New Statement Visitors
    void visit(LoopStmt& node) override;
    void visit(WithStmt& node) override;
    void visit(ScopeStmt& node) override;
    void visit(RequireStmt& node) override;
    void visit(EnsureStmt& node) override;
    void visit(InvariantStmt& node) override;
    void visit(ComptimeBlock& node) override;
    // Algebraic Effects
    void visit(EffectDecl& node) override;
    void visit(PerformEffectExpr& node) override;
    void visit(HandleExpr& node) override;
    void visit(ResumeExpr& node) override;
    void visit(Program& node) override;
};

} // namespace tyl

#endif // TYL_NATIVE_CODEGEN_H
