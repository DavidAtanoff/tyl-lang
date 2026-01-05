// Tyl Compiler - Object File Implementation
#include "object_file.h"
#include <iostream>
#include <cstring>

namespace tyl {

void ObjectFile::addSymbol(const ObjSymbol& sym) {
    symbolIndex[sym.name] = symbols.size();
    symbols.push_back(sym);
}

ObjSymbol* ObjectFile::findSymbol(const std::string& name) {
    auto it = symbolIndex.find(name);
    return (it != symbolIndex.end()) ? &symbols[it->second] : nullptr;
}

uint32_t ObjectFile::addCode(const std::vector<uint8_t>& code) {
    uint32_t offset = (uint32_t)codeSection.size();
    codeSection.insert(codeSection.end(), code.begin(), code.end());
    return offset;
}

uint32_t ObjectFile::addData(const void* data, size_t size) {
    uint32_t offset = (uint32_t)dataSection.size();
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    dataSection.insert(dataSection.end(), bytes, bytes + size);
    while (dataSection.size() % 8 != 0) dataSection.push_back(0);
    return offset;
}

uint32_t ObjectFile::addRodata(const void* data, size_t size) {
    uint32_t offset = (uint32_t)rodataSection.size();
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    rodataSection.insert(rodataSection.end(), bytes, bytes + size);
    while (rodataSection.size() % 8 != 0) rodataSection.push_back(0);
    return offset;
}

uint32_t ObjectFile::addString(const std::string& str) {
    uint32_t offset = (uint32_t)rodataSection.size();
    for (char c : str) rodataSection.push_back(static_cast<uint8_t>(c));
    rodataSection.push_back(0);
    while (rodataSection.size() % 8 != 0) rodataSection.push_back(0);
    return offset;
}

void ObjectFile::addCodeRelocation(const Relocation& reloc) { codeRelocations.push_back(reloc); }
void ObjectFile::addDataRelocation(const Relocation& reloc) { dataRelocations.push_back(reloc); }
void ObjectFile::addImport(const std::string& dll, const std::string& function) { imports.emplace_back(dll, function); }


bool ObjectFile::write(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<char> stringTable;
    auto addStr = [&](const std::string& s) -> uint32_t {
        uint32_t off = (uint32_t)stringTable.size();
        for (char c : s) stringTable.push_back(c);
        stringTable.push_back(0);
        return off;
    };
    uint32_t moduleNameOff = addStr(moduleName);
    std::vector<uint32_t> symbolNameOffsets;
    for (auto& sym : symbols) symbolNameOffsets.push_back(addStr(sym.name));
    std::vector<uint32_t> codeRelocSymOffsets;
    for (auto& rel : codeRelocations) codeRelocSymOffsets.push_back(addStr(rel.symbol));
    std::vector<uint32_t> dataRelocSymOffsets;
    for (auto& rel : dataRelocations) dataRelocSymOffsets.push_back(addStr(rel.symbol));
    std::vector<std::pair<uint32_t, uint32_t>> importOffsets;
    for (auto& imp : imports) importOffsets.push_back({addStr(imp.dll), addStr(imp.function)});
    ObjectFileHeader header;
    header.magic = TYL_OBJ_MAGIC; header.version = TYL_OBJ_VERSION; header.flags = 0;
    header.codeSize = (uint32_t)codeSection.size(); header.dataSize = (uint32_t)dataSection.size();
    header.rodataSize = (uint32_t)rodataSection.size(); header.symbolCount = (uint32_t)symbols.size();
    header.codeRelocCount = (uint32_t)codeRelocations.size(); header.dataRelocCount = (uint32_t)dataRelocations.size();
    header.importCount = (uint32_t)imports.size(); header.moduleNameOffset = moduleNameOff;
    header.stringTableSize = (uint32_t)stringTable.size();
    file.write(reinterpret_cast<char*>(&header), sizeof(header));
    if (!codeSection.empty()) file.write(reinterpret_cast<char*>(codeSection.data()), codeSection.size());
    if (!dataSection.empty()) file.write(reinterpret_cast<char*>(dataSection.data()), dataSection.size());
    if (!rodataSection.empty()) file.write(reinterpret_cast<char*>(rodataSection.data()), rodataSection.size());
    for (size_t i = 0; i < symbols.size(); i++) {
        auto& sym = symbols[i]; uint32_t nameOff = symbolNameOffsets[i];
        file.write(reinterpret_cast<char*>(&nameOff), 4);
        file.write(reinterpret_cast<char*>(&sym.type), 1);
        // Flags: bit 0 = isExported, bit 1 = isHidden, bit 2 = isWeak
        uint8_t flags = (sym.isExported ? 1 : 0) | (sym.isHidden ? 2 : 0) | (sym.isWeak ? 4 : 0);
        file.write(reinterpret_cast<char*>(&flags), 1);
        uint16_t pad = 0; file.write(reinterpret_cast<char*>(&pad), 2);
        file.write(reinterpret_cast<char*>(&sym.section), 4);
        file.write(reinterpret_cast<char*>(&sym.offset), 4);
        file.write(reinterpret_cast<char*>(&sym.size), 4);
    }
    for (size_t i = 0; i < codeRelocations.size(); i++) {
        auto& rel = codeRelocations[i]; uint32_t symOff = codeRelocSymOffsets[i];
        file.write(reinterpret_cast<char*>(&rel.offset), 4);
        file.write(reinterpret_cast<char*>(&rel.type), 1);
        uint8_t pad[3] = {0, 0, 0}; file.write(reinterpret_cast<char*>(pad), 3);
        file.write(reinterpret_cast<char*>(&symOff), 4);
        file.write(reinterpret_cast<char*>(&rel.addend), 4);
    }
    for (size_t i = 0; i < dataRelocations.size(); i++) {
        auto& rel = dataRelocations[i]; uint32_t symOff = dataRelocSymOffsets[i];
        file.write(reinterpret_cast<char*>(&rel.offset), 4);
        file.write(reinterpret_cast<char*>(&rel.type), 1);
        uint8_t pad[3] = {0, 0, 0}; file.write(reinterpret_cast<char*>(pad), 3);
        file.write(reinterpret_cast<char*>(&symOff), 4);
        file.write(reinterpret_cast<char*>(&rel.addend), 4);
    }
    for (size_t i = 0; i < imports.size(); i++) {
        file.write(reinterpret_cast<const char*>(&importOffsets[i].first), 4);
        file.write(reinterpret_cast<const char*>(&importOffsets[i].second), 4);
    }
    if (!stringTable.empty()) file.write(stringTable.data(), stringTable.size());
    file.close();
    return true;
}


bool ObjectFile::read(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    ObjectFileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.magic != TYL_OBJ_MAGIC || header.version != TYL_OBJ_VERSION) return false;
    codeSection.resize(header.codeSize);
    if (header.codeSize > 0) file.read(reinterpret_cast<char*>(codeSection.data()), header.codeSize);
    dataSection.resize(header.dataSize);
    if (header.dataSize > 0) file.read(reinterpret_cast<char*>(dataSection.data()), header.dataSize);
    rodataSection.resize(header.rodataSize);
    if (header.rodataSize > 0) file.read(reinterpret_cast<char*>(rodataSection.data()), header.rodataSize);
    struct RawSymbol { uint32_t nameOff; uint8_t type; uint8_t flags; uint16_t pad; uint32_t section; uint32_t offset; uint32_t size; };
    std::vector<RawSymbol> rawSymbols(header.symbolCount);
    for (uint32_t i = 0; i < header.symbolCount; i++) file.read(reinterpret_cast<char*>(&rawSymbols[i]), sizeof(RawSymbol));
    struct RawReloc { uint32_t offset; uint8_t type; uint8_t pad[3]; uint32_t symOff; int32_t addend; };
    std::vector<RawReloc> rawCodeRelocs(header.codeRelocCount);
    for (uint32_t i = 0; i < header.codeRelocCount; i++) file.read(reinterpret_cast<char*>(&rawCodeRelocs[i]), sizeof(RawReloc));
    std::vector<RawReloc> rawDataRelocs(header.dataRelocCount);
    for (uint32_t i = 0; i < header.dataRelocCount; i++) file.read(reinterpret_cast<char*>(&rawDataRelocs[i]), sizeof(RawReloc));
    struct RawImport { uint32_t dllOff; uint32_t funcOff; };
    std::vector<RawImport> rawImports(header.importCount);
    for (uint32_t i = 0; i < header.importCount; i++) file.read(reinterpret_cast<char*>(&rawImports[i]), sizeof(RawImport));
    std::vector<char> stringTable(header.stringTableSize);
    if (header.stringTableSize > 0) file.read(stringTable.data(), header.stringTableSize);
    auto getString = [&](uint32_t off) -> std::string { return (off >= stringTable.size()) ? "" : std::string(&stringTable[off]); };
    moduleName = getString(header.moduleNameOffset);
    symbols.clear(); symbolIndex.clear();
    for (auto& raw : rawSymbols) {
        ObjSymbol sym; sym.name = getString(raw.nameOff); sym.type = static_cast<ObjSymbolType>(raw.type);
        // Flags: bit 0 = isExported, bit 1 = isHidden, bit 2 = isWeak
        sym.isExported = (raw.flags & 1) != 0;
        sym.isHidden = (raw.flags & 2) != 0;
        sym.isWeak = (raw.flags & 4) != 0;
        sym.section = raw.section; sym.offset = raw.offset; sym.size = raw.size;
        addSymbol(sym);
    }
    codeRelocations.clear();
    for (auto& raw : rawCodeRelocs) codeRelocations.emplace_back(raw.offset, static_cast<RelocType>(raw.type), getString(raw.symOff), raw.addend);
    dataRelocations.clear();
    for (auto& raw : rawDataRelocs) dataRelocations.emplace_back(raw.offset, static_cast<RelocType>(raw.type), getString(raw.symOff), raw.addend);
    imports.clear();
    for (auto& raw : rawImports) imports.emplace_back(getString(raw.dllOff), getString(raw.funcOff));
    file.close();
    return true;
}

void ObjectFile::dump() const {
    std::cout << "=== Object File: " << moduleName << " ===\n";
    std::cout << "Code: " << codeSection.size() << " bytes, Data: " << dataSection.size() << " bytes, Rodata: " << rodataSection.size() << " bytes\n";
    std::cout << "\nSymbols:\n";
    const char* typeStr[] = {"UNDEF", "FUNC", "DATA", "CONST", "LOCAL"};
    for (auto& sym : symbols) std::cout << "  " << sym.name << " [" << typeStr[(int)sym.type] << "] sec=" << sym.section << " off=" << sym.offset << " size=" << sym.size << (sym.isExported ? " EXPORT" : "") << "\n";
    std::cout << "\nCode Relocations:\n";
    const char* relTypeStr[] = {"REL32", "RIP32", "ABS64", "ABS32"};
    for (auto& rel : codeRelocations) std::cout << "  @" << rel.offset << " " << relTypeStr[(int)rel.type] << " -> " << rel.symbol << " +" << rel.addend << "\n";
    std::cout << "\nImports:\n";
    for (auto& imp : imports) std::cout << "  " << imp.dll << "::" << imp.function << "\n";
}

} // namespace tyl
