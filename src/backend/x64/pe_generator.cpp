// Tyl Compiler - PE Generator Implementation
#include "pe_generator.h"
#include <cstring>
#include <iostream>

namespace tyl {

void PEGenerator::addCode(const std::vector<uint8_t>& code) {
    codeSection.insert(codeSection.end(), code.begin(), code.end());
}

void PEGenerator::addCodeWithFixups(const std::vector<uint8_t>& code,
                                     const std::vector<std::pair<size_t, uint32_t>>& ripFixups) {
    size_t baseOffset = codeSection.size();
    codeSection.insert(codeSection.end(), code.begin(), code.end());
    
    // Record fixups with their types
    for (const auto& [offset, targetRVA] : ripFixups) {
        CodeFixup fixup;
        fixup.offset = baseOffset + offset;
        fixup.targetRVA = targetRVA;
        
        // Determine fixup type based on placeholder range
        if (targetRVA >= IDATA_RVA_PLACEHOLDER) {
            fixup.type = FixupType::IDATA;
        } else if (targetRVA >= DATA_RVA_PLACEHOLDER) {
            fixup.type = FixupType::DATA;
        } else {
            // Code-relative, no adjustment needed
            continue;
        }
        codeFixups.push_back(fixup);
    }
}

uint32_t PEGenerator::addData(const void* data, size_t size) {
    // Return offset relative to DATA_RVA_PLACEHOLDER - will be fixed up later
    uint32_t offset = (uint32_t)dataSection.size();
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    dataSection.insert(dataSection.end(), bytes, bytes + size);
    while (dataSection.size() % 8 != 0) dataSection.push_back(0);
    return DATA_RVA_PLACEHOLDER + offset;
}

uint32_t PEGenerator::addString(const std::string& str) {
    uint32_t offset = (uint32_t)dataSection.size();
    for (char c : str) dataSection.push_back(static_cast<uint8_t>(c));
    dataSection.push_back(0);
    while (dataSection.size() % 8 != 0) dataSection.push_back(0);
    return DATA_RVA_PLACEHOLDER + offset;
}

uint32_t PEGenerator::addQword(uint64_t value) {
    uint32_t offset = (uint32_t)dataSection.size();
    for (int i = 0; i < 8; i++) dataSection.push_back((value >> (i * 8)) & 0xFF);
    return DATA_RVA_PLACEHOLDER + offset;
}

void PEGenerator::addImport(const std::string& dll, const std::string& function) {
    // Check if this function is already imported from this DLL
    auto& funcs = imports[dll];
    for (const auto& f : funcs) {
        if (f == function) return;  // Already imported, skip duplicate
    }
    funcs.push_back(function);
}

void PEGenerator::finalizeImports() {
    if (imports.empty()) { importsFinalized = true; return; }
    size_t numDlls = imports.size();
    size_t totalFuncs = 0;
    for (auto& [dll, funcs] : imports) totalFuncs += funcs.size();
    uint32_t idtSize = (uint32_t)((numDlls + 1) * 20);
    uint32_t iltSize = (uint32_t)((totalFuncs + numDlls) * 8);
    // Store offsets relative to IDATA_RVA_PLACEHOLDER
    uint32_t iatStart = idtSize + iltSize;
    uint32_t currentIAT = iatStart;
    for (auto& [dll, funcs] : imports) {
        for (auto& func : funcs) { 
            importRVAs[func] = IDATA_RVA_PLACEHOLDER + currentIAT; 
            currentIAT += 8; 
        }
        currentIAT += 8;
    }
    importsFinalized = true;
}

uint32_t PEGenerator::getImportRVA(const std::string& function) {
    auto it = importRVAs.find(function);
    return (it != importRVAs.end()) ? it->second : 0;
}

uint32_t PEGenerator::getActualDataRVA() const { return actualDataRVA_; }
uint32_t PEGenerator::getActualIdataRVA() const { return actualIdataRVA_; }

void PEGenerator::calculateActualRVAs() {
    const uint32_t SECT_ALIGN = 0x1000;
    // Code section starts at CODE_RVA (0x1000)
    // Calculate code virtual size (aligned)
    uint32_t codeVirtSize = ((uint32_t)codeSection.size() + SECT_ALIGN - 1) & ~(SECT_ALIGN - 1);
    if (codeVirtSize == 0) codeVirtSize = SECT_ALIGN;
    
    // Data section follows code
    actualDataRVA_ = CODE_RVA + codeVirtSize;
    
    // Calculate data virtual size
    uint32_t dataVirtSize = ((uint32_t)dataSection.size() + SECT_ALIGN - 1) & ~(SECT_ALIGN - 1);
    if (dataVirtSize == 0 && !dataSection.empty()) dataVirtSize = SECT_ALIGN;
    
    // Idata section follows data
    actualIdataRVA_ = actualDataRVA_ + dataVirtSize;
}

void PEGenerator::buildImportSection() {
    if (imports.empty()) return;
    idataSection.clear();
    size_t numDlls = imports.size();
    size_t totalFuncs = 0;
    for (auto& [dll, funcs] : imports) totalFuncs += funcs.size();
    uint32_t idtSize = (uint32_t)((numDlls + 1) * 20);
    uint32_t iltSize = (uint32_t)((totalFuncs + numDlls) * 8);
    uint32_t iatSize = iltSize;
    uint32_t hintNameStart = idtSize + iltSize + iatSize;
    uint32_t currentOffset = hintNameStart;
    for (auto& [dll, funcs] : imports) {
        for (auto& func : funcs) {
            currentOffset += 2 + (uint32_t)func.size() + 1;
            if (currentOffset % 2) currentOffset++;
        }
        currentOffset += (uint32_t)dll.size() + 1;
        if (currentOffset % 2) currentOffset++;
    }
    idataSection.resize(currentOffset, 0);
    uint32_t iltOffset = idtSize;
    uint32_t iatOffset = idtSize + iltSize;
    uint32_t hintNameOffset = hintNameStart;
    uint32_t idtEntry = 0;
    uint32_t currentILT = iltOffset;
    uint32_t currentIAT = iatOffset;
    for (auto& [dll, funcs] : imports) {
        uint32_t iltRVA = actualIdataRVA_ + currentILT;
        uint32_t iatRVA = actualIdataRVA_ + currentIAT;
        memcpy(&idataSection[idtEntry], &iltRVA, 4);
        uint32_t dllNameOffset = hintNameOffset;
        for (auto& func : funcs) {
            dllNameOffset += 2 + (uint32_t)func.size() + 1;
            if (dllNameOffset % 2) dllNameOffset++;
        }
        uint32_t dllNameRVA = actualIdataRVA_ + dllNameOffset;
        memcpy(&idataSection[idtEntry + 12], &dllNameRVA, 4);
        memcpy(&idataSection[idtEntry + 16], &iatRVA, 4);
        for (size_t i = 0; i < funcs.size(); i++) {
            uint64_t hintNameRVA = actualIdataRVA_ + hintNameOffset;
            memcpy(&idataSection[currentILT], &hintNameRVA, 8);
            memcpy(&idataSection[currentIAT], &hintNameRVA, 8);
            idataSection[hintNameOffset] = 0;
            idataSection[hintNameOffset + 1] = 0;
            memcpy(&idataSection[hintNameOffset + 2], funcs[i].c_str(), funcs[i].size() + 1);
            hintNameOffset += 2 + (uint32_t)funcs[i].size() + 1;
            if (hintNameOffset % 2) hintNameOffset++;
            currentILT += 8;
            currentIAT += 8;
        }
        currentILT += 8;
        currentIAT += 8;
        memcpy(&idataSection[hintNameOffset], dll.c_str(), dll.size() + 1);
        hintNameOffset += (uint32_t)dll.size() + 1;
        if (hintNameOffset % 2) hintNameOffset++;
        idtEntry += 20;
    }
}

void PEGenerator::applyFixups() {
    int32_t dataAdjust = (int32_t)actualDataRVA_ - (int32_t)DATA_RVA_PLACEHOLDER;
    int32_t idataAdjust = (int32_t)actualIdataRVA_ - (int32_t)IDATA_RVA_PLACEHOLDER;
    
    for (const auto& fixup : codeFixups) {
        if (fixup.offset + 4 > codeSection.size()) continue;
        
        int32_t val;
        memcpy(&val, &codeSection[fixup.offset], 4);
        
        int32_t adjustment = (fixup.type == FixupType::DATA) ? dataAdjust : idataAdjust;
        int32_t newVal = val + adjustment;
        
        memcpy(&codeSection[fixup.offset], &newVal, 4);
    }
}

void PEGenerator::addVtableFixup(uint32_t dataRVA, const std::string& label) {
    VtableFixup fixup;
    // Convert from placeholder RVA to actual data section offset
    fixup.dataOffset = dataRVA - DATA_RVA_PLACEHOLDER;
    fixup.label = label;
    vtableFixups.push_back(fixup);
}

void PEGenerator::setLabelOffsets(const std::map<std::string, size_t>& labels) {
    labelOffsets_ = labels;
}

void PEGenerator::applyVtableFixups() {
    // Apply vtable fixups - write function addresses into data section
    for (const auto& fixup : vtableFixups) {
        auto it = labelOffsets_.find(fixup.label);
        if (it == labelOffsets_.end()) continue;
        
        // Calculate absolute address: IMAGE_BASE + CODE_RVA + label_offset
        uint64_t funcAddr = IMAGE_BASE + CODE_RVA + it->second;
        
        // Write to data section at the specified offset
        if (fixup.dataOffset + 8 <= dataSection.size()) {
            memcpy(&dataSection[fixup.dataOffset], &funcAddr, 8);
        }
    }
}

bool PEGenerator::write(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    
    // Calculate actual RVAs based on code size
    calculateActualRVAs();
    
    // Apply fixups using tracked locations only (no blind scanning)
    applyFixups();
    
    // Apply vtable fixups (write function addresses into data section)
    applyVtableFixups();
    
    buildImportSection();
    
    const uint32_t FILE_ALIGN = 0x200;
    const uint32_t SECT_ALIGN = 0x1000;
    uint32_t codeRawSize = ((uint32_t)codeSection.size() + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1);
    uint32_t dataRawSize = dataSection.empty() ? 0 : (((uint32_t)dataSection.size() + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1));
    uint32_t idataRawSize = idataSection.empty() ? 0 : (((uint32_t)idataSection.size() + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1));
    uint32_t numSections = 1 + (dataSection.empty() ? 0 : 1) + (idataSection.empty() ? 0 : 1);
    uint32_t headersSize = 0x200;
    
    uint32_t idataVirtSize = ((uint32_t)idataSection.size() + SECT_ALIGN - 1) & ~(SECT_ALIGN - 1);
    if (idataVirtSize == 0 && !idataSection.empty()) idataVirtSize = SECT_ALIGN;
    uint32_t imageSize = actualIdataRVA_ + idataVirtSize + SECT_ALIGN;
    
    auto write8 = [&](uint8_t v) { file.write((char*)&v, 1); };
    auto write16 = [&](uint16_t v) { file.write((char*)&v, 2); };
    auto write32 = [&](uint32_t v) { file.write((char*)&v, 4); };
    auto write64 = [&](uint64_t v) { file.write((char*)&v, 8); };
    auto writeBytes = [&](const void* d, size_t s) { file.write((const char*)d, s); };
    auto pad = [&](size_t align) { size_t pos = file.tellp(); size_t p = (align - (pos % align)) % align; for (size_t i = 0; i < p; i++) write8(0); };
    
    // DOS Header
    write16(0x5A4D); write16(0x90); write16(0x03); write16(0x00); write16(0x04); write16(0x00);
    write16(0xFFFF); write16(0x00); write16(0xB8); write16(0x00); write16(0x00); write16(0x00);
    write16(0x40); write16(0x00);
    for (int i = 0; i < 4; i++) write16(0);
    write16(0x00); write16(0x00);
    for (int i = 0; i < 10; i++) write16(0);
    write32(0x80);
    for (int i = 0; i < 16; i++) write32(0);
    write32(0x00004550);
    write16(0x8664); write16((uint16_t)numSections); write32(0); write32(0); write32(0); write16(240); write16(0x0022);
    write16(0x020B); write8(14); write8(0); write32(codeRawSize); write32(dataRawSize + idataRawSize); write32(0);
    write32(CODE_RVA); write32(CODE_RVA); write64(IMAGE_BASE); write32(SECT_ALIGN); write32(FILE_ALIGN);
    write16(6); write16(0); write16(0); write16(0); write16(6); write16(0); write32(0);
    write32(imageSize); write32(headersSize); write32(0); write16(3); write16(0x8160);
    write64(0x100000); write64(0x1000); write64(0x100000); write64(0x1000); write32(0); write32(16);
    for (int i = 0; i < 16; i++) {
        if (i == 1 && !idataSection.empty()) { write32(actualIdataRVA_); write32((uint32_t)idataSection.size()); }
        else { write32(0); write32(0); }
    }
    uint32_t fileOff = headersSize;
    writeBytes(".text\0\0\0", 8); write32((uint32_t)codeSection.size()); write32(CODE_RVA); write32(codeRawSize);
    write32(fileOff); write32(0); write32(0); write16(0); write16(0); write32(0x60000020); fileOff += codeRawSize;
    if (!dataSection.empty()) {
        writeBytes(".data\0\0\0", 8); write32((uint32_t)dataSection.size()); write32(actualDataRVA_); write32(dataRawSize);
        write32(fileOff); write32(0); write32(0); write16(0); write16(0); write32(0xC0000040); fileOff += dataRawSize;
    }
    if (!idataSection.empty()) {
        writeBytes(".idata\0\0", 8); write32((uint32_t)idataSection.size()); write32(actualIdataRVA_); write32(idataRawSize);
        write32(fileOff); write32(0); write32(0); write16(0); write16(0); write32(0xC0000040);
    }
    pad(FILE_ALIGN); writeBytes(codeSection.data(), codeSection.size()); pad(FILE_ALIGN);
    if (!dataSection.empty()) { writeBytes(dataSection.data(), dataSection.size()); pad(FILE_ALIGN); }
    if (!idataSection.empty()) { writeBytes(idataSection.data(), idataSection.size()); pad(FILE_ALIGN); }
    file.close();
    return true;
}

} // namespace tyl
