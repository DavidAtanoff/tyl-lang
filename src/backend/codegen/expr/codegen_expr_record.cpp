// Tyl Compiler - Native Code Generator Record/Map Expressions
// Handles: RecordExpr, MapExpr, MemberExpr

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(RecordExpr& node) {
    if (node.fields.empty()) {
        asm_.xor_rax_rax();
        return;
    }
    
    size_t fieldCount = node.fields.size();
    
    // Get or assign type ID for RTTI
    uint64_t typeId = 0;
    if (!node.typeName.empty()) {
        auto typeIdIt = typeIds_.find(node.typeName);
        if (typeIdIt != typeIds_.end()) {
            typeId = typeIdIt->second;
        } else {
            typeId = nextTypeId_++;
            typeIds_[node.typeName] = typeId;
        }
    }
    
    // Check if this is a named record type with known layout
    if (!node.typeName.empty()) {
        auto typeIt = recordTypes_.find(node.typeName);
        if (typeIt != recordTypes_.end()) {
            if (!typeIt->second.offsetsComputed) {
                computeRecordLayout(typeIt->second);
            }
            
            size_t recordSize = static_cast<size_t>(typeIt->second.totalSize);
            emitGCAllocRaw(recordSize);
            
            allocLocal("$record_ptr");
            asm_.mov_mem_rbp_rax(locals["$record_ptr"]);
            
            // Store type ID at offset 0 for RTTI (for raw allocated records)
            // Note: For raw records, we store typeId at the start
            asm_.mov_rcx_imm64(static_cast<int64_t>(typeId));
            asm_.mov_rax_mem_rbp(locals["$record_ptr"]);
            asm_.mov_mem_rax_rcx();  // [rax] = typeId
            
            for (size_t i = 0; i < node.fields.size(); i++) {
                int fieldIndex = -1;
                for (size_t j = 0; j < typeIt->second.fieldNames.size(); j++) {
                    if (typeIt->second.fieldNames[j] == node.fields[i].first) {
                        fieldIndex = static_cast<int>(j);
                        break;
                    }
                }
                
                if (fieldIndex >= 0) {
                    node.fields[i].second->accept(*this);
                    
                    asm_.mov_rcx_mem_rbp(locals["$record_ptr"]);
                    
                    int32_t offset = typeIt->second.fieldOffsets[fieldIndex];
                    if (offset > 0) {
                        asm_.add_rcx_imm32(offset);
                    }
                    
                    int32_t fieldSize = getTypeSize(typeIt->second.fieldTypes[fieldIndex]);
                    if (fieldSize == 1) {
                        asm_.code.push_back(0x88);
                        asm_.code.push_back(0x01);
                    } else if (fieldSize == 2) {
                        asm_.code.push_back(0x66);
                        asm_.code.push_back(0x89);
                        asm_.code.push_back(0x01);
                    } else if (fieldSize == 4) {
                        asm_.code.push_back(0x89);
                        asm_.code.push_back(0x01);
                    } else {
                        asm_.mov_mem_rcx_rax();
                    }
                }
            }
            
            asm_.mov_rax_mem_rbp(locals["$record_ptr"]);
            return;
        }
    }
    
    // Anonymous record - use GC allocation with type ID
    emitGCAllocRecord(fieldCount, typeId);
    
    allocLocal("$record_ptr");
    asm_.mov_mem_rbp_rax(locals["$record_ptr"]);
    
    for (size_t i = 0; i < node.fields.size(); i++) {
        node.fields[i].second->accept(*this);
        
        asm_.mov_rcx_mem_rbp(locals["$record_ptr"]);
        
        // Fields start at offset 16 now (after fieldCount and typeId)
        int32_t offset = 16 + static_cast<int32_t>(i * 8);
        if (offset > 0) {
            asm_.add_rcx_imm32(offset);
        }
        asm_.mov_mem_rcx_rax();
    }
    
    asm_.mov_rax_mem_rbp(locals["$record_ptr"]);
}

void NativeCodeGen::visit(MapExpr& node) {
    if (node.entries.empty()) {
        emitGCAllocMap(16);
        return;
    }
    
    size_t capacity = 16;
    while (capacity < node.entries.size() * 2) capacity *= 2;
    
    emitGCAllocMap(capacity);
    
    allocLocal("$map_ptr");
    asm_.mov_mem_rbp_rax(locals["$map_ptr"]);
    
    asm_.mov_rcx_imm64(static_cast<int64_t>(node.entries.size()));
    asm_.mov_rax_mem_rbp(locals["$map_ptr"]);
    asm_.add_rax_imm32(8);
    asm_.mov_mem_rax_rcx();
    
    for (size_t i = 0; i < node.entries.size(); i++) {
        auto* keyStr = dynamic_cast<StringLiteral*>(node.entries[i].first.get());
        if (!keyStr) continue;
        
        uint32_t keyRva = addString(keyStr->value);
        
        uint64_t hash = 5381;
        for (char c : keyStr->value) {
            hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
        }
        
        emitGCAllocMapEntry();
        
        allocLocal("$entry_ptr");
        asm_.mov_mem_rbp_rax(locals["$entry_ptr"]);
        
        asm_.mov_rcx_imm64(static_cast<int64_t>(hash));
        asm_.mov_mem_rax_rcx();
        
        asm_.mov_rcx_mem_rbp(locals["$entry_ptr"]);
        asm_.add_rcx_imm32(8);
        asm_.lea_rax_rip_fixup(keyRva);
        asm_.mov_mem_rcx_rax();
        
        node.entries[i].second->accept(*this);
        asm_.mov_rcx_mem_rbp(locals["$entry_ptr"]);
        asm_.add_rcx_imm32(16);
        asm_.mov_mem_rcx_rax();
        
        asm_.mov_rcx_mem_rbp(locals["$entry_ptr"]);
        asm_.add_rcx_imm32(24);
        asm_.xor_rax_rax();
        asm_.mov_mem_rcx_rax();
        
        size_t bucketIdx = hash % capacity;
        
        asm_.mov_rax_mem_rbp(locals["$map_ptr"]);
        asm_.add_rax_imm32(16 + static_cast<int32_t>(bucketIdx * 8));
        
        asm_.mov_rcx_mem_rax();
        
        asm_.push_rax();
        asm_.mov_rax_mem_rbp(locals["$entry_ptr"]);
        asm_.add_rax_imm32(24);
        asm_.mov_mem_rax_rcx();
        
        asm_.pop_rax();
        asm_.mov_rcx_mem_rbp(locals["$entry_ptr"]);
        asm_.mov_mem_rax_rcx();
    }
    
    asm_.mov_rax_mem_rbp(locals["$map_ptr"]);
}

void NativeCodeGen::visit(MemberExpr& node) {
    // Check if this is an enum member access
    if (auto* id = dynamic_cast<Identifier*>(node.object.get())) {
        std::string qualifiedName = id->name + "." + node.member;
        auto it = constVars.find(qualifiedName);
        if (it != constVars.end()) {
            asm_.mov_rax_imm64(it->second);
            lastExprWasFloat_ = false;
            return;
        }
        
        // Check if this is a record field access
        auto varTypeIt = varRecordTypes_.find(id->name);
        if (varTypeIt != varRecordTypes_.end()) {
            auto typeIt = recordTypes_.find(varTypeIt->second);
            if (typeIt != recordTypes_.end()) {
                int fieldIndex = -1;
                for (size_t i = 0; i < typeIt->second.fieldNames.size(); i++) {
                    if (typeIt->second.fieldNames[i] == node.member) {
                        fieldIndex = static_cast<int>(i);
                        break;
                    }
                }
                
                if (fieldIndex >= 0) {
                    node.object->accept(*this);
                    
                    bool isBitfield = false;
                    int bitWidth = 0;
                    if (fieldIndex < static_cast<int>(typeIt->second.fieldBitWidths.size())) {
                        bitWidth = typeIt->second.fieldBitWidths[fieldIndex];
                        isBitfield = (bitWidth > 0);
                    }
                    
                    if (isBitfield) {
                        emitBitfieldRead(varTypeIt->second, fieldIndex);
                        lastExprWasFloat_ = false;
                        return;
                    }
                    
                    int32_t offset = getRecordFieldOffset(varTypeIt->second, fieldIndex);
                    if (offset > 0) {
                        asm_.add_rax_imm32(offset);
                    }
                    
                    const std::string& fieldType = typeIt->second.fieldTypes[fieldIndex];
                    int32_t fieldSize = getTypeSize(fieldType);
                    bool isFloat = isFloatTypeName(fieldType);
                    
                    if (isFloat) {
                        asm_.code.push_back(0xF2);
                        asm_.code.push_back(0x0F);
                        asm_.code.push_back(0x10);
                        asm_.code.push_back(0x00);
                        asm_.code.push_back(0x66);
                        asm_.code.push_back(0x48);
                        asm_.code.push_back(0x0F);
                        asm_.code.push_back(0x7E);
                        asm_.code.push_back(0xC0);
                    } else if (fieldSize == 1) {
                        asm_.code.push_back(0x48);
                        asm_.code.push_back(0x0F);
                        asm_.code.push_back(0xB6);
                        asm_.code.push_back(0x00);
                    } else if (fieldSize == 2) {
                        asm_.code.push_back(0x48);
                        asm_.code.push_back(0x0F);
                        asm_.code.push_back(0xB7);
                        asm_.code.push_back(0x00);
                    } else if (fieldSize == 4) {
                        asm_.code.push_back(0x8B);
                        asm_.code.push_back(0x00);
                    } else {
                        asm_.mov_rax_mem_rax();
                    }
                    
                    lastExprWasFloat_ = isFloat;
                    return;
                }
            }
        }
    }
    
    // Check for trait method
    if (auto* id = dynamic_cast<Identifier*>(node.object.get())) {
        for (const auto& [implKey, info] : impls_) {
            if (info.typeName == id->name || implKey.find(":" + id->name) != std::string::npos) {
                auto methodIt = info.methodLabels.find(node.member);
                if (methodIt != info.methodLabels.end()) {
                    node.object->accept(*this);
                    return;
                }
            }
        }
    }
    
    // Default: evaluate object and try field access
    node.object->accept(*this);
    
    // Try to find field in known record types
    for (const auto& [typeName, typeInfo] : recordTypes_) {
        for (size_t i = 0; i < typeInfo.fieldNames.size(); i++) {
            if (typeInfo.fieldNames[i] == node.member) {
                if (!typeInfo.offsetsComputed) {
                    computeRecordLayout(const_cast<RecordTypeInfo&>(typeInfo));
                }
                
                int32_t offset = typeInfo.fieldOffsets[i];
                if (offset > 0) {
                    asm_.add_rax_imm32(offset);
                }
                
                const std::string& fieldType = typeInfo.fieldTypes[i];
                int32_t fieldSize = getTypeSize(fieldType);
                bool isFloat = isFloatTypeName(fieldType);
                
                if (isFloat) {
                    asm_.code.push_back(0xF2);
                    asm_.code.push_back(0x0F);
                    asm_.code.push_back(0x10);
                    asm_.code.push_back(0x00);
                    asm_.code.push_back(0x66);
                    asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F);
                    asm_.code.push_back(0x7E);
                    asm_.code.push_back(0xC0);
                } else if (fieldSize == 1) {
                    asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F);
                    asm_.code.push_back(0xB6);
                    asm_.code.push_back(0x00);
                } else if (fieldSize == 2) {
                    asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F);
                    asm_.code.push_back(0xB7);
                    asm_.code.push_back(0x00);
                } else if (fieldSize == 4) {
                    asm_.code.push_back(0x8B);
                    asm_.code.push_back(0x00);
                } else {
                    asm_.mov_rax_mem_rax();
                }
                
                lastExprWasFloat_ = isFloat;
                return;
            }
        }
    }
}

} // namespace tyl
