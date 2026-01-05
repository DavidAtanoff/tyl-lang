// Tyl Compiler - Linker DLL Generation and Export Support

#include "linker_base.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace tyl {

bool Linker::addExport(const std::string& name, const std::string& internalName) {
    ExportEntry entry;
    entry.name = name;
    entry.internalName = internalName.empty() ? name : internalName;
    entry.ordinal = 0;
    entry.noName = false;
    entry.isData = false;
    exports_.push_back(entry);
    return true;
}

bool Linker::loadDefFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        error("Cannot open DEF file: " + filename);
        return false;
    }
    
    std::string line;
    bool inExports = false;
    
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        // Skip comments
        if (line.empty() || line[0] == ';') continue;
        
        // Convert to uppercase for keyword matching
        std::string upper = line;
        for (char& c : upper) c = (char)toupper(c);
        
        if (upper.substr(0, 7) == "LIBRARY") {
            // LIBRARY name
            size_t pos = line.find_first_of(" \t", 7);
            if (pos != std::string::npos) {
                defFile_.libraryName = line.substr(pos + 1);
                // Remove quotes if present
                if (!defFile_.libraryName.empty() && defFile_.libraryName[0] == '"') {
                    defFile_.libraryName = defFile_.libraryName.substr(1);
                    size_t q = defFile_.libraryName.find('"');
                    if (q != std::string::npos) defFile_.libraryName = defFile_.libraryName.substr(0, q);
                }
            }
            inExports = false;
        } else if (upper.substr(0, 11) == "DESCRIPTION") {
            size_t pos = line.find('"');
            if (pos != std::string::npos) {
                size_t end = line.rfind('"');
                if (end > pos) {
                    defFile_.description = line.substr(pos + 1, end - pos - 1);
                }
            }
            inExports = false;
        } else if (upper.substr(0, 4) == "BASE") {
            // BASE=address
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string val = line.substr(eq + 1);
                defFile_.imageBase = std::stoull(val, nullptr, 0);
            }
            inExports = false;
        } else if (upper.substr(0, 8) == "HEAPSIZE") {
            size_t pos = line.find_first_of(" \t=", 8);
            if (pos != std::string::npos) {
                std::string val = line.substr(pos + 1);
                defFile_.heapSize = std::stoul(val, nullptr, 0);
            }
            inExports = false;
        } else if (upper.substr(0, 9) == "STACKSIZE") {
            size_t pos = line.find_first_of(" \t=", 9);
            if (pos != std::string::npos) {
                std::string val = line.substr(pos + 1);
                defFile_.stackSize = std::stoul(val, nullptr, 0);
            }
            inExports = false;
        } else if (upper == "EXPORTS") {
            inExports = true;
        } else if (inExports) {
            // Parse export entry
            // Format: name [=internalname] [@ordinal] [NONAME] [DATA] [PRIVATE]
            ExportEntry entry;
            
            std::istringstream iss(line);
            std::string token;
            iss >> token;
            
            // Check for name=internalname
            size_t eq = token.find('=');
            if (eq != std::string::npos) {
                entry.name = token.substr(0, eq);
                entry.internalName = token.substr(eq + 1);
            } else {
                entry.name = token;
                entry.internalName = token;
            }
            
            // Parse remaining tokens
            while (iss >> token) {
                std::string upperToken = token;
                for (char& c : upperToken) c = (char)toupper(c);
                
                if (token[0] == '@') {
                    entry.ordinal = std::stoul(token.substr(1));
                } else if (upperToken == "NONAME") {
                    entry.noName = true;
                } else if (upperToken == "DATA") {
                    entry.isData = true;
                } else if (upperToken == "PRIVATE") {
                    // Private exports - not included in import lib
                    // We'll handle this later if needed
                }
            }
            
            defFile_.exports.push_back(entry);
        }
    }
    
    if (config_.verbose) {
        std::cout << "Loaded DEF file: " << filename << "\n";
        if (!defFile_.libraryName.empty()) {
            std::cout << "  LIBRARY: " << defFile_.libraryName << "\n";
        }
        std::cout << "  Exports: " << defFile_.exports.size() << "\n";
    }
    
    return true;
}

void Linker::collectExports() {
    // Add exports from DEF file
    for (const auto& exp : defFile_.exports) {
        exports_.push_back(exp);
    }
    
    // Add exports from command line
    for (const auto& name : config_.exportSymbols) {
        ExportEntry entry;
        entry.name = name;
        entry.internalName = name;
        entry.ordinal = 0;
        entry.noName = false;
        entry.isData = false;
        exports_.push_back(entry);
    }
    
    // Add exports from #[export] attribute on functions
    for (const auto& [name, sym] : globalSymbols_) {
        if (sym.isExported && sym.type == ObjSymbolType::FUNCTION && !sym.isHidden) {
            // Check if already in exports list
            bool found = false;
            for (const auto& exp : exports_) {
                if (exp.name == name || exp.internalName == name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ExportEntry entry;
                entry.name = name;
                entry.internalName = name;
                entry.ordinal = 0;
                entry.noName = false;
                entry.isData = false;
                exports_.push_back(entry);
            }
        }
    }
    
    // If no exports specified, export all public symbols (except internal ones)
    if (exports_.empty() && config_.generateDll) {
        for (const auto& [name, sym] : globalSymbols_) {
            if (sym.type == ObjSymbolType::FUNCTION && !sym.isHidden &&
                name != "_start" && name.substr(0, 2) != "__") {
                ExportEntry entry;
                entry.name = name;
                entry.internalName = name;
                entry.ordinal = 0;
                entry.noName = false;
                entry.isData = false;
                exports_.push_back(entry);
            }
        }
    }
    
    // Sort exports by name for binary search in loader
    std::sort(exports_.begin(), exports_.end(), 
              [](const ExportEntry& a, const ExportEntry& b) {
                  return a.name < b.name;
              });
    
    // Assign ordinals if not specified
    uint32_t nextOrdinal = 1;
    for (auto& exp : exports_) {
        if (exp.ordinal == 0) {
            exp.ordinal = nextOrdinal++;
        } else {
            if (exp.ordinal >= nextOrdinal) {
                nextOrdinal = exp.ordinal + 1;
            }
        }
    }
}

void Linker::buildExportSection(std::vector<uint8_t>& section, uint32_t baseRVA) {
    if (exports_.empty()) return;
    
    // Export Directory Table (40 bytes)
    // + Address Table (4 bytes per export)
    // + Name Pointer Table (4 bytes per named export)
    // + Ordinal Table (2 bytes per named export)
    // + DLL name string
    // + Export name strings
    
    size_t numExports = exports_.size();
    size_t numNamedExports = 0;
    for (const auto& exp : exports_) {
        if (!exp.noName) numNamedExports++;
    }
    
    // Calculate sizes
    uint32_t edtSize = 40;  // Export Directory Table
    uint32_t eatSize = (uint32_t)(numExports * 4);  // Export Address Table
    uint32_t nptSize = (uint32_t)(numNamedExports * 4);  // Name Pointer Table
    uint32_t otSize = (uint32_t)(numNamedExports * 2);  // Ordinal Table
    
    // DLL name
    std::string dllName = defFile_.libraryName;
    if (dllName.empty()) {
        // Extract from output file
        size_t slash = config_.outputFile.find_last_of("/\\");
        dllName = (slash != std::string::npos) ? 
                  config_.outputFile.substr(slash + 1) : config_.outputFile;
    }
    
    // Calculate string table size
    uint32_t stringTableStart = edtSize + eatSize + nptSize + otSize;
    uint32_t currentStringOffset = stringTableStart;
    currentStringOffset += (uint32_t)dllName.size() + 1;  // DLL name
    
    std::vector<uint32_t> nameOffsets;
    for (const auto& exp : exports_) {
        if (!exp.noName) {
            nameOffsets.push_back(currentStringOffset);
            currentStringOffset += (uint32_t)exp.name.size() + 1;
        }
    }
    
    // Align to 4 bytes
    while (currentStringOffset % 4 != 0) currentStringOffset++;
    
    section.resize(currentStringOffset, 0);
    
    // Find ordinal base (minimum ordinal)
    uint32_t ordinalBase = UINT32_MAX;
    for (const auto& exp : exports_) {
        if (exp.ordinal < ordinalBase) ordinalBase = exp.ordinal;
    }
    if (ordinalBase == UINT32_MAX) ordinalBase = 1;
    
    // Write Export Directory Table
    uint32_t offset = 0;
    
    // Characteristics (reserved)
    memset(&section[offset], 0, 4); offset += 4;
    // TimeDateStamp
    memset(&section[offset], 0, 4); offset += 4;
    // MajorVersion, MinorVersion
    memset(&section[offset], 0, 4); offset += 4;
    // Name RVA
    uint32_t nameRVA = baseRVA + stringTableStart;
    memcpy(&section[offset], &nameRVA, 4); offset += 4;
    // Ordinal Base
    memcpy(&section[offset], &ordinalBase, 4); offset += 4;
    // Number of Functions (Address Table entries)
    uint32_t numFunctions = (uint32_t)numExports;
    memcpy(&section[offset], &numFunctions, 4); offset += 4;
    // Number of Names
    uint32_t numNames = (uint32_t)numNamedExports;
    memcpy(&section[offset], &numNames, 4); offset += 4;
    // Address Table RVA
    uint32_t eatRVA = baseRVA + edtSize;
    memcpy(&section[offset], &eatRVA, 4); offset += 4;
    // Name Pointer Table RVA
    uint32_t nptRVA = baseRVA + edtSize + eatSize;
    memcpy(&section[offset], &nptRVA, 4); offset += 4;
    // Ordinal Table RVA
    uint32_t otRVA = baseRVA + edtSize + eatSize + nptSize;
    memcpy(&section[offset], &otRVA, 4); offset += 4;
    
    // Write Export Address Table
    for (const auto& exp : exports_) {
        auto it = globalSymbols_.find(exp.internalName);
        uint32_t funcRVA = 0;
        if (it != globalSymbols_.end()) {
            funcRVA = it->second.rva;
        }
        memcpy(&section[offset], &funcRVA, 4);
        offset += 4;
    }
    
    // Write Name Pointer Table (only for named exports)
    size_t nameIdx = 0;
    for (const auto& exp : exports_) {
        if (!exp.noName) {
            uint32_t nameRVA = baseRVA + nameOffsets[nameIdx++];
            memcpy(&section[offset], &nameRVA, 4);
            offset += 4;
        }
    }
    
    // Write Ordinal Table
    for (const auto& exp : exports_) {
        if (!exp.noName) {
            uint16_t ordinal = (uint16_t)(exp.ordinal - ordinalBase);
            memcpy(&section[offset], &ordinal, 2);
            offset += 2;
        }
    }
    
    // Write DLL name
    memcpy(&section[stringTableStart], dllName.c_str(), dllName.size() + 1);
    
    // Write export names
    nameIdx = 0;
    for (const auto& exp : exports_) {
        if (!exp.noName) {
            memcpy(&section[nameOffsets[nameIdx]], exp.name.c_str(), exp.name.size() + 1);
            nameIdx++;
        }
    }
}

bool Linker::generateDll() {
    if (config_.verbose) {
        std::cout << "Generating DLL: " << config_.outputFile << "\n";
    }
    
    // Collect exports
    collectExports();
    
    if (exports_.empty()) {
        error("No exports defined for DLL");
        return false;
    }
    
    if (config_.verbose) {
        std::cout << "  Exporting " << exports_.size() << " symbol(s)\n";
    }
    
    std::ofstream file(config_.outputFile, std::ios::binary);
    if (!file) {
        error("Cannot create output file: " + config_.outputFile);
        return false;
    }
    
    const uint32_t FILE_ALIGN = config_.fileAlignment;
    const uint32_t SECT_ALIGN = config_.sectionAlignment;
    
    // Calculate section sizes (file-aligned)
    uint32_t codeRawSize = alignUp((uint32_t)mergedCode_.size(), FILE_ALIGN);
    if (codeRawSize == 0) codeRawSize = FILE_ALIGN;
    uint32_t dataRawSize = mergedData_.empty() ? 0 : alignUp((uint32_t)mergedData_.size(), FILE_ALIGN);
    uint32_t rodataRawSize = mergedRodata_.empty() ? 0 : alignUp((uint32_t)mergedRodata_.size(), FILE_ALIGN);
    
    // Calculate section RVAs (section-aligned)
    uint32_t textRVA = 0x1000;
    uint32_t textVirtSize = (uint32_t)mergedCode_.size();
    uint32_t textAlignedSize = alignUp(textVirtSize, SECT_ALIGN);
    if (textAlignedSize == 0) textAlignedSize = SECT_ALIGN;
    
    uint32_t dataRVA = textRVA + textAlignedSize;
    uint32_t dataVirtSize = (uint32_t)mergedData_.size();
    uint32_t dataAlignedSize = mergedData_.empty() ? 0 : alignUp(dataVirtSize, SECT_ALIGN);
    
    uint32_t rdataRVA = dataRVA + dataAlignedSize;
    uint32_t rdataVirtSize = (uint32_t)mergedRodata_.size();
    uint32_t rdataAlignedSize = mergedRodata_.empty() ? 0 : alignUp(rdataVirtSize, SECT_ALIGN);
    
    // Export section comes after rdata (or data if no rdata, or text if no data)
    uint32_t edataRVA = rdataRVA + rdataAlignedSize;
    
    // Build export section with correct RVA
    std::vector<uint8_t> edataSection;
    buildExportSection(edataSection, edataRVA);
    uint32_t edataVirtSize = (uint32_t)edataSection.size();
    uint32_t edataRawSize = edataSection.empty() ? 0 : alignUp(edataVirtSize, FILE_ALIGN);
    uint32_t edataAlignedSize = edataSection.empty() ? 0 : alignUp(edataVirtSize, SECT_ALIGN);
    
    // Import section comes after export
    uint32_t idataRVA = edataRVA + edataAlignedSize;
    
    // Build import section
    std::vector<uint8_t> idataSection;
    buildImportSection(idataSection, collectedImports_, idataRVA);
    uint32_t idataVirtSize = (uint32_t)idataSection.size();
    uint32_t idataRawSize = idataSection.empty() ? 0 : alignUp(idataVirtSize, FILE_ALIGN);
    uint32_t idataAlignedSize = idataSection.empty() ? 0 : alignUp(idataVirtSize, SECT_ALIGN);
    
    // Count sections
    uint32_t numSections = 1;  // .text always present
    if (!mergedData_.empty()) numSections++;
    if (!mergedRodata_.empty()) numSections++;
    if (!edataSection.empty()) numSections++;
    if (!idataSection.empty()) numSections++;
    
    // Headers size: DOS header+stub (128) + PE sig (4) + COFF header (20) + Optional header (240) + section headers (40 * n)
    uint32_t headersSize = alignUp(128 + 4 + 20 + 240 + (numSections * 40), FILE_ALIGN);
    
    // Image size = last section RVA + last section aligned size
    uint32_t imageSize = idataRVA + idataAlignedSize;
    if (idataSection.empty()) imageSize = edataRVA + edataAlignedSize;
    if (edataSection.empty() && idataSection.empty()) imageSize = rdataRVA + rdataAlignedSize;
    if (mergedRodata_.empty() && edataSection.empty() && idataSection.empty()) imageSize = dataRVA + dataAlignedSize;
    if (mergedData_.empty() && mergedRodata_.empty() && edataSection.empty() && idataSection.empty()) imageSize = textRVA + textAlignedSize;
    imageSize = alignUp(imageSize, SECT_ALIGN);
    
    // For DLL, entry point is DllMain or 0
    uint32_t entryRVA = 0;
    auto entryIt = globalSymbols_.find("DllMain");
    if (entryIt != globalSymbols_.end()) {
        entryRVA = entryIt->second.rva;
    } else {
        entryIt = globalSymbols_.find(config_.entryPoint);
        if (entryIt != globalSymbols_.end()) {
            entryRVA = entryIt->second.rva;
        }
    }
    
    if (config_.verbose) {
        std::cout << "  Section layout:\n";
        std::cout << "    .text:  RVA=0x" << std::hex << textRVA << " size=0x" << textVirtSize << "\n";
        if (!mergedData_.empty()) std::cout << "    .data:  RVA=0x" << dataRVA << " size=0x" << dataVirtSize << "\n";
        if (!mergedRodata_.empty()) std::cout << "    .rdata: RVA=0x" << rdataRVA << " size=0x" << rdataVirtSize << "\n";
        if (!edataSection.empty()) std::cout << "    .edata: RVA=0x" << edataRVA << " size=0x" << edataVirtSize << "\n";
        if (!idataSection.empty()) std::cout << "    .idata: RVA=0x" << idataRVA << " size=0x" << idataVirtSize << "\n";
        std::cout << "    Image size: 0x" << imageSize << std::dec << "\n";
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
    
    // PE Signature
    write32(0x00004550);
    
    // COFF Header
    write16(0x8664);  // Machine: AMD64
    write16((uint16_t)numSections);
    write32(0);  // TimeDateStamp
    write32(0);  // PointerToSymbolTable
    write32(0);  // NumberOfSymbols
    write16(240);  // SizeOfOptionalHeader
    write16(0x2022);  // Characteristics: DLL, EXECUTABLE_IMAGE, LARGE_ADDRESS_AWARE
    
    // Optional Header (PE32+)
    write16(0x020B);  // Magic: PE32+
    write8(14); write8(0);  // Linker version
    write32(codeRawSize);  // SizeOfCode
    write32(dataRawSize + rodataRawSize + edataRawSize + idataRawSize);  // SizeOfInitializedData
    write32(0);  // SizeOfUninitializedData
    write32(entryRVA);  // AddressOfEntryPoint
    write32(textRVA);  // BaseOfCode
    write64(config_.imageBase);  // ImageBase
    write32(SECT_ALIGN);  // SectionAlignment
    write32(FILE_ALIGN);  // FileAlignment
    write16(6); write16(0);  // OS version
    write16(0); write16(0);  // Image version
    write16(6); write16(0);  // Subsystem version
    write32(0);  // Win32VersionValue
    write32(imageSize);  // SizeOfImage
    write32(headersSize);  // SizeOfHeaders
    write32(0);  // CheckSum
    write16(3);  // Subsystem: CONSOLE
    write16(0x8160);  // DllCharacteristics: DYNAMIC_BASE, NX_COMPAT, HIGH_ENTROPY_VA, TERMINAL_SERVER_AWARE
    write64(0x100000);  // SizeOfStackReserve
    write64(0x1000);  // SizeOfStackCommit
    write64(0x100000);  // SizeOfHeapReserve
    write64(0x1000);  // SizeOfHeapCommit
    write32(0);  // LoaderFlags
    write32(16);  // NumberOfRvaAndSizes
    
    // Data Directories
    for (int i = 0; i < 16; i++) {
        if (i == 0 && !edataSection.empty()) {
            // Export Directory
            write32(edataRVA);
            write32(edataVirtSize);
        } else if (i == 1 && !idataSection.empty()) {
            // Import Directory
            write32(idataRVA);
            write32(idataVirtSize);
        } else {
            write32(0); write32(0);
        }
    }
    
    // Section Headers
    uint32_t fileOff = headersSize;
    
    // .text
    writeBytes(".text\0\0\0", 8);
    write32(textVirtSize);
    write32(textRVA);
    write32(codeRawSize);
    write32(fileOff);
    write32(0); write32(0); write16(0); write16(0);
    write32(0x60000020);  // CODE, EXECUTE, READ
    fileOff += codeRawSize;
    
    // .data
    if (!mergedData_.empty()) {
        writeBytes(".data\0\0\0", 8);
        write32(dataVirtSize);
        write32(dataRVA);
        write32(dataRawSize);
        write32(fileOff);
        write32(0); write32(0); write16(0); write16(0);
        write32(0xC0000040);  // INITIALIZED_DATA, READ, WRITE
        fileOff += dataRawSize;
    }
    
    // .rdata
    if (!mergedRodata_.empty()) {
        writeBytes(".rdata\0\0", 8);
        write32(rdataVirtSize);
        write32(rdataRVA);
        write32(rodataRawSize);
        write32(fileOff);
        write32(0); write32(0); write16(0); write16(0);
        write32(0x40000040);  // INITIALIZED_DATA, READ
        fileOff += rodataRawSize;
    }
    
    // .edata (exports)
    if (!edataSection.empty()) {
        writeBytes(".edata\0\0", 8);
        write32(edataVirtSize);
        write32(edataRVA);
        write32(edataRawSize);
        write32(fileOff);
        write32(0); write32(0); write16(0); write16(0);
        write32(0x40000040);  // INITIALIZED_DATA, READ
        fileOff += edataRawSize;
    }
    
    // .idata (imports)
    if (!idataSection.empty()) {
        writeBytes(".idata\0\0", 8);
        write32(idataVirtSize);
        write32(idataRVA);
        write32(idataRawSize);
        write32(fileOff);
        write32(0); write32(0); write16(0); write16(0);
        write32(0xC0000040);  // INITIALIZED_DATA, READ, WRITE
    }
    
    // Pad to file alignment
    pad(FILE_ALIGN);
    
    // Write sections
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
    
    if (!edataSection.empty()) {
        writeBytes(edataSection.data(), edataSection.size());
        pad(FILE_ALIGN);
    }
    
    if (!idataSection.empty()) {
        writeBytes(idataSection.data(), idataSection.size());
        pad(FILE_ALIGN);
    }
    
    file.close();
    
    // Generate import library if requested
    if (config_.generateImportLib) {
        if (!writeImportLibrary()) {
            return false;
        }
    }
    
    if (config_.generateMap) {
        generateMapFile();
    }
    
    return true;
}


bool Linker::writeImportLibrary() {
    std::string libFile = config_.importLibFile;
    if (libFile.empty()) {
        // Generate from output file name
        size_t dot = config_.outputFile.rfind('.');
        if (dot != std::string::npos) {
            libFile = config_.outputFile.substr(0, dot) + ".lib";
        } else {
            libFile = config_.outputFile + ".lib";
        }
    }
    
    // Get DLL name
    std::string dllName = defFile_.libraryName;
    if (dllName.empty()) {
        size_t slash = config_.outputFile.find_last_of("/\\");
        dllName = (slash != std::string::npos) ? 
                  config_.outputFile.substr(slash + 1) : config_.outputFile;
    }
    
    return generateImportLibrary(dllName, exports_, libFile);
}

bool Linker::generateImportLibrary(const std::string& dllName,
                                   const std::vector<ExportEntry>& exports,
                                   const std::string& outputFile) {
    // Generate a COFF import library (.lib)
    // This is an AR archive containing import object files
    
    std::ofstream file(outputFile, std::ios::binary);
    if (!file) {
        return false;
    }
    
    // AR archive magic
    file.write("!<arch>\n", 8);
    
    // We'll create a simplified import library with:
    // 1. First linker member (symbol index)
    // 2. Second linker member (symbol index with sorted names)
    // 3. Import header for each export
    
    // For simplicity, we'll use the short import format (IMPORT_OBJECT_HDR)
    // which is more compact than full COFF objects
    
    // Build symbol table
    std::vector<std::string> symbols;
    std::vector<uint32_t> memberOffsets;
    
    // Calculate member offsets
    // First linker member starts at offset 8 (after magic)
    // Each AR header is 60 bytes
    
    uint32_t currentOffset = 8;  // After "!<arch>\n"
    
    // First linker member (we'll skip for simplicity and use short imports)
    // Second linker member (we'll skip for simplicity)
    
    // For each export, create an import descriptor
    for (const auto& exp : exports) {
        if (exp.noName) continue;  // Skip ordinal-only exports
        
        symbols.push_back("__imp_" + exp.name);  // Import symbol
        symbols.push_back(exp.name);              // Function symbol
        memberOffsets.push_back(currentOffset);
        memberOffsets.push_back(currentOffset);
        
        // Calculate size of this import object
        // Import object header: 20 bytes
        // DLL name + null
        // Symbol name + null
        size_t memberSize = 20 + dllName.size() + 1 + exp.name.size() + 1;
        // Align to 2 bytes
        if (memberSize % 2) memberSize++;
        
        currentOffset += 60 + (uint32_t)memberSize;  // AR header + content
        if (currentOffset % 2) currentOffset++;
    }
    
    // Write import objects for each export
    for (const auto& exp : exports) {
        if (exp.noName) continue;
        
        // Calculate member size
        size_t contentSize = 20 + dllName.size() + 1 + exp.name.size() + 1;
        
        // AR member header (60 bytes)
        char header[60];
        memset(header, ' ', 60);
        
        // Name: "/" for import objects
        header[0] = '/';
        
        // Date (12 bytes) - use 0
        memset(header + 16, '0', 12);
        
        // UID (6 bytes)
        memset(header + 28, '0', 6);
        
        // GID (6 bytes)
        memset(header + 34, '0', 6);
        
        // Mode (8 bytes)
        memcpy(header + 40, "100666  ", 8);
        
        // Size (10 bytes)
        char sizeStr[11];
        snprintf(sizeStr, sizeof(sizeStr), "%-10zu", contentSize);
        memcpy(header + 48, sizeStr, 10);
        
        // End marker
        header[58] = '`';
        header[59] = '\n';
        
        file.write(header, 60);
        
        // Import object header (IMPORT_OBJECT_HEADER)
        // Sig1: 0x0000
        uint16_t sig1 = 0x0000;
        file.write((char*)&sig1, 2);
        
        // Sig2: 0xFFFF
        uint16_t sig2 = 0xFFFF;
        file.write((char*)&sig2, 2);
        
        // Version: 0
        uint16_t version = 0;
        file.write((char*)&version, 2);
        
        // Machine: AMD64 (0x8664)
        uint16_t machine = 0x8664;
        file.write((char*)&machine, 2);
        
        // TimeDateStamp
        uint32_t timestamp = 0;
        file.write((char*)&timestamp, 4);
        
        // SizeOfData
        uint32_t sizeOfData = (uint32_t)(dllName.size() + 1 + exp.name.size() + 1);
        file.write((char*)&sizeOfData, 4);
        
        // Ordinal/Hint
        uint16_t hint = (uint16_t)exp.ordinal;
        file.write((char*)&hint, 2);
        
        // Type: IMPORT_NAME (0) | IMPORT_CODE (0 << 2)
        // For functions: type = 0 (IMPORT_CODE)
        // Name type: IMPORT_NAME (0)
        uint16_t type = 0;  // IMPORT_CODE, IMPORT_NAME
        if (exp.isData) {
            type = 1;  // IMPORT_DATA
        }
        file.write((char*)&type, 2);
        
        // DLL name (null-terminated)
        file.write(dllName.c_str(), dllName.size() + 1);
        
        // Symbol name (null-terminated)
        file.write(exp.name.c_str(), exp.name.size() + 1);
        
        // Pad to even boundary
        if (contentSize % 2) {
            char pad = '\n';
            file.write(&pad, 1);
        }
    }
    
    file.close();
    return true;
}

} // namespace tyl
