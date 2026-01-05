// Tyl Compiler - Type Checker and Inference
#ifndef TYL_TYPE_CHECKER_H
#define TYL_TYPE_CHECKER_H

#include "frontend/ast/ast.h"
#include "semantic/types/types.h"
#include "semantic/symbols/symbol_table.h"
#include "semantic/ownership/ownership.h"
#include <vector>

namespace tyl {

struct TypeDiagnostic {
    enum class Level { ERROR, WARNING, NOTE };
    Level level;
    std::string message;
    SourceLocation location;
    TypeDiagnostic(Level l, std::string msg, SourceLocation loc) : level(l), message(std::move(msg)), location(loc) {}
};

class TypeChecker : public ASTVisitor {
public:
    TypeChecker();
    bool check(Program& program);
    const std::vector<TypeDiagnostic>& diagnostics() const { return diagnostics_; }
    bool hasErrors() const;
    TypePtr getType(Expression* expr);
    SymbolTable& symbols() { return symbols_; }
    OwnershipTracker& ownership() { return ownership_; }
    
private:
    SymbolTable symbols_;
    std::vector<TypeDiagnostic> diagnostics_;
    TypePtr currentType_;
    TypePtr expectedReturn_;
    OwnershipTracker ownership_;  // Ownership and borrow tracking
    bool borrowCheckEnabled_ = true;  // Enable/disable borrow checking
    
    // Generic type context
    std::unordered_map<std::string, TypePtr> currentTypeParams_;  // Active type parameter bindings
    std::vector<std::string> currentTypeParamNames_;              // Type params in scope
    
    TypePtr inferType(Expression* expr);
    TypePtr unify(TypePtr a, TypePtr b, const SourceLocation& loc);
    TypePtr commonType(TypePtr a, TypePtr b);
    bool isAssignable(TypePtr target, TypePtr source);
    bool isComparable(TypePtr a, TypePtr b);
    void error(const std::string& msg, const SourceLocation& loc);
    void warning(const std::string& msg, const SourceLocation& loc);
    void note(const std::string& msg, const SourceLocation& loc);
    TypePtr parseTypeAnnotation(const std::string& str);
    void registerBuiltins();  // Register built-in functions
    void checkUnusedVariables(Scope* scope);  // Check for unused variables in scope
    
    // Ownership and borrow checking
    void checkOwnership(Expression* expr, bool isMove = false);
    void checkBorrow(Expression* expr, bool isMutable);
    ParamMode parseParamMode(const std::string& typeName);
    std::string stripBorrowPrefix(const std::string& typeName);
    void emitOwnershipError(const std::string& msg, const SourceLocation& loc);
    
    // Generic and trait type checking
    TypePtr parseGenericType(const std::string& str);
    TypePtr resolveTypeParam(const std::string& name);
    bool checkTraitBounds(TypePtr type, const std::vector<std::string>& bounds, const SourceLocation& loc);
    TypePtr instantiateGenericFunction(FunctionType* fnType, const std::vector<TypePtr>& typeArgs, const SourceLocation& loc);
    void checkTraitImpl(const std::string& traitName, const std::string& typeName, 
                        const std::vector<std::unique_ptr<FnDecl>>& methods, const SourceLocation& loc);
    
    void visit(IntegerLiteral& node) override;
    void visit(FloatLiteral& node) override;
    void visit(StringLiteral& node) override;
    void visit(CharLiteral& node) override;
    void visit(ByteStringLiteral& node) override;
    void visit(InterpolatedString& node) override;
    void visit(BoolLiteral& node) override;
    void visit(NilLiteral& node) override;
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
    void visit(DSLBlock& node) override;
    void visit(AssignExpr& node) override;
    void visit(PropagateExpr& node) override;
    void visit(ChanSendExpr& node) override;
    void visit(ChanRecvExpr& node) override;
    void visit(MakeChanExpr& node) override;
    void visit(MakeMutexExpr& node) override;
    void visit(MakeRWLockExpr& node) override;
    void visit(MakeCondExpr& node) override;
    void visit(MakeSemaphoreExpr& node) override;
    void visit(MutexLockExpr& node) override;
    void visit(MutexUnlockExpr& node) override;
    void visit(RWLockReadExpr& node) override;
    void visit(RWLockWriteExpr& node) override;
    void visit(RWLockUnlockExpr& node) override;
    void visit(CondWaitExpr& node) override;
    void visit(CondSignalExpr& node) override;
    void visit(CondBroadcastExpr& node) override;
    void visit(SemAcquireExpr& node) override;
    void visit(SemReleaseExpr& node) override;
    void visit(SemTryAcquireExpr& node) override;
    void visit(MakeAtomicExpr& node) override;
    void visit(AtomicLoadExpr& node) override;
    void visit(AtomicStoreExpr& node) override;
    void visit(AtomicSwapExpr& node) override;
    void visit(AtomicCasExpr& node) override;
    void visit(AtomicAddExpr& node) override;
    void visit(AtomicSubExpr& node) override;
    void visit(AtomicAndExpr& node) override;
    void visit(AtomicOrExpr& node) override;
    void visit(AtomicXorExpr& node) override;
    // Smart Pointer expressions
    void visit(MakeBoxExpr& node) override;
    void visit(MakeRcExpr& node) override;
    void visit(MakeArcExpr& node) override;
    void visit(MakeWeakExpr& node) override;
    void visit(MakeCellExpr& node) override;
    void visit(MakeRefCellExpr& node) override;
    // Advanced Concurrency
    void visit(MakeFutureExpr& node) override;
    void visit(FutureGetExpr& node) override;
    void visit(FutureSetExpr& node) override;
    void visit(FutureIsReadyExpr& node) override;
    void visit(MakeThreadPoolExpr& node) override;
    void visit(ThreadPoolSubmitExpr& node) override;
    void visit(ThreadPoolShutdownExpr& node) override;
    void visit(SelectExpr& node) override;
    void visit(TimeoutExpr& node) override;
    void visit(ChanRecvTimeoutExpr& node) override;
    void visit(ChanSendTimeoutExpr& node) override;
    void visit(MakeCancelTokenExpr& node) override;
    void visit(CancelExpr& node) override;
    void visit(IsCancelledExpr& node) override;
    // Async Runtime - Event Loop and Task Management
    void visit(AsyncRuntimeInitExpr& node) override;
    void visit(AsyncRuntimeRunExpr& node) override;
    void visit(AsyncRuntimeShutdownExpr& node) override;
    void visit(AsyncSpawnExpr& node) override;
    void visit(AsyncSleepExpr& node) override;
    void visit(AsyncYieldExpr& node) override;
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
    void visit(BreakStmt& node) override;
    void visit(ContinueStmt& node) override;
    void visit(TryStmt& node) override;
    void visit(FnDecl& node) override;
    void visit(RecordDecl& node) override;
    void visit(UnionDecl& node) override;
    void visit(EnumDecl& node) override;
    void visit(TypeAlias& node) override;
    void visit(TraitDecl& node) override;
    void visit(ImplBlock& node) override;
    void visit(UnsafeBlock& node) override;
    void visit(ImportStmt& node) override;
    void visit(ExternDecl& node) override;
    void visit(MacroDecl& node) override;
    void visit(SyntaxMacroDecl& node) override;
    void visit(LayerDecl& node) override;
    void visit(UseStmt& node) override;
    void visit(ModuleDecl& node) override;
    void visit(DeleteStmt& node) override;
    void visit(LockStmt& node) override;
    void visit(AsmStmt& node) override;
    // Syntax Redesign - New Expression Visitors
    void visit(PlaceholderExpr& node) override;
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
    
    std::unordered_map<Expression*, TypePtr> exprTypes_;
};

} // namespace tyl

#endif // TYL_TYPE_CHECKER_H
