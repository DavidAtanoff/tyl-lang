// Tyl Compiler - Linker Symbol Collection and Resolution

#include "linker_base.h"
#include <iostream>

namespace tyl {

bool Linker::collectSymbols() {
    if (config_.verbose) {
        std::cout << "Phase 1: Collecting symbols...\n";
    }
    
    for (size_t i = 0; i < objects_.size(); i++) {
        auto& obj = objects_[i];
        
        for (auto& imp : obj.imports) {
            collectedImports_[imp.dll].insert(imp.function);
        }
        
        for (auto& sym : obj.symbols) {
            if (sym.type == ObjSymbolType::UNDEFINED) continue;
            if (!sym.isExported) continue;
            
            // Skip hidden symbols for global symbol table (they're module-local)
            if (sym.isHidden) continue;
            
            auto it = globalSymbols_.find(sym.name);
            if (it != globalSymbols_.end()) {
                // Handle weak symbols - weak can be overridden by strong
                if (sym.isWeak && !it->second.isWeak) {
                    // Existing symbol is strong, skip this weak one
                    continue;
                } else if (!sym.isWeak && it->second.isWeak) {
                    // New symbol is strong, override the weak one
                    // Fall through to replace
                } else {
                    // Both strong or both weak - duplicate error
                    error("Duplicate symbol: " + sym.name + " (in " + obj.moduleName + 
                          " and " + it->second.sourceModule + ")");
                    return false;
                }
            }
            
            LinkedSymbol linked;
            linked.name = sym.name;
            linked.type = sym.type;
            linked.size = sym.size;
            linked.sourceModule = obj.moduleName;
            linked.rva = 0;
            linked.isExported = sym.isExported;
            linked.isHidden = sym.isHidden;
            linked.isWeak = sym.isWeak;
            globalSymbols_[sym.name] = linked;
            
            if (config_.verbose) {
                std::cout << "  Symbol: " << sym.name << " from " << obj.moduleName;
                if (sym.isWeak) std::cout << " [weak]";
                std::cout << "\n";
            }
        }
    }
    
    return true;
}

bool Linker::resolveSymbols() {
    if (config_.verbose) {
        std::cout << "Phase 2: Resolving symbols...\n";
    }
    
    for (auto& obj : objects_) {
        for (auto& rel : obj.codeRelocations) {
            // Skip empty symbol names - these are internal relocations
            if (rel.symbol.empty()) continue;
            
            // Skip special section symbols - handled by linker
            if (rel.symbol == "__data" || rel.symbol == "__idata") continue;
            
            // Skip import symbols - handled by linker
            if (rel.symbol.substr(0, 9) == "__import_") continue;
            
            if (globalSymbols_.find(rel.symbol) != globalSymbols_.end()) continue;
            
            bool isImport = false;
            for (auto& [dll, funcs] : collectedImports_) {
                if (funcs.count(rel.symbol)) {
                    isImport = true;
                    break;
                }
            }
            if (isImport) continue;
            
            if (obj.findSymbol(rel.symbol)) continue;
            
            error("Undefined symbol: " + rel.symbol + " (referenced in " + obj.moduleName + ")");
            return false;
        }
    }
    
    // Check for entry point - try common names
    bool hasEntryPoint = false;
    if (globalSymbols_.find(config_.entryPoint) != globalSymbols_.end()) {
        hasEntryPoint = true;
    } else if (globalSymbols_.find("_start") != globalSymbols_.end()) {
        config_.entryPoint = "_start";
        hasEntryPoint = true;
    } else if (globalSymbols_.find("main") != globalSymbols_.end()) {
        config_.entryPoint = "main";
        hasEntryPoint = true;
    } else if (globalSymbols_.find("__TYL_main") != globalSymbols_.end()) {
        config_.entryPoint = "__TYL_main";
        hasEntryPoint = true;
    }
    
    // If no standard entry point found, use the first exported function
    if (!hasEntryPoint && !globalSymbols_.empty()) {
        for (auto& [name, sym] : globalSymbols_) {
            if (sym.type == ObjSymbolType::FUNCTION) {
                config_.entryPoint = name;
                hasEntryPoint = true;
                if (config_.verbose) {
                    std::cout << "  Using entry point: " << name << "\n";
                }
                break;
            }
        }
    }
    
    if (!hasEntryPoint) {
        error("Entry point not found: " + config_.entryPoint);
        return false;
    }
    
    return true;
}

} // namespace tyl
