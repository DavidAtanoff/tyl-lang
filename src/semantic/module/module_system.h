// Tyl Compiler - Module System
// Handles namespaces, imports, exports, and dependency resolution
#ifndef TYL_MODULE_SYSTEM_H
#define TYL_MODULE_SYSTEM_H

#include "common/common.h"
#include "frontend/ast/ast.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace tyl {

// Helper for C++17 compatibility (ends_with is C++20)
inline bool strEndsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Represents an exported symbol from a module
struct ModuleExport {
    std::string name;           // Symbol name
    std::string qualifiedName;  // Full path: module::submodule::name
    bool isPublic = true;       // pub vs priv
    enum class Kind { Function, Record, Enum, Constant, Type, Module } kind;
    SourceLocation location;
};

// Represents a module (file or namespace)
struct Module {
    std::string name;                   // Module name (e.g., "math")
    std::string path;                   // File path if file-based
    std::string parentModule;           // Parent module name (for submodules)
    
    std::vector<ModuleExport> exports;  // Public exports
    std::unordered_set<std::string> imports;  // Modules this depends on
    std::unordered_map<std::string, std::unique_ptr<Module>> submodules;
    
    std::unique_ptr<Program> ast;       // Parsed AST (if loaded)
    bool isLoaded = false;
    bool isBuiltin = false;
    
    std::string fullName() const {
        return parentModule.empty() ? name : parentModule + "::" + name;
    }
};

// Module resolution and management
class ModuleSystem {
public:
    static ModuleSystem& instance() {
        static ModuleSystem sys;
        return sys;
    }
    
    // Add a search path for modules
    void addSearchPath(const std::string& path) {
        searchPaths_.push_back(path);
    }
    
    // Resolve a module path to a file
    std::string resolveModulePath(const std::string& moduleName, const std::string& fromFile = "");
    
    // Load a module by name or path
    Module* loadModule(const std::string& name, const std::string& fromFile = "");
    
    // Get a loaded module
    Module* getModule(const std::string& name);
    
    // Check for circular dependencies
    bool hasCircularDependency(const std::string& from, const std::string& to);
    
    // Process all imports in a program
    void processImports(Program& program, const std::string& currentFile);
    
    // Get all exported symbols visible from a module
    std::vector<ModuleExport> getVisibleExports(const std::string& moduleName);
    
    // Register a module declaration (module name:)
    void registerModuleDecl(const std::string& name, const std::string& file);
    
    // Clear all loaded modules (for fresh compilation)
    void clear() {
        modules_.clear();
        loadStack_.clear();
        importChain_.clear();
        moduleFiles_.clear();
        errors_.clear();
    }
    
    // Get errors
    const std::vector<std::string>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }
    void clearErrors() { errors_.clear(); }
    
    // Get the circular dependency cycle path (if any)
    std::string getCircularDependencyPath(const std::string& moduleName) const;
    
private:
    ModuleSystem() {
        // Add current directory as default search path
        searchPaths_.push_back(".");
    }
    
    std::vector<std::string> searchPaths_;
    std::unordered_map<std::string, std::unique_ptr<Module>> modules_;
    std::unordered_set<std::string> loadStack_;  // For circular dependency detection
    std::vector<std::string> importChain_;       // Ordered import chain for cycle path reporting
    std::unordered_map<std::string, std::string> moduleFiles_;  // module name -> file path
    std::vector<std::string> errors_;
    
    // Parse module name from path (e.g., "math/calculus.tyl" -> "math::calculus")
    std::string pathToModuleName(const std::string& path);
    
    // Convert module name to path (e.g., "math::calculus" -> "math/calculus.tyl")
    std::string moduleNameToPath(const std::string& name);
    
    // Extract exports from a parsed AST
    void extractExports(Module& mod);
    
    // Load and parse a file
    std::unique_ptr<Program> parseFile(const std::string& filename);
};

// ModuleDecl is defined in ast.h

} // namespace tyl

#endif // TYL_MODULE_SYSTEM_H
