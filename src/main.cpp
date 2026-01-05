// Tyl Compiler - Main Entry Point
#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser_base.h"
#include "semantic/expander/macro_expander.h"
#include "semantic/checker/type_checker.h"
#include "semantic/optimizer/optimizer.h"
#include "backend/codegen/native_codegen.h"
#include "backend/object/object_file.h"
#include "backend/linker/linker.h"
#include "cli/ast_printer.h"
#include "common/errors.h"
#include <fstream>
#include <sstream>
#include <set>
#include <filesystem>

using namespace tyl;
namespace fs = std::filesystem;

// Track imported files to avoid circular imports
std::set<std::string> importedFiles;
// Track the import chain for cycle path reporting
std::vector<std::string> importChain;

void printUsage(const char* prog) {
    std::cout << "Tyl Compiler v1.0\n";
    std::cout << "Usage: " << prog << " [options] <file.fx>\n";
    std::cout << "Options:\n";
    std::cout << "  -o <file>       Output file name\n";
    std::cout << "  -S, --obj       Compile to object file (.o/.obj)\n";
    std::cout << "  --dll           Compile to dynamic library (.dll)\n";
    std::cout << "  --def <file>    Use DEF file for DLL exports\n";
    std::cout << "  --implib        Generate import library (.lib) for DLL\n";
    std::cout << "  --export <sym>  Export symbol from DLL\n";
    std::cout << "  -l <file>       Link static library (.lib/.a) or object file (.o/.obj)\n";
    std::cout << "  --link          Link mode (combine .o files into .exe)\n";
    std::cout << "  -t, --tokens    Print tokens\n";
    std::cout << "  -a, --ast       Print AST\n";
    std::cout << "  -s, --asm       Print generated assembly\n";
    std::cout << "  -v, --verbose   Verbose output\n";
    std::cout << "  -O0             No optimization (fastest compile, debug friendly)\n";
    std::cout << "  -O1             Basic optimizations (constant folding, DCE)\n";
    std::cout << "  -O2             Standard optimizations (default)\n";
    std::cout << "  -O3             Aggressive optimizations (vectorization, more inlining)\n";
    std::cout << "  -Os             Optimize for size\n";
    std::cout << "  -Oz             Aggressive size optimization\n";
    std::cout << "  -Ofast          Maximum optimization (includes unsafe opts)\n";
    std::cout << "  --no-typecheck  Skip type checking (faster compile, less safe)\n";
    std::cout << "  --map           Generate map file\n";
    std::cout << "  -h, --help      Show this help\n";
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        auto diag = errors::cannotOpenFile(path);
        throw TylDiagnosticError(diag);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    // Cache source for error display
    std::string content = buffer.str();
    SourceCache::instance().cacheSource(path, content);
    return content;
}

// Resolve file path relative to the importing file
std::string resolveImportPath(const std::string& importPath, const std::string& currentFile) {
    fs::path current(currentFile);
    fs::path import(importPath);
    
    if (import.is_absolute()) {
        return import.string();
    }
    
    // Resolve relative to current file's directory
    fs::path resolved = current.parent_path() / import;
    return resolved.string();
}

// Parse a file and return its AST
std::unique_ptr<Program> parseFile(const std::string& filename);

// Process imports in an AST, recursively loading and merging imported files
void processImports(Program& program, const std::string& currentFile) {
    std::vector<StmtPtr> newStatements;
    
    for (auto& stmt : program.statements) {
        if (auto* useStmt = dynamic_cast<UseStmt*>(stmt.get())) {
            if (useStmt->isFileImport) {
                std::string importPath = resolveImportPath(useStmt->layerName, currentFile);
                
                // Normalize the path for consistent comparison
                try {
                    importPath = fs::canonical(importPath).string();
                } catch (...) {
                    // If canonical fails, use the resolved path as-is
                }
                
                // Check for circular imports - if file is in the current import chain
                bool isCircular = false;
                for (const auto& chainFile : importChain) {
                    if (chainFile == importPath) {
                        isCircular = true;
                        break;
                    }
                }
                
                if (isCircular) {
                    // Build the cycle path for a clear error message
                    std::string cyclePath;
                    bool inCycle = false;
                    for (const auto& chainFile : importChain) {
                        if (chainFile == importPath) {
                            inCycle = true;
                        }
                        if (inCycle) {
                            if (!cyclePath.empty()) {
                                cyclePath += " -> ";
                            }
                            // Extract just the filename for readability
                            fs::path p(chainFile);
                            cyclePath += p.filename().string();
                        }
                    }
                    cyclePath += " -> " + fs::path(importPath).filename().string();
                    
                    std::cerr << currentFile << ":" << useStmt->location.line << ": error: "
                              << "Circular import detected: " << cyclePath << "\n";
                    continue;
                }
                
                // Check if already imported (not circular, just already processed)
                if (importedFiles.count(importPath)) {
                    continue;  // Already imported, skip
                }
                importedFiles.insert(importPath);
                
                // Add to import chain before processing
                importChain.push_back(importPath);
                
                // Parse the imported file
                try {
                    auto importedAST = parseFile(importPath);
                    
                    // Process imports in the imported file recursively
                    processImports(*importedAST, importPath);
                    
                    // Add all statements from imported file
                    for (auto& importedStmt : importedAST->statements) {
                        newStatements.push_back(std::move(importedStmt));
                    }
                } catch (const TylDiagnosticError& e) {
                    e.render();
                } catch (const TylError& e) {
                    std::cerr << "Error importing '" << useStmt->layerName << "': " << e.what() << "\n";
                }
                
                // Remove from import chain after processing
                importChain.pop_back();
            } else {
                // Keep non-file use statements
                newStatements.push_back(std::move(stmt));
            }
        } else {
            newStatements.push_back(std::move(stmt));
        }
    }
    
    program.statements = std::move(newStatements);
}

std::unique_ptr<Program> parseFile(const std::string& filename) {
    std::string source = readFile(filename);
    Lexer lexer(source, filename);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    return parser.parse();
}

int main(int argc, char* argv[]) {
    bool showTokens = false;
    bool showAST = false;
    bool showAsm = false;
    bool compileObject = false;
    bool compileDll = false;
    bool linkMode = false;
    bool verbose = false;
    bool generateMap = false;
    bool generateImplib = false;
    bool skipTypeCheck = false;
    OptLevel optLevel = OptLevel::O2;
    std::string filename;
    std::string outputFile;
    std::string defFile;
    std::vector<std::string> objectFiles;
    std::vector<std::string> staticLibs;
    std::vector<std::string> exportSymbols;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-t" || arg == "--tokens") {
            showTokens = true;
        } else if (arg == "-a" || arg == "--ast") {
            showAST = true;
        } else if (arg == "-s" || arg == "--asm") {
            showAsm = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-S" || arg == "--obj") {
            compileObject = true;
        } else if (arg == "--dll") {
            compileDll = true;
        } else if (arg == "--def" && i + 1 < argc) {
            defFile = argv[++i];
        } else if (arg == "--implib") {
            generateImplib = true;
        } else if (arg == "--export" && i + 1 < argc) {
            exportSymbols.push_back(argv[++i]);
        } else if (arg == "--link") {
            linkMode = true;
        } else if (arg == "--map") {
            generateMap = true;
        } else if (arg == "--no-typecheck") {
            skipTypeCheck = true;
        } else if (arg == "-O0") {
            optLevel = OptLevel::O0;
        } else if (arg == "-O1") {
            optLevel = OptLevel::O1;
        } else if (arg == "-O" || arg == "-O2") {
            optLevel = OptLevel::O2;
        } else if (arg == "-O3") {
            optLevel = OptLevel::O3;
        } else if (arg == "-Os") {
            optLevel = OptLevel::Os;
        } else if (arg == "-Oz") {
            optLevel = OptLevel::Oz;
        } else if (arg == "-Ofast") {
            optLevel = OptLevel::Ofast;
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "-l" && i + 1 < argc) {
            std::string libArg = argv[++i];
            // Check if it's a static library (.lib/.a) or object file (.o/.obj)
            std::string ext;
            size_t dotPos = libArg.rfind('.');
            if (dotPos != std::string::npos) {
                ext = libArg.substr(dotPos);
                for (char& c : ext) c = (char)tolower(c);
            }
            if (ext == ".lib" || ext == ".a") {
                staticLibs.push_back(libArg);
            } else {
                objectFiles.push_back(libArg);
            }
        } else if (arg[0] != '-') {
            // Check file extension
            std::string ext;
            size_t dotPos = arg.rfind('.');
            if (dotPos != std::string::npos) {
                ext = arg.substr(dotPos);
                for (char& c : ext) c = (char)tolower(c);
            }
            if (ext == ".o" || ext == ".obj") {
                objectFiles.push_back(arg);
            } else if (ext == ".lib" || ext == ".a") {
                staticLibs.push_back(arg);
            } else {
                filename = arg;
            }
        }
    }
    
    // Link mode - combine object files (supports both EXE and DLL)
    if (linkMode || compileDll || (!filename.empty() && (filename.size() > 2 && 
        (filename.substr(filename.size() - 2) == ".o" || 
         filename.substr(filename.size() - 4) == ".obj")))) {
        if (objectFiles.empty() && !filename.empty()) {
            objectFiles.push_back(filename);
        }
        
        if (objectFiles.empty()) {
            std::cerr << "No object files to link\n";
            return 1;
        }
        
        if (outputFile.empty()) {
            outputFile = compileDll ? "a.dll" : "a.exe";
        }
        
        Linker linker;
        linker.config().outputFile = outputFile;
        linker.config().verbose = verbose;
        linker.config().generateMap = generateMap;
        linker.config().generateDll = compileDll;
        linker.config().generateImportLib = generateImplib;
        linker.config().defFile = defFile;
        linker.config().staticLibs = staticLibs;
        linker.config().exportSymbols = exportSymbols;
        
        for (auto& objFile : objectFiles) {
            if (!linker.addObjectFile(objFile)) {
                std::cerr << "Failed to load: " << objFile << "\n";
                return 1;
            }
        }
        
        if (linker.link()) {
            if (compileDll) {
                std::cout << "Created DLL: " << outputFile << "\n";
            } else {
                std::cout << "Linked: " << outputFile << "\n";
            }
            return 0;
        } else {
            std::cerr << "Link failed:\n";
            for (auto& err : linker.getErrors()) {
                std::cerr << "  " << err << "\n";
            }
            return 1;
        }
    }
    
    if (filename.empty()) {
        printUsage(argv[0]);
        return 1;
    }
    
    try {
        // Clear imported files set for fresh compilation
        importedFiles.clear();
        importChain.clear();
        
        // Normalize the main filename
        std::string normalizedFilename = filename;
        try {
            normalizedFilename = fs::canonical(filename).string();
        } catch (...) {
            // If canonical fails, use the filename as-is
        }
        
        importedFiles.insert(normalizedFilename);
        importChain.push_back(normalizedFilename);
        
        // Parse the main file
        auto ast = parseFile(filename);
        
        // Process imports (recursively loads and merges imported files)
        processImports(*ast, normalizedFilename);
        
        // Clear import chain after processing
        importChain.clear();
        
        // Re-lex for token display if needed
        if (showTokens) {
            std::string source = readFile(filename);
            Lexer lexer(source, filename);
            auto tokens = lexer.tokenize();
            printTokens(tokens);
        }
        
        // Macro expansion (before type checking)
        MacroExpander macroExpander;
        macroExpander.expand(*ast);
        
        if (macroExpander.hasErrors()) {
            for (const auto& err : macroExpander.getErrors()) {
                std::cerr << err << "\n";
            }
            return 1;
        }
        
        // Type checking (after macro expansion, before optimization)
        if (!skipTypeCheck) {
            TypeChecker typeChecker;
            bool typeCheckOk = typeChecker.check(*ast);
            
            // Report diagnostics
            for (const auto& diag : typeChecker.diagnostics()) {
                const char* levelStr = "note";
                if (diag.level == TypeDiagnostic::Level::ERROR) levelStr = "error";
                else if (diag.level == TypeDiagnostic::Level::WARNING) levelStr = "warning";
                
                std::cerr << diag.location.filename << ":" << diag.location.line << ":" 
                          << diag.location.column << ": " << levelStr << ": " 
                          << diag.message << "\n";
            }
            
            if (!typeCheckOk) {
                std::cerr << "Type checking failed\n";
                return 1;
            }
        }
        
        // Optimization passes
        if (optLevel != OptLevel::O0) {
            Optimizer optimizer;
            optimizer.setOptLevel(optLevel);
            optimizer.setVerbose(verbose);
            optimizer.optimize(*ast);
        }
        
        if (showAST) {
            std::cout << "=== AST ===\n";
            ASTPrinter printer;
            ast->accept(printer);
            std::cout << "\n";
        }
        
        if (compileObject) {
            // Compile to object file
            if (outputFile.empty()) {
                outputFile = filename;
                size_t dot = outputFile.rfind('.');
                if (dot != std::string::npos) {
                    outputFile = outputFile.substr(0, dot);
                }
                outputFile += ".o";
            }
            
            NativeCodeGen nativeCompiler;
            
            // Set codegen optimization level
            switch (optLevel) {
                case OptLevel::O0: nativeCompiler.setOptLevel(CodeGenOptLevel::O0); break;
                case OptLevel::O1: nativeCompiler.setOptLevel(CodeGenOptLevel::O1); break;
                case OptLevel::O2: nativeCompiler.setOptLevel(CodeGenOptLevel::O2); break;
                case OptLevel::O3: nativeCompiler.setOptLevel(CodeGenOptLevel::O3); break;
                case OptLevel::Os: nativeCompiler.setOptLevel(CodeGenOptLevel::Os); break;
                case OptLevel::Oz: nativeCompiler.setOptLevel(CodeGenOptLevel::Oz); break;
                case OptLevel::Ofast: nativeCompiler.setOptLevel(CodeGenOptLevel::Ofast); break;
            }
            
            if (nativeCompiler.compileToObject(*ast, outputFile)) {
                if (showAsm) {
                    nativeCompiler.dumpAssembly(std::cout);
                }
                std::cout << "Compiled to object file: " << outputFile << "\n";
            } else {
                std::cerr << "Failed to compile to object file\n";
                return 1;
            }
        } else {
            // Default: Native compilation to executable
            if (outputFile.empty()) {
                outputFile = filename;
                size_t dot = outputFile.rfind('.');
                if (dot != std::string::npos) {
                    outputFile = outputFile.substr(0, dot);
                }
                outputFile += ".exe";
            }
            
            NativeCodeGen nativeCompiler;
            
            // Set codegen optimization level
            switch (optLevel) {
                case OptLevel::O0: nativeCompiler.setOptLevel(CodeGenOptLevel::O0); break;
                case OptLevel::O1: nativeCompiler.setOptLevel(CodeGenOptLevel::O1); break;
                case OptLevel::O2: nativeCompiler.setOptLevel(CodeGenOptLevel::O2); break;
                case OptLevel::O3: nativeCompiler.setOptLevel(CodeGenOptLevel::O3); break;
                case OptLevel::Os: nativeCompiler.setOptLevel(CodeGenOptLevel::Os); break;
                case OptLevel::Oz: nativeCompiler.setOptLevel(CodeGenOptLevel::Oz); break;
                case OptLevel::Ofast: nativeCompiler.setOptLevel(CodeGenOptLevel::Ofast); break;
            }
            
            if (nativeCompiler.compile(*ast, outputFile)) {
                if (showAsm) {
                    nativeCompiler.dumpAssembly(std::cout);
                }
                std::cout << "Compiled to: " << outputFile << "\n";
            } else {
                std::cerr << "Failed to compile to native executable\n";
                return 1;
            }
        }
        
    } catch (const TylDiagnosticError& e) {
        e.render();
        return 1;
    } catch (const TylError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Internal error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
