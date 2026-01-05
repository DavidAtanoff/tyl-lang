// Tyl Compiler - AST Cloner Implementation
// Deep clones AST nodes with type parameter substitution for monomorphization

#include "ast_cloner.h"
#include <algorithm>

namespace tyl {

ASTCloner::ASTCloner(const std::vector<std::string>& typeParams,
                     const std::vector<TypePtr>& typeArgs) {
    // Build substitution map
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); i++) {
        typeSubstitutions_[typeParams[i]] = typeArgs[i]->toString();
    }
}

std::string ASTCloner::substituteType(const std::string& typeStr) {
    std::string result = typeStr;
    
    for (const auto& [param, arg] : typeSubstitutions_) {
        size_t pos = 0;
        while ((pos = result.find(param, pos)) != std::string::npos) {
            // Check if it's a whole word match
            bool isWordStart = (pos == 0 || !std::isalnum(result[pos - 1]));
            bool isWordEnd = (pos + param.length() >= result.length() || 
                             !std::isalnum(result[pos + param.length()]));
            if (isWordStart && isWordEnd) {
                result.replace(pos, param.length(), arg);
                pos += arg.length();
            } else {
                pos++;
            }
        }
    }
    
    return result;
}

ExprPtr ASTCloner::clone(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* p = dynamic_cast<IntegerLiteral*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<FloatLiteral*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<StringLiteral*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<InterpolatedString*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<BoolLiteral*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<NilLiteral*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<Identifier*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<BinaryExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<UnaryExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<CallExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<MemberExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<IndexExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<ListExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<RecordExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<MapExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<RangeExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<LambdaExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<TernaryExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<ListCompExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<AddressOfExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<DerefExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<NewExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<CastExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<AwaitExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<SpawnExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<DSLBlock*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<AssignExpr*>(expr)) return cloneExpr(p);
    if (auto* p = dynamic_cast<PropagateExpr*>(expr)) return cloneExpr(p);
    
    return nullptr;
}

StmtPtr ASTCloner::clone(Statement* stmt) {
    if (!stmt) return nullptr;
    
    if (auto* p = dynamic_cast<ExprStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<VarDecl*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<DestructuringDecl*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<AssignStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<Block*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<IfStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<WhileStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<ForStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<MatchStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<ReturnStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<BreakStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<ContinueStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<TryStmt*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<UnsafeBlock*>(stmt)) return cloneStmt(p);
    if (auto* p = dynamic_cast<DeleteStmt*>(stmt)) return cloneStmt(p);
    
    return nullptr;
}

StmtPtr ASTCloner::cloneFunctionBody(Statement* body) {
    return clone(body);
}

// Expression cloning implementations

ExprPtr ASTCloner::cloneExpr(IntegerLiteral* node) {
    return std::make_unique<IntegerLiteral>(node->value, node->location);
}

ExprPtr ASTCloner::cloneExpr(FloatLiteral* node) {
    return std::make_unique<FloatLiteral>(node->value, node->location);
}

ExprPtr ASTCloner::cloneExpr(StringLiteral* node) {
    return std::make_unique<StringLiteral>(node->value, node->location);
}

ExprPtr ASTCloner::cloneExpr(InterpolatedString* node) {
    auto result = std::make_unique<InterpolatedString>(node->location);
    for (const auto& part : node->parts) {
        if (auto* strPtr = std::get_if<std::string>(&part)) {
            result->parts.push_back(*strPtr);
        } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
            result->parts.push_back(clone(exprPtr->get()));
        }
    }
    return result;
}

ExprPtr ASTCloner::cloneExpr(BoolLiteral* node) {
    return std::make_unique<BoolLiteral>(node->value, node->location);
}

ExprPtr ASTCloner::cloneExpr(NilLiteral* node) {
    return std::make_unique<NilLiteral>(node->location);
}

ExprPtr ASTCloner::cloneExpr(Identifier* node) {
    return std::make_unique<Identifier>(node->name, node->location);
}

ExprPtr ASTCloner::cloneExpr(BinaryExpr* node) {
    return std::make_unique<BinaryExpr>(
        clone(node->left.get()),
        node->op,
        clone(node->right.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(UnaryExpr* node) {
    return std::make_unique<UnaryExpr>(
        node->op,
        clone(node->operand.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(CallExpr* node) {
    auto result = std::make_unique<CallExpr>(
        clone(node->callee.get()),
        node->location
    );
    for (auto& arg : node->args) {
        result->args.push_back(clone(arg.get()));
    }
    for (auto& namedArg : node->namedArgs) {
        result->namedArgs.push_back({namedArg.first, clone(namedArg.second.get())});
    }
    result->typeArgs = node->typeArgs;
    result->isHotCallSite = node->isHotCallSite;
    return result;
}

ExprPtr ASTCloner::cloneExpr(MemberExpr* node) {
    return std::make_unique<MemberExpr>(
        clone(node->object.get()),
        node->member,
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(IndexExpr* node) {
    return std::make_unique<IndexExpr>(
        clone(node->object.get()),
        clone(node->index.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(ListExpr* node) {
    auto result = std::make_unique<ListExpr>(node->location);
    for (auto& elem : node->elements) {
        result->elements.push_back(clone(elem.get()));
    }
    return result;
}

ExprPtr ASTCloner::cloneExpr(RecordExpr* node) {
    auto result = std::make_unique<RecordExpr>(node->location);
    for (auto& field : node->fields) {
        result->fields.push_back({field.first, clone(field.second.get())});
    }
    return result;
}

ExprPtr ASTCloner::cloneExpr(MapExpr* node) {
    auto result = std::make_unique<MapExpr>(node->location);
    for (auto& entry : node->entries) {
        result->entries.push_back({clone(entry.first.get()), clone(entry.second.get())});
    }
    return result;
}

ExprPtr ASTCloner::cloneExpr(RangeExpr* node) {
    return std::make_unique<RangeExpr>(
        clone(node->start.get()),
        clone(node->end.get()),
        node->step ? clone(node->step.get()) : nullptr,
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(LambdaExpr* node) {
    auto result = std::make_unique<LambdaExpr>(node->location);
    result->params = node->params;  // Copy parameter names and types
    result->body = clone(node->body.get());
    return result;
}

ExprPtr ASTCloner::cloneExpr(TernaryExpr* node) {
    return std::make_unique<TernaryExpr>(
        clone(node->condition.get()),
        clone(node->thenExpr.get()),
        clone(node->elseExpr.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(ListCompExpr* node) {
    return std::make_unique<ListCompExpr>(
        clone(node->expr.get()),
        node->var,
        clone(node->iterable.get()),
        node->condition ? clone(node->condition.get()) : nullptr,
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(AddressOfExpr* node) {
    return std::make_unique<AddressOfExpr>(
        clone(node->operand.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(DerefExpr* node) {
    return std::make_unique<DerefExpr>(
        clone(node->operand.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(NewExpr* node) {
    auto result = std::make_unique<NewExpr>(
        substituteType(node->typeName),
        node->location
    );
    for (auto& arg : node->args) {
        result->args.push_back(clone(arg.get()));
    }
    return result;
}

ExprPtr ASTCloner::cloneExpr(CastExpr* node) {
    return std::make_unique<CastExpr>(
        clone(node->expr.get()),
        substituteType(node->targetType),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(AwaitExpr* node) {
    return std::make_unique<AwaitExpr>(
        clone(node->operand.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(SpawnExpr* node) {
    return std::make_unique<SpawnExpr>(
        clone(node->operand.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(DSLBlock* node) {
    return std::make_unique<DSLBlock>(
        node->dslName,
        node->rawContent,
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(AssignExpr* node) {
    return std::make_unique<AssignExpr>(
        clone(node->target.get()),
        node->op,
        clone(node->value.get()),
        node->location
    );
}

ExprPtr ASTCloner::cloneExpr(PropagateExpr* node) {
    return std::make_unique<PropagateExpr>(
        clone(node->operand.get()),
        node->location
    );
}

// Statement cloning implementations

StmtPtr ASTCloner::cloneStmt(ExprStmt* node) {
    return std::make_unique<ExprStmt>(
        clone(node->expr.get()),
        node->location
    );
}

StmtPtr ASTCloner::cloneStmt(VarDecl* node) {
    auto result = std::make_unique<VarDecl>(
        node->name,
        substituteType(node->typeName),
        node->initializer ? clone(node->initializer.get()) : nullptr,
        node->location
    );
    result->isMutable = node->isMutable;
    result->isConst = node->isConst;
    return result;
}

StmtPtr ASTCloner::cloneStmt(DestructuringDecl* node) {
    auto result = std::make_unique<DestructuringDecl>(
        node->kind,
        node->names,
        node->initializer ? clone(node->initializer.get()) : nullptr,
        node->location
    );
    result->isMutable = node->isMutable;
    return result;
}

StmtPtr ASTCloner::cloneStmt(AssignStmt* node) {
    return std::make_unique<AssignStmt>(
        clone(node->target.get()),
        node->op,
        clone(node->value.get()),
        node->location
    );
}

StmtPtr ASTCloner::cloneStmt(Block* node) {
    auto result = std::make_unique<Block>(node->location);
    for (auto& stmt : node->statements) {
        result->statements.push_back(clone(stmt.get()));
    }
    return result;
}

StmtPtr ASTCloner::cloneStmt(IfStmt* node) {
    auto result = std::make_unique<IfStmt>(
        clone(node->condition.get()),
        clone(node->thenBranch.get()),
        node->location
    );
    for (auto& elif : node->elifBranches) {
        result->elifBranches.push_back({
            clone(elif.first.get()),
            clone(elif.second.get())
        });
    }
    if (node->elseBranch) {
        result->elseBranch = clone(node->elseBranch.get());
    }
    return result;
}

StmtPtr ASTCloner::cloneStmt(WhileStmt* node) {
    return std::make_unique<WhileStmt>(
        clone(node->condition.get()),
        clone(node->body.get()),
        node->location
    );
}

StmtPtr ASTCloner::cloneStmt(ForStmt* node) {
    auto result = std::make_unique<ForStmt>(
        node->var,
        clone(node->iterable.get()),
        clone(node->body.get()),
        node->location
    );
    result->unrollHint = node->unrollHint;
    return result;
}

StmtPtr ASTCloner::cloneStmt(MatchStmt* node) {
    auto result = std::make_unique<MatchStmt>(
        clone(node->value.get()),
        node->location
    );
    for (auto& c : node->cases) {
        result->cases.emplace_back(
            c.pattern ? clone(c.pattern.get()) : nullptr,
            c.guard ? clone(c.guard.get()) : nullptr,
            c.body ? clone(c.body.get()) : nullptr
        );
    }
    if (node->defaultCase) {
        result->defaultCase = clone(node->defaultCase.get());
    }
    return result;
}

StmtPtr ASTCloner::cloneStmt(ReturnStmt* node) {
    return std::make_unique<ReturnStmt>(
        node->value ? clone(node->value.get()) : nullptr,
        node->location
    );
}

StmtPtr ASTCloner::cloneStmt(BreakStmt* node) {
    return std::make_unique<BreakStmt>(node->location);
}

StmtPtr ASTCloner::cloneStmt(ContinueStmt* node) {
    return std::make_unique<ContinueStmt>(node->location);
}

StmtPtr ASTCloner::cloneStmt(TryStmt* node) {
    return std::make_unique<TryStmt>(
        clone(node->tryExpr.get()),
        node->elseExpr ? clone(node->elseExpr.get()) : nullptr,
        node->location
    );
}

StmtPtr ASTCloner::cloneStmt(UnsafeBlock* node) {
    return std::make_unique<UnsafeBlock>(
        node->body ? clone(node->body.get()) : nullptr,
        node->location
    );
}

StmtPtr ASTCloner::cloneStmt(DeleteStmt* node) {
    return std::make_unique<DeleteStmt>(
        clone(node->expr.get()),
        node->location
    );
}

} // namespace tyl
