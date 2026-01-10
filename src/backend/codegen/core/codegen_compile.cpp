// Tyl Compiler - Native Code Generator Core Compile
// Main compile entry point and PE setup

#include "backend/codegen/codegen_base.h"
#include "backend/x64/peephole.h"
#include <cstring>

namespace tyl {

NativeCodeGen::NativeCodeGen() : lastExprWasFloat_(false), useOptimizedStackFrame_(true), 
    functionStackSize_(0), stackAllocated_(false), stdoutHandleCached_(false), useStdoutCaching_(true),
    optLevel_(CodeGenOptLevel::O2), runtimeRoutinesEmitted_(false) {
    // Initialize runtime routine labels
    itoaRoutineLabel_ = "__TYL_itoa";
    ftoaRoutineLabel_ = "__TYL_ftoa";
    printIntRoutineLabel_ = "__TYL_print_int";
}

bool NativeCodeGen::compile(Program& program, const std::string& outputFile) {
    pe_.addImport("kernel32.dll", "GetStdHandle");
    pe_.addImport("kernel32.dll", "WriteConsoleA");
    pe_.addImport("kernel32.dll", "ExitProcess");
    pe_.addImport("kernel32.dll", "GetProcessHeap");
    pe_.addImport("kernel32.dll", "HeapAlloc");
    pe_.addImport("kernel32.dll", "HeapFree");
    pe_.addImport("kernel32.dll", "GetComputerNameA");
    pe_.addImport("kernel32.dll", "GetSystemInfo");
    pe_.addImport("kernel32.dll", "Sleep");
    pe_.addImport("kernel32.dll", "GetLocalTime");
    pe_.addImport("kernel32.dll", "GetTickCount64");
    pe_.addImport("kernel32.dll", "GetEnvironmentVariableA");
    pe_.addImport("kernel32.dll", "GetSystemTimeAsFileTime");
    pe_.addImport("kernel32.dll", "SetEnvironmentVariableA");
    pe_.addImport("kernel32.dll", "GetTempPathA");
    pe_.addImport("kernel32.dll", "QueryPerformanceCounter");
    pe_.addImport("kernel32.dll", "QueryPerformanceFrequency");
    // Async/threading support
    pe_.addImport("kernel32.dll", "CreateThread");
    pe_.addImport("kernel32.dll", "WaitForSingleObject");
    pe_.addImport("kernel32.dll", "GetExitCodeThread");
    pe_.addImport("kernel32.dll", "CloseHandle");
    // Channel/synchronization support
    pe_.addImport("kernel32.dll", "CreateMutexA");
    pe_.addImport("kernel32.dll", "ReleaseMutex");
    pe_.addImport("kernel32.dll", "CreateEventA");
    pe_.addImport("kernel32.dll", "SetEvent");
    pe_.addImport("kernel32.dll", "ResetEvent");
    // Semaphore support
    pe_.addImport("kernel32.dll", "CreateSemaphoreA");
    pe_.addImport("kernel32.dll", "ReleaseSemaphore");
    // SRWLock support (Windows Vista+)
    pe_.addImport("kernel32.dll", "InitializeSRWLock");
    pe_.addImport("kernel32.dll", "AcquireSRWLockExclusive");
    pe_.addImport("kernel32.dll", "AcquireSRWLockShared");
    pe_.addImport("kernel32.dll", "ReleaseSRWLockExclusive");
    pe_.addImport("kernel32.dll", "ReleaseSRWLockShared");
    // Condition variable support (Windows Vista+)
    pe_.addImport("kernel32.dll", "InitializeConditionVariable");
    pe_.addImport("kernel32.dll", "SleepConditionVariableSRW");
    pe_.addImport("kernel32.dll", "WakeConditionVariable");
    pe_.addImport("kernel32.dll", "WakeAllConditionVariable");
    // File I/O support
    pe_.addImport("kernel32.dll", "CreateFileA");
    pe_.addImport("kernel32.dll", "ReadFile");
    pe_.addImport("kernel32.dll", "WriteFile");
    pe_.addImport("kernel32.dll", "GetFileSize");
    // Shell/system support
    pe_.addImport("shell32.dll", "SHGetFolderPathA");
    // User info support
    pe_.addImport("advapi32.dll", "GetUserNameA");
    
    // Pre-scan for extern declarations to add their imports BEFORE finalizing
    // This ensures extern function RVAs are calculated correctly
    for (auto& stmt : program.statements) {
        if (auto* ext = dynamic_cast<ExternDecl*>(stmt.get())) {
            if (!ext->library.empty()) {
                for (auto& fn : ext->functions) {
                    pe_.addImport(ext->library, fn->name);
                    externFunctions_[fn->name] = 0;
                }
            }
        }
    }
    
    pe_.finalizeImports();
    
    addString("%d");
    addString("\r\n");
    
    std::vector<uint8_t> itoaBuf(32, 0);
    itoaBufferRVA_ = pe_.addData(itoaBuf.data(), itoaBuf.size());
    
    // Initialize GC data section globals (48 bytes)
    // Layout: gc_alloc_head(8), gc_total_bytes(8), gc_threshold(8), gc_enabled(8), gc_collections(8), gc_stack_bottom(8)
    if (useGC_) {
        std::vector<uint8_t> gcData(48, 0);
        // Set gc_threshold to 1MB (1048576 bytes) at offset 16
        uint64_t threshold = 1048576;
        memcpy(&gcData[16], &threshold, 8);
        // Set gc_enabled to 1 at offset 24
        uint64_t enabled = 1;
        memcpy(&gcData[24], &enabled, 8);
        gcDataRVA_ = pe_.addData(gcData.data(), gcData.size());
        gcCollectLabel_ = "__TYL_gc_collect";
    }
    
    // First pass: scan for record declarations to populate recordTypes_
    for (auto& stmt : program.statements) {
        if (auto* rec = dynamic_cast<RecordDecl*>(stmt.get())) {
            RecordTypeInfo info;
            info.name = rec->name;
            info.reprC = rec->reprC;
            info.reprPacked = rec->reprPacked;
            info.reprAlign = rec->reprAlign;
            info.isUnion = false;
            info.hasBitfields = false;
            
            for (size_t i = 0; i < rec->fields.size(); i++) {
                const auto& [fieldName, fieldType] = rec->fields[i];
                info.fieldNames.push_back(fieldName);
                info.fieldTypes.push_back(fieldType);
                
                // Handle bitfield specification
                int bitWidth = 0;
                if (i < rec->bitfields.size() && rec->bitfields[i].isBitfield()) {
                    bitWidth = rec->bitfields[i].bitWidth;
                    info.hasBitfields = true;
                }
                info.fieldBitWidths.push_back(bitWidth);
                info.fieldBitOffsets.push_back(0);
            }
            recordTypes_[rec->name] = info;
        }
        // Also handle unions
        if (auto* uni = dynamic_cast<UnionDecl*>(stmt.get())) {
            RecordTypeInfo info;
            info.name = uni->name;
            info.reprC = uni->reprC;
            info.reprPacked = false;
            info.reprAlign = uni->reprAlign;
            info.isUnion = true;
            
            for (auto& [fieldName, fieldType] : uni->fields) {
                info.fieldNames.push_back(fieldName);
                info.fieldTypes.push_back(fieldType);
            }
            recordTypes_[uni->name] = info;
        }
    }
    
    // Second pass: identify mutable variables (they should not be treated as constants)
    std::set<std::string> mutableVars;
    for (auto& stmt : program.statements) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (varDecl->isMutable) {
                mutableVars.insert(varDecl->name);
            }
            // Track record types from type annotations
            if (!varDecl->typeName.empty() && recordTypes_.find(varDecl->typeName) != recordTypes_.end()) {
                varRecordTypes_[varDecl->name] = varDecl->typeName;
            }
        }
        // Also scan function bodies for record type declarations
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (fn->body) {
                if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                    for (auto& bodyStmt : block->statements) {
                        if (auto* varDecl = dynamic_cast<VarDecl*>(bodyStmt.get())) {
                            if (varDecl->isMutable) {
                                mutableVars.insert(varDecl->name);
                            }
                            // Track record types from type annotations
                            if (!varDecl->typeName.empty() && recordTypes_.find(varDecl->typeName) != recordTypes_.end()) {
                                varRecordTypes_[varDecl->name] = varDecl->typeName;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Pre-scan for generic functions (needed for type inference in isFloatExpression)
    for (auto& stmt : program.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (!fn->typeParams.empty()) {
                genericFunctions_[fn->name] = fn;
            }
            // Register comptime functions early so they can be used in constant evaluation
            if (fn->isComptime) {
                ctfe_.registerComptimeFunction(fn);
                comptimeFunctions_.insert(fn->name);
            }
        }
    }
    
    // Collect callback functions that need trampolines for C interop
    collectCallbackFunctions(program);
    
    // Collect generic instantiations BEFORE the pre-scan for float variables
    // This ensures monomorphizer has recorded all instantiations so isFloatExpression
    // can correctly identify generic function calls that return float
    collectGenericInstantiations(program);
    
    // Pre-scan for constants (both int and float) and lists
    for (auto& stmt : program.statements) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (varDecl->initializer) {
                // Check if it's a list expression
                if (auto* list = dynamic_cast<ListExpr*>(varDecl->initializer.get())) {
                    listSizes[varDecl->name] = list->elements.size();
                    
                    // Check if all elements are constants
                    std::vector<int64_t> values;
                    bool allConst = true;
                    for (auto& elem : list->elements) {
                        int64_t val;
                        if (tryEvalConstant(elem.get(), val)) {
                            values.push_back(val);
                        } else {
                            allConst = false;
                            break;
                        }
                    }
                    if (allConst) {
                        constListVars[varDecl->name] = values;
                    }
                }
                
                // Track float variables (both mutable and immutable)
                if (isFloatExpression(varDecl->initializer.get())) {
                    floatVars.insert(varDecl->name);
                }
                
                // Skip mutable variables for constant tracking
                if (mutableVars.count(varDecl->name)) continue;
                
                // Check for int constants FIRST (before float)
                // This ensures integer comptime functions are stored as integers
                int64_t intVal;
                bool isFloatExpr = isFloatExpression(varDecl->initializer.get());
                bool evalSuccess = tryEvalConstant(varDecl->initializer.get(), intVal);
                
                if (evalSuccess) {
                    // Only store as int constant if it's NOT a float expression
                    if (!isFloatExpr) {
                        constVars[varDecl->name] = intVal;
                    }
                }
                
                // Check for float constants (only if not already stored as int)
                if (constVars.find(varDecl->name) == constVars.end()) {
                    double floatVal;
                    if (tryEvalConstantFloat(varDecl->initializer.get(), floatVal)) {
                        if (dynamic_cast<FloatLiteral*>(varDecl->initializer.get()) || isFloatExpr) {
                            constFloatVars[varDecl->name] = floatVal;
                            floatVars.insert(varDecl->name);
                        }
                    }
                }
                
                // Check for string constants
                std::string strVal;
                if (tryEvalConstantString(varDecl->initializer.get(), strVal)) {
                    constStrVars[varDecl->name] = strVal;
                }
            }
        }
        // Also handle AssignExpr (e.g., "pi = 3.14" without let/var keyword)
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (auto* id = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                    // Track float variables
                    if (isFloatExpression(assignExpr->value.get())) {
                        floatVars.insert(id->name);
                        
                        // Check for float constants (only for simple assignment, not compound)
                        if (assignExpr->op == TokenType::ASSIGN) {
                            double floatVal;
                            if (tryEvalConstantFloat(assignExpr->value.get(), floatVal)) {
                                constFloatVars[id->name] = floatVal;
                            }
                        }
                    }
                    
                    // Check for int constants
                    if (assignExpr->op == TokenType::ASSIGN) {
                        int64_t intVal;
                        if (tryEvalConstant(assignExpr->value.get(), intVal)) {
                            constVars[id->name] = intVal;
                        }
                        
                        // Check for string constants
                        std::string strVal;
                        if (tryEvalConstantString(assignExpr->value.get(), strVal)) {
                            constStrVars[id->name] = strVal;
                        }
                    }
                }
            }
        }
    }
    
    // Visit the program to generate code
    program.accept(*this);
    
    // Emit shared runtime routines (itoa, ftoa, etc.) at end of code section
    // This must be done BEFORE label resolution
    emitRuntimeRoutines();
    
    // Finalize vtables with actual function addresses
    finalizeVtables();
    
    // Resolve label fixups
    asm_.resolve(PEGenerator::CODE_RVA);
    
    // Apply peephole optimizations
    PeepholeOptimizer peephole;
    peephole.optimize(asm_.code);
    
    // Add code to PE and write output
    pe_.addCodeWithFixups(asm_.code, asm_.ripFixups);
    return pe_.write(outputFile);
}

bool NativeCodeGen::compileToObject(Program& program, const std::string& outputFile) {
    // Initialize imports (same as compile)
    pe_.addImport("kernel32.dll", "GetStdHandle");
    pe_.addImport("kernel32.dll", "WriteConsoleA");
    pe_.addImport("kernel32.dll", "ExitProcess");
    pe_.addImport("kernel32.dll", "GetProcessHeap");
    pe_.addImport("kernel32.dll", "HeapAlloc");
    pe_.addImport("kernel32.dll", "HeapFree");
    pe_.addImport("kernel32.dll", "GetComputerNameA");
    pe_.addImport("kernel32.dll", "GetSystemInfo");
    pe_.addImport("kernel32.dll", "Sleep");
    pe_.addImport("kernel32.dll", "GetLocalTime");
    pe_.addImport("kernel32.dll", "GetTickCount64");
    pe_.addImport("kernel32.dll", "GetEnvironmentVariableA");
    pe_.addImport("kernel32.dll", "GetSystemTimeAsFileTime");
    pe_.addImport("kernel32.dll", "SetEnvironmentVariableA");
    pe_.addImport("kernel32.dll", "GetTempPathA");
    pe_.addImport("kernel32.dll", "QueryPerformanceCounter");
    pe_.addImport("kernel32.dll", "QueryPerformanceFrequency");
    pe_.addImport("kernel32.dll", "CreateThread");
    pe_.addImport("kernel32.dll", "WaitForSingleObject");
    pe_.addImport("kernel32.dll", "GetExitCodeThread");
    pe_.addImport("kernel32.dll", "CloseHandle");
    pe_.addImport("kernel32.dll", "CreateMutexA");
    pe_.addImport("kernel32.dll", "ReleaseMutex");
    pe_.addImport("kernel32.dll", "CreateEventA");
    pe_.addImport("kernel32.dll", "SetEvent");
    pe_.addImport("kernel32.dll", "ResetEvent");
    pe_.addImport("kernel32.dll", "CreateSemaphoreA");
    pe_.addImport("kernel32.dll", "ReleaseSemaphore");
    pe_.addImport("kernel32.dll", "InitializeSRWLock");
    pe_.addImport("kernel32.dll", "AcquireSRWLockExclusive");
    pe_.addImport("kernel32.dll", "AcquireSRWLockShared");
    pe_.addImport("kernel32.dll", "ReleaseSRWLockExclusive");
    pe_.addImport("kernel32.dll", "ReleaseSRWLockShared");
    pe_.addImport("kernel32.dll", "InitializeConditionVariable");
    pe_.addImport("kernel32.dll", "SleepConditionVariableSRW");
    pe_.addImport("kernel32.dll", "WakeConditionVariable");
    pe_.addImport("kernel32.dll", "WakeAllConditionVariable");
    pe_.addImport("kernel32.dll", "CreateFileA");
    pe_.addImport("kernel32.dll", "ReadFile");
    pe_.addImport("kernel32.dll", "WriteFile");
    pe_.addImport("kernel32.dll", "GetFileSize");
    pe_.addImport("shell32.dll", "SHGetFolderPathA");
    pe_.addImport("advapi32.dll", "GetUserNameA");
    
    // Pre-scan for extern declarations to add their imports BEFORE finalizing
    for (auto& stmt : program.statements) {
        if (auto* ext = dynamic_cast<ExternDecl*>(stmt.get())) {
            if (!ext->library.empty()) {
                for (auto& fn : ext->functions) {
                    pe_.addImport(ext->library, fn->name);
                    externFunctions_[fn->name] = 0;
                }
            }
        }
    }
    
    pe_.finalizeImports();
    
    addString("%d");
    addString("\r\n");
    
    std::vector<uint8_t> itoaBuf(32, 0);
    itoaBufferRVA_ = pe_.addData(itoaBuf.data(), itoaBuf.size());
    
    // Initialize GC data section globals
    if (useGC_) {
        std::vector<uint8_t> gcData(48, 0);
        uint64_t threshold = 1048576;
        memcpy(&gcData[16], &threshold, 8);
        uint64_t enabled = 1;
        memcpy(&gcData[24], &enabled, 8);
        gcDataRVA_ = pe_.addData(gcData.data(), gcData.size());
        gcCollectLabel_ = "__TYL_gc_collect";
    }
    
    // First pass: scan for record declarations
    for (auto& stmt : program.statements) {
        if (auto* rec = dynamic_cast<RecordDecl*>(stmt.get())) {
            RecordTypeInfo info;
            info.name = rec->name;
            info.reprC = rec->reprC;
            info.reprPacked = rec->reprPacked;
            info.reprAlign = rec->reprAlign;
            info.isUnion = false;
            info.hasBitfields = false;
            
            for (size_t i = 0; i < rec->fields.size(); i++) {
                const auto& [fieldName, fieldType] = rec->fields[i];
                info.fieldNames.push_back(fieldName);
                info.fieldTypes.push_back(fieldType);
                
                int bitWidth = 0;
                if (i < rec->bitfields.size() && rec->bitfields[i].isBitfield()) {
                    bitWidth = rec->bitfields[i].bitWidth;
                    info.hasBitfields = true;
                }
                info.fieldBitWidths.push_back(bitWidth);
                info.fieldBitOffsets.push_back(0);
            }
            recordTypes_[rec->name] = info;
        }
        if (auto* uni = dynamic_cast<UnionDecl*>(stmt.get())) {
            RecordTypeInfo info;
            info.name = uni->name;
            info.reprC = uni->reprC;
            info.reprPacked = false;
            info.reprAlign = uni->reprAlign;
            info.isUnion = true;
            
            for (auto& [fieldName, fieldType] : uni->fields) {
                info.fieldNames.push_back(fieldName);
                info.fieldTypes.push_back(fieldType);
            }
            recordTypes_[uni->name] = info;
        }
    }
    
    // Second pass: identify mutable variables
    std::set<std::string> mutableVars;
    for (auto& stmt : program.statements) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (varDecl->isMutable) {
                mutableVars.insert(varDecl->name);
            }
            if (!varDecl->typeName.empty() && recordTypes_.find(varDecl->typeName) != recordTypes_.end()) {
                varRecordTypes_[varDecl->name] = varDecl->typeName;
            }
        }
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (fn->body) {
                if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                    for (auto& bodyStmt : block->statements) {
                        if (auto* varDecl = dynamic_cast<VarDecl*>(bodyStmt.get())) {
                            if (varDecl->isMutable) {
                                mutableVars.insert(varDecl->name);
                            }
                            if (!varDecl->typeName.empty() && recordTypes_.find(varDecl->typeName) != recordTypes_.end()) {
                                varRecordTypes_[varDecl->name] = varDecl->typeName;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Pre-scan for generic functions
    for (auto& stmt : program.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (!fn->typeParams.empty()) {
                genericFunctions_[fn->name] = fn;
            }
        }
    }
    
    collectCallbackFunctions(program);
    collectGenericInstantiations(program);
    
    // Pre-scan for constants
    for (auto& stmt : program.statements) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (varDecl->initializer) {
                if (auto* list = dynamic_cast<ListExpr*>(varDecl->initializer.get())) {
                    listSizes[varDecl->name] = list->elements.size();
                    
                    std::vector<int64_t> values;
                    bool allConst = true;
                    for (auto& elem : list->elements) {
                        int64_t val;
                        if (tryEvalConstant(elem.get(), val)) {
                            values.push_back(val);
                        } else {
                            allConst = false;
                            break;
                        }
                    }
                    if (allConst) {
                        constListVars[varDecl->name] = values;
                    }
                }
                
                if (isFloatExpression(varDecl->initializer.get())) {
                    floatVars.insert(varDecl->name);
                }
                
                if (mutableVars.count(varDecl->name)) continue;
                
                // Check for int constants FIRST (before float)
                int64_t intVal;
                if (tryEvalConstant(varDecl->initializer.get(), intVal)) {
                    if (!isFloatExpression(varDecl->initializer.get())) {
                        constVars[varDecl->name] = intVal;
                    }
                }
                
                // Check for float constants (only if not already stored as int)
                if (constVars.find(varDecl->name) == constVars.end()) {
                    double floatVal;
                    if (tryEvalConstantFloat(varDecl->initializer.get(), floatVal)) {
                        if (dynamic_cast<FloatLiteral*>(varDecl->initializer.get()) ||
                            isFloatExpression(varDecl->initializer.get())) {
                            constFloatVars[varDecl->name] = floatVal;
                            floatVars.insert(varDecl->name);
                        }
                    }
                }
                
                std::string strVal;
                if (tryEvalConstantString(varDecl->initializer.get(), strVal)) {
                    constStrVars[varDecl->name] = strVal;
                }
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (auto* id = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                    if (isFloatExpression(assignExpr->value.get())) {
                        floatVars.insert(id->name);
                        
                        if (assignExpr->op == TokenType::ASSIGN) {
                            double floatVal;
                            if (tryEvalConstantFloat(assignExpr->value.get(), floatVal)) {
                                constFloatVars[id->name] = floatVal;
                            }
                        }
                    }
                    
                    if (assignExpr->op == TokenType::ASSIGN) {
                        int64_t intVal;
                        if (tryEvalConstant(assignExpr->value.get(), intVal)) {
                            constVars[id->name] = intVal;
                        }
                        
                        std::string strVal;
                        if (tryEvalConstantString(assignExpr->value.get(), strVal)) {
                            constStrVars[id->name] = strVal;
                        }
                    }
                }
            }
        }
    }
    
    // Visit the program to generate code
    program.accept(*this);
    
    // Finalize vtables
    finalizeVtables();
    
    // Resolve label fixups
    asm_.resolve(PEGenerator::CODE_RVA);
    
    // Apply peephole optimizations
    PeepholeOptimizer peephole;
    peephole.optimize(asm_.code);
    
    // Create ObjectFile from generated code
    ObjectFile obj;
    obj.moduleName = outputFile;
    
    // Add code section
    obj.codeSection = asm_.code;
    
    // Add data section from PE generator (contains strings, constants, etc.)
    obj.dataSection = pe_.getDataSection();
    
    // Add symbols for all functions (using asm_.labels which maps label name to offset)
    for (const auto& [label, offset] : asm_.labels) {
        ObjSymbol sym;
        sym.name = label;
        sym.type = ObjSymbolType::FUNCTION;
        sym.section = 0;  // Code section
        sym.offset = static_cast<uint32_t>(offset);
        sym.size = 0;  // Size unknown at this point
        
        // Check for export/hidden/weak attributes
        auto attrIt = fnAttributes_.find(label);
        if (attrIt != fnAttributes_.end()) {
            sym.isExported = !attrIt->second.isHidden;  // Export unless hidden
            sym.isHidden = attrIt->second.isHidden;
            sym.isWeak = attrIt->second.isWeak;
            // If explicitly marked #[export], ensure it's exported
            if (attrIt->second.isExport) {
                sym.isExported = true;
                sym.isHidden = false;
            }
        } else {
            // Default: export non-internal symbols
            sym.isExported = (label == "_start" || label.find("__") != 0);
            sym.isHidden = false;
            sym.isWeak = false;
        }
        obj.addSymbol(sym);
    }
    
    // Build a reverse map from import RVA to function name
    std::map<uint32_t, std::string> importRVAToName;
    for (const auto& [dll, funcs] : pe_.getImports()) {
        for (const auto& func : funcs) {
            uint32_t rva = pe_.getImportRVA(func);
            importRVAToName[rva] = func;
        }
    }
    
    // Add relocations from assembler's RIP fixups
    for (const auto& ripFixup : asm_.ripFixups) {
        uint32_t offset = static_cast<uint32_t>(ripFixup.first);
        uint32_t targetRVA = ripFixup.second;
        
        Relocation reloc;
        reloc.offset = offset;
        reloc.type = RelocType::RIP32;
        reloc.addend = 0;
        
        // Determine what section this references based on placeholder RVA
        if (targetRVA >= PEGenerator::IDATA_RVA_PLACEHOLDER) {
            // Reference to import table - find which import function this refers to
            auto it = importRVAToName.find(targetRVA);
            if (it != importRVAToName.end()) {
                // Store the import function name as the symbol
                reloc.symbol = "__import_" + it->second;
            } else {
                // Unknown import, store as generic idata reference
                reloc.symbol = "__idata";
                reloc.addend = static_cast<int32_t>(targetRVA - PEGenerator::IDATA_RVA_PLACEHOLDER);
            }
        } else if (targetRVA >= PEGenerator::DATA_RVA_PLACEHOLDER) {
            // Reference to data section
            reloc.symbol = "__data";
            reloc.addend = static_cast<int32_t>(targetRVA - PEGenerator::DATA_RVA_PLACEHOLDER);
        } else {
            // Code-relative reference - already resolved, skip
            continue;
        }
        obj.addCodeRelocation(reloc);
    }
    
    // Add imports from PE generator
    for (const auto& [dll, funcs] : pe_.getImports()) {
        for (const auto& func : funcs) {
            obj.addImport(dll, func);
        }
    }
    
    // Write the object file
    return obj.write(outputFile);
}

} // namespace tyl
