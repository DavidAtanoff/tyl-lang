// Tyl Compiler - Profile-Guided Optimization Implementation
// Uses runtime profile data to make better optimization decisions

#include "pgo.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstring>

namespace tyl {

// ============================================
// ProgramProfile Implementation
// ============================================

void ProgramProfile::computeStatistics() {
    hotFunctions.clear();
    coldFunctions.clear();
    
    // Calculate total cycles for percentage calculation
    uint64_t totalCycles = 0;
    for (const auto& [name, func] : functions) {
        totalCycles += func.totalCycles;
    }
    
    // Classify functions as hot or cold
    for (auto& [name, func] : functions) {
        // Hot if called frequently OR uses significant CPU time
        double cyclePercent = totalCycles > 0 ? 
            static_cast<double>(func.totalCycles) / totalCycles : 0.0;
        
        func.isHot = (func.callCount >= hotThreshold) || 
                     (cyclePercent >= hotCyclePercent);
        func.isCold = (func.callCount <= coldThreshold) && 
                      (cyclePercent < 0.001);  // < 0.1% of time
        
        if (func.isHot) {
            hotFunctions.push_back(name);
        } else if (func.isCold) {
            coldFunctions.push_back(name);
        }
        
        // Calculate average cycles per call
        if (func.callCount > 0) {
            func.avgCyclesPerCall = static_cast<double>(func.totalCycles) / func.callCount;
        }
    }
    
    // Sort hot functions by call count (most called first)
    std::sort(hotFunctions.begin(), hotFunctions.end(),
        [this](const std::string& a, const std::string& b) {
            return functions[a].callCount > functions[b].callCount;
        });
}

// ============================================
// ProfileCollector Implementation
// ============================================

ProfileCollector::ProfileCollector() {}

void ProfileCollector::instrument(Program& ast) {
    functionsInstrumented_ = 0;
    branchesInstrumented_ = 0;
    loopsInstrumented_ = 0;
    counterIndex_ = 0;
    
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            instrumentFunction(fn);
        }
    }
}

void ProfileCollector::instrumentFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    if (fn->isExtern) return;  // Can't instrument extern functions
    
    functionsInstrumented_++;
    
    // Instrument the function body
    instrumentStatement(fn->body, fn->name);
}

void ProfileCollector::instrumentStatement(StmtPtr& stmt, const std::string& funcName) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        for (auto& s : block->statements) {
            instrumentStatement(s, funcName);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        instrumentBranch(ifStmt, funcName);
        instrumentStatement(ifStmt->thenBranch, funcName);
        for (auto& elif : ifStmt->elifBranches) {
            instrumentStatement(elif.second, funcName);
        }
        instrumentStatement(ifStmt->elseBranch, funcName);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        instrumentLoop(forStmt, funcName);
        instrumentStatement(forStmt->body, funcName);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        instrumentLoop(whileStmt, funcName);
        instrumentStatement(whileStmt->body, funcName);
    }
}

void ProfileCollector::instrumentBranch(IfStmt* ifStmt, const std::string& funcName) {
    (void)ifStmt;
    (void)funcName;
    branchesInstrumented_++;
    counterIndex_++;
    // In a full implementation, we would insert counter increment code
    // at the start of each branch
}

void ProfileCollector::instrumentLoop(ForStmt* forStmt, const std::string& funcName) {
    (void)forStmt;
    (void)funcName;
    loopsInstrumented_++;
    counterIndex_++;
    // In a full implementation, we would insert iteration counter code
}

void ProfileCollector::instrumentLoop(WhileStmt* whileStmt, const std::string& funcName) {
    (void)whileStmt;
    (void)funcName;
    loopsInstrumented_++;
    counterIndex_++;
}

std::string ProfileCollector::generateProfileFormat() const {
    std::ostringstream ss;
    ss << "# Tyl Profile Data Format v1.0\n";
    ss << "# Counters: " << counterIndex_ << "\n";
    ss << "# Functions: " << functionsInstrumented_ << "\n";
    ss << "# Branches: " << branchesInstrumented_ << "\n";
    ss << "# Loops: " << loopsInstrumented_ << "\n";
    return ss.str();
}

// ============================================
// ProfileReader Implementation
// ============================================

ProfileReader::ProfileReader() {}

bool ProfileReader::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    
    std::string line;
    FunctionProfile* currentFunc = nullptr;
    
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        // Parse function entry: "FUNC name callCount totalCycles"
        if (line.substr(0, 5) == "FUNC ") {
            std::istringstream iss(line.substr(5));
            std::string name;
            uint64_t calls, cycles;
            if (iss >> name >> calls >> cycles) {
                profile_.functions[name] = FunctionProfile{};
                profile_.functions[name].name = name;
                profile_.functions[name].callCount = calls;
                profile_.functions[name].totalCycles = cycles;
                currentFunc = &profile_.functions[name];
            }
        }
        // Parse branch entry: "BRANCH line taken notTaken"
        else if (line.substr(0, 7) == "BRANCH " && currentFunc) {
            std::istringstream iss(line.substr(7));
            size_t lineNum;
            uint64_t taken, notTaken;
            if (iss >> lineNum >> taken >> notTaken) {
                FunctionProfile::BranchInfo branch;
                branch.lineNumber = lineNum;
                branch.takenCount = taken;
                branch.notTakenCount = notTaken;
                currentFunc->branches.push_back(branch);
            }
        }
        // Parse loop entry: "LOOP line iterations executions"
        else if (line.substr(0, 5) == "LOOP " && currentFunc) {
            std::istringstream iss(line.substr(5));
            size_t lineNum;
            uint64_t iters, execs;
            if (iss >> lineNum >> iters >> execs) {
                FunctionProfile::LoopInfo loop;
                loop.lineNumber = lineNum;
                loop.iterationCount = iters;
                loop.executionCount = execs;
                currentFunc->loops.push_back(loop);
            }
        }
        // Parse call site: "CALL callee line count"
        else if (line.substr(0, 5) == "CALL " && currentFunc) {
            std::istringstream iss(line.substr(5));
            std::string callee;
            size_t lineNum;
            uint64_t count;
            if (iss >> callee >> lineNum >> count) {
                FunctionProfile::CallSiteInfo callSite;
                callSite.callee = callee;
                callSite.lineNumber = lineNum;
                callSite.callCount = count;
                currentFunc->callSites.push_back(callSite);
            }
        }
    }
    
    profile_.computeStatistics();
    loaded_ = true;
    return true;
}

bool ProfileReader::loadBinary(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Read magic number
    char magic[4];
    file.read(magic, 4);
    if (std::memcmp(magic, "FXPF", 4) != 0) return false;
    
    // Read version
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) return false;
    
    // Read function count
    uint32_t funcCount;
    file.read(reinterpret_cast<char*>(&funcCount), sizeof(funcCount));
    
    for (uint32_t i = 0; i < funcCount; ++i) {
        // Read function name length and name
        uint32_t nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        std::string name(nameLen, '\0');
        file.read(&name[0], nameLen);
        
        // Read call count and cycles
        uint64_t calls, cycles;
        file.read(reinterpret_cast<char*>(&calls), sizeof(calls));
        file.read(reinterpret_cast<char*>(&cycles), sizeof(cycles));
        
        profile_.functions[name] = FunctionProfile{};
        profile_.functions[name].name = name;
        profile_.functions[name].callCount = calls;
        profile_.functions[name].totalCycles = cycles;
        
        // Read branch count and data
        uint32_t branchCount;
        file.read(reinterpret_cast<char*>(&branchCount), sizeof(branchCount));
        for (uint32_t j = 0; j < branchCount; ++j) {
            FunctionProfile::BranchInfo branch;
            file.read(reinterpret_cast<char*>(&branch.lineNumber), sizeof(branch.lineNumber));
            file.read(reinterpret_cast<char*>(&branch.takenCount), sizeof(branch.takenCount));
            file.read(reinterpret_cast<char*>(&branch.notTakenCount), sizeof(branch.notTakenCount));
            profile_.functions[name].branches.push_back(branch);
        }
        
        // Read loop count and data
        uint32_t loopCount;
        file.read(reinterpret_cast<char*>(&loopCount), sizeof(loopCount));
        for (uint32_t j = 0; j < loopCount; ++j) {
            FunctionProfile::LoopInfo loop;
            file.read(reinterpret_cast<char*>(&loop.lineNumber), sizeof(loop.lineNumber));
            file.read(reinterpret_cast<char*>(&loop.iterationCount), sizeof(loop.iterationCount));
            file.read(reinterpret_cast<char*>(&loop.executionCount), sizeof(loop.executionCount));
            profile_.functions[name].loops.push_back(loop);
        }
    }
    
    profile_.computeStatistics();
    loaded_ = true;
    return true;
}

bool ProfileReader::isHotFunction(const std::string& name) const {
    auto it = profile_.functions.find(name);
    return it != profile_.functions.end() && it->second.isHot;
}

bool ProfileReader::isColdFunction(const std::string& name) const {
    auto it = profile_.functions.find(name);
    return it != profile_.functions.end() && it->second.isCold;
}

uint64_t ProfileReader::getCallCount(const std::string& name) const {
    auto it = profile_.functions.find(name);
    return it != profile_.functions.end() ? it->second.callCount : 0;
}

double ProfileReader::getBranchProbability(const std::string& func, size_t line) const {
    auto it = profile_.functions.find(func);
    if (it == profile_.functions.end()) return 0.5;
    
    for (const auto& branch : it->second.branches) {
        if (branch.lineNumber == line) {
            return branch.takenProbability();
        }
    }
    return 0.5;  // Default: 50/50
}

double ProfileReader::getLoopIterations(const std::string& func, size_t line) const {
    auto it = profile_.functions.find(func);
    if (it == profile_.functions.end()) return 0.0;
    
    for (const auto& loop : it->second.loops) {
        if (loop.lineNumber == line) {
            return loop.avgIterations();
        }
    }
    return 0.0;
}

// ============================================
// PGOPass Implementation
// ============================================

PGOPass::PGOPass() {}

void PGOPass::run(Program& ast) {
    transformations_ = 0;
    
    if (!hasProfile_) {
        // No profile data - nothing to do
        return;
    }
    
    // Phase 1: Mark hot/cold functions
    markHotColdFunctions(ast);
    
    // Phase 2: Adjust inlining decisions based on call site frequency
    adjustInliningDecisions(ast);
    
    // Phase 3: Optimize individual functions
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            optimizeFunction(fn);
        }
    }
}

void PGOPass::markHotColdFunctions(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            auto it = profile_.functions.find(fn->name);
            if (it != profile_.functions.end()) {
                // Store hot/cold info in function metadata
                // This can be used by code generator for layout decisions
                if (it->second.isHot) {
                    fn->isHot = true;
                    transformations_++;
                }
                if (it->second.isCold) {
                    fn->isCold = true;
                    transformations_++;
                }
            }
        }
    }
}

void PGOPass::adjustInliningDecisions(Program& ast) {
    // Mark call sites with their frequency for the inlining pass
    std::function<void(Statement*, const std::string&)> markCallSites = 
        [&](Statement* stmt, const std::string& funcName) {
        if (!stmt) return;
        
        if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                markCallSites(s.get(), funcName);
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            // Check for call expressions
            if (auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
                if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                    // Look up call site frequency
                    auto funcIt = profile_.functions.find(funcName);
                    if (funcIt != profile_.functions.end()) {
                        for (const auto& site : funcIt->second.callSites) {
                            if (site.callee == callee->name) {
                                // Mark as hot call site if frequently called
                                if (site.callCount >= profile_.hotThreshold) {
                                    call->isHotCallSite = true;
                                    transformations_++;
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            // Check initializer for calls
            if (auto* call = dynamic_cast<CallExpr*>(varDecl->initializer.get())) {
                if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                    auto funcIt = profile_.functions.find(funcName);
                    if (funcIt != profile_.functions.end()) {
                        for (const auto& site : funcIt->second.callSites) {
                            if (site.callee == callee->name && 
                                site.callCount >= profile_.hotThreshold) {
                                call->isHotCallSite = true;
                                transformations_++;
                            }
                        }
                    }
                }
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            markCallSites(ifStmt->thenBranch.get(), funcName);
            for (auto& elif : ifStmt->elifBranches) {
                markCallSites(elif.second.get(), funcName);
            }
            markCallSites(ifStmt->elseBranch.get(), funcName);
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
            markCallSites(forStmt->body.get(), funcName);
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            markCallSites(whileStmt->body.get(), funcName);
        }
    };
    
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            markCallSites(fn->body.get(), fn->name);
        }
    }
}

void PGOPass::optimizeFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    auto funcIt = profile_.functions.find(fn->name);
    if (funcIt == profile_.functions.end()) return;
    
    const FunctionProfile& funcProfile = funcIt->second;
    
    // Optimize branches and loops in function body
    std::function<void(Statement*)> optimize = [&](Statement* stmt) {
        if (!stmt) return;
        
        if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                optimize(s.get());
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            if (branchReordering_) {
                reorderBranches(ifStmt, fn->name);
            }
            optimize(ifStmt->thenBranch.get());
            for (auto& elif : ifStmt->elifBranches) {
                optimize(elif.second.get());
            }
            optimize(ifStmt->elseBranch.get());
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
            adjustLoopUnrolling(forStmt, fn->name);
            optimize(forStmt->body.get());
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            optimize(whileStmt->body.get());
        }
    };
    
    optimize(fn->body.get());
}

void PGOPass::reorderBranches(IfStmt* ifStmt, const std::string& funcName) {
    if (!ifStmt || ifStmt->elifBranches.empty()) return;
    
    auto funcIt = profile_.functions.find(funcName);
    if (funcIt == profile_.functions.end()) return;
    
    // Find branch probabilities for elif branches
    std::vector<std::pair<size_t, double>> branchProbs;
    
    for (size_t i = 0; i < ifStmt->elifBranches.size(); ++i) {
        size_t line = ifStmt->elifBranches[i].first->location.line;
        double prob = 0.5;
        
        for (const auto& branch : funcIt->second.branches) {
            if (branch.lineNumber == line) {
                prob = branch.takenProbability();
                break;
            }
        }
        branchProbs.push_back({i, prob});
    }
    
    // Sort by probability (highest first)
    std::sort(branchProbs.begin(), branchProbs.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Check if reordering would help
    bool needsReorder = false;
    for (size_t i = 0; i < branchProbs.size(); ++i) {
        if (branchProbs[i].first != i) {
            needsReorder = true;
            break;
        }
    }
    
    if (needsReorder) {
        // Reorder elif branches
        std::vector<std::pair<ExprPtr, StmtPtr>> newElifs;
        newElifs.reserve(ifStmt->elifBranches.size());
        
        for (const auto& [idx, prob] : branchProbs) {
            newElifs.push_back(std::move(ifStmt->elifBranches[idx]));
        }
        
        ifStmt->elifBranches = std::move(newElifs);
        transformations_++;
    }
}

void PGOPass::adjustLoopUnrolling(ForStmt* forStmt, const std::string& funcName) {
    auto funcIt = profile_.functions.find(funcName);
    if (funcIt == profile_.functions.end()) return;
    
    size_t line = forStmt->location.line;
    
    for (const auto& loop : funcIt->second.loops) {
        if (loop.lineNumber == line) {
            double avgIters = loop.avgIterations();
            
            // Store hint for loop optimizer
            if (avgIters > 100) {
                // Hot loop - suggest more aggressive unrolling
                forStmt->unrollHint = static_cast<int>(unrollBias_ * 4);
                transformations_++;
            } else if (avgIters < 4) {
                // Small loop - suggest full unrolling
                forStmt->unrollHint = static_cast<int>(avgIters);
                transformations_++;
            }
            break;
        }
    }
}

bool PGOPass::loadProfile(const std::string& filename) {
    ProfileReader reader;
    if (reader.load(filename)) {
        profile_ = reader.profile();
        hasProfile_ = true;
        return true;
    }
    return false;
}

// ============================================
// ProfileWriter Implementation
// ============================================

bool ProfileWriter::writeText(const ProgramProfile& profile, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << "# Tyl Profile Data v1.0\n";
    file << "# Program: " << profile.programName << "\n";
    file << "# Total execution time: " << profile.totalExecutionTime << " cycles\n";
    file << "# Hot functions: " << profile.hotFunctions.size() << "\n";
    file << "# Cold functions: " << profile.coldFunctions.size() << "\n\n";
    
    for (const auto& [name, func] : profile.functions) {
        file << "FUNC " << name << " " << func.callCount << " " << func.totalCycles << "\n";
        
        for (const auto& branch : func.branches) {
            file << "BRANCH " << branch.lineNumber << " " 
                 << branch.takenCount << " " << branch.notTakenCount << "\n";
        }
        
        for (const auto& loop : func.loops) {
            file << "LOOP " << loop.lineNumber << " "
                 << loop.iterationCount << " " << loop.executionCount << "\n";
        }
        
        for (const auto& call : func.callSites) {
            file << "CALL " << call.callee << " " << call.lineNumber << " "
                 << call.callCount << "\n";
        }
        
        file << "\n";
    }
    
    return true;
}

bool ProfileWriter::writeBinary(const ProgramProfile& profile, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Write magic number
    file.write("FXPF", 4);
    
    // Write version
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write function count
    uint32_t funcCount = static_cast<uint32_t>(profile.functions.size());
    file.write(reinterpret_cast<const char*>(&funcCount), sizeof(funcCount));
    
    for (const auto& [name, func] : profile.functions) {
        // Write function name
        uint32_t nameLen = static_cast<uint32_t>(name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(name.c_str(), nameLen);
        
        // Write call count and cycles
        file.write(reinterpret_cast<const char*>(&func.callCount), sizeof(func.callCount));
        file.write(reinterpret_cast<const char*>(&func.totalCycles), sizeof(func.totalCycles));
        
        // Write branches
        uint32_t branchCount = static_cast<uint32_t>(func.branches.size());
        file.write(reinterpret_cast<const char*>(&branchCount), sizeof(branchCount));
        for (const auto& branch : func.branches) {
            file.write(reinterpret_cast<const char*>(&branch.lineNumber), sizeof(branch.lineNumber));
            file.write(reinterpret_cast<const char*>(&branch.takenCount), sizeof(branch.takenCount));
            file.write(reinterpret_cast<const char*>(&branch.notTakenCount), sizeof(branch.notTakenCount));
        }
        
        // Write loops
        uint32_t loopCount = static_cast<uint32_t>(func.loops.size());
        file.write(reinterpret_cast<const char*>(&loopCount), sizeof(loopCount));
        for (const auto& loop : func.loops) {
            file.write(reinterpret_cast<const char*>(&loop.lineNumber), sizeof(loop.lineNumber));
            file.write(reinterpret_cast<const char*>(&loop.iterationCount), sizeof(loop.iterationCount));
            file.write(reinterpret_cast<const char*>(&loop.executionCount), sizeof(loop.executionCount));
        }
    }
    
    return true;
}

std::string ProfileWriter::generateCounterCode(int numCounters) {
    std::ostringstream ss;
    ss << "// Auto-generated profile counters\n";
    ss << "static uint64_t __TYL_profile_counters[" << numCounters << "] = {0};\n\n";
    ss << "void __TYL_profile_increment(int idx) {\n";
    ss << "    __TYL_profile_counters[idx]++;\n";
    ss << "}\n\n";
    ss << "void __TYL_profile_dump(const char* filename) {\n";
    ss << "    FILE* f = fopen(filename, \"wb\");\n";
    ss << "    if (f) {\n";
    ss << "        fwrite(__TYL_profile_counters, sizeof(uint64_t), " << numCounters << ", f);\n";
    ss << "        fclose(f);\n";
    ss << "    }\n";
    ss << "}\n";
    return ss.str();
}

// ============================================
// Factory Functions
// ============================================

std::unique_ptr<PGOPass> createPGOPass() {
    return std::make_unique<PGOPass>();
}

std::unique_ptr<PGOPass> createPGOPass(const std::string& profileFile) {
    auto pass = std::make_unique<PGOPass>();
    pass->loadProfile(profileFile);
    return pass;
}

} // namespace tyl
