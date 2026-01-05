// Tyl Compiler - Native Code Generator Trait Support
// Handles: vtable generation, trait method dispatch, trait objects

#include "backend/codegen/codegen_base.h"

namespace tyl {

// Get the index of a method in a trait's vtable
int NativeCodeGen::getMethodIndex(const std::string& traitName, const std::string& methodName) {
    auto it = traits_.find(traitName);
    if (it == traits_.end()) return -1;
    
    const auto& methodNames = it->second.methodNames;
    for (size_t i = 0; i < methodNames.size(); i++) {
        if (methodNames[i] == methodName) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Resolve a trait method call to a concrete implementation label
std::string NativeCodeGen::resolveTraitMethod(const std::string& typeName, const std::string& traitName, 
                                               const std::string& methodName) {
    std::string implKey = traitName + ":" + typeName;
    auto it = impls_.find(implKey);
    if (it == impls_.end()) {
        // Try without trait name (inherent impl)
        implKey = ":" + typeName;
        it = impls_.find(implKey);
        if (it == impls_.end()) return "";
    }
    
    auto methodIt = it->second.methodLabels.find(methodName);
    if (methodIt == it->second.methodLabels.end()) return "";
    
    return methodIt->second;
}

// Finalize vtables after all code has been emitted
// This fills in the actual function addresses
void NativeCodeGen::finalizeVtables() {
    for (auto& [implKey, info] : impls_) {
        if (info.traitName.empty()) continue;  // Skip inherent impls
        
        auto traitIt = traits_.find(info.traitName);
        if (traitIt == traits_.end()) continue;
        
        const auto& trait = traitIt->second;
        
        // Build vtable data with placeholder zeros (will be fixed up by PE generator)
        std::vector<uint8_t> vtableData;
        std::vector<std::pair<size_t, std::string>> fixups;  // offset -> label
        
        for (const auto& methodName : trait.methodNames) {
            auto labelIt = info.methodLabels.find(methodName);
            if (labelIt != info.methodLabels.end()) {
                // Record fixup location and label
                fixups.push_back({vtableData.size(), labelIt->second});
            }
            // Reserve 8 bytes for function pointer (will be fixed up)
            for (int i = 0; i < 8; i++) {
                vtableData.push_back(0);
            }
        }
        
        if (!vtableData.empty()) {
            uint32_t vtableRVA = pe_.addData(vtableData.data(), vtableData.size());
            vtables_[implKey] = vtableRVA;
            
            // Register fixups with PE generator
            for (const auto& [offset, label] : fixups) {
                pe_.addVtableFixup(vtableRVA + static_cast<uint32_t>(offset), label);
            }
        }
    }
    
    // Pass label offsets to PE generator for vtable fixup resolution
    pe_.setLabelOffsets(asm_.labels);
}

// Emit a dynamic dispatch call through a vtable
// This is used when calling a method on a trait object (dyn Trait)
// 
// Trait object layout (fat pointer):
//   [data_ptr: 8 bytes]   - pointer to the actual data
//   [vtable_ptr: 8 bytes] - pointer to the vtable
//
// Calling convention (Windows x64):
//   RCX = first arg (self/data pointer)
//   RDX = second arg
//   R8  = third arg
//   R9  = fourth arg
//
// On entry: RAX = trait object pointer (points to fat pointer)
// After setup: RCX = data pointer (self), call through vtable
void NativeCodeGen::emitTraitMethodCall(const std::string& traitName, const std::string& methodName, 
                                         int argCount) {
    int methodIndex = getMethodIndex(traitName, methodName);
    if (methodIndex < 0) {
        // Method not found in trait - this is a compile error
        // For now, just return without emitting anything
        return;
    }
    
    // RAX = trait object (fat pointer)
    // Save trait object pointer
    asm_.push_rax();
    
    // Load vtable pointer from trait object (offset 8)
    // mov r10, [rax+8]
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8B); asm_.code.push_back(0x50);
    asm_.code.push_back(0x08);
    
    // Load function pointer from vtable[methodIndex * 8]
    // mov r11, [r10 + methodIndex*8]
    int vtableOffset = methodIndex * 8;
    if (vtableOffset == 0) {
        // mov r11, [r10]
        asm_.code.push_back(0x4D); asm_.code.push_back(0x8B); asm_.code.push_back(0x1A);
    } else if (vtableOffset < 128) {
        // mov r11, [r10 + imm8]
        asm_.code.push_back(0x4D); asm_.code.push_back(0x8B); asm_.code.push_back(0x5A);
        asm_.code.push_back(static_cast<uint8_t>(vtableOffset));
    } else {
        // mov r11, [r10 + imm32]
        asm_.code.push_back(0x4D); asm_.code.push_back(0x8B); asm_.code.push_back(0x9A);
        asm_.code.push_back(vtableOffset & 0xFF);
        asm_.code.push_back((vtableOffset >> 8) & 0xFF);
        asm_.code.push_back((vtableOffset >> 16) & 0xFF);
        asm_.code.push_back((vtableOffset >> 24) & 0xFF);
    }
    
    // Restore trait object pointer
    asm_.pop_rax();
    
    // Load data pointer from trait object (offset 0) into RCX (first arg = self)
    // mov rcx, [rax]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x08);
    
    // Allocate shadow space for Windows x64 calling convention
    if (!stackAllocated_) {
        asm_.sub_rsp_imm32(0x20);
    }
    
    // Call through R11 (function pointer)
    // call r11
    asm_.code.push_back(0x41); asm_.code.push_back(0xFF); asm_.code.push_back(0xD3);
    
    // Clean up shadow space
    if (!stackAllocated_) {
        asm_.add_rsp_imm32(0x20);
    }
    
    // Result is in RAX
}

} // namespace tyl
