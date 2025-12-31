// Flex Compiler - Macro Expander Clone Functions
// Expression and statement cloning with parameter substitution

#include "expander_base.h"

namespace flex {

ExprPtr MacroExpander::cloneExpr(Expression* expr, const std::unordered_map<std::string, Expression*>& params) {
    if (!expr) return nullptr;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = params.find(ident->name);
        if (it != params.end()) {
            return cloneExpr(it->second, {});
        }
        return std::make_unique<Identifier>(ident->name, ident->location);
    }
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(intLit->value, intLit->location);
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(floatLit->value, floatLit->location);
    }
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(strLit->value, strLit->location);
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, boolLit->location);
    }
    if (auto* nilLit = dynamic_cast<NilLiteral*>(expr)) {
        return std::make_unique<NilLiteral>(nilLit->location);
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpr(binary->left.get(), params), binary->op,
            cloneExpr(binary->right.get(), params), binary->location);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op, cloneExpr(unary->operand.get(), params), unary->location);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(cloneExpr(call->callee.get(), params), call->location);
        for (auto& arg : call->args) newCall->args.push_back(cloneExpr(arg.get(), params));
        return newCall;
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpr(member->object.get(), params), member->member, member->location);
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpr(index->object.get(), params),
            cloneExpr(index->index.get(), params), index->location);
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            cloneExpr(ternary->condition.get(), params),
            cloneExpr(ternary->thenExpr.get(), params),
            cloneExpr(ternary->elseExpr.get(), params), ternary->location);
    }
    
    if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        auto newInterp = std::make_unique<InterpolatedString>(interp->location);
        for (auto& part : interp->parts) {
            if (auto* str = std::get_if<std::string>(&part)) {
                newInterp->parts.push_back(*str);
            } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
                newInterp->parts.push_back(cloneExpr(exprPtr->get(), params));
            }
        }
        return newInterp;
    }
    
    if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        auto newList = std::make_unique<ListExpr>(list->location);
        for (auto& elem : list->elements) {
            newList->elements.push_back(cloneExpr(elem.get(), params));
        }
        return newList;
    }
    
    if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        auto newRecord = std::make_unique<RecordExpr>(record->location);
        for (auto& field : record->fields) {
            newRecord->fields.push_back({field.first, cloneExpr(field.second.get(), params)});
        }
        return newRecord;
    }
    
    if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        return std::make_unique<RangeExpr>(
            cloneExpr(range->start.get(), params),
            cloneExpr(range->end.get(), params),
            range->step ? cloneExpr(range->step.get(), params) : nullptr,
            range->location);
    }
    
    if (auto* lambda = dynamic_cast<LambdaExpr*>(expr)) {
        auto newLambda = std::make_unique<LambdaExpr>(lambda->location);
        newLambda->params = lambda->params;
        newLambda->body = cloneExpr(lambda->body.get(), params);
        return newLambda;
    }
    
    if (auto* listComp = dynamic_cast<ListCompExpr*>(expr)) {
        return std::make_unique<ListCompExpr>(
            cloneExpr(listComp->expr.get(), params),
            listComp->var,
            cloneExpr(listComp->iterable.get(), params),
            listComp->condition ? cloneExpr(listComp->condition.get(), params) : nullptr,
            listComp->location);
    }
    
    if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            cloneExpr(assign->target.get(), params),
            assign->op,
            cloneExpr(assign->value.get(), params),
            assign->location);
    }
    
    if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr)) {
        return std::make_unique<AddressOfExpr>(
            cloneExpr(addrOf->operand.get(), params), addrOf->location);
    }
    
    if (auto* deref = dynamic_cast<DerefExpr*>(expr)) {
        return std::make_unique<DerefExpr>(
            cloneExpr(deref->operand.get(), params), deref->location);
    }
    
    if (auto* newExpr = dynamic_cast<NewExpr*>(expr)) {
        auto cloned = std::make_unique<NewExpr>(newExpr->typeName, newExpr->location);
        for (auto& arg : newExpr->args) {
            cloned->args.push_back(cloneExpr(arg.get(), params));
        }
        return cloned;
    }
    
    if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        return std::make_unique<CastExpr>(
            cloneExpr(cast->expr.get(), params), cast->targetType, cast->location);
    }
    
    if (auto* await = dynamic_cast<AwaitExpr*>(expr)) {
        return std::make_unique<AwaitExpr>(
            cloneExpr(await->operand.get(), params), await->location);
    }
    
    if (auto* spawn = dynamic_cast<SpawnExpr*>(expr)) {
        return std::make_unique<SpawnExpr>(
            cloneExpr(spawn->operand.get(), params), spawn->location);
    }
    
    if (auto* dsl = dynamic_cast<DSLBlock*>(expr)) {
        return std::make_unique<DSLBlock>(dsl->dslName, dsl->rawContent, dsl->location);
    }
    
    return nullptr;
}


StmtPtr MacroExpander::cloneStmt(Statement* stmt, const std::unordered_map<std::string, Expression*>& params) {
    if (!stmt) return nullptr;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(cloneExpr(exprStmt->expr.get(), params), exprStmt->location);
    }
    
    if (auto* retStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(
            retStmt->value ? cloneExpr(retStmt->value.get(), params) : nullptr, retStmt->location);
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto newDecl = std::make_unique<VarDecl>(
            varDecl->name, varDecl->typeName,
            varDecl->initializer ? cloneExpr(varDecl->initializer.get(), params) : nullptr,
            varDecl->location);
        newDecl->isMutable = varDecl->isMutable;
        newDecl->isConst = varDecl->isConst;
        return newDecl;
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return std::make_unique<AssignStmt>(
            cloneExpr(assignStmt->target.get(), params), assignStmt->op,
            cloneExpr(assignStmt->value.get(), params), assignStmt->location);
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto newIf = std::make_unique<IfStmt>(
            cloneExpr(ifStmt->condition.get(), params),
            cloneStmt(ifStmt->thenBranch.get(), params), ifStmt->location);
        for (auto& [cond, branch] : ifStmt->elifBranches) {
            newIf->elifBranches.emplace_back(
                cloneExpr(cond.get(), params), cloneStmt(branch.get(), params));
        }
        if (ifStmt->elseBranch) newIf->elseBranch = cloneStmt(ifStmt->elseBranch.get(), params);
        return newIf;
    }
    
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return std::make_unique<WhileStmt>(
            cloneExpr(whileStmt->condition.get(), params),
            cloneStmt(whileStmt->body.get(), params), whileStmt->location);
    }
    
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        return std::make_unique<ForStmt>(
            forStmt->var, cloneExpr(forStmt->iterable.get(), params),
            cloneStmt(forStmt->body.get(), params), forStmt->location);
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) newBlock->statements.push_back(cloneStmt(s.get(), params));
        return newBlock;
    }
    
    if (auto* breakStmt = dynamic_cast<BreakStmt*>(stmt)) {
        return std::make_unique<BreakStmt>(breakStmt->location);
    }
    
    if (auto* contStmt = dynamic_cast<ContinueStmt*>(stmt)) {
        return std::make_unique<ContinueStmt>(contStmt->location);
    }
    
    if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt)) {
        auto newMatch = std::make_unique<MatchStmt>(
            cloneExpr(matchStmt->value.get(), params), matchStmt->location);
        for (auto& [pattern, body] : matchStmt->cases) {
            newMatch->cases.push_back({
                cloneExpr(pattern.get(), params),
                cloneStmt(body.get(), params)
            });
        }
        if (matchStmt->defaultCase) {
            newMatch->defaultCase = cloneStmt(matchStmt->defaultCase.get(), params);
        }
        return newMatch;
    }
    
    if (auto* tryStmt = dynamic_cast<TryStmt*>(stmt)) {
        return std::make_unique<TryStmt>(
            cloneExpr(tryStmt->tryExpr.get(), params),
            tryStmt->elseExpr ? cloneExpr(tryStmt->elseExpr.get(), params) : nullptr,
            tryStmt->location);
    }
    
    if (auto* unsafeBlock = dynamic_cast<UnsafeBlock*>(stmt)) {
        return std::make_unique<UnsafeBlock>(
            cloneStmt(unsafeBlock->body.get(), params), unsafeBlock->location);
    }
    
    if (auto* deleteStmt = dynamic_cast<DeleteStmt*>(stmt)) {
        return std::make_unique<DeleteStmt>(
            cloneExpr(deleteStmt->expr.get(), params), deleteStmt->location);
    }
    
    if (auto* destructDecl = dynamic_cast<DestructuringDecl*>(stmt)) {
        auto newDecl = std::make_unique<DestructuringDecl>(
            destructDecl->kind,
            destructDecl->names,
            cloneExpr(destructDecl->initializer.get(), params),
            destructDecl->location);
        newDecl->isMutable = destructDecl->isMutable;
        return newDecl;
    }
    
    return nullptr;
}

std::vector<StmtPtr> MacroExpander::cloneStmts(const std::vector<StmtPtr>& stmts,
                                                const std::unordered_map<std::string, Expression*>& params,
                                                Statement* blockParam) {
    std::vector<StmtPtr> result;
    for (auto& stmt : stmts) {
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto* ident = dynamic_cast<Identifier*>(exprStmt->expr.get())) {
                if (blockParam && (ident->name == "body" || ident->name == "block" || ident->name == "content")) {
                    result.push_back(cloneStmt(blockParam, params));
                    continue;
                }
            }
        }
        result.push_back(cloneStmt(stmt.get(), params));
    }
    return result;
}

ExprPtr MacroExpander::convertIfToTernary(IfStmt* ifStmt, 
                                           const std::unordered_map<std::string, Expression*>& params,
                                           SourceLocation loc) {
    auto condition = cloneExpr(ifStmt->condition.get(), params);
    
    ExprPtr thenValue = nullptr;
    if (auto* block = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
        for (auto& stmt : block->statements) {
            if (auto* ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
                thenValue = cloneExpr(ret->value.get(), params);
                break;
            }
        }
    } else if (auto* ret = dynamic_cast<ReturnStmt*>(ifStmt->thenBranch.get())) {
        thenValue = cloneExpr(ret->value.get(), params);
    }
    
    if (!thenValue) thenValue = std::make_unique<NilLiteral>(loc);
    
    ExprPtr elseValue = nullptr;
    
    if (!ifStmt->elifBranches.empty()) {
        for (auto it = ifStmt->elifBranches.rbegin(); it != ifStmt->elifBranches.rend(); ++it) {
            auto& [elifCond, elifBody] = *it;
            auto elifCondClone = cloneExpr(elifCond.get(), params);
            
            ExprPtr elifValue = nullptr;
            if (auto* block = dynamic_cast<Block*>(elifBody.get())) {
                for (auto& stmt : block->statements) {
                    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
                        elifValue = cloneExpr(ret->value.get(), params);
                        break;
                    }
                }
            }
            if (!elifValue) elifValue = std::make_unique<NilLiteral>(loc);
            
            if (!elseValue) {
                if (ifStmt->elseBranch) {
                    if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                        for (auto& stmt : block->statements) {
                            if (auto* ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
                                elseValue = cloneExpr(ret->value.get(), params);
                                break;
                            }
                        }
                    }
                }
                if (!elseValue) elseValue = std::make_unique<NilLiteral>(loc);
            }
            
            elseValue = std::make_unique<TernaryExpr>(
                std::move(elifCondClone), std::move(elifValue), std::move(elseValue), loc);
        }
    } else if (ifStmt->elseBranch) {
        if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            for (auto& stmt : block->statements) {
                if (auto* ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
                    elseValue = cloneExpr(ret->value.get(), params);
                    break;
                }
            }
        }
    }
    
    if (!elseValue) elseValue = std::make_unique<NilLiteral>(loc);
    
    return std::make_unique<TernaryExpr>(std::move(condition), std::move(thenValue), std::move(elseValue), loc);
}

} // namespace flex
