// Tyl Compiler - Linker Output Generation

#include "linker_base.h"
#include <iostream>
#include <fstream>

namespace tyl {

bool Linker::generateExecutable() {
    if (config_.verbose) {
        std::cout << "Phase 5: Generating executable...\n";
    }
    
    PEGenerator pe;
    
    for (auto& [dll, funcs] : collectedImports_) {
        for (auto& func : funcs) {
            pe.addImport(dll, func);
        }
    }
    pe.finalizeImports();
    
    std::ofstream file(config_.outputFile, std::ios::binary);
    if (!file) {
        error("Cannot create output file: " + config_.outputFile);
        return false;
    }
    
    const uint32_t FILE_ALIGN = config_.fileAlignment;
    const uint32_t SECT_ALIGN = config_.sectionAlignment;
    
    uint32_t codeRawSize = alignUp((uint32_t)mergedCode_.size(), FILE_ALIGN);
    uint32_t dataRawSize = mergedData_.empty() ? 0 : alignUp((uint32_t)mergedData_.size(), FILE_ALIGN);
    uint32_t rodataRawSize = mergedRodata_.empty() ? 0 : alignUp((uint32_t)mergedRodata_.size(), FILE_ALIGN);
    
    std::vector<uint8_t> idataSection;
    buildImportSection(idataSection, collectedImports_, idataRVA_);
    uint32_t idataRawSize = idataSection.empty() ? 0 : alignUp((uint32_t)idataSection.size(), FILE_ALIGN);
    
    uint32_t numSections = 1;
    if (!mergedData_.empty()) numSections++;
    if (!mergedRodata_.empty()) numSections++;
    if (!idataSection.empty()) numSections++;
    
    uint32_t headersSize = 0x200;
    uint32_t imageSize = alignUp(idataRVA_ + (uint32_t)idataSection.size(), SECT_ALIGN);
    
    uint32_t entryRVA = codeRVA_;
    auto entryIt = globalSymbols_.find(config_.entryPoint);
    if (entryIt != globalSymbols_.end()) {
        entryRVA = entryIt->second.rva;
    }
    
    auto write8 = [&](uint8_t v) { file.write((char*)&v, 1); };
    auto write16 = [&](uint16_t v) { file.write((char*)&v, 2); };
    auto write32 = [&](uint32_t v) { file.write((char*)&v, 4); };
    auto write64 = [&](uint64_t v) { file.write((char*)&v, 8); };
    auto writeBytes = [&](const void* d, size_t s) { file.write((const char*)d, s); };
    auto pad = [&](size_t align) {
        size_t pos = file.tellp();
        size_t p = (align - (pos % align)) % align;
        for (size_t i = 0; i < p; i++) write8(0);
    };
    
    // DOS Header
    write16(0x5A4D);
    write16(0x90); write16(0x03); write16(0x00); write16(0x04);
    write16(0x00); write16(0xFFFF); write16(0x00); write16(0xB8);
    write16(0x00); write16(0x00); write16(0x00); write16(0x40); write16(0x00);
    for (int i = 0; i < 4; i++) write16(0);
    write16(0x00); write16(0x00);
    for (int i = 0; i < 10; i++) write16(0);
    write32(0x80);
    for (int i = 0; i < 16; i++) write32(0);
    
    write32(0x00004550);
    
    write16(0x8664);
    write16((uint16_t)numSections);
    write32(0); write32(0); write32(0);
    write16(240);
    write16(0x0022);
    
    write16(0x020B);
    write8(14); write8(0);
    write32(codeRawSize);
    write32(dataRawSize + rodataRawSize + idataRawSize);
    write32(0);
    write32(entryRVA);
    write32(codeRVA_);
    write64(config_.imageBase);
    write32(SECT_ALIGN);
    write32(FILE_ALIGN);
    write16(6); write16(0);
    write16(0); write16(0);
    write16(6); write16(0);
    write32(0);
    write32(imageSize);
    write32(headersSize);
    write32(0);
    write16(3);
    write16(0x8160);
    write64(0x100000); write64(0x1000);
    write64(0x100000); write64(0x1000);
    write32(0);
    write32(16);
    
    for (int i = 0; i < 16; i++) {
        if (i == 1 && !idataSection.empty()) {
            write32(idataRVA_);
            write32((uint32_t)idataSection.size());
        } else {
            write32(0); write32(0);
        }
    }
    
    uint32_t fileOff = headersSize;
    
    writeBytes(".text\0\0\0", 8);
    write32((uint32_t)mergedCode_.size());
    write32(codeRVA_);
    write32(codeRawSize);
    write32(fileOff);
    write32(0); write32(0); write16(0); write16(0);
    write32(0x60000020);
    fileOff += codeRawSize;
    
    if (!mergedData_.empty()) {
        writeBytes(".data\0\0\0", 8);
        write32((uint32_t)mergedData_.size());
        write32(dataRVA_);
        write32(dataRawSize);
        write32(fileOff);
        write32(0); write32(0); write16(0); write16(0);
        write32(0xC0000040);
        fileOff += dataRawSize;
    }
    
    if (!mergedRodata_.empty()) {
        writeBytes(".rdata\0\0", 8);
        write32((uint32_t)mergedRodata_.size());
        write32(rodataRVA_);
        write32(rodataRawSize);
        write32(fileOff);
        write32(0); write32(0); write16(0); write16(0);
        write32(0x40000040);
        fileOff += rodataRawSize;
    }
    
    if (!idataSection.empty()) {
        writeBytes(".idata\0\0", 8);
        write32((uint32_t)idataSection.size());
        write32(idataRVA_);
        write32(idataRawSize);
        write32(fileOff);
        write32(0); write32(0); write16(0); write16(0);
        write32(0xC0000040);
    }
    
    pad(FILE_ALIGN);
    
    writeBytes(mergedCode_.data(), mergedCode_.size());
    pad(FILE_ALIGN);
    
    if (!mergedData_.empty()) {
        writeBytes(mergedData_.data(), mergedData_.size());
        pad(FILE_ALIGN);
    }
    
    if (!mergedRodata_.empty()) {
        writeBytes(mergedRodata_.data(), mergedRodata_.size());
        pad(FILE_ALIGN);
    }
    
    if (!idataSection.empty()) {
        writeBytes(idataSection.data(), idataSection.size());
        pad(FILE_ALIGN);
    }
    
    file.close();
    
    if (config_.generateMap) {
        generateMapFile();
    }
    
    return true;
}

void Linker::buildImportSection(std::vector<uint8_t>& section,
                                const std::map<std::string, std::set<std::string>>& imports,
                                uint32_t baseRVA) {
    if (imports.empty()) return;
    
    size_t numDlls = imports.size();
    size_t totalFuncs = 0;
    for (auto& [dll, funcs] : imports) {
        totalFuncs += funcs.size();
    }
    
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
    
    section.resize(currentOffset, 0);
    
    uint32_t iltOffset = idtSize;
    uint32_t iatOffset = idtSize + iltSize;
    uint32_t hintNameOffset = hintNameStart;
    
    uint32_t idtEntry = 0;
    uint32_t currentILT = iltOffset;
    uint32_t currentIAT = iatOffset;
    
    for (auto& [dll, funcs] : imports) {
        uint32_t iltRVA = baseRVA + currentILT;
        uint32_t iatRVA = baseRVA + currentIAT;
        
        memcpy(&section[idtEntry], &iltRVA, 4);
        
        uint32_t dllNameOffset = hintNameOffset;
        for (auto& func : funcs) {
            dllNameOffset += 2 + (uint32_t)func.size() + 1;
            if (dllNameOffset % 2) dllNameOffset++;
        }
        uint32_t dllNameRVA = baseRVA + dllNameOffset;
        memcpy(&section[idtEntry + 12], &dllNameRVA, 4);
        memcpy(&section[idtEntry + 16], &iatRVA, 4);
        
        for (auto& func : funcs) {
            uint64_t hintNameRVA = baseRVA + hintNameOffset;
            memcpy(&section[currentILT], &hintNameRVA, 8);
            memcpy(&section[currentIAT], &hintNameRVA, 8);
            
            section[hintNameOffset] = 0;
            section[hintNameOffset + 1] = 0;
            memcpy(&section[hintNameOffset + 2], func.c_str(), func.size() + 1);
            hintNameOffset += 2 + (uint32_t)func.size() + 1;
            if (hintNameOffset % 2) hintNameOffset++;
            
            currentILT += 8;
            currentIAT += 8;
        }
        
        currentILT += 8;
        currentIAT += 8;
        
        memcpy(&section[hintNameOffset], dll.c_str(), dll.size() + 1);
        hintNameOffset += (uint32_t)dll.size() + 1;
        if (hintNameOffset % 2) hintNameOffset++;
        
        idtEntry += 20;
    }
}

void Linker::generateMapFile() {
    std::string mapPath = config_.mapFile.empty() ? 
        config_.outputFile.substr(0, config_.outputFile.rfind('.')) + ".map" : config_.mapFile;
    
    std::ofstream map(mapPath);
    if (!map) return;
    
    map << "Flex Linker Map File\n";
    map << "Output: " << config_.outputFile << "\n\n";
    
    map << "Sections:\n";
    map << "  .text   RVA: 0x" << std::hex << codeRVA_ << " Size: " << std::dec << mergedCode_.size() << "\n";
    map << "  .data   RVA: 0x" << std::hex << dataRVA_ << " Size: " << std::dec << mergedData_.size() << "\n";
    map << "  .rdata  RVA: 0x" << std::hex << rodataRVA_ << " Size: " << std::dec << mergedRodata_.size() << "\n";
    map << "  .idata  RVA: 0x" << std::hex << idataRVA_ << "\n\n";
    
    map << "Symbols:\n";
    for (auto& [name, sym] : globalSymbols_) {
        map << "  0x" << std::hex << sym.rva << " " << name << " (" << sym.sourceModule << ")\n";
    }
    
    map << "\nImports:\n";
    for (auto& [dll, funcs] : collectedImports_) {
        map << "  " << dll << ":\n";
        for (auto& func : funcs) {
            auto it = importSymbols_.find(func);
            if (it != importSymbols_.end()) {
                map << "    0x" << std::hex << it->second << " " << func << "\n";
            }
        }
    }
    
    map.close();
}

} // namespace tyl
