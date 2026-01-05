// Tyl Compiler - Linker
#ifndef TYL_LINKER_H
#define TYL_LINKER_H

#include "backend/object/object_file.h"
#include "backend/x64/pe_generator.h"
#include <set>

namespace tyl {

struct LinkedSymbol {
    std::string name;
    ObjSymbolType type;
    uint32_t rva;
    uint32_t size;
    std::string sourceModule;
    bool isExported;    // Marked for export with #[export]
    bool isHidden;      // Hidden symbol (not visible outside module)
    bool isWeak;        // Weak symbol (can be overridden)
    LinkedSymbol() : type(ObjSymbolType::UNDEFINED), rva(0), size(0), isExported(false), isHidden(false), isWeak(false) {}
};

// Export entry for DLL exports
struct ExportEntry {
    std::string name;           // Export name
    std::string internalName;   // Internal symbol name (if different)
    uint32_t ordinal;           // Ordinal number (0 = auto-assign)
    bool noName;                // Export by ordinal only (NONAME)
    bool isData;                // DATA export (not a function)
    ExportEntry() : ordinal(0), noName(false), isData(false) {}
};

// DEF file parsed content
struct DefFile {
    std::string libraryName;    // LIBRARY name
    std::string description;    // DESCRIPTION string
    uint64_t imageBase;         // BASE address
    uint32_t heapSize;          // HEAPSIZE
    uint32_t stackSize;         // STACKSIZE
    std::vector<ExportEntry> exports;  // EXPORTS section
    
    DefFile() : imageBase(0), heapSize(0), stackSize(0) {}
};

struct LinkerConfig {
    uint64_t imageBase = 0x140000000ULL;
    uint32_t sectionAlignment = 0x1000;
    uint32_t fileAlignment = 0x200;
    std::string entryPoint = "_start";
    std::string outputFile = "a.exe";
    bool verbose = false;
    bool generateMap = false;
    bool generateDll = false;           // Generate DLL instead of EXE
    bool generateImportLib = false;     // Generate import library (.lib)
    std::string importLibFile;          // Import library output file
    std::string defFile;                // DEF file for exports
    std::string mapFile;
    std::vector<std::string> libraryPaths;
    std::vector<std::string> defaultLibs = {"kernel32.dll"};
    std::vector<std::string> staticLibs;  // Static libraries to link (.lib/.a)
    std::vector<std::string> exportSymbols;  // Symbols to export (command line)
};

class Linker {
public:
    Linker();
    void setConfig(const LinkerConfig& config) { config_ = config; }
    LinkerConfig& config() { return config_; }
    bool addObjectFile(const std::string& filename);
    bool addObjectFile(const ObjectFile& obj);
    bool addLibrary(const std::string& filename);
    bool addStaticLibrary(const std::string& filename);  // Add .lib/.a static library
    bool addExport(const std::string& name, const std::string& internalName = "");
    bool loadDefFile(const std::string& filename);       // Load .def file
    bool link();
    const std::vector<std::string>& getErrors() const { return errors_; }
    
    // Static method to generate import library from DLL
    static bool generateImportLibrary(const std::string& dllName, 
                                      const std::vector<ExportEntry>& exports,
                                      const std::string& outputFile);
    
private:
    LinkerConfig config_;
    std::vector<ObjectFile> objects_;
    std::vector<std::string> errors_;
    std::map<std::string, LinkedSymbol> globalSymbols_;
    std::map<std::string, uint32_t> importSymbols_;
    std::vector<uint8_t> mergedCode_;
    std::vector<uint8_t> mergedData_;
    std::vector<uint8_t> mergedRodata_;
    uint32_t codeRVA_ = 0;
    uint32_t dataRVA_ = 0;
    uint32_t rodataRVA_ = 0;
    uint32_t idataRVA_ = 0;
    uint32_t edataRVA_ = 0;  // Export directory RVA
    struct ObjectLayout { uint32_t codeOffset; uint32_t dataOffset; uint32_t rodataOffset; };
    std::vector<ObjectLayout> objectLayouts_;
    std::map<std::string, std::set<std::string>> collectedImports_;
    
    // Export support
    DefFile defFile_;
    std::vector<ExportEntry> exports_;
    
    bool collectSymbols();
    bool resolveSymbols();
    bool layoutSections();
    bool applyRelocations();
    bool generateExecutable();
    bool generateDll();                 // Generate DLL output
    bool writeImportLibrary();          // Write import library for DLL
    void error(const std::string& msg);
    uint32_t alignUp(uint32_t value, uint32_t alignment);
    void buildImportSection(std::vector<uint8_t>& section, const std::map<std::string, std::set<std::string>>& imports, uint32_t baseRVA);
    void buildExportSection(std::vector<uint8_t>& section, uint32_t baseRVA);
    void generateMapFile();
    void collectExports();              // Collect exports from def file and command line
    
    // Static library support
    bool loadCOFFLibrary(const std::string& filename);    // Load Windows .lib (COFF archive)
    bool loadARLibrary(const std::string& filename);      // Load Unix .a (ar archive)
    bool parseCOFFObject(const std::vector<uint8_t>& data, const std::string& memberName);
    std::vector<std::string> staticLibraries_;            // Loaded static library paths
    std::set<std::string> unresolvedSymbols_;             // Symbols that need resolution from libs
};

} // namespace tyl

#endif // TYL_LINKER_H
