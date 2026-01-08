// Tyl Compiler - AST Printer
#ifndef TYL_AST_PRINTER_H
#define TYL_AST_PRINTER_H

#include "frontend/ast/ast.h"

namespace tyl {

class ASTPrinter : public ASTVisitor {
public:
    int indent = 0;
    
    void print(const std::string& s);
    
    void visit(IntegerLiteral& n) override;
    void visit(FloatLiteral& n) override;
    void visit(StringLiteral& n) override;
    void visit(CharLiteral& n) override;
    void visit(ByteStringLiteral& n) override;
    void visit(InterpolatedString& n) override;
    void visit(BoolLiteral& n) override;
    void visit(NilLiteral& n) override;
    void visit(Identifier& n) override;
    void visit(BinaryExpr& n) override;
    void visit(UnaryExpr& n) override;
    void visit(CallExpr& n) override;
    void visit(MemberExpr& n) override;
    void visit(IndexExpr& n) override;
    void visit(ListExpr& n) override;
    void visit(RecordExpr& n) override;
    void visit(MapExpr& n) override;
    void visit(RangeExpr& n) override;
    void visit(LambdaExpr& n) override;
    void visit(TernaryExpr& n) override;
    void visit(ListCompExpr& n) override;
    void visit(AddressOfExpr& n) override;
    void visit(BorrowExpr& n) override;
    void visit(DerefExpr& n) override;
    void visit(NewExpr& n) override;
    void visit(CastExpr& n) override;
    void visit(AwaitExpr& n) override;
    void visit(SpawnExpr& n) override;
    void visit(DSLBlock& n) override;
    void visit(AssignExpr& n) override;
    void visit(PropagateExpr& n) override;
    void visit(ChanSendExpr& n) override;
    void visit(ChanRecvExpr& n) override;
    void visit(MakeChanExpr& n) override;
    void visit(MakeMutexExpr& n) override;
    void visit(MakeRWLockExpr& n) override;
    void visit(MakeCondExpr& n) override;
    void visit(MakeSemaphoreExpr& n) override;
    void visit(MutexLockExpr& n) override;
    void visit(MutexUnlockExpr& n) override;
    void visit(RWLockReadExpr& n) override;
    void visit(RWLockWriteExpr& n) override;
    void visit(RWLockUnlockExpr& n) override;
    void visit(CondWaitExpr& n) override;
    void visit(CondSignalExpr& n) override;
    void visit(CondBroadcastExpr& n) override;
    void visit(SemAcquireExpr& n) override;
    void visit(SemReleaseExpr& n) override;
    void visit(SemTryAcquireExpr& n) override;
    void visit(MakeAtomicExpr& n) override;
    void visit(AtomicLoadExpr& n) override;
    void visit(AtomicStoreExpr& n) override;
    void visit(AtomicSwapExpr& n) override;
    void visit(AtomicCasExpr& n) override;
    void visit(AtomicAddExpr& n) override;
    void visit(AtomicSubExpr& n) override;
    void visit(AtomicAndExpr& n) override;
    void visit(AtomicOrExpr& n) override;
    void visit(AtomicXorExpr& n) override;
    // Smart Pointer expressions
    void visit(MakeBoxExpr& n) override;
    void visit(MakeRcExpr& n) override;
    void visit(MakeArcExpr& n) override;
    void visit(MakeWeakExpr& n) override;
    void visit(MakeCellExpr& n) override;
    void visit(MakeRefCellExpr& n) override;
    // Advanced Concurrency
    void visit(MakeFutureExpr& n) override;
    void visit(FutureGetExpr& n) override;
    void visit(FutureSetExpr& n) override;
    void visit(FutureIsReadyExpr& n) override;
    void visit(MakeThreadPoolExpr& n) override;
    void visit(ThreadPoolSubmitExpr& n) override;
    void visit(ThreadPoolShutdownExpr& n) override;
    void visit(SelectExpr& n) override;
    void visit(TimeoutExpr& n) override;
    void visit(ChanRecvTimeoutExpr& n) override;
    void visit(ChanSendTimeoutExpr& n) override;
    void visit(MakeCancelTokenExpr& n) override;
    void visit(CancelExpr& n) override;
    void visit(IsCancelledExpr& n) override;
    // Async Runtime - Event Loop and Task Management
    void visit(AsyncRuntimeInitExpr& n) override;
    void visit(AsyncRuntimeRunExpr& n) override;
    void visit(AsyncRuntimeShutdownExpr& n) override;
    void visit(AsyncSpawnExpr& n) override;
    void visit(AsyncSleepExpr& n) override;
    void visit(AsyncYieldExpr& n) override;
    void visit(ExprStmt& n) override;
    void visit(VarDecl& n) override;
    void visit(DestructuringDecl& n) override;
    void visit(AssignStmt& n) override;
    void visit(Block& n) override;
    void visit(IfStmt& n) override;
    void visit(WhileStmt& n) override;
    void visit(ForStmt& n) override;
    void visit(MatchStmt& n) override;
    void visit(ReturnStmt& n) override;
    void visit(BreakStmt& n) override;
    void visit(ContinueStmt& n) override;
    void visit(TryStmt& n) override;
    void visit(FnDecl& n) override;
    void visit(RecordDecl& n) override;
    void visit(UnionDecl& n) override;
    void visit(EnumDecl& n) override;
    void visit(TypeAlias& n) override;
    void visit(TraitDecl& n) override;
    void visit(ImplBlock& n) override;
    void visit(ConceptDecl& n) override;
    void visit(UnsafeBlock& n) override;
    void visit(ImportStmt& n) override;
    void visit(ExternDecl& n) override;
    void visit(MacroDecl& n) override;
    void visit(SyntaxMacroDecl& n) override;
    void visit(LayerDecl& n) override;
    void visit(UseStmt& n) override;
    void visit(ModuleDecl& n) override;
    void visit(DeleteStmt& n) override;
    void visit(LockStmt& n) override;
    void visit(AsmStmt& n) override;
    // Syntax Redesign - New Expression Visitors
    void visit(PlaceholderExpr& n) override;
    void visit(InclusiveRangeExpr& n) override;
    void visit(SafeNavExpr& n) override;
    void visit(TypeCheckExpr& n) override;
    // Syntax Redesign - New Statement Visitors
    void visit(LoopStmt& n) override;
    void visit(WithStmt& n) override;
    void visit(ScopeStmt& n) override;
    void visit(RequireStmt& n) override;
    void visit(EnsureStmt& n) override;
    void visit(InvariantStmt& n) override;
    void visit(ComptimeBlock& n) override;
    // Algebraic Effects
    void visit(EffectDecl& n) override;
    void visit(PerformEffectExpr& n) override;
    void visit(HandleExpr& n) override;
    void visit(ResumeExpr& n) override;
    void visit(Program& n) override;
};

void printTokens(const std::vector<Token>& tokens);

} // namespace tyl

#endif // TYL_AST_PRINTER_H
