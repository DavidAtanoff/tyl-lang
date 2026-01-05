// Tyl Compiler - AST Implementation
#include "frontend/ast/ast.h"

namespace tyl {

void IntegerLiteral::accept(ASTVisitor& v) { v.visit(*this); }
void FloatLiteral::accept(ASTVisitor& v) { v.visit(*this); }
void StringLiteral::accept(ASTVisitor& v) { v.visit(*this); }
void CharLiteral::accept(ASTVisitor& v) { v.visit(*this); }
void ByteStringLiteral::accept(ASTVisitor& v) { v.visit(*this); }
void InterpolatedString::accept(ASTVisitor& v) { v.visit(*this); }
void BoolLiteral::accept(ASTVisitor& v) { v.visit(*this); }
void NilLiteral::accept(ASTVisitor& v) { v.visit(*this); }
void Identifier::accept(ASTVisitor& v) { v.visit(*this); }
void BinaryExpr::accept(ASTVisitor& v) { v.visit(*this); }
void UnaryExpr::accept(ASTVisitor& v) { v.visit(*this); }
void CallExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MemberExpr::accept(ASTVisitor& v) { v.visit(*this); }
void IndexExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ListExpr::accept(ASTVisitor& v) { v.visit(*this); }
void RecordExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MapExpr::accept(ASTVisitor& v) { v.visit(*this); }
void RangeExpr::accept(ASTVisitor& v) { v.visit(*this); }
void LambdaExpr::accept(ASTVisitor& v) { v.visit(*this); }
void TernaryExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ListCompExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AddressOfExpr::accept(ASTVisitor& v) { v.visit(*this); }
void BorrowExpr::accept(ASTVisitor& v) { v.visit(*this); }
void DerefExpr::accept(ASTVisitor& v) { v.visit(*this); }
void NewExpr::accept(ASTVisitor& v) { v.visit(*this); }
void CastExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AwaitExpr::accept(ASTVisitor& v) { v.visit(*this); }
void SpawnExpr::accept(ASTVisitor& v) { v.visit(*this); }
void DSLBlock::accept(ASTVisitor& v) { v.visit(*this); }
void AssignExpr::accept(ASTVisitor& v) { v.visit(*this); }
void PropagateExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ChanSendExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ChanRecvExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeChanExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeMutexExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeRWLockExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeCondExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeSemaphoreExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MutexLockExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MutexUnlockExpr::accept(ASTVisitor& v) { v.visit(*this); }
void RWLockReadExpr::accept(ASTVisitor& v) { v.visit(*this); }
void RWLockWriteExpr::accept(ASTVisitor& v) { v.visit(*this); }
void RWLockUnlockExpr::accept(ASTVisitor& v) { v.visit(*this); }
void CondWaitExpr::accept(ASTVisitor& v) { v.visit(*this); }
void CondSignalExpr::accept(ASTVisitor& v) { v.visit(*this); }
void CondBroadcastExpr::accept(ASTVisitor& v) { v.visit(*this); }
void SemAcquireExpr::accept(ASTVisitor& v) { v.visit(*this); }
void SemReleaseExpr::accept(ASTVisitor& v) { v.visit(*this); }
void SemTryAcquireExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeAtomicExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicLoadExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicStoreExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicSwapExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicCasExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicAddExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicSubExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicAndExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicOrExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AtomicXorExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Smart Pointer expressions
void MakeBoxExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeRcExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeArcExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeWeakExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeCellExpr::accept(ASTVisitor& v) { v.visit(*this); }
void MakeRefCellExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Advanced Concurrency - Future/Promise
void MakeFutureExpr::accept(ASTVisitor& v) { v.visit(*this); }
void FutureGetExpr::accept(ASTVisitor& v) { v.visit(*this); }
void FutureSetExpr::accept(ASTVisitor& v) { v.visit(*this); }
void FutureIsReadyExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Advanced Concurrency - Thread Pool
void MakeThreadPoolExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ThreadPoolSubmitExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ThreadPoolShutdownExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Advanced Concurrency - Select
void SelectExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Advanced Concurrency - Timeout
void TimeoutExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ChanRecvTimeoutExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ChanSendTimeoutExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Advanced Concurrency - Cancellation
void MakeCancelTokenExpr::accept(ASTVisitor& v) { v.visit(*this); }
void CancelExpr::accept(ASTVisitor& v) { v.visit(*this); }
void IsCancelledExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Async Runtime - Event Loop and Task Management
void AsyncRuntimeInitExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AsyncRuntimeRunExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AsyncRuntimeShutdownExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AsyncSpawnExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AsyncSleepExpr::accept(ASTVisitor& v) { v.visit(*this); }
void AsyncYieldExpr::accept(ASTVisitor& v) { v.visit(*this); }
// Syntax Redesign - New Expression Types
void PlaceholderExpr::accept(ASTVisitor& v) { v.visit(*this); }
void InclusiveRangeExpr::accept(ASTVisitor& v) { v.visit(*this); }
void SafeNavExpr::accept(ASTVisitor& v) { v.visit(*this); }
void TypeCheckExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ExprStmt::accept(ASTVisitor& v) { v.visit(*this); }
void VarDecl::accept(ASTVisitor& v) { v.visit(*this); }
void DestructuringDecl::accept(ASTVisitor& v) { v.visit(*this); }
void AssignStmt::accept(ASTVisitor& v) { v.visit(*this); }
void Block::accept(ASTVisitor& v) { v.visit(*this); }
void IfStmt::accept(ASTVisitor& v) { v.visit(*this); }
void WhileStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ForStmt::accept(ASTVisitor& v) { v.visit(*this); }
void MatchStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ReturnStmt::accept(ASTVisitor& v) { v.visit(*this); }
void BreakStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ContinueStmt::accept(ASTVisitor& v) { v.visit(*this); }
void TryStmt::accept(ASTVisitor& v) { v.visit(*this); }
void FnDecl::accept(ASTVisitor& v) { v.visit(*this); }
void RecordDecl::accept(ASTVisitor& v) { v.visit(*this); }
void UnionDecl::accept(ASTVisitor& v) { v.visit(*this); }
void EnumDecl::accept(ASTVisitor& v) { v.visit(*this); }
void TypeAlias::accept(ASTVisitor& v) { v.visit(*this); }
void TraitDecl::accept(ASTVisitor& v) { v.visit(*this); }
void ImplBlock::accept(ASTVisitor& v) { v.visit(*this); }
void UnsafeBlock::accept(ASTVisitor& v) { v.visit(*this); }
void ImportStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ExternDecl::accept(ASTVisitor& v) { v.visit(*this); }
void MacroDecl::accept(ASTVisitor& v) { v.visit(*this); }
void SyntaxMacroDecl::accept(ASTVisitor& v) { v.visit(*this); }
void LayerDecl::accept(ASTVisitor& v) { v.visit(*this); }
void UseStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ModuleDecl::accept(ASTVisitor& v) { v.visit(*this); }
void DeleteStmt::accept(ASTVisitor& v) { v.visit(*this); }
void LockStmt::accept(ASTVisitor& v) { v.visit(*this); }
void AsmStmt::accept(ASTVisitor& v) { v.visit(*this); }
// Syntax Redesign - New Statement Types
void LoopStmt::accept(ASTVisitor& v) { v.visit(*this); }
void WithStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ScopeStmt::accept(ASTVisitor& v) { v.visit(*this); }
void RequireStmt::accept(ASTVisitor& v) { v.visit(*this); }
void EnsureStmt::accept(ASTVisitor& v) { v.visit(*this); }
void InvariantStmt::accept(ASTVisitor& v) { v.visit(*this); }
void ComptimeBlock::accept(ASTVisitor& v) { v.visit(*this); }
// Algebraic Effects
void EffectDecl::accept(ASTVisitor& v) { v.visit(*this); }
void PerformEffectExpr::accept(ASTVisitor& v) { v.visit(*this); }
void HandleExpr::accept(ASTVisitor& v) { v.visit(*this); }
void ResumeExpr::accept(ASTVisitor& v) { v.visit(*this); }
void Program::accept(ASTVisitor& v) { v.visit(*this); }

} // namespace tyl
