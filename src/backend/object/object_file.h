// Tyl Compiler - Object File Format
#ifndef TYL_OBJECT_FILE_H
#define TYL_OBJECT_FILE_H

#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <fstream>

namespace tyl {

constexpr uint32_t TYL_OBJ_MAGIC = 0x4F584C46;
constexpr uint16_t TYL_OBJ_VERSION = 1;

enum class ObjSymbolType : uint8_t { UNDEFINED = 0, FUNCTION = 1, DATA = 2, CONST = 3, LOCAL = 4 };
enum class RelocType : uint8_t { REL32 = 0, RIP32 = 1, ABS64 = 2, ABS32 = 3 };

struct ObjSymbol {
    std::string name;
    ObjSymbolType type;
    uint32_t section;
    uint32_t offset;
    uint32_t size;
    bool isExported;
    bool isHidden;      // Symbol not visible outside module
    bool isWeak;        // Weak symbol - can be overridden
    ObjSymbol() : type(ObjSymbolType::UNDEFINED), section(0), offset(0), size(0), isExported(false), isHidden(false), isWeak(false) {}
    ObjSymbol(std::string n, ObjSymbolType t, uint32_t sec, uint32_t off, uint32_t sz, bool exp = true, bool hidden = false, bool weak = false)
        : name(std::move(n)), type(t), section(sec), offset(off), size(sz), isExported(exp), isHidden(hidden), isWeak(weak) {}
};

struct Relocation {
    uint32_t offset;
    RelocType type;
    std::string symbol;
    int32_t addend;
    Relocation() : offset(0), type(RelocType::REL32), addend(0) {}
    Relocation(uint32_t off, RelocType t, std::string sym, int32_t add = 0)
        : offset(off), type(t), symbol(std::move(sym)), addend(add) {}
};

struct Import {
    std::string dll;
    std::string function;
    Import() = default;
    Import(std::string d, std::string f) : dll(std::move(d)), function(std::move(f)) {}
};

class ObjectFile {
public:
    std::string moduleName;
    std::vector<uint8_t> codeSection;
    std::vector<uint8_t> dataSection;
    std::vector<uint8_t> rodataSection;
    std::vector<ObjSymbol> symbols;
    std::map<std::string, size_t> symbolIndex;
    std::vector<Relocation> codeRelocations;
    std::vector<Relocation> dataRelocations;
    std::vector<Import> imports;
    
    void addSymbol(const ObjSymbol& sym);
    ObjSymbol* findSymbol(const std::string& name);
    uint32_t addCode(const std::vector<uint8_t>& code);
    uint32_t addData(const void* data, size_t size);
    uint32_t addRodata(const void* data, size_t size);
    uint32_t addString(const std::string& str);
    void addCodeRelocation(const Relocation& reloc);
    void addDataRelocation(const Relocation& reloc);
    void addImport(const std::string& dll, const std::string& function);
    bool write(const std::string& filename);
    bool read(const std::string& filename);
    void dump() const;
};

struct ObjectFileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t codeSize;
    uint32_t dataSize;
    uint32_t rodataSize;
    uint32_t symbolCount;
    uint32_t codeRelocCount;
    uint32_t dataRelocCount;
    uint32_t importCount;
    uint32_t moduleNameOffset;
    uint32_t stringTableSize;
};

} // namespace tyl

#endif // TYL_OBJECT_FILE_H
