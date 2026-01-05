// Tyl Compiler - Linker Core
// Main link entry point and helpers

#include "linker_base.h"
#include <iostream>
#include <fstream>
#include <cstring>

namespace tyl {

// COFF Archive magic number
static const char AR_MAGIC[] = "!<arch>\n";
static const size_t AR_MAGIC_LEN = 8;

// COFF file header signature
static const uint16_t COFF_MACHINE_AMD64 = 0x8664;
static const uint16_t COFF_MACHINE_I386 = 0x14c;

Linker::Linker() {}

void Linker::error(const std::string& msg) {
    errors_.push_back(msg);
    if (config_.verbose) {
        std::cerr << "Linker error: " << msg << "\n";
    }
}

uint32_t Linker::alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

bool Linker::addObjectFile(const std::string& filename) {
    ObjectFile obj;
    if (!obj.read(filename)) {
        error("Failed to read object file: " + filename);
        return false;
    }
    objects_.push_back(std::move(obj));
    return true;
}

bool Linker::addObjectFile(const ObjectFile& obj) {
    objects_.push_back(obj);
    return true;
}

bool Linker::addLibrary(const std::string& filename) {
    // For DLL imports, just record the library name
    // The actual import resolution happens during linking
    (void)filename;
    return true;
}

bool Linker::addStaticLibrary(const std::string& filename) {
    // Determine library type by extension
    std::string ext;
    size_t dotPos = filename.rfind('.');
    if (dotPos != std::string::npos) {
        ext = filename.substr(dotPos);
        // Convert to lowercase for comparison
        for (char& c : ext) c = (char)tolower(c);
    }
    
    if (ext == ".lib") {
        return loadCOFFLibrary(filename);
    } else if (ext == ".a") {
        return loadARLibrary(filename);
    } else {
        // Try COFF first, then AR
        if (loadCOFFLibrary(filename)) return true;
        return loadARLibrary(filename);
    }
}

bool Linker::loadCOFFLibrary(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        error("Cannot open library file: " + filename);
        return false;
    }
    
    // Read the entire file
    file.seekg(0, std::ios::end);
    size_t fileSize = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    
    // Check for AR archive magic
    if (fileSize >= AR_MAGIC_LEN && memcmp(data.data(), AR_MAGIC, AR_MAGIC_LEN) == 0) {
        // This is an AR archive (standard .lib format on Windows)
        size_t offset = AR_MAGIC_LEN;
        int memberIndex = 0;
        
        while (offset + 60 <= fileSize) {
            // AR header is 60 bytes
            // Format: name[16] date[12] uid[6] gid[6] mode[8] size[10] magic[2]
            char name[17] = {0};
            memcpy(name, &data[offset], 16);
            
            // Parse size (ASCII decimal, space-padded)
            char sizeStr[11] = {0};
            memcpy(sizeStr, &data[offset + 48], 10);
            size_t memberSize = (size_t)atol(sizeStr);
            
            // Check magic bytes
            if (data[offset + 58] != '`' || data[offset + 59] != '\n') {
                break;  // Invalid AR header
            }
            
            offset += 60;  // Skip header
            
            // Skip special members (symbol table, string table)
            std::string memberName(name);
            // Trim trailing spaces
            while (!memberName.empty() && memberName.back() == ' ') {
                memberName.pop_back();
            }
            
            // Skip linker member (/ or //) and string table
            if (memberName != "/" && memberName != "//" && memberName != "") {
                // This is an object file member
                if (offset + memberSize <= fileSize) {
                    std::vector<uint8_t> memberData(data.begin() + offset, 
                                                     data.begin() + offset + memberSize);
                    
                    // Try to parse as COFF object
                    if (!parseCOFFObject(memberData, memberName)) {
                        // Not a valid COFF object, skip
                        if (config_.verbose) {
                            std::cout << "  Skipping non-COFF member: " << memberName << "\n";
                        }
                    }
                }
            }
            
            // Move to next member (aligned to 2 bytes)
            offset += memberSize;
            if (offset % 2 != 0) offset++;
            memberIndex++;
        }
        
        staticLibraries_.push_back(filename);
        if (config_.verbose) {
            std::cout << "Loaded static library: " << filename << "\n";
        }
        return true;
    }
    
    // Not an AR archive - might be a single COFF object or import library
    // For now, just record it
    staticLibraries_.push_back(filename);
    if (config_.verbose) {
        std::cout << "Added library: " << filename << "\n";
    }
    return true;
}

bool Linker::loadARLibrary(const std::string& filename) {
    // Unix .a format is the same as Windows .lib (both use AR format)
    return loadCOFFLibrary(filename);
}

bool Linker::parseCOFFObject(const std::vector<uint8_t>& data, const std::string& memberName) {
    if (data.size() < 20) return false;  // Minimum COFF header size
    
    // Check COFF machine type
    uint16_t machine = data[0] | (data[1] << 8);
    if (machine != COFF_MACHINE_AMD64 && machine != COFF_MACHINE_I386) {
        return false;  // Not a valid COFF object
    }
    
    // For now, we just validate it's a COFF object
    // Full COFF parsing would extract symbols and sections
    // This is a simplified implementation that records the library was loaded
    
    if (config_.verbose) {
        std::cout << "  Found COFF object: " << memberName << " (machine: 0x" 
                  << std::hex << machine << std::dec << ")\n";
    }
    
    // TODO: Full COFF symbol extraction for proper static linking
    // This would involve:
    // 1. Parse COFF header to get section count and symbol table offset
    // 2. Parse section headers
    // 3. Parse symbol table
    // 4. Extract code/data sections
    // 5. Create ObjectFile from COFF data
    
    return true;
}

bool Linker::link() {
    if (objects_.empty() && config_.staticLibs.empty()) {
        error("No input files");
        return false;
    }
    
    errors_.clear();
    globalSymbols_.clear();
    importSymbols_.clear();
    mergedCode_.clear();
    mergedData_.clear();
    mergedRodata_.clear();
    objectLayouts_.clear();
    collectedImports_.clear();
    unresolvedSymbols_.clear();
    exports_.clear();
    
    if (config_.verbose) {
        std::cout << "Linking " << objects_.size() << " object file(s)";
        if (!config_.staticLibs.empty()) {
            std::cout << " with " << config_.staticLibs.size() << " static library(ies)";
        }
        if (config_.generateDll) {
            std::cout << " as DLL";
        }
        std::cout << "...\n";
    }
    
    // Load DEF file if specified
    if (!config_.defFile.empty()) {
        if (!loadDefFile(config_.defFile)) {
            return false;
        }
    }
    
    // Load static libraries first
    for (const auto& lib : config_.staticLibs) {
        if (!addStaticLibrary(lib)) {
            error("Failed to load static library: " + lib);
            return false;
        }
    }
    
    if (!collectSymbols()) return false;
    if (!resolveSymbols()) return false;
    if (!layoutSections()) return false;
    if (!applyRelocations()) return false;
    
    // Generate DLL or EXE
    if (config_.generateDll) {
        if (!generateDll()) return false;
    } else {
        if (!generateExecutable()) return false;
    }
    
    if (config_.verbose) {
        std::cout << "Successfully linked: " << config_.outputFile << "\n";
    }
    
    return true;
}

} // namespace tyl
