// Tyl Compiler - FFI/Extern Code Generation Helpers
// Handles: C type utilities, calling convention helpers, callbacks

#include "backend/codegen/codegen_base.h"

namespace tyl {

// FFI helper functions for C interop
// These are used by the main codegen to handle extern function calls

// Check if a type string represents a pointer type
bool isFFIPointerType(const std::string& type) {
    return !type.empty() && type[0] == '*';
}

// Check if a type string represents void
bool isFFIVoidType(const std::string& type) {
    return type == "void" || type == "*void";
}

// Get the size of a C-compatible type in bytes
size_t getFFICTypeSize(const std::string& type) {
    if (type.empty() || type == "void") return 0;
    if (isFFIPointerType(type)) return 8;  // All pointers are 64-bit
    if (type == "int" || type == "int32" || type == "i32") return 4;
    if (type == "int64" || type == "i64" || type == "long") return 8;
    if (type == "int16" || type == "i16" || type == "short") return 2;
    if (type == "int8" || type == "i8" || type == "char" || type == "byte") return 1;
    if (type == "uint" || type == "uint32" || type == "u32") return 4;
    if (type == "uint64" || type == "u64" || type == "ulong" || type == "usize") return 8;
    if (type == "uint16" || type == "u16" || type == "ushort") return 2;
    if (type == "uint8" || type == "u8" || type == "uchar") return 1;
    if (type == "float" || type == "f32" || type == "float32") return 4;
    if (type == "float64" || type == "f64" || type == "double") return 8;
    if (type == "bool") return 1;
    if (type == "str" || type == "string") return 8;  // String pointer
    return 8;  // Default to pointer size for unknown types
}

// Check if type should be passed in XMM register (floating point)
bool isFFIFloatType(const std::string& type) {
    return type == "float" || type == "f32" || type == "float32" ||
           type == "float64" || type == "f64" || type == "double";
}

// Collect functions that need callback trampolines
// This scans for functions with calling convention attributes or that are passed to extern functions
void NativeCodeGen::collectCallbackFunctions(Program& program) {
    for (auto& stmt : program.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            // Track calling convention for all functions
            fnCallingConvs_[fn->name] = fn->callingConv;
            
            // If function has explicit calling convention, create callback info
            if (fn->callingConv != CallingConvention::Default) {
                CallbackInfo info;
                info.flexFnName = fn->name;
                info.trampolineLabel = "__callback_" + fn->name;
                info.callingConv = fn->callingConv;
                info.returnType = fn->returnType;
                for (const auto& param : fn->params) {
                    info.paramTypes.push_back(param.second);
                }
                callbacks_[fn->name] = info;
            }
        }
    }
}

// Emit a callback trampoline that wraps a Flex function for C calling conventions
// The trampoline converts from the specified calling convention to Flex's internal convention
void NativeCodeGen::emitCallbackTrampoline(const std::string& fnName, const CallbackInfo& info) {
    // Create label for the trampoline
    asm_.label(info.trampolineLabel);
    
    // For Windows x64, the calling convention is already compatible
    // The trampoline just needs to forward the call
    // For cdecl on x64, parameters are still in RCX, RDX, R8, R9 (Windows x64 ABI)
    
    // Standard function prologue
    asm_.push_rbp();
    asm_.mov_rbp_rsp();
    
    // Allocate shadow space for the call
    asm_.sub_rsp_imm32(0x28);  // 32 bytes shadow + 8 for alignment
    
    // Parameters are already in the right registers for Windows x64 ABI
    // RCX, RDX, R8, R9 for first 4 integer/pointer args
    // XMM0-XMM3 for first 4 float args
    
    // Call the actual Flex function
    asm_.call_rel32(fnName);
    
    // Epilogue
    asm_.add_rsp_imm32(0x28);
    asm_.pop_rbp();
    asm_.ret();
}

// Get the address of a callback wrapper for a function
// Returns 0 if no callback exists
uint32_t NativeCodeGen::getCallbackAddress(const std::string& fnName) {
    auto it = callbacks_.find(fnName);
    if (it != callbacks_.end()) {
        // Return the address of the trampoline
        auto trampolineIt = callbackTrampolines_.find(it->second.trampolineLabel);
        if (trampolineIt != callbackTrampolines_.end()) {
            return trampolineIt->second;
        }
    }
    return 0;
}

} // namespace tyl
