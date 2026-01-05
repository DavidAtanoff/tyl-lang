// Tyl Compiler - Module System Implementation
#include "module_system.h"
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser_base.h"
#include "common/errors.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace tyl {

std::string ModuleSystem::resolveModulePath(const std::string& moduleName, const std::string& fromFile) {
    // If it's already a file path (contains / or \ or ends with .fx/.flex)
    if (moduleName.find('/') != std::string::npos || 
        moduleName.find('\\') != std::string::npos ||
        strEndsWith(moduleName, ".tyl") || 
        strEndsWith(moduleName, ".flex")) {
        
        // Resolve relative to fromFile if provided
        if (!fromFile.empty()) {
            fs::path from(fromFile);
            fs::path resolved = from.parent_path() / moduleName;
            if (fs::exists(resolved)) {
                return resolved.string();
            }
        }
        
        // Try as-is
        if (fs::exists(moduleName)) {
            return moduleName;
        }
        
        // Try in search paths
        for (const auto& searchPath : searchPaths_) {
            fs::path candidate = fs::path(searchPath) / moduleName;
            if (fs::exists(candidate)) {
                return candidate.string();
            }
        }
        
        return "";
    }
    
    // Convert module name to path (math::calculus -> math/calculus.fx)
    std::string pathName = moduleNameToPath(moduleName);
    
    // Try relative to fromFile first
    if (!fromFile.empty()) {
        fs::path from(fromFile);
        fs::path resolved = from.parent_path() / pathName;
        if (fs::exists(resolved)) {
            return resolved.string();
        }
    }
    
    // Try in search paths
    for (const auto& searchPath : searchPaths_) {
        fs::path candidate = fs::path(searchPath) / pathName;
        if (fs::exists(candidate)) {
            return candidate.string();
        }
        
        // Also try without .fx extension (directory with mod.fx)
        fs::path dirCandidate = fs::path(searchPath) / moduleName;
        std::replace(dirCandidate.string().begin(), dirCandidate.string().end(), ':', '/');
        fs::path modFile = dirCandidate / "mod.tyl";
        if (fs::exists(modFile)) {
            return modFile.string();
        }
    }
    
    return "";
}

std::string ModuleSystem::pathToModuleName(const std::string& path) {
    fs::path p(path);
    std::string name = p.stem().string();  // Remove extension
    
    // Get parent directories as module path
    std::vector<std::string> parts;
    for (auto it = p.parent_path().begin(); it != p.parent_path().end(); ++it) {
        std::string part = it->string();
        if (part != "." && part != "..") {
            parts.push_back(part);
        }
    }
    parts.push_back(name);
    
    // Join with ::
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += "::";
        result += parts[i];
    }
    return result;
}

std::string ModuleSystem::moduleNameToPath(const std::string& name) {
    std::string path = name;
    // Replace :: with /
    size_t pos = 0;
    while ((pos = path.find("::", pos)) != std::string::npos) {
        path.replace(pos, 2, "/");
    }
    return path + ".tyl";
}

Module* ModuleSystem::loadModule(const std::string& name, const std::string& fromFile) {
    // Check if already loaded
    auto it = modules_.find(name);
    if (it != modules_.end()) {
        return it->second.get();
    }
    
    // Check for circular dependency
    if (loadStack_.count(name)) {
        // Build the cycle path for a clear error message
        std::string cyclePath = getCircularDependencyPath(name);
        errors_.push_back("Circular import detected: " + cyclePath);
        return nullptr;
    }
    
    // Resolve path
    std::string path = resolveModulePath(name, fromFile);
    if (path.empty()) {
        errors_.push_back("Cannot find module: " + name);
        return nullptr;
    }
    
    // Mark as loading and add to import chain
    loadStack_.insert(name);
    importChain_.push_back(name);
    
    // Create module
    auto mod = std::make_unique<Module>();
    mod->name = name;
    mod->path = path;
    
    // Parse the file
    try {
        mod->ast = parseFile(path);
        mod->isLoaded = true;
        
        // Extract exports
        extractExports(*mod);
        
        // Process imports in the loaded module
        processImports(*mod->ast, path);
        
    } catch (const std::exception& e) {
        errors_.push_back("Error loading module " + name + ": " + e.what());
        loadStack_.erase(name);
        importChain_.pop_back();
        return nullptr;
    }
    
    loadStack_.erase(name);
    importChain_.pop_back();
    
    Module* result = mod.get();
    modules_[name] = std::move(mod);
    return result;
}

std::string ModuleSystem::getCircularDependencyPath(const std::string& moduleName) const {
    // Find where the cycle starts in the import chain
    std::string cyclePath;
    bool inCycle = false;
    
    for (const auto& mod : importChain_) {
        if (mod == moduleName) {
            inCycle = true;
        }
        if (inCycle) {
            if (!cyclePath.empty()) {
                cyclePath += " -> ";
            }
            cyclePath += mod;
        }
    }
    
    // Complete the cycle by adding the module that caused it
    if (!cyclePath.empty()) {
        cyclePath += " -> " + moduleName;
    } else {
        cyclePath = moduleName + " -> " + moduleName;
    }
    
    return cyclePath;
}

Module* ModuleSystem::getModule(const std::string& name) {
    auto it = modules_.find(name);
    return it != modules_.end() ? it->second.get() : nullptr;
}

bool ModuleSystem::hasCircularDependency(const std::string& from, const std::string& to) {
    // Check if 'to' is currently being loaded (in the import chain)
    return loadStack_.count(to) > 0;
}

void ModuleSystem::processImports(Program& program, const std::string& currentFile) {
    std::vector<StmtPtr> newStatements;
    
    for (auto& stmt : program.statements) {
        if (auto* useStmt = dynamic_cast<UseStmt*>(stmt.get())) {
            std::string moduleName = useStmt->layerName;
            
            // Handle file imports (use "file.tyl")
            if (useStmt->isFileImport) {
                std::string importPath = resolveModulePath(moduleName, currentFile);
                
                if (importPath.empty()) {
                    std::string errorMsg = "Cannot find file: " + moduleName;
                    if (useStmt->location.line > 0) {
                        errorMsg += " (at line " + std::to_string(useStmt->location.line) + ")";
                    }
                    errors_.push_back(errorMsg);
                    continue;
                }
                
                // Convert to module name for circular dependency check
                std::string modName = pathToModuleName(importPath);
                
                // Check for circular dependency before loading
                if (hasCircularDependency(currentFile, modName)) {
                    std::string cyclePath = getCircularDependencyPath(modName);
                    std::string errorMsg = "Circular import detected: " + cyclePath;
                    if (useStmt->location.line > 0) {
                        errorMsg += "\n  at " + currentFile + ":" + std::to_string(useStmt->location.line);
                    }
                    errors_.push_back(errorMsg);
                    continue;
                }
                
                // Load the module
                Module* mod = loadModule(modName, currentFile);
                if (mod && mod->ast) {
                    // Add all statements from imported file
                    for (auto& importedStmt : mod->ast->statements) {
                        // Clone the statement (we need to keep original in module)
                        // For now, just skip - the symbols are available via module system
                    }
                }
                
                // Keep the use statement for later resolution
                newStatements.push_back(std::move(stmt));
            }
            // Handle qualified imports (use math::calculus)
            else if (moduleName.find("::") != std::string::npos) {
                // Check for circular dependency before loading
                if (hasCircularDependency(currentFile, moduleName)) {
                    std::string cyclePath = getCircularDependencyPath(moduleName);
                    std::string errorMsg = "Circular import detected: " + cyclePath;
                    if (useStmt->location.line > 0) {
                        errorMsg += "\n  at " + currentFile + ":" + std::to_string(useStmt->location.line);
                    }
                    errors_.push_back(errorMsg);
                    newStatements.push_back(std::move(stmt));
                    continue;
                }
                
                Module* mod = loadModule(moduleName, currentFile);
                if (!mod) {
                    std::string errorMsg = "Cannot load module: " + moduleName;
                    if (useStmt->location.line > 0) {
                        errorMsg += " (at line " + std::to_string(useStmt->location.line) + ")";
                    }
                    errors_.push_back(errorMsg);
                }
                newStatements.push_back(std::move(stmt));
            }
            // Handle layer imports (use layer "name")
            else if (useStmt->isLayer) {
                newStatements.push_back(std::move(stmt));
            }
            // Handle simple module imports (use math)
            else {
                // Check for circular dependency before loading
                if (hasCircularDependency(currentFile, moduleName)) {
                    std::string cyclePath = getCircularDependencyPath(moduleName);
                    std::string errorMsg = "Circular import detected: " + cyclePath;
                    if (useStmt->location.line > 0) {
                        errorMsg += "\n  at " + currentFile + ":" + std::to_string(useStmt->location.line);
                    }
                    errors_.push_back(errorMsg);
                    newStatements.push_back(std::move(stmt));
                    continue;
                }
                
                Module* mod = loadModule(moduleName, currentFile);
                if (!mod) {
                    // Try as file with .fx extension
                    mod = loadModule(moduleName + ".tyl", currentFile);
                }
                newStatements.push_back(std::move(stmt));
            }
        } else {
            newStatements.push_back(std::move(stmt));
        }
    }
    
    program.statements = std::move(newStatements);
}

std::vector<ModuleExport> ModuleSystem::getVisibleExports(const std::string& moduleName) {
    std::vector<ModuleExport> result;
    
    Module* mod = getModule(moduleName);
    if (!mod) return result;
    
    for (const auto& exp : mod->exports) {
        if (exp.isPublic) {
            result.push_back(exp);
        }
    }
    
    return result;
}

void ModuleSystem::registerModuleDecl(const std::string& name, const std::string& file) {
    moduleFiles_[name] = file;
}

void ModuleSystem::extractExports(Module& mod) {
    if (!mod.ast) return;
    
    for (const auto& stmt : mod.ast->statements) {
        // Functions
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            ModuleExport exp;
            exp.name = fn->name;
            exp.qualifiedName = mod.fullName() + "::" + fn->name;
            exp.isPublic = fn->isPublic;
            exp.kind = ModuleExport::Kind::Function;
            exp.location = fn->location;
            mod.exports.push_back(exp);
        }
        // Records
        else if (auto* rec = dynamic_cast<RecordDecl*>(stmt.get())) {
            ModuleExport exp;
            exp.name = rec->name;
            exp.qualifiedName = mod.fullName() + "::" + rec->name;
            exp.isPublic = rec->isPublic;
            exp.kind = ModuleExport::Kind::Record;
            exp.location = rec->location;
            mod.exports.push_back(exp);
        }
        // Enums
        else if (auto* en = dynamic_cast<EnumDecl*>(stmt.get())) {
            ModuleExport exp;
            exp.name = en->name;
            exp.qualifiedName = mod.fullName() + "::" + en->name;
            exp.isPublic = true;  // Enums are public by default
            exp.kind = ModuleExport::Kind::Enum;
            exp.location = en->location;
            mod.exports.push_back(exp);
        }
        // Constants (compile-time)
        else if (auto* var = dynamic_cast<VarDecl*>(stmt.get())) {
            if (var->isConst) {
                ModuleExport exp;
                exp.name = var->name;
                exp.qualifiedName = mod.fullName() + "::" + var->name;
                exp.isPublic = true;  // Constants are public by default
                exp.kind = ModuleExport::Kind::Constant;
                exp.location = var->location;
                mod.exports.push_back(exp);
            }
        }
        // Type aliases
        else if (auto* alias = dynamic_cast<TypeAlias*>(stmt.get())) {
            ModuleExport exp;
            exp.name = alias->name;
            exp.qualifiedName = mod.fullName() + "::" + alias->name;
            exp.isPublic = true;
            exp.kind = ModuleExport::Kind::Type;
            exp.location = alias->location;
            mod.exports.push_back(exp);
        }
    }
}

std::unique_ptr<Program> ModuleSystem::parseFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw TylError("Cannot open file: " + filename);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    // Cache source for error display
    SourceCache::instance().cacheSource(filename, source);
    
    Lexer lexer(source, filename);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    return parser.parse();
}

} // namespace tyl
