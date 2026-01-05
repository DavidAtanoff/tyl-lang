// Tyl Compiler - AST Cloner for Monomorphization
// Deep clones AST nodes with type parameter substitution
#ifndef TYL_AST_CLONER_H
#define TYL_AST_CLONER_H

#include "frontend/ast/ast.h"
#include "semantic/types/types.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace tyl {

// Deep clones AST nodes with type parameter substitution
class ASTCloner {
public:
    ASTCloner(const std::vector<std::string>& typeParams,
              const std::vector<TypePtr>& typeArgs);
    
    // Clone expressions
    ExprPtr clone(Expression* expr);
    
    // Clone statements
    StmtPtr clone(Statement* stmt);
    
    // Clone a function body for monomorphization
    StmtPtr cloneFunctionBody(Statement* body);
    
private:
    std::unordered_map<std::string, std::string> typeSubstitutions_;
    
    // Substitute type parameters in a type string
    std::string substituteType(const std::string& typeStr);
    
    // Expression cloning
    ExprPtr cloneExpr(IntegerLiteral* node);
    ExprPtr cloneExpr(FloatLiteral* node);
    ExprPtr cloneExpr(StringLiteral* node);
    ExprPtr cloneExpr(InterpolatedString* node);
    ExprPtr cloneExpr(BoolLiteral* node);
    ExprPtr cloneExpr(NilLiteral* node);
    ExprPtr cloneExpr(Identifier* node);
    ExprPtr cloneExpr(BinaryExpr* node);
    ExprPtr cloneExpr(UnaryExpr* node);
    ExprPtr cloneExpr(CallExpr* node);
    ExprPtr cloneExpr(MemberExpr* node);
    ExprPtr cloneExpr(IndexExpr* node);
    ExprPtr cloneExpr(ListExpr* node);
    ExprPtr cloneExpr(RecordExpr* node);
    ExprPtr cloneExpr(MapExpr* node);
    ExprPtr cloneExpr(RangeExpr* node);
    ExprPtr cloneExpr(LambdaExpr* node);
    ExprPtr cloneExpr(TernaryExpr* node);
    ExprPtr cloneExpr(ListCompExpr* node);
    ExprPtr cloneExpr(AddressOfExpr* node);
    ExprPtr cloneExpr(DerefExpr* node);
    ExprPtr cloneExpr(NewExpr* node);
    ExprPtr cloneExpr(CastExpr* node);
    ExprPtr cloneExpr(AwaitExpr* node);
    ExprPtr cloneExpr(SpawnExpr* node);
    ExprPtr cloneExpr(DSLBlock* node);
    ExprPtr cloneExpr(AssignExpr* node);
    ExprPtr cloneExpr(PropagateExpr* node);
    
    // Statement cloning
    StmtPtr cloneStmt(ExprStmt* node);
    StmtPtr cloneStmt(VarDecl* node);
    StmtPtr cloneStmt(DestructuringDecl* node);
    StmtPtr cloneStmt(AssignStmt* node);
    StmtPtr cloneStmt(Block* node);
    StmtPtr cloneStmt(IfStmt* node);
    StmtPtr cloneStmt(WhileStmt* node);
    StmtPtr cloneStmt(ForStmt* node);
    StmtPtr cloneStmt(MatchStmt* node);
    StmtPtr cloneStmt(ReturnStmt* node);
    StmtPtr cloneStmt(BreakStmt* node);
    StmtPtr cloneStmt(ContinueStmt* node);
    StmtPtr cloneStmt(TryStmt* node);
    StmtPtr cloneStmt(UnsafeBlock* node);
    StmtPtr cloneStmt(DeleteStmt* node);
};

} // namespace tyl

#endif // TYL_AST_CLONER_H
