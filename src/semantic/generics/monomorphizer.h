// Tyl Compiler - Monomorphization for Generics
// Generates specialized code for each concrete type instantiation
#ifndef TYL_MONOMORPHIZER_H
#define TYL_MONOMORPHIZER_H

#include "frontend/ast/ast.h"
#include "semantic/types/types.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace tyl {

// Represents a specific instantiation of a generic function/type
struct GenericInstantiation {
    std::string baseName;                          // Original generic name
    std::vector<TypePtr> typeArgs;                 // Concrete type arguments
    std::string mangledName;                       // Mangled name for this instantiation
    std::string returnType;                        // Concrete return type after substitution
    
    bool operator==(const GenericInstantiation& other) const;
    
    // Check if this instantiation returns a float
    bool returnsFloat() const { return returnType == "float"; }
    
    // Check if this instantiation returns a string
    bool returnsString() const { return returnType == "string" || returnType == "str"; }
};

// Hash function for GenericInstantiation
struct GenericInstantiationHash {
    size_t operator()(const GenericInstantiation& inst) const;
};

// Tracks all generic instantiations needed during compilation
class Monomorphizer {
public:
    Monomorphizer() = default;
    
    // Record a generic function instantiation
    void recordFunctionInstantiation(const std::string& fnName, 
                                     const std::vector<TypePtr>& typeArgs,
                                     FnDecl* originalDecl);
    
    // Record a generic record instantiation
    void recordRecordInstantiation(const std::string& recordName,
                                   const std::vector<TypePtr>& typeArgs,
                                   RecordDecl* originalDecl);
    
    // Get mangled name for an instantiation
    std::string getMangledName(const std::string& baseName, 
                               const std::vector<TypePtr>& typeArgs) const;
    
    // Check if an instantiation already exists
    bool hasInstantiation(const std::string& baseName,
                          const std::vector<TypePtr>& typeArgs) const;
    
    // Get all function instantiations
    const std::vector<std::pair<GenericInstantiation, FnDecl*>>& getFunctionInstantiations() const {
        return functionInstantiations_;
    }
    
    // Get all record instantiations
    const std::vector<std::pair<GenericInstantiation, RecordDecl*>>& getRecordInstantiations() const {
        return recordInstantiations_;
    }
    
    // Check if a mangled function name returns float
    bool functionReturnsFloat(const std::string& mangledName) const;
    
    // Check if a mangled function name returns string
    bool functionReturnsString(const std::string& mangledName) const;
    
    // Get the return type for a mangled function name
    std::string getFunctionReturnType(const std::string& mangledName) const;
    
    // Create a specialized copy of a function declaration
    std::unique_ptr<FnDecl> specializeFunction(FnDecl* original,
                                                const std::vector<TypePtr>& typeArgs);
    
    // Create a specialized copy of a record declaration
    std::unique_ptr<RecordDecl> specializeRecord(RecordDecl* original,
                                                  const std::vector<TypePtr>& typeArgs);
    
    // Substitute type parameters in a type string
    std::string substituteTypeString(const std::string& typeStr,
                                     const std::vector<std::string>& typeParams,
                                     const std::vector<TypePtr>& typeArgs) const;
    
    // Clear all recorded instantiations
    void clear();
    
private:
    std::vector<std::pair<GenericInstantiation, FnDecl*>> functionInstantiations_;
    std::vector<std::pair<GenericInstantiation, RecordDecl*>> recordInstantiations_;
    std::unordered_set<std::string> instantiatedNames_;  // Set of mangled names already created
    
    // Helper to create mangled name from type args
    std::string mangleTypeArgs(const std::vector<TypePtr>& typeArgs) const;
};

// Expression visitor to collect generic instantiations
class GenericCollector : public ASTVisitor {
public:
    GenericCollector(Monomorphizer& mono, 
                     std::unordered_map<std::string, FnDecl*>& genericFunctions,
                     std::unordered_map<std::string, RecordDecl*>& genericRecords);
    
    void collect(Program& program);
    
    // Visitor methods
    void visit(IntegerLiteral& node) override {}
    void visit(FloatLiteral& node) override {}
    void visit(StringLiteral& node) override {}
    void visit(CharLiteral& node) override {}
    void visit(ByteStringLiteral& node) override {}
    void visit(InterpolatedString& node) override;
    void visit(BoolLiteral& node) override {}
    void visit(NilLiteral& node) override {}
    void visit(Identifier& node) override;
    void visit(BinaryExpr& node) override;
    void visit(UnaryExpr& node) override;
    void visit(CallExpr& node) override;
    void visit(MemberExpr& node) override;
    void visit(IndexExpr& node) override;
    void visit(ListExpr& node) override;
    void visit(RecordExpr& node) override;
    void visit(MapExpr& node) override;
    void visit(RangeExpr& node) override;
    void visit(LambdaExpr& node) override;
    void visit(TernaryExpr& node) override;
    void visit(ListCompExpr& node) override;
    void visit(AddressOfExpr& node) override;
    void visit(BorrowExpr& node) override;
    void visit(DerefExpr& node) override;
    void visit(NewExpr& node) override;
    void visit(CastExpr& node) override;
    void visit(AwaitExpr& node) override;
    void visit(SpawnExpr& node) override;
    void visit(DSLBlock& node) override {}
    void visit(AssignExpr& node) override;
    void visit(PropagateExpr& node) override;
    void visit(ChanSendExpr& node) override {}
    void visit(ChanRecvExpr& node) override {}
    void visit(MakeChanExpr& node) override {}
    void visit(MakeMutexExpr& node) override {}
    void visit(MakeRWLockExpr& node) override {}
    void visit(MakeCondExpr& node) override {}
    void visit(MakeSemaphoreExpr& node) override {}
    void visit(MutexLockExpr& node) override {}
    void visit(MutexUnlockExpr& node) override {}
    void visit(RWLockReadExpr& node) override {}
    void visit(RWLockWriteExpr& node) override {}
    void visit(RWLockUnlockExpr& node) override {}
    void visit(CondWaitExpr& node) override {}
    void visit(CondSignalExpr& node) override {}
    void visit(CondBroadcastExpr& node) override {}
    void visit(SemAcquireExpr& node) override {}
    void visit(SemReleaseExpr& node) override {}
    void visit(SemTryAcquireExpr& node) override {}
    void visit(MakeAtomicExpr& node) override {}
    void visit(AtomicLoadExpr& node) override {}
    void visit(AtomicStoreExpr& node) override {}
    void visit(AtomicSwapExpr& node) override {}
    void visit(AtomicCasExpr& node) override {}
    void visit(AtomicAddExpr& node) override {}
    void visit(AtomicSubExpr& node) override {}
    void visit(AtomicAndExpr& node) override {}
    void visit(AtomicOrExpr& node) override {}
    void visit(AtomicXorExpr& node) override {}
    // Smart Pointer expressions
    void visit(MakeBoxExpr& node) override {}
    void visit(MakeRcExpr& node) override {}
    void visit(MakeArcExpr& node) override {}
    void visit(MakeWeakExpr& node) override {}
    void visit(MakeCellExpr& node) override {}
    void visit(MakeRefCellExpr& node) override {}
    // Advanced Concurrency
    void visit(MakeFutureExpr& node) override {}
    void visit(FutureGetExpr& node) override {}
    void visit(FutureSetExpr& node) override {}
    void visit(FutureIsReadyExpr& node) override {}
    void visit(MakeThreadPoolExpr& node) override {}
    void visit(ThreadPoolSubmitExpr& node) override {}
    void visit(ThreadPoolShutdownExpr& node) override {}
    void visit(SelectExpr& node) override {}
    void visit(TimeoutExpr& node) override {}
    void visit(ChanRecvTimeoutExpr& node) override {}
    void visit(ChanSendTimeoutExpr& node) override {}
    void visit(MakeCancelTokenExpr& node) override {}
    void visit(CancelExpr& node) override {}
    void visit(IsCancelledExpr& node) override {}
    // Async Runtime - Event Loop and Task Management
    void visit(AsyncRuntimeInitExpr& node) override {}
    void visit(AsyncRuntimeRunExpr& node) override {}
    void visit(AsyncRuntimeShutdownExpr& node) override {}
    void visit(AsyncSpawnExpr& node) override {}
    void visit(AsyncSleepExpr& node) override {}
    void visit(AsyncYieldExpr& node) override {}
    void visit(ExprStmt& node) override;
    void visit(VarDecl& node) override;
    void visit(DestructuringDecl& node) override;
    void visit(AssignStmt& node) override;
    void visit(Block& node) override;
    void visit(IfStmt& node) override;
    void visit(WhileStmt& node) override;
    void visit(ForStmt& node) override;
    void visit(MatchStmt& node) override;
    void visit(ReturnStmt& node) override;
    void visit(BreakStmt& node) override {}
    void visit(ContinueStmt& node) override {}
    void visit(TryStmt& node) override;
    void visit(FnDecl& node) override;
    void visit(RecordDecl& node) override;
    void visit(UnionDecl& node) override {}
    void visit(EnumDecl& node) override {}
    void visit(TypeAlias& node) override {}
    void visit(TraitDecl& node) override {}
    void visit(ImplBlock& node) override;
    void visit(ConceptDecl& node) override {}  // Concepts are compile-time only
    void visit(UnsafeBlock& node) override;
    void visit(ImportStmt& node) override {}
    void visit(ExternDecl& node) override {}
    void visit(MacroDecl& node) override {}
    void visit(SyntaxMacroDecl& node) override {}
    void visit(LayerDecl& node) override {}
    void visit(UseStmt& node) override {}
    void visit(ModuleDecl& node) override;
    void visit(DeleteStmt& node) override;
    void visit(LockStmt& node) override {}
    void visit(AsmStmt& node) override {}
    // Syntax Redesign - New Expression Visitors
    void visit(PlaceholderExpr& node) override {}
    void visit(InclusiveRangeExpr& node) override;
    void visit(SafeNavExpr& node) override;
    void visit(TypeCheckExpr& node) override;
    // Syntax Redesign - New Statement Visitors
    void visit(LoopStmt& node) override;
    void visit(WithStmt& node) override;
    void visit(ScopeStmt& node) override;
    void visit(RequireStmt& node) override;
    void visit(EnsureStmt& node) override;
    void visit(InvariantStmt& node) override;
    void visit(ComptimeBlock& node) override;
    // Algebraic Effects
    void visit(EffectDecl& node) override;
    void visit(PerformEffectExpr& node) override;
    void visit(HandleExpr& node) override;
    void visit(ResumeExpr& node) override;
    void visit(Program& node) override;
    
private:
    Monomorphizer& mono_;
    std::unordered_map<std::string, FnDecl*>& genericFunctions_;
    std::unordered_map<std::string, RecordDecl*>& genericRecords_;
    
    // Current type parameter context for inference
    std::unordered_map<std::string, TypePtr> currentTypeBindings_;
    
    // Infer type arguments from call arguments
    std::vector<TypePtr> inferTypeArgs(FnDecl* fn, CallExpr& call);
    
    // Parse type from string (simplified)
    TypePtr parseType(const std::string& typeStr);
};

} // namespace tyl

#endif // TYL_MONOMORPHIZER_H
