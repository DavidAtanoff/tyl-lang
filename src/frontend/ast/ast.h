// Tyl Compiler - Abstract Syntax Tree
#ifndef TYL_AST_H
#define TYL_AST_H

#include "common/common.h"
#include "frontend/token/token.h"

namespace tyl {

struct ASTVisitor;
struct ASTNode { SourceLocation location; virtual ~ASTNode() = default; virtual void accept(ASTVisitor& visitor) = 0; };
using ASTPtr = std::unique_ptr<ASTNode>;

struct Expression : ASTNode {};
using ExprPtr = std::unique_ptr<Expression>;

struct IntegerLiteral : Expression { int64_t value; std::string suffix; IntegerLiteral(int64_t v, SourceLocation loc, const std::string& suf = "") : value(v), suffix(suf) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct FloatLiteral : Expression { double value; std::string suffix; FloatLiteral(double v, SourceLocation loc, const std::string& suf = "") : value(v), suffix(suf) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct StringLiteral : Expression { std::string value; StringLiteral(std::string v, SourceLocation loc) : value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct CharLiteral : Expression { uint32_t value; CharLiteral(uint32_t v, SourceLocation loc) : value(v) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ByteStringLiteral : Expression { std::vector<uint8_t> value; bool isRaw; ByteStringLiteral(std::vector<uint8_t> v, bool raw, SourceLocation loc) : value(std::move(v)), isRaw(raw) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct InterpolatedString : Expression { std::vector<std::variant<std::string, ExprPtr>> parts; InterpolatedString(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct BoolLiteral : Expression { bool value; BoolLiteral(bool v, SourceLocation loc) : value(v) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct NilLiteral : Expression { NilLiteral(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct Identifier : Expression { std::string name; Identifier(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct BinaryExpr : Expression { ExprPtr left; TokenType op; ExprPtr right; BinaryExpr(ExprPtr l, TokenType o, ExprPtr r, SourceLocation loc) : left(std::move(l)), op(o), right(std::move(r)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct UnaryExpr : Expression { TokenType op; ExprPtr operand; UnaryExpr(TokenType o, ExprPtr e, SourceLocation loc) : op(o), operand(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct CallExpr : Expression { ExprPtr callee; std::vector<ExprPtr> args; std::vector<std::pair<std::string, ExprPtr>> namedArgs; std::vector<std::string> typeArgs; bool isHotCallSite = false; CallExpr(ExprPtr c, SourceLocation loc) : callee(std::move(c)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MemberExpr : Expression { ExprPtr object; std::string member; MemberExpr(ExprPtr obj, std::string m, SourceLocation loc) : object(std::move(obj)), member(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct IndexExpr : Expression { ExprPtr object; ExprPtr index; IndexExpr(ExprPtr obj, ExprPtr idx, SourceLocation loc) : object(std::move(obj)), index(std::move(idx)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ListExpr : Expression { std::vector<ExprPtr> elements; ListExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct RecordExpr : Expression { std::string typeName; std::vector<std::string> typeArgs; std::vector<std::pair<std::string, ExprPtr>> fields; RecordExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MapExpr : Expression { std::vector<std::pair<ExprPtr, ExprPtr>> entries; MapExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct RangeExpr : Expression { ExprPtr start; ExprPtr end; ExprPtr step; RangeExpr(ExprPtr s, ExprPtr e, ExprPtr st, SourceLocation loc) : start(std::move(s)), end(std::move(e)), step(std::move(st)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct LambdaExpr : Expression { std::vector<std::pair<std::string, std::string>> params; ExprPtr body; LambdaExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct TernaryExpr : Expression { ExprPtr condition; ExprPtr thenExpr; ExprPtr elseExpr; TernaryExpr(ExprPtr c, ExprPtr t, ExprPtr e, SourceLocation loc) : condition(std::move(c)), thenExpr(std::move(t)), elseExpr(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ListCompExpr : Expression { ExprPtr expr; std::string var; ExprPtr iterable; ExprPtr condition; ListCompExpr(ExprPtr e, std::string v, ExprPtr it, ExprPtr cond, SourceLocation loc) : expr(std::move(e)), var(std::move(v)), iterable(std::move(it)), condition(std::move(cond)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AddressOfExpr : Expression { ExprPtr operand; AddressOfExpr(ExprPtr e, SourceLocation loc) : operand(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct BorrowExpr : Expression { ExprPtr operand; bool isMutable; BorrowExpr(ExprPtr e, bool mut, SourceLocation loc) : operand(std::move(e)), isMutable(mut) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct DerefExpr : Expression { ExprPtr operand; DerefExpr(ExprPtr e, SourceLocation loc) : operand(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct NewExpr : Expression { std::string typeName; std::vector<ExprPtr> args; NewExpr(std::string t, SourceLocation loc) : typeName(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct CastExpr : Expression { ExprPtr expr; std::string targetType; CastExpr(ExprPtr e, std::string t, SourceLocation loc) : expr(std::move(e)), targetType(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AwaitExpr : Expression { ExprPtr operand; AwaitExpr(ExprPtr e, SourceLocation loc) : operand(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct SpawnExpr : Expression { ExprPtr operand; SpawnExpr(ExprPtr e, SourceLocation loc) : operand(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct DSLBlock : Expression { std::string dslName; std::string rawContent; DSLBlock(std::string name, std::string content, SourceLocation loc) : dslName(std::move(name)), rawContent(std::move(content)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AssignExpr : Expression { ExprPtr target; TokenType op; ExprPtr value; AssignExpr(ExprPtr t, TokenType o, ExprPtr v, SourceLocation loc) : target(std::move(t)), op(o), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct PropagateExpr : Expression { ExprPtr operand; PropagateExpr(ExprPtr e, SourceLocation loc) : operand(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Channel expressions for inter-thread communication
struct ChanSendExpr : Expression { ExprPtr channel; ExprPtr value; ChanSendExpr(ExprPtr ch, ExprPtr v, SourceLocation loc) : channel(std::move(ch)), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ChanRecvExpr : Expression { ExprPtr channel; ChanRecvExpr(ExprPtr ch, SourceLocation loc) : channel(std::move(ch)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeChanExpr : Expression { std::string elementType; int64_t bufferSize; MakeChanExpr(std::string t, int64_t sz, SourceLocation loc) : elementType(std::move(t)), bufferSize(sz) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Synchronization primitive expressions
struct MakeMutexExpr : Expression { std::string elementType; MakeMutexExpr(std::string t, SourceLocation loc) : elementType(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeRWLockExpr : Expression { std::string elementType; MakeRWLockExpr(std::string t, SourceLocation loc) : elementType(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeCondExpr : Expression { MakeCondExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeSemaphoreExpr : Expression { int64_t initialCount; int64_t maxCount; MakeSemaphoreExpr(int64_t init, int64_t max, SourceLocation loc) : initialCount(init), maxCount(max) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MutexLockExpr : Expression { ExprPtr mutex; MutexLockExpr(ExprPtr m, SourceLocation loc) : mutex(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MutexUnlockExpr : Expression { ExprPtr mutex; MutexUnlockExpr(ExprPtr m, SourceLocation loc) : mutex(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct RWLockReadExpr : Expression { ExprPtr rwlock; RWLockReadExpr(ExprPtr r, SourceLocation loc) : rwlock(std::move(r)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct RWLockWriteExpr : Expression { ExprPtr rwlock; RWLockWriteExpr(ExprPtr r, SourceLocation loc) : rwlock(std::move(r)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct RWLockUnlockExpr : Expression { ExprPtr rwlock; RWLockUnlockExpr(ExprPtr r, SourceLocation loc) : rwlock(std::move(r)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct CondWaitExpr : Expression { ExprPtr cond; ExprPtr mutex; CondWaitExpr(ExprPtr c, ExprPtr m, SourceLocation loc) : cond(std::move(c)), mutex(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct CondSignalExpr : Expression { ExprPtr cond; CondSignalExpr(ExprPtr c, SourceLocation loc) : cond(std::move(c)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct CondBroadcastExpr : Expression { ExprPtr cond; CondBroadcastExpr(ExprPtr c, SourceLocation loc) : cond(std::move(c)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct SemAcquireExpr : Expression { ExprPtr sem; SemAcquireExpr(ExprPtr s, SourceLocation loc) : sem(std::move(s)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct SemReleaseExpr : Expression { ExprPtr sem; SemReleaseExpr(ExprPtr s, SourceLocation loc) : sem(std::move(s)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct SemTryAcquireExpr : Expression { ExprPtr sem; SemTryAcquireExpr(ExprPtr s, SourceLocation loc) : sem(std::move(s)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Memory ordering for atomic operations
enum class MemoryOrder { Relaxed, Acquire, Release, AcqRel, SeqCst };

// Atomic integer expressions
struct MakeAtomicExpr : Expression { std::string elementType; ExprPtr initialValue; MakeAtomicExpr(std::string t, ExprPtr init, SourceLocation loc) : elementType(std::move(t)), initialValue(std::move(init)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicLoadExpr : Expression { ExprPtr atomic; MemoryOrder order = MemoryOrder::SeqCst; AtomicLoadExpr(ExprPtr a, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicStoreExpr : Expression { ExprPtr atomic; ExprPtr value; MemoryOrder order = MemoryOrder::SeqCst; AtomicStoreExpr(ExprPtr a, ExprPtr v, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), value(std::move(v)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicSwapExpr : Expression { ExprPtr atomic; ExprPtr value; MemoryOrder order = MemoryOrder::SeqCst; AtomicSwapExpr(ExprPtr a, ExprPtr v, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), value(std::move(v)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicCasExpr : Expression { ExprPtr atomic; ExprPtr expected; ExprPtr desired; MemoryOrder successOrder = MemoryOrder::SeqCst; MemoryOrder failureOrder = MemoryOrder::SeqCst; AtomicCasExpr(ExprPtr a, ExprPtr e, ExprPtr d, SourceLocation loc, MemoryOrder so = MemoryOrder::SeqCst, MemoryOrder fo = MemoryOrder::SeqCst) : atomic(std::move(a)), expected(std::move(e)), desired(std::move(d)), successOrder(so), failureOrder(fo) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicAddExpr : Expression { ExprPtr atomic; ExprPtr value; MemoryOrder order = MemoryOrder::SeqCst; AtomicAddExpr(ExprPtr a, ExprPtr v, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), value(std::move(v)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicSubExpr : Expression { ExprPtr atomic; ExprPtr value; MemoryOrder order = MemoryOrder::SeqCst; AtomicSubExpr(ExprPtr a, ExprPtr v, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), value(std::move(v)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicAndExpr : Expression { ExprPtr atomic; ExprPtr value; MemoryOrder order = MemoryOrder::SeqCst; AtomicAndExpr(ExprPtr a, ExprPtr v, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), value(std::move(v)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicOrExpr : Expression { ExprPtr atomic; ExprPtr value; MemoryOrder order = MemoryOrder::SeqCst; AtomicOrExpr(ExprPtr a, ExprPtr v, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), value(std::move(v)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AtomicXorExpr : Expression { ExprPtr atomic; ExprPtr value; MemoryOrder order = MemoryOrder::SeqCst; AtomicXorExpr(ExprPtr a, ExprPtr v, SourceLocation loc, MemoryOrder o = MemoryOrder::SeqCst) : atomic(std::move(a)), value(std::move(v)), order(o) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Smart Pointer expressions - Box, Rc, Arc, Weak, Cell, RefCell
struct MakeBoxExpr : Expression { std::string elementType; ExprPtr value; MakeBoxExpr(std::string t, ExprPtr v, SourceLocation loc) : elementType(std::move(t)), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeRcExpr : Expression { std::string elementType; ExprPtr value; MakeRcExpr(std::string t, ExprPtr v, SourceLocation loc) : elementType(std::move(t)), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeArcExpr : Expression { std::string elementType; ExprPtr value; MakeArcExpr(std::string t, ExprPtr v, SourceLocation loc) : elementType(std::move(t)), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeWeakExpr : Expression { ExprPtr source; bool isAtomic; MakeWeakExpr(ExprPtr s, bool atomic, SourceLocation loc) : source(std::move(s)), isAtomic(atomic) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeCellExpr : Expression { std::string elementType; ExprPtr value; MakeCellExpr(std::string t, ExprPtr v, SourceLocation loc) : elementType(std::move(t)), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MakeRefCellExpr : Expression { std::string elementType; ExprPtr value; MakeRefCellExpr(std::string t, ExprPtr v, SourceLocation loc) : elementType(std::move(t)), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Advanced Concurrency - Future/Promise
struct MakeFutureExpr : Expression { std::string elementType; MakeFutureExpr(std::string t, SourceLocation loc) : elementType(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct FutureGetExpr : Expression { ExprPtr future; FutureGetExpr(ExprPtr f, SourceLocation loc) : future(std::move(f)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct FutureSetExpr : Expression { ExprPtr future; ExprPtr value; FutureSetExpr(ExprPtr f, ExprPtr v, SourceLocation loc) : future(std::move(f)), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct FutureIsReadyExpr : Expression { ExprPtr future; FutureIsReadyExpr(ExprPtr f, SourceLocation loc) : future(std::move(f)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Advanced Concurrency - Thread Pool
struct MakeThreadPoolExpr : Expression { ExprPtr numWorkers; MakeThreadPoolExpr(ExprPtr n, SourceLocation loc) : numWorkers(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ThreadPoolSubmitExpr : Expression { ExprPtr pool; ExprPtr task; ThreadPoolSubmitExpr(ExprPtr p, ExprPtr t, SourceLocation loc) : pool(std::move(p)), task(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ThreadPoolShutdownExpr : Expression { ExprPtr pool; ThreadPoolShutdownExpr(ExprPtr p, SourceLocation loc) : pool(std::move(p)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Advanced Concurrency - Timeout operations
struct TimeoutExpr : Expression { ExprPtr operation; ExprPtr timeoutMs; TimeoutExpr(ExprPtr op, ExprPtr ms, SourceLocation loc) : operation(std::move(op)), timeoutMs(std::move(ms)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ChanRecvTimeoutExpr : Expression { ExprPtr channel; ExprPtr timeoutMs; ChanRecvTimeoutExpr(ExprPtr ch, ExprPtr ms, SourceLocation loc) : channel(std::move(ch)), timeoutMs(std::move(ms)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ChanSendTimeoutExpr : Expression { ExprPtr channel; ExprPtr value; ExprPtr timeoutMs; ChanSendTimeoutExpr(ExprPtr ch, ExprPtr v, ExprPtr ms, SourceLocation loc) : channel(std::move(ch)), value(std::move(v)), timeoutMs(std::move(ms)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Advanced Concurrency - Cancellation
struct MakeCancelTokenExpr : Expression { MakeCancelTokenExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct CancelExpr : Expression { ExprPtr token; CancelExpr(ExprPtr t, SourceLocation loc) : token(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct IsCancelledExpr : Expression { ExprPtr token; IsCancelledExpr(ExprPtr t, SourceLocation loc) : token(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Async Runtime - Event Loop and Task Management
struct AsyncRuntimeInitExpr : Expression { ExprPtr numWorkers; AsyncRuntimeInitExpr(ExprPtr n, SourceLocation loc) : numWorkers(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AsyncRuntimeRunExpr : Expression { AsyncRuntimeRunExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AsyncRuntimeShutdownExpr : Expression { AsyncRuntimeShutdownExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AsyncSpawnExpr : Expression { ExprPtr task; AsyncSpawnExpr(ExprPtr t, SourceLocation loc) : task(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AsyncSleepExpr : Expression { ExprPtr durationMs; AsyncSleepExpr(ExprPtr d, SourceLocation loc) : durationMs(std::move(d)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AsyncYieldExpr : Expression { AsyncYieldExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Syntax Redesign - New Expression Types
// Placeholder expression for _ in lambdas (e.g., _ * 2 becomes x => x * 2)
struct PlaceholderExpr : Expression { PlaceholderExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Inclusive range expression (..=)
struct InclusiveRangeExpr : Expression { ExprPtr start; ExprPtr end; ExprPtr step; InclusiveRangeExpr(ExprPtr s, ExprPtr e, ExprPtr st, SourceLocation loc) : start(std::move(s)), end(std::move(e)), step(std::move(st)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Safe navigation expression (?.)
struct SafeNavExpr : Expression { ExprPtr object; std::string member; SafeNavExpr(ExprPtr obj, std::string m, SourceLocation loc) : object(std::move(obj)), member(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Type check expression (is)
struct TypeCheckExpr : Expression { ExprPtr value; std::string typeName; TypeCheckExpr(ExprPtr v, std::string t, SourceLocation loc) : value(std::move(v)), typeName(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };

struct Statement : ASTNode {};
using StmtPtr = std::unique_ptr<Statement>;

// Advanced Concurrency - Select (wait on multiple channels) - must be after StmtPtr
struct SelectCase { ExprPtr channel; bool isSend; ExprPtr value; StmtPtr body; SelectCase(ExprPtr ch, bool send, ExprPtr v, StmtPtr b) : channel(std::move(ch)), isSend(send), value(std::move(v)), body(std::move(b)) {} };
struct SelectExpr : Expression { std::vector<SelectCase> cases; StmtPtr defaultCase; SelectExpr(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };

struct ExprStmt : Statement { ExprPtr expr; ExprStmt(ExprPtr e, SourceLocation loc) : expr(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct VarDecl : Statement { std::string name; std::string typeName; ExprPtr initializer; bool isMutable = true; bool isConst = false; VarDecl(std::string n, std::string t, ExprPtr init, SourceLocation loc) : name(std::move(n)), typeName(std::move(t)), initializer(std::move(init)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct DestructuringDecl : Statement { enum class Kind { TUPLE, RECORD }; Kind kind; std::vector<std::string> names; ExprPtr initializer; bool isMutable = true; DestructuringDecl(Kind k, std::vector<std::string> n, ExprPtr init, SourceLocation loc) : kind(k), names(std::move(n)), initializer(std::move(init)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AssignStmt : Statement { ExprPtr target; TokenType op; ExprPtr value; AssignStmt(ExprPtr t, TokenType o, ExprPtr v, SourceLocation loc) : target(std::move(t)), op(o), value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct Block : Statement { std::vector<StmtPtr> statements; Block(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct IfStmt : Statement { ExprPtr condition; StmtPtr thenBranch; std::vector<std::pair<ExprPtr, StmtPtr>> elifBranches; StmtPtr elseBranch; IfStmt(ExprPtr c, StmtPtr t, SourceLocation loc) : condition(std::move(c)), thenBranch(std::move(t)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct WhileStmt : Statement { std::string label; ExprPtr condition; StmtPtr body; WhileStmt(ExprPtr c, StmtPtr b, SourceLocation loc) : condition(std::move(c)), body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ForStmt : Statement { std::string label; std::string var; ExprPtr iterable; StmtPtr body; int unrollHint = 0; ForStmt(std::string v, ExprPtr it, StmtPtr b, SourceLocation loc) : var(std::move(v)), iterable(std::move(it)), body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MatchCase { ExprPtr pattern; ExprPtr guard; StmtPtr body; MatchCase(ExprPtr p, ExprPtr g, StmtPtr b) : pattern(std::move(p)), guard(std::move(g)), body(std::move(b)) {} };
struct MatchStmt : Statement { ExprPtr value; std::vector<MatchCase> cases; StmtPtr defaultCase; MatchStmt(ExprPtr v, SourceLocation loc) : value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ReturnStmt : Statement { ExprPtr value; ReturnStmt(ExprPtr v, SourceLocation loc) : value(std::move(v)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct BreakStmt : Statement { std::string label; BreakStmt(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ContinueStmt : Statement { std::string label; ContinueStmt(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct TryStmt : Statement { ExprPtr tryExpr; ExprPtr elseExpr; TryStmt(ExprPtr t, ExprPtr e, SourceLocation loc) : tryExpr(std::move(t)), elseExpr(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
// Calling convention for FFI
enum class CallingConvention {
    Default,    // Platform default (win64 on Windows)
    Cdecl,      // C calling convention
    Stdcall,    // Windows stdcall
    Fastcall,   // Fastcall convention
    Win64       // Windows x64 ABI
};

struct FnDecl : Statement { std::string name; std::vector<std::string> typeParams; std::vector<std::string> lifetimeParams; std::vector<std::pair<std::string, std::string>> params; std::vector<ExprPtr> paramDefaults; std::string returnType; StmtPtr body; bool isPublic = false; bool isExtern = false; bool isAsync = false; bool isHot = false; bool isCold = false; bool isVariadic = false; bool isNaked = false; bool isExport = false; bool isHidden = false; bool isWeak = false; bool isComptime = false; CallingConvention callingConv = CallingConvention::Default; FnDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; bool hasVariadicParams() const { for (const auto& p : params) { if (p.second == "...") return true; } return false; } };
// Bitfield specification for a record field
struct BitfieldSpec {
    int bitWidth = 0;          // Number of bits (0 = not a bitfield)
    bool isBitfield() const { return bitWidth > 0; }
};

struct RecordDecl : Statement { 
    std::string name; 
    std::vector<std::string> typeParams; 
    std::vector<std::pair<std::string, std::string>> fields; 
    std::vector<BitfieldSpec> bitfields;  // Bitfield specs for each field (parallel to fields)
    bool isPublic = false;
    // Attributes for FFI/layout control
    bool reprC = false;        // #[repr(C)] - C-compatible layout
    bool reprPacked = false;   // #[repr(packed)] - no padding
    int reprAlign = 0;         // #[repr(align(N))] - explicit alignment
    // Derive attribute for automatic trait implementation
    std::vector<std::string> deriveTraits;  // @derive(Debug, Clone, Eq)
    RecordDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } 
    void accept(ASTVisitor& visitor) override; 
};
struct UnionDecl : Statement { 
    std::string name; 
    std::vector<std::string> typeParams; 
    std::vector<std::pair<std::string, std::string>> fields; 
    bool isPublic = false;
    // Attributes for FFI/layout control
    bool reprC = false;        // #[repr(C)] - C-compatible layout
    int reprAlign = 0;         // #[repr(align(N))] - explicit alignment
    UnionDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } 
    void accept(ASTVisitor& visitor) override; 
};
struct EnumDecl : Statement { std::string name; std::vector<std::string> typeParams; std::vector<std::pair<std::string, std::optional<int64_t>>> variants; EnumDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Type parameter for dependent types - can be a type (T) or a value (N: int) or a type constructor (F[_])
struct DependentTypeParam {
    std::string name;           // Parameter name (e.g., "T" or "N" or "F")
    std::string kind;           // "type" for type params, or a type name for value params (e.g., "int"), or "type_constructor" for HKT
    bool isValue = false;       // true if this is a value parameter (N: int)
    bool isTypeConstructor = false;  // true if this is a type constructor (F[_])
    size_t constructorArity = 0;     // Number of type params for type constructor (1 for F[_], 2 for F[_, _])
    DependentTypeParam(std::string n, std::string k = "type", bool val = false) 
        : name(std::move(n)), kind(std::move(k)), isValue(val) {}
};

struct TypeAlias : Statement { 
    std::string name; 
    std::string targetType; 
    ExprPtr constraint;                              // where clause constraint (e.g., len(_) > 0)
    std::vector<DependentTypeParam> typeParams;     // Type and value parameters [T, N: int]
    TypeAlias(std::string n, std::string t, SourceLocation loc) : name(std::move(n)), targetType(std::move(t)) { location = loc; } 
    void accept(ASTVisitor& visitor) override; 
};
// Higher-Kinded Type parameter info for traits
struct HKTTypeParam {
    std::string name;           // Parameter name (e.g., "F", "M")
    size_t arity;               // Number of type args (1 for F[_], 2 for F[_, _])
    std::vector<std::string> bounds;  // Trait bounds (e.g., "Functor" for F[_]: Functor)
    HKTTypeParam(std::string n, size_t ar = 1) : name(std::move(n)), arity(ar) {}
};

// Associated type declaration in traits
// type Item                    // No default
// type Index = int             // With default
struct AssociatedTypeDecl {
    std::string name;           // Type name (e.g., "Item", "Index")
    std::string defaultType;    // Default type (empty if none)
    std::vector<std::string> bounds;  // Trait bounds (e.g., "Add" for type Item: Add)
    AssociatedTypeDecl(std::string n, std::string def = "") : name(std::move(n)), defaultType(std::move(def)) {}
};

struct TraitDecl : Statement { 
    std::string name; 
    std::vector<std::string> typeParams;           // Regular type parameters (T, U, etc.)
    std::vector<HKTTypeParam> hktTypeParams;       // Higher-kinded type parameters (F[_], M[_], etc.)
    std::vector<std::string> superTraits; 
    std::vector<AssociatedTypeDecl> associatedTypes;  // Associated type declarations
    std::vector<std::unique_ptr<FnDecl>> methods; 
    TraitDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } 
    void accept(ASTVisitor& visitor) override; 
};

// Concept declaration for type classes / constrained generics
// concept Numeric[T]:
//     fn add(T, T) -> T
//     fn zero() -> T
struct ConceptRequirement {
    std::string name;                                    // Function name (e.g., "add", "zero")
    std::vector<std::pair<std::string, std::string>> params;  // Parameters (name, type)
    std::string returnType;                              // Return type
    bool isStatic = false;                               // Static function (no self parameter)
    ConceptRequirement(std::string n) : name(std::move(n)) {}
};

struct ConceptDecl : Statement {
    std::string name;                                    // Concept name (e.g., "Numeric", "Orderable")
    std::vector<std::string> typeParams;                 // Type parameters [T]
    std::vector<std::string> superConcepts;              // Inherited concepts
    std::vector<ConceptRequirement> requirements;        // Required functions
    ConceptDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};
// Associated type binding in impl blocks: type Item = int
struct AssociatedTypeBinding {
    std::string name;           // Type name (e.g., "Item")
    std::string boundType;      // Concrete type (e.g., "int")
    AssociatedTypeBinding(std::string n, std::string t) : name(std::move(n)), boundType(std::move(t)) {}
};

struct ImplBlock : Statement { std::string traitName; std::string typeName; std::vector<std::string> typeParams; std::vector<AssociatedTypeBinding> associatedTypes; std::vector<std::unique_ptr<FnDecl>> methods; ImplBlock(std::string trait, std::string type, SourceLocation loc) : traitName(std::move(trait)), typeName(std::move(type)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct UnsafeBlock : Statement { StmtPtr body; UnsafeBlock(StmtPtr b, SourceLocation loc) : body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ImportStmt : Statement { std::string path; std::string alias; std::vector<std::string> items; ImportStmt(std::string p, SourceLocation loc) : path(std::move(p)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct ExternDecl : Statement { std::string abi; std::string library; std::vector<std::unique_ptr<FnDecl>> functions; ExternDecl(std::string a, std::string lib, SourceLocation loc) : abi(std::move(a)), library(std::move(lib)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct MacroDecl : Statement { std::string name; std::vector<std::string> params; std::vector<StmtPtr> body; bool isOperator = false; std::string operatorSymbol; int precedence = 0; bool isInfix = false; bool isPrefix = false; bool isPostfix = false; MacroDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct SyntaxMacroDecl : Statement { std::string name; std::vector<StmtPtr> body; std::string transformExpr; SyntaxMacroDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct LayerDecl : Statement { std::string name; std::vector<StmtPtr> declarations; LayerDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct UseStmt : Statement { 
    std::string layerName; 
    bool isLayer = false; 
    bool isFileImport = false; 
    std::vector<std::string> importItems;  // For selective imports: use math::{sin, cos}
    std::string alias;  // For aliased imports: use math as m
    UseStmt(std::string n, SourceLocation loc) : layerName(std::move(n)) { location = loc; } 
    void accept(ASTVisitor& visitor) override; 
};
struct ModuleDecl : Statement {
    std::string name;
    bool isPublic = true;
    std::vector<StmtPtr> body;
    ModuleDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};
struct DeleteStmt : Statement { ExprPtr expr; DeleteStmt(ExprPtr e, SourceLocation loc) : expr(std::move(e)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct LockStmt : Statement { ExprPtr mutex; StmtPtr body; LockStmt(ExprPtr m, StmtPtr b, SourceLocation loc) : mutex(std::move(m)), body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct AsmStmt : Statement { std::string code; std::vector<std::string> outputs; std::vector<std::string> inputs; std::vector<std::string> clobbers; AsmStmt(std::string c, SourceLocation loc) : code(std::move(c)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Syntax Redesign - New Statement Types
// Loop statement (infinite loop)
struct LoopStmt : Statement { std::string label; StmtPtr body; LoopStmt(StmtPtr b, SourceLocation loc) : body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// With statement (resource management)
struct WithStmt : Statement { ExprPtr resource; std::string alias; StmtPtr body; WithStmt(ExprPtr r, std::string a, StmtPtr b, SourceLocation loc) : resource(std::move(r)), alias(std::move(a)), body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Scope statement (structured concurrency)
struct ScopeStmt : Statement { std::string label; ExprPtr timeout; StmtPtr body; ScopeStmt(StmtPtr b, SourceLocation loc) : body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Contract statements
struct RequireStmt : Statement { ExprPtr condition; std::string message; RequireStmt(ExprPtr c, std::string m, SourceLocation loc) : condition(std::move(c)), message(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct EnsureStmt : Statement { ExprPtr condition; std::string message; EnsureStmt(ExprPtr c, std::string m, SourceLocation loc) : condition(std::move(c)), message(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };
struct InvariantStmt : Statement { ExprPtr condition; std::string message; InvariantStmt(ExprPtr c, std::string m, SourceLocation loc) : condition(std::move(c)), message(std::move(m)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Comptime block (compile-time execution)
struct ComptimeBlock : Statement { StmtPtr body; ComptimeBlock(StmtPtr b, SourceLocation loc) : body(std::move(b)) { location = loc; } void accept(ASTVisitor& visitor) override; };

// Compile-time assertion
// comptime assert sizeof(Header) == 16, "Header must be 16 bytes"
struct ComptimeAssertStmt : Statement { 
    ExprPtr condition; 
    std::string message; 
    ComptimeAssertStmt(ExprPtr c, std::string m, SourceLocation loc) : condition(std::move(c)), message(std::move(m)) { location = loc; } 
    void accept(ASTVisitor& visitor) override; 
};

// Algebraic Effects - Effect Declaration
// effect Error[E]:
//     fn raise e: E -> never
struct EffectOpDecl {
    std::string name;                                    // Operation name (e.g., "raise", "get", "put")
    std::vector<std::pair<std::string, std::string>> params;  // Parameters
    std::string returnType;                              // Return type (can be "never" for non-returning ops)
    EffectOpDecl(std::string n) : name(std::move(n)) {}
};

struct EffectDecl : Statement {
    std::string name;                                    // Effect name (e.g., "Error", "State", "Async")
    std::vector<std::string> typeParams;                 // Type parameters [E], [S], etc.
    std::vector<EffectOpDecl> operations;                // Effect operations
    EffectDecl(std::string n, SourceLocation loc) : name(std::move(n)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// Algebraic Effects - Perform Effect Operation
// raise("division by zero")  or  get()  or  put(n + 1)
struct PerformEffectExpr : Expression {
    std::string effectName;                              // Effect name (e.g., "Error", "State")
    std::string opName;                                  // Operation name (e.g., "raise", "get", "put")
    std::vector<ExprPtr> args;                           // Arguments to the operation
    PerformEffectExpr(std::string effect, std::string op, SourceLocation loc) 
        : effectName(std::move(effect)), opName(std::move(op)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// Algebraic Effects - Effect Handler Case
// Error.raise(e) => println("Error: {e}"); 0
struct EffectHandlerCase {
    std::string effectName;                              // Effect name
    std::string opName;                                  // Operation name
    std::vector<std::string> paramNames;                 // Bound parameter names
    std::string resumeParam;                             // Optional resume continuation parameter
    StmtPtr body;                                        // Handler body
    EffectHandlerCase(std::string effect, std::string op) 
        : effectName(std::move(effect)), opName(std::move(op)) {}
};

// Algebraic Effects - Handle Expression
// handle divide(10, 0):
//     Error.raise(e) => println("Error: {e}"); 0
struct HandleExpr : Expression {
    ExprPtr expr;                                        // Expression to handle
    std::vector<EffectHandlerCase> handlers;             // Effect handlers
    HandleExpr(ExprPtr e, SourceLocation loc) : expr(std::move(e)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// Algebraic Effects - Resume Expression (continue from effect handler)
// resume(value)  - continues the computation with the given value
struct ResumeExpr : Expression {
    ExprPtr value;                                       // Value to resume with
    ResumeExpr(ExprPtr v, SourceLocation loc) : value(std::move(v)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// Compile-Time Reflection Expressions
// Type metadata access: T.__name__, T.__fields__, T.__methods__
struct TypeMetadataExpr : Expression {
    std::string typeName;                                // Type name (e.g., "Point")
    std::string metadataKind;                            // "name", "fields", "methods", "size", "align"
    TypeMetadataExpr(std::string type, std::string kind, SourceLocation loc) 
        : typeName(std::move(type)), metadataKind(std::move(kind)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// fields_of[T]() - returns list of (name, type) tuples for record fields
struct FieldsOfExpr : Expression {
    std::string typeName;                                // Type to introspect
    FieldsOfExpr(std::string type, SourceLocation loc) : typeName(std::move(type)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// methods_of[T]() - returns list of method names for a type
struct MethodsOfExpr : Expression {
    std::string typeName;                                // Type to introspect
    MethodsOfExpr(std::string type, SourceLocation loc) : typeName(std::move(type)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// has_field[T](name) - check if type has a field with given name
struct HasFieldExpr : Expression {
    std::string typeName;                                // Type to check
    ExprPtr fieldName;                                   // Field name expression (string)
    HasFieldExpr(std::string type, ExprPtr name, SourceLocation loc) 
        : typeName(std::move(type)), fieldName(std::move(name)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// has_method[T](name) - check if type has a method with given name
struct HasMethodExpr : Expression {
    std::string typeName;                                // Type to check
    ExprPtr methodName;                                  // Method name expression (string)
    HasMethodExpr(std::string type, ExprPtr name, SourceLocation loc) 
        : typeName(std::move(type)), methodName(std::move(name)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// field_type[T](name) - get the type of a field
struct FieldTypeExpr : Expression {
    std::string typeName;                                // Type to introspect
    ExprPtr fieldName;                                   // Field name expression (string)
    FieldTypeExpr(std::string type, ExprPtr name, SourceLocation loc) 
        : typeName(std::move(type)), fieldName(std::move(name)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// New Syntax Enhancements

// If-let statement: if let pattern = expr: body
// Combines pattern matching with conditional execution
struct IfLetStmt : Statement {
    std::string varName;                                 // Variable to bind
    ExprPtr value;                                       // Expression to match
    ExprPtr guard;                                       // Optional guard condition (and condition)
    StmtPtr thenBranch;                                  // Body if match succeeds
    StmtPtr elseBranch;                                  // Optional else branch
    IfLetStmt(std::string var, ExprPtr val, StmtPtr then, SourceLocation loc)
        : varName(std::move(var)), value(std::move(val)), thenBranch(std::move(then)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// Multi-variable declaration: a = b = c = 0
// All variables get the same value
struct MultiVarDecl : Statement {
    std::vector<std::string> names;                      // Variable names
    ExprPtr initializer;                                 // Shared initial value
    bool isMutable = true;                               // mut a = mut b = 0
    bool isConst = false;                                // A :: B :: 100
    MultiVarDecl(std::vector<std::string> n, ExprPtr init, SourceLocation loc)
        : names(std::move(n)), initializer(std::move(init)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

// Walrus expression: (n := len(items)) > 0
// Assignment that returns the assigned value
struct WalrusExpr : Expression {
    std::string varName;                                 // Variable to assign
    ExprPtr value;                                       // Value to assign
    WalrusExpr(std::string var, ExprPtr val, SourceLocation loc)
        : varName(std::move(var)), value(std::move(val)) { location = loc; }
    void accept(ASTVisitor& visitor) override;
};

struct Program : ASTNode { std::vector<StmtPtr> statements; Program(SourceLocation loc) { location = loc; } void accept(ASTVisitor& visitor) override; };

struct ASTVisitor {
    virtual ~ASTVisitor() = default;
    virtual void visit(IntegerLiteral& node) = 0; virtual void visit(FloatLiteral& node) = 0;
    virtual void visit(StringLiteral& node) = 0; virtual void visit(CharLiteral& node) = 0;
    virtual void visit(ByteStringLiteral& node) = 0; virtual void visit(InterpolatedString& node) = 0;
    virtual void visit(BoolLiteral& node) = 0; virtual void visit(NilLiteral& node) = 0;
    virtual void visit(Identifier& node) = 0; virtual void visit(BinaryExpr& node) = 0;
    virtual void visit(UnaryExpr& node) = 0; virtual void visit(CallExpr& node) = 0;
    virtual void visit(MemberExpr& node) = 0; virtual void visit(IndexExpr& node) = 0;
    virtual void visit(ListExpr& node) = 0; virtual void visit(RecordExpr& node) = 0;
    virtual void visit(MapExpr& node) = 0;
    virtual void visit(RangeExpr& node) = 0; virtual void visit(LambdaExpr& node) = 0;
    virtual void visit(TernaryExpr& node) = 0; virtual void visit(ListCompExpr& node) = 0;
    virtual void visit(AddressOfExpr& node) = 0; virtual void visit(BorrowExpr& node) = 0; virtual void visit(DerefExpr& node) = 0;
    virtual void visit(NewExpr& node) = 0; virtual void visit(CastExpr& node) = 0;
    virtual void visit(AwaitExpr& node) = 0; virtual void visit(SpawnExpr& node) = 0;
    virtual void visit(DSLBlock& node) = 0; virtual void visit(AssignExpr& node) = 0;
    virtual void visit(PropagateExpr& node) = 0;
    virtual void visit(ChanSendExpr& node) = 0;
    virtual void visit(ChanRecvExpr& node) = 0;
    virtual void visit(MakeChanExpr& node) = 0;
    virtual void visit(MakeMutexExpr& node) = 0;
    virtual void visit(MakeRWLockExpr& node) = 0;
    virtual void visit(MakeCondExpr& node) = 0;
    virtual void visit(MakeSemaphoreExpr& node) = 0;
    virtual void visit(MutexLockExpr& node) = 0;
    virtual void visit(MutexUnlockExpr& node) = 0;
    virtual void visit(RWLockReadExpr& node) = 0;
    virtual void visit(RWLockWriteExpr& node) = 0;
    virtual void visit(RWLockUnlockExpr& node) = 0;
    virtual void visit(CondWaitExpr& node) = 0;
    virtual void visit(CondSignalExpr& node) = 0;
    virtual void visit(CondBroadcastExpr& node) = 0;
    virtual void visit(SemAcquireExpr& node) = 0;
    virtual void visit(SemReleaseExpr& node) = 0;
    virtual void visit(SemTryAcquireExpr& node) = 0;
    virtual void visit(MakeAtomicExpr& node) = 0;
    virtual void visit(AtomicLoadExpr& node) = 0;
    virtual void visit(AtomicStoreExpr& node) = 0;
    virtual void visit(AtomicSwapExpr& node) = 0;
    virtual void visit(AtomicCasExpr& node) = 0;
    virtual void visit(AtomicAddExpr& node) = 0;
    virtual void visit(AtomicSubExpr& node) = 0;
    virtual void visit(AtomicAndExpr& node) = 0;
    virtual void visit(AtomicOrExpr& node) = 0;
    virtual void visit(AtomicXorExpr& node) = 0;
    // Smart Pointer expressions
    virtual void visit(MakeBoxExpr& node) = 0;
    virtual void visit(MakeRcExpr& node) = 0;
    virtual void visit(MakeArcExpr& node) = 0;
    virtual void visit(MakeWeakExpr& node) = 0;
    virtual void visit(MakeCellExpr& node) = 0;
    virtual void visit(MakeRefCellExpr& node) = 0;
    // Advanced Concurrency - Future/Promise
    virtual void visit(MakeFutureExpr& node) = 0;
    virtual void visit(FutureGetExpr& node) = 0;
    virtual void visit(FutureSetExpr& node) = 0;
    virtual void visit(FutureIsReadyExpr& node) = 0;
    // Advanced Concurrency - Thread Pool
    virtual void visit(MakeThreadPoolExpr& node) = 0;
    virtual void visit(ThreadPoolSubmitExpr& node) = 0;
    virtual void visit(ThreadPoolShutdownExpr& node) = 0;
    // Advanced Concurrency - Select
    virtual void visit(SelectExpr& node) = 0;
    // Advanced Concurrency - Timeout
    virtual void visit(TimeoutExpr& node) = 0;
    virtual void visit(ChanRecvTimeoutExpr& node) = 0;
    virtual void visit(ChanSendTimeoutExpr& node) = 0;
    // Advanced Concurrency - Cancellation
    virtual void visit(MakeCancelTokenExpr& node) = 0;
    virtual void visit(CancelExpr& node) = 0;
    virtual void visit(IsCancelledExpr& node) = 0;
    // Async Runtime - Event Loop and Task Management
    virtual void visit(AsyncRuntimeInitExpr& node) = 0;
    virtual void visit(AsyncRuntimeRunExpr& node) = 0;
    virtual void visit(AsyncRuntimeShutdownExpr& node) = 0;
    virtual void visit(AsyncSpawnExpr& node) = 0;
    virtual void visit(AsyncSleepExpr& node) = 0;
    virtual void visit(AsyncYieldExpr& node) = 0;
    virtual void visit(ExprStmt& node) = 0;
    virtual void visit(VarDecl& node) = 0; virtual void visit(DestructuringDecl& node) = 0;
    virtual void visit(AssignStmt& node) = 0; virtual void visit(Block& node) = 0;
    virtual void visit(IfStmt& node) = 0; virtual void visit(WhileStmt& node) = 0;
    virtual void visit(ForStmt& node) = 0; virtual void visit(MatchStmt& node) = 0;
    virtual void visit(ReturnStmt& node) = 0; virtual void visit(BreakStmt& node) = 0;
    virtual void visit(ContinueStmt& node) = 0; virtual void visit(TryStmt& node) = 0;
    virtual void visit(FnDecl& node) = 0; virtual void visit(RecordDecl& node) = 0;
    virtual void visit(UnionDecl& node) = 0;
    virtual void visit(EnumDecl& node) = 0; virtual void visit(TypeAlias& node) = 0;
    virtual void visit(TraitDecl& node) = 0; virtual void visit(ImplBlock& node) = 0;
    virtual void visit(ConceptDecl& node) = 0;
    virtual void visit(UnsafeBlock& node) = 0; virtual void visit(ImportStmt& node) = 0;
    virtual void visit(ExternDecl& node) = 0; virtual void visit(MacroDecl& node) = 0;
    virtual void visit(SyntaxMacroDecl& node) = 0; virtual void visit(LayerDecl& node) = 0;
    virtual void visit(UseStmt& node) = 0; 
    virtual void visit(ModuleDecl& node) = 0;
    virtual void visit(DeleteStmt& node) = 0;
    virtual void visit(LockStmt& node) = 0;
    virtual void visit(AsmStmt& node) = 0;
    // Syntax Redesign - New Expression Visitors
    virtual void visit(PlaceholderExpr& node) = 0;
    virtual void visit(InclusiveRangeExpr& node) = 0;
    virtual void visit(SafeNavExpr& node) = 0;
    virtual void visit(TypeCheckExpr& node) = 0;
    // Syntax Redesign - New Statement Visitors
    virtual void visit(LoopStmt& node) = 0;
    virtual void visit(WithStmt& node) = 0;
    virtual void visit(ScopeStmt& node) = 0;
    virtual void visit(RequireStmt& node) = 0;
    virtual void visit(EnsureStmt& node) = 0;
    virtual void visit(InvariantStmt& node) = 0;
    virtual void visit(ComptimeBlock& node) = 0;
    virtual void visit(ComptimeAssertStmt& node) = 0;
    // Algebraic Effects
    virtual void visit(EffectDecl& node) = 0;
    virtual void visit(PerformEffectExpr& node) = 0;
    virtual void visit(HandleExpr& node) = 0;
    virtual void visit(ResumeExpr& node) = 0;
    // Compile-Time Reflection
    virtual void visit(TypeMetadataExpr& node) = 0;
    virtual void visit(FieldsOfExpr& node) = 0;
    virtual void visit(MethodsOfExpr& node) = 0;
    virtual void visit(HasFieldExpr& node) = 0;
    virtual void visit(HasMethodExpr& node) = 0;
    virtual void visit(FieldTypeExpr& node) = 0;
    // New Syntax Enhancements
    virtual void visit(IfLetStmt& node) = 0;
    virtual void visit(MultiVarDecl& node) = 0;
    virtual void visit(WalrusExpr& node) = 0;
    virtual void visit(Program& node) = 0;
};

} // namespace tyl

#endif // TYL_AST_H
