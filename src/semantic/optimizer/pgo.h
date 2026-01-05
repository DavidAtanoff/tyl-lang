// Tyl Compiler - Profile-Guided Optimization (PGO)
// Collects and uses runtime profile data to guide optimization decisions
#ifndef TYL_PGO_H
#define TYL_PGO_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

namespace tyl {

// Profile data for a single function
struct FunctionProfile {
    std::string name;
    uint64_t callCount = 0;           // How many times function was called
    uint64_t totalCycles = 0;         // Total CPU cycles spent in function
    double avgCyclesPerCall = 0.0;    // Average cycles per call
    bool isHot = false;               // Frequently called (> threshold)
    bool isCold = false;              // Rarely called
    
    // Branch profile data
    struct BranchInfo {
        size_t lineNumber;
        uint64_t takenCount = 0;
        uint64_t notTakenCount = 0;
        double takenProbability() const {
            uint64_t total = takenCount + notTakenCount;
            return total > 0 ? static_cast<double>(takenCount) / total : 0.5;
        }
    };
    std::vector<BranchInfo> branches;
    
    // Loop profile data
    struct LoopInfo {
        size_t lineNumber;
        uint64_t iterationCount = 0;  // Total iterations across all executions
        uint64_t executionCount = 0;  // How many times loop was entered
        double avgIterations() const {
            return executionCount > 0 ? 
                   static_cast<double>(iterationCount) / executionCount : 0.0;
        }
    };
    std::vector<LoopInfo> loops;
    
    // Call site data (for inlining decisions)
    struct CallSiteInfo {
        std::string callee;
        size_t lineNumber;
        uint64_t callCount = 0;
    };
    std::vector<CallSiteInfo> callSites;
};

// Profile data for entire program
struct ProgramProfile {
    std::string programName;
    uint64_t totalExecutionTime = 0;  // Total cycles
    uint64_t totalInstructions = 0;   // Estimated instruction count
    std::map<std::string, FunctionProfile> functions;
    
    // Hot/cold thresholds (configurable)
    uint64_t hotThreshold = 1000;     // Calls > this = hot
    uint64_t coldThreshold = 10;      // Calls < this = cold
    double hotCyclePercent = 0.05;    // Functions using > 5% of cycles = hot
    
    // Computed statistics
    std::vector<std::string> hotFunctions;
    std::vector<std::string> coldFunctions;
    
    void computeStatistics();
};

// Profile data collector - generates instrumented code
class ProfileCollector {
public:
    ProfileCollector();
    
    // Instrument AST for profiling
    void instrument(Program& ast);
    
    // Generate profile data file format
    std::string generateProfileFormat() const;
    
    // Get instrumentation statistics
    int functionsInstrumented() const { return functionsInstrumented_; }
    int branchesInstrumented() const { return branchesInstrumented_; }
    int loopsInstrumented() const { return loopsInstrumented_; }
    
private:
    void instrumentFunction(FnDecl* fn);
    void instrumentStatement(StmtPtr& stmt, const std::string& funcName);
    void instrumentBranch(IfStmt* ifStmt, const std::string& funcName);
    void instrumentLoop(ForStmt* forStmt, const std::string& funcName);
    void instrumentLoop(WhileStmt* whileStmt, const std::string& funcName);
    
    int functionsInstrumented_ = 0;
    int branchesInstrumented_ = 0;
    int loopsInstrumented_ = 0;
    int counterIndex_ = 0;
};

// Profile data reader - loads profile from file
class ProfileReader {
public:
    ProfileReader();
    
    // Load profile from file
    bool load(const std::string& filename);
    
    // Load profile from binary format
    bool loadBinary(const std::string& filename);
    
    // Get loaded profile
    const ProgramProfile& profile() const { return profile_; }
    ProgramProfile& profile() { return profile_; }
    
    // Query functions
    bool isHotFunction(const std::string& name) const;
    bool isColdFunction(const std::string& name) const;
    uint64_t getCallCount(const std::string& name) const;
    double getBranchProbability(const std::string& func, size_t line) const;
    double getLoopIterations(const std::string& func, size_t line) const;
    
private:
    ProgramProfile profile_;
    bool loaded_ = false;
};

// Profile-Guided Optimization Pass
class PGOPass : public OptimizationPass {
public:
    PGOPass();
    
    void run(Program& ast) override;
    std::string name() const override { return "ProfileGuidedOptimization"; }
    
    // Set profile data
    void setProfile(const ProgramProfile& profile) { profile_ = profile; hasProfile_ = true; }
    void setProfile(ProgramProfile&& profile) { profile_ = std::move(profile); hasProfile_ = true; }
    
    // Load profile from file
    bool loadProfile(const std::string& filename);
    
    // Configuration
    void setInliningBias(double bias) { inliningBias_ = bias; }
    void setUnrollBias(double bias) { unrollBias_ = bias; }
    void enableBranchReordering(bool enable) { branchReordering_ = enable; }
    void enableColdCodeSeparation(bool enable) { coldCodeSeparation_ = enable; }
    
private:
    // PGO transformations
    void optimizeFunction(FnDecl* fn);
    void reorderBranches(IfStmt* ifStmt, const std::string& funcName);
    void adjustLoopUnrolling(ForStmt* forStmt, const std::string& funcName);
    void markHotColdFunctions(Program& ast);
    void adjustInliningDecisions(Program& ast);
    
    // Profile data
    ProgramProfile profile_;
    bool hasProfile_ = false;
    
    // Configuration
    double inliningBias_ = 2.0;       // Multiply inline threshold for hot call sites
    double unrollBias_ = 1.5;         // Multiply unroll factor for hot loops
    bool branchReordering_ = true;    // Reorder if/elif based on probability
    bool coldCodeSeparation_ = true;  // Mark cold functions for separate section
};

// Profile data writer - saves profile to file
class ProfileWriter {
public:
    // Write profile to text format
    static bool writeText(const ProgramProfile& profile, const std::string& filename);
    
    // Write profile to binary format (more compact)
    static bool writeBinary(const ProgramProfile& profile, const std::string& filename);
    
    // Generate C code for profile counters (for instrumentation)
    static std::string generateCounterCode(int numCounters);
};

// Factory functions
std::unique_ptr<PGOPass> createPGOPass();
std::unique_ptr<PGOPass> createPGOPass(const std::string& profileFile);

} // namespace tyl

#endif // TYL_PGO_H
