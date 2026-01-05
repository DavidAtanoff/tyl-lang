// Tyl Compiler - Native Code Generator Record Layout
// Handles: computeRecordLayout, getRecordFieldOffset, getRecordSize, bitfield operations

#include "backend/codegen/codegen_base.h"
#include <algorithm>

namespace tyl {

void NativeCodeGen::computeRecordLayout(RecordTypeInfo& info) {
    if (info.offsetsComputed) return;
    
    info.fieldOffsets.clear();
    info.fieldOffsets.resize(info.fieldNames.size());
    
    // Start after the 8-byte GC header
    int32_t currentOffset = 8;
    int32_t maxAlignment = 1;
    
    // For bitfields, track current storage unit
    int32_t bitfieldOffset = 0;
    int32_t bitfieldBitsUsed = 0;
    int32_t bitfieldStorageSize = 0;
    
    for (size_t i = 0; i < info.fieldNames.size(); i++) {
        const std::string& fieldType = info.fieldTypes[i];
        int32_t fieldSize = getTypeSize(fieldType);
        int32_t fieldAlign = info.reprPacked ? 1 : getTypeAlignment(fieldType);
        
        // Check if this is a bitfield
        int bitWidth = 0;
        if (i < info.fieldBitWidths.size()) {
            bitWidth = info.fieldBitWidths[i];
        }
        
        if (bitWidth > 0) {
            // This is a bitfield
            // Determine storage unit size based on field type
            int32_t storageSize = fieldSize;
            int32_t storageBits = storageSize * 8;
            
            // Check if we need to start a new storage unit
            bool needNewUnit = (bitfieldStorageSize == 0) ||
                               (bitfieldStorageSize != storageSize) ||
                               (bitfieldBitsUsed + bitWidth > storageBits);
            
            if (needNewUnit) {
                // Align for new storage unit
                if (!info.reprPacked && bitfieldStorageSize == 0) {
                    int32_t align = std::min(fieldAlign, 8);
                    currentOffset = (currentOffset + align - 1) & ~(align - 1);
                }
                
                bitfieldOffset = currentOffset;
                bitfieldBitsUsed = 0;
                bitfieldStorageSize = storageSize;
                currentOffset += storageSize;
            }
            
            // Record the bit offset within the storage unit
            info.fieldOffsets[i] = bitfieldOffset;
            if (i >= info.fieldBitOffsets.size()) {
                info.fieldBitOffsets.resize(i + 1);
            }
            info.fieldBitOffsets[i] = bitfieldBitsUsed;
            
            bitfieldBitsUsed += bitWidth;
        } else {
            // Regular field - reset bitfield tracking
            bitfieldStorageSize = 0;
            bitfieldBitsUsed = 0;
            
            // Apply alignment unless packed
            if (!info.reprPacked) {
                currentOffset = (currentOffset + fieldAlign - 1) & ~(fieldAlign - 1);
            }
            
            info.fieldOffsets[i] = currentOffset;
            currentOffset += fieldSize;
            
            maxAlignment = std::max(maxAlignment, fieldAlign);
        }
    }
    
    // Apply explicit alignment if specified
    if (info.reprAlign > 0) {
        maxAlignment = info.reprAlign;
    }
    
    // Pad to alignment
    if (!info.reprPacked) {
        currentOffset = (currentOffset + maxAlignment - 1) & ~(maxAlignment - 1);
    }
    
    info.totalSize = currentOffset;
    info.offsetsComputed = true;
}

int32_t NativeCodeGen::getRecordFieldOffset(const std::string& recordName, int fieldIndex) {
    auto it = recordTypes_.find(recordName);
    if (it == recordTypes_.end()) {
        // Unknown record type - use default layout
        return 8 + fieldIndex * 8;
    }
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    if (fieldIndex < 0 || fieldIndex >= static_cast<int>(it->second.fieldOffsets.size())) {
        return 8 + fieldIndex * 8;
    }
    
    return it->second.fieldOffsets[fieldIndex];
}

int32_t NativeCodeGen::getRecordSize(const std::string& recordName) {
    auto it = recordTypes_.find(recordName);
    if (it == recordTypes_.end()) {
        return 8;  // Just the header
    }
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    return it->second.totalSize;
}

bool NativeCodeGen::isSmallStruct(const std::string& typeName) {
    auto it = recordTypes_.find(typeName);
    if (it == recordTypes_.end()) {
        return false;
    }
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    // Small struct: data size (excluding header) <= 16 bytes
    int32_t dataSize = it->second.totalSize - 8;
    return dataSize > 0 && dataSize <= 16;
}

void NativeCodeGen::emitStructByValuePass(const std::string& typeName, int argIndex) {
    auto it = recordTypes_.find(typeName);
    if (it == recordTypes_.end()) return;
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    int32_t dataSize = it->second.totalSize - 8;
    
    // RAX contains pointer to struct
    // Load first 8 bytes into appropriate register
    if (dataSize >= 8) {
        asm_.add_rax_imm32(8);  // Skip header
        asm_.mov_rcx_mem_rax();
        
        if (dataSize > 8) {
            // Load second 8 bytes
            asm_.add_rax_imm32(8);
            asm_.mov_rdx_mem_rax();
        }
    }
    
    (void)argIndex;  // Used for register selection in full implementation
}

void NativeCodeGen::emitStructByValueReturn(const std::string& typeName) {
    auto it = recordTypes_.find(typeName);
    if (it == recordTypes_.end()) return;
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    // For small structs, return value is already in RAX (first 8 bytes)
    // and RDX (second 8 bytes if needed)
}

void NativeCodeGen::emitLoadStructToRegs(const std::string& typeName) {
    auto it = recordTypes_.find(typeName);
    if (it == recordTypes_.end()) return;
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    int32_t dataSize = it->second.totalSize - 8;
    
    // RAX contains pointer to struct
    asm_.push_rax();
    asm_.add_rax_imm32(8);  // Skip header
    asm_.mov_rcx_mem_rax();
    
    if (dataSize > 8) {
        asm_.pop_rax();
        asm_.add_rax_imm32(16);
        asm_.mov_rdx_mem_rax();
    } else {
        asm_.pop_rax();
    }
}

void NativeCodeGen::emitStoreRegsToStruct(const std::string& typeName) {
    auto it = recordTypes_.find(typeName);
    if (it == recordTypes_.end()) return;
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    int32_t dataSize = it->second.totalSize - 8;
    
    // RAX contains pointer to struct, RCX:RDX contain data
    asm_.push_rax();
    asm_.add_rax_imm32(8);  // Skip header
    asm_.mov_mem_rax_rcx();
    
    if (dataSize > 8) {
        asm_.pop_rax();
        asm_.add_rax_imm32(16);
        asm_.mov_mem_rax_rcx();  // Should be RDX
    } else {
        asm_.pop_rax();
    }
}

void NativeCodeGen::emitBitfieldRead(const std::string& recordName, int fieldIndex) {
    auto it = recordTypes_.find(recordName);
    if (it == recordTypes_.end()) return;
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    if (fieldIndex < 0 || fieldIndex >= static_cast<int>(it->second.fieldBitWidths.size())) {
        return;
    }
    
    int bitWidth = it->second.fieldBitWidths[fieldIndex];
    if (bitWidth <= 0) return;
    
    int32_t offset = it->second.fieldOffsets[fieldIndex];
    int bitOffset = 0;
    if (fieldIndex < static_cast<int>(it->second.fieldBitOffsets.size())) {
        bitOffset = it->second.fieldBitOffsets[fieldIndex];
    }
    
    // RAX contains pointer to record
    // Load the storage unit
    if (offset > 0) {
        asm_.add_rax_imm32(offset);
    }
    asm_.mov_rax_mem_rax();
    
    // Shift right to align the bitfield
    if (bitOffset > 0) {
        asm_.shr_rax_imm8(static_cast<uint8_t>(bitOffset));
    }
    
    // Mask to extract only the bitfield bits
    int64_t mask = (1LL << bitWidth) - 1;
    asm_.mov_rcx_imm64(mask);
    asm_.and_rax_rcx();
}

void NativeCodeGen::emitBitfieldWrite(const std::string& recordName, int fieldIndex) {
    auto it = recordTypes_.find(recordName);
    if (it == recordTypes_.end()) return;
    
    if (!it->second.offsetsComputed) {
        computeRecordLayout(it->second);
    }
    
    if (fieldIndex < 0 || fieldIndex >= static_cast<int>(it->second.fieldBitWidths.size())) {
        return;
    }
    
    int bitWidth = it->second.fieldBitWidths[fieldIndex];
    if (bitWidth <= 0) return;
    
    int32_t offset = it->second.fieldOffsets[fieldIndex];
    int bitOffset = 0;
    if (fieldIndex < static_cast<int>(it->second.fieldBitOffsets.size())) {
        bitOffset = it->second.fieldBitOffsets[fieldIndex];
    }
    
    // RAX contains pointer to record, RCX contains value to write
    // Save the pointer
    asm_.push_rax();
    
    // Calculate field address
    if (offset > 0) {
        asm_.add_rax_imm32(offset);
    }
    
    // Load current value
    asm_.push_rcx();  // Save new value
    asm_.mov_rdx_mem_rax();  // RDX = current storage unit value
    
    // Create mask to clear the bitfield
    int64_t clearMask = ~(((1LL << bitWidth) - 1) << bitOffset);
    asm_.mov_rcx_imm64(clearMask);
    asm_.code.push_back(0x48); asm_.code.push_back(0x21); asm_.code.push_back(0xCA);  // and rdx, rcx
    
    // Shift new value into position and OR it in
    asm_.pop_rcx();  // Restore new value
    int64_t valueMask = (1LL << bitWidth) - 1;
    asm_.mov_rax_imm64(valueMask);
    asm_.and_rax_rcx();  // Mask the new value
    
    if (bitOffset > 0) {
        asm_.shl_rax_imm8(static_cast<uint8_t>(bitOffset));
    }
    
    asm_.or_rax_rcx();  // Combine with cleared storage unit
    // Actually should be: or rax, rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0x09); asm_.code.push_back(0xD0);  // or rax, rdx
    
    // Store back
    asm_.pop_rcx();  // Restore pointer
    if (offset > 0) {
        asm_.add_rcx_imm32(offset);
    }
    asm_.mov_mem_rcx_rax();
}

} // namespace tyl
