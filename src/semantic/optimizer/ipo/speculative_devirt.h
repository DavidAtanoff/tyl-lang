// Tyl Compiler - Speculative Devirtualization Pass
// Converts virtual/trait method calls to direct calls when the type is known
#ifndef TYL_SPECULATIVE_DEVIRT_H
#define TYL_SPECULATIVE_DEVIRT_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>

namespace tyl {

// Information about a trait method implementation
struct TraitMethodImpl {
    std::string traitName;
    std::string methodName;
    std::string implType;        // The concrete type implementing the trait
    FnDecl* implementation = nullptr;
};

// Information about a virtual call site
struct VirtualCallSite {
    CallExpr* call = nullptr;
    MemberExpr* memberAccess = nullptr;
    std::string methodName;
    std::string receiverType;    // Static type of the receiver (if known)
    std::string inferredType;    // Inferred concrete type (if determinable)
    bool canDevirtualize = false;
    bool isMonomorphic = false;  // Only one possible implementation
    std::vector<std::string> possibleTypes;  // All possible concrete types
};

// Information about type usage patterns
struct TypeUsageInfo {
    std::string typeName;
    std::set<std::string> implementedTraits;
    std::set<std::string> calledMethods;
    int constructionCount = 0;   // How many times this type is constructed
    bool isSealed = false;       // No subtypes possible
};

// Statistics for Speculative Devirtualization
struct SpeculativeDevirtStats {
    int virtualCallsAnalyzed = 0;
    int callsDevirtualized = 0;
    int speculativeGuardsInserted = 0;
    int monomorphicSites = 0;
    int polymorphicSites = 0;
};

// Speculative Devirtualization Pass
// Converts virtual/trait method calls to direct calls:
// 1. Single Implementation: If only one type implements a trait method, devirtualize
// 2. Type Inference: If the concrete type can be inferred, devirtualize
// 3. Speculative: Insert type check + direct call with fallback to virtual call
//
// Example transformation:
//   trait Drawable { fn draw(self); }
//   record Circle { ... }
//   impl Drawable for Circle { fn draw(self) { ... } }
//
//   fn render(d: &Drawable) {
//       d.draw();  // Virtual call
//   }
//
// After devirtualization (if Circle is the only Drawable):
//   fn render(d: &Drawable) {
//       Circle::draw(d as &Circle);  // Direct call
//   }
//
// Or with speculative guard:
//   fn render(d: &Drawable) {
//       if type_of(d) == Circle {
//           Circle::draw(d as &Circle);  // Fast path
//       } else {
//           d.draw();  // Fallback
//       }
//   }
class SpeculativeDevirtPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "SpeculativeDevirt"; }
    
    // Get detailed statistics
    const SpeculativeDevirtStats& stats() const { return stats_; }
    
    // Configuration
    void setEnableSpeculativeGuards(bool enable) { enableSpeculativeGuards_ = enable; }
    void setMinCallFrequency(int freq) { minCallFrequency_ = freq; }
    
private:
    SpeculativeDevirtStats stats_;
    
    // Trait information
    std::map<std::string, TraitDecl*> traits_;
    
    // Type information
    std::map<std::string, TypeUsageInfo> types_;
    
    // Trait implementations: trait::method -> list of implementations
    std::map<std::string, std::vector<TraitMethodImpl>> traitImpls_;
    
    // Virtual call sites found
    std::vector<VirtualCallSite> virtualCalls_;
    
    // Configuration
    bool enableSpeculativeGuards_ = true;
    int minCallFrequency_ = 1;
    
    // === Phase 1: Collection ===
    
    // Collect all traits
    void collectTraits(Program& ast);
    
    // Collect all types and their trait implementations
    void collectTypes(Program& ast);
    
    // Collect trait implementations
    void collectImplementations(Program& ast);
    
    // === Phase 2: Analysis ===
    
    // Find all virtual call sites
    void findVirtualCalls(Program& ast);
    void findVirtualCallsInStmt(Statement* stmt);
    void findVirtualCallsInExpr(Expression* expr);
    
    // Analyze each virtual call site
    void analyzeVirtualCalls();
    
    // Check if a call site is monomorphic (single implementation)
    bool isMonomorphicCallSite(VirtualCallSite& site);
    
    // Try to infer the concrete type at a call site
    std::string inferConcreteType(VirtualCallSite& site);
    
    // === Phase 3: Transformation ===
    
    // Apply devirtualization transformations
    void applyDevirtualization(Program& ast);
    
    // Devirtualize a single call site
    void devirtualizeCall(VirtualCallSite& site);
    
    // Create a direct call to the concrete implementation
    ExprPtr createDirectCall(VirtualCallSite& site, const TraitMethodImpl& impl);
    
    // Create a speculative guard with direct call and fallback
    StmtPtr createSpeculativeGuard(VirtualCallSite& site, const TraitMethodImpl& impl);
    
    // Replace virtual calls in statements
    void replaceVirtualCallsInStmt(StmtPtr& stmt);
    void replaceVirtualCallsInExpr(ExprPtr& expr);
    
    // Helper: Get the mangled name for a trait method implementation
    std::string getMangledMethodName(const std::string& typeName, 
                                     const std::string& traitName,
                                     const std::string& methodName);
};

} // namespace tyl

#endif // TYL_SPECULATIVE_DEVIRT_H
