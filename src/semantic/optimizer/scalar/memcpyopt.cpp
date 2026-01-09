// Tyl Compiler - Memory Copy Optimization Implementation
#include "memcpyopt.h"
#include <algorithm>
#include <iostream>

namespace tyl {

void MemCpyOptPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = MemCpyOptStats{};
    
    processStatements(ast.statements);
    
    transformations_ = stats_.storesMergedToMemset + stats_.storesMergedToMemcpy +
                       stats_.memcpyToMemset + stats_.deadMemcpyRemoved;
}

void MemCpyOptPass::processStatements(std::vector<StmtPtr>& stmts) {
    // First recurse into nested structures
    for (auto& stmt : stmts) {
        processStatement(stmt);
    }
    
    // Analyze stores in this block
    auto stores = analyzeStores(stmts);
    
    // Find mergeable ranges
    auto ranges = findMergeableRanges(stores);
    
    // Apply transformations
    if (!ranges.empty()) {
        applyTransformations(stmts, ranges);
    }
    
    // Remove dead memcpy operations
    removeDeadMemcpy(stmts);
}

void MemCpyOptPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    // Recurse into nested structures
    if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
        if (fn->body) {
            if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        if (ifStmt->thenBranch) {
            if (auto* block = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                processStatements(block->statements);
            }
        }
        if (ifStmt->elseBranch) {
            if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processStatements(block->statements);
            }
        }
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.second) {
                if (auto* block = dynamic_cast<Block*>(elif.second.get())) {
                    processStatements(block->statements);
                }
            }
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        if (whileStmt->body) {
            if (auto* block = dynamic_cast<Block*>(whileStmt->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        if (forStmt->body) {
            if (auto* block = dynamic_cast<Block*>(forStmt->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processStatements(block->statements);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt.get())) {
        for (auto& c : matchStmt->cases) {
            if (c.body) {
                if (auto* block = dynamic_cast<Block*>(c.body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        if (matchStmt->defaultCase) {
            if (auto* block = dynamic_cast<Block*>(matchStmt->defaultCase.get())) {
                processStatements(block->statements);
            }
        }
    }
}

std::vector<StoreOp> MemCpyOptPass::analyzeStores(const std::vector<StmtPtr>& stmts) {
    std::vector<StoreOp> stores;
    
    for (size_t i = 0; i < stmts.size(); ++i) {
        StoreOp info;
        info.stmtIndex = i;
        
        if (isArrayStore(stmts[i].get(), info)) {
            stores.push_back(info);
        }
    }
    
    return stores;
}

bool MemCpyOptPass::isArrayStore(Statement* stmt, StoreOp& info) {
    if (!stmt) return false;
    
    // Check AssignStmt
    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        // Only simple assignment (not +=, -=, etc.)
        if (assign->op != TokenType::ASSIGN) return false;
        
        return isArrayStoreExpr(assign->target.get(), info) &&
               parseArrayAccess(assign->target.get(), info.arrayName, 
                               info.index, info.hasConstantIndex) &&
               getConstantValue(assign->value.get(), info.constantValue);
    }
    
    // Check ExprStmt with AssignExpr
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (assignExpr->op != TokenType::ASSIGN) return false;
            
            if (!parseArrayAccess(assignExpr->target.get(), info.arrayName,
                                  info.index, info.hasConstantIndex)) {
                return false;
            }
            
            // Check if value is a constant
            info.hasConstantValue = getConstantValue(assignExpr->value.get(), info.constantValue);
            
            // Check if value is from another array
            std::string srcArray;
            int64_t srcIndex;
            bool srcConstIndex;
            if (parseArrayAccess(assignExpr->value.get(), srcArray, srcIndex, srcConstIndex)) {
                info.isFromArray = true;
                info.sourceArray = srcArray;
                info.sourceIndex = srcIndex;
            }
            
            return info.hasConstantIndex && (info.hasConstantValue || info.isFromArray);
        }
    }
    
    return false;
}

bool MemCpyOptPass::isArrayStoreExpr(Expression* expr, StoreOp& info) {
    auto* indexExpr = dynamic_cast<IndexExpr*>(expr);
    if (!indexExpr) return false;
    
    // Check if object is an identifier
    auto* arrayId = dynamic_cast<Identifier*>(indexExpr->object.get());
    if (!arrayId) return false;
    
    info.arrayName = arrayId->name;
    
    // Check if index is a constant
    if (auto* indexLit = dynamic_cast<IntegerLiteral*>(indexExpr->index.get())) {
        info.hasConstantIndex = true;
        info.index = indexLit->value;
        return true;
    }
    
    return false;
}

bool MemCpyOptPass::parseArrayAccess(Expression* expr, std::string& arrayName,
                                      int64_t& index, bool& isConstantIndex) {
    auto* indexExpr = dynamic_cast<IndexExpr*>(expr);
    if (!indexExpr) return false;
    
    auto* arrayId = dynamic_cast<Identifier*>(indexExpr->object.get());
    if (!arrayId) return false;
    
    arrayName = arrayId->name;
    
    if (auto* indexLit = dynamic_cast<IntegerLiteral*>(indexExpr->index.get())) {
        isConstantIndex = true;
        index = indexLit->value;
        return true;
    }
    
    isConstantIndex = false;
    return true;
}

bool MemCpyOptPass::getConstantValue(Expression* expr, int64_t& value) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        value = intLit->value;
        return true;
    }
    return false;
}

std::vector<MemoryRange> MemCpyOptPass::findMergeableRanges(const std::vector<StoreOp>& stores) {
    std::vector<MemoryRange> ranges;
    
    if (stores.empty()) return ranges;
    
    // Group stores by array name
    std::map<std::string, std::vector<StoreOp>> storesByArray;
    for (const auto& store : stores) {
        if (store.hasConstantIndex) {
            storesByArray[store.arrayName].push_back(store);
        }
    }
    
    // For each array, find contiguous ranges
    for (auto& [arrayName, arrayStores] : storesByArray) {
        // Sort by index
        std::sort(arrayStores.begin(), arrayStores.end(),
                  [](const StoreOp& a, const StoreOp& b) { return a.index < b.index; });
        
        // Find contiguous ranges with same value (for memset)
        MemoryRange currentRange;
        currentRange.arrayName = arrayName;
        currentRange.startIndex = -1;
        
        for (size_t i = 0; i < arrayStores.size(); ++i) {
            const auto& store = arrayStores[i];
            
            if (currentRange.startIndex < 0) {
                // Start new range
                currentRange.startIndex = store.index;
                currentRange.endIndex = store.index + 1;
                currentRange.hasConstantValue = store.hasConstantValue;
                currentRange.constantValue = store.constantValue;
                currentRange.sourceArray = store.sourceArray;
                currentRange.sourceStartIndex = store.sourceIndex;
                currentRange.stmtIndices.push_back(store.stmtIndex);
            }
            else if (store.index == currentRange.endIndex &&
                     store.hasConstantValue == currentRange.hasConstantValue &&
                     (!store.hasConstantValue || store.constantValue == currentRange.constantValue) &&
                     (store.sourceArray.empty() || 
                      (store.sourceArray == currentRange.sourceArray &&
                       store.sourceIndex == currentRange.sourceStartIndex + 
                                           (store.index - currentRange.startIndex)))) {
                // Extend current range
                currentRange.endIndex = store.index + 1;
                currentRange.stmtIndices.push_back(store.stmtIndex);
            }
            else {
                // Save current range if large enough
                if (currentRange.stmtIndices.size() >= static_cast<size_t>(minStoresForMemset_)) {
                    ranges.push_back(currentRange);
                }
                
                // Start new range
                currentRange = MemoryRange{};
                currentRange.arrayName = arrayName;
                currentRange.startIndex = store.index;
                currentRange.endIndex = store.index + 1;
                currentRange.hasConstantValue = store.hasConstantValue;
                currentRange.constantValue = store.constantValue;
                currentRange.sourceArray = store.sourceArray;
                currentRange.sourceStartIndex = store.sourceIndex;
                currentRange.stmtIndices.clear();
                currentRange.stmtIndices.push_back(store.stmtIndex);
            }
        }
        
        // Don't forget the last range
        if (currentRange.stmtIndices.size() >= static_cast<size_t>(minStoresForMemset_)) {
            ranges.push_back(currentRange);
        }
    }
    
    return ranges;
}

bool MemCpyOptPass::areAdjacentStores(const StoreOp& a, const StoreOp& b) {
    return a.arrayName == b.arrayName &&
           a.hasConstantIndex && b.hasConstantIndex &&
           b.index == a.index + 1;
}

void MemCpyOptPass::applyTransformations(std::vector<StmtPtr>& stmts,
                                          const std::vector<MemoryRange>& ranges) {
    // Collect all statement indices to remove
    std::set<size_t> toRemove;
    std::vector<std::pair<size_t, StmtPtr>> toInsert;
    
    for (const auto& range : ranges) {
        // Mark all statements in range for removal
        for (size_t idx : range.stmtIndices) {
            toRemove.insert(idx);
        }
        
        // Create replacement statement
        SourceLocation loc = stmts[range.stmtIndices[0]]->location;
        StmtPtr replacement;
        
        if (range.hasConstantValue) {
            replacement = createMemsetForRange(range, loc);
            ++stats_.storesMergedToMemset;
        }
        else if (!range.sourceArray.empty()) {
            replacement = createMemcpyForRange(range, loc);
            ++stats_.storesMergedToMemcpy;
        }
        
        if (replacement) {
            toInsert.push_back({range.stmtIndices[0], std::move(replacement)});
        }
    }
    
    // Apply changes in reverse order to preserve indices
    // First, remove statements
    std::vector<size_t> removeIndices(toRemove.begin(), toRemove.end());
    std::sort(removeIndices.rbegin(), removeIndices.rend());
    
    for (size_t idx : removeIndices) {
        stmts.erase(stmts.begin() + idx);
    }
    
    // Adjust insertion indices based on removals
    for (auto& [insertIdx, stmt] : toInsert) {
        size_t adjustedIdx = insertIdx;
        for (size_t removeIdx : removeIndices) {
            if (removeIdx < insertIdx) {
                --adjustedIdx;
            }
        }
        stmts.insert(stmts.begin() + adjustedIdx, std::move(stmt));
    }
}

StmtPtr MemCpyOptPass::createMemsetForRange(const MemoryRange& range, SourceLocation loc) {
    // Create: __builtin_memset(&array[start], value, count)
    
    auto callee = std::make_unique<Identifier>("__builtin_memset", loc);
    auto call = std::make_unique<CallExpr>(std::move(callee), loc);
    
    // Argument 1: &array[startIndex]
    auto arrayId = std::make_unique<Identifier>(range.arrayName, loc);
    auto startIdx = std::make_unique<IntegerLiteral>(range.startIndex, loc);
    auto indexExpr = std::make_unique<IndexExpr>(std::move(arrayId), std::move(startIdx), loc);
    auto addrOf = std::make_unique<AddressOfExpr>(std::move(indexExpr), loc);
    call->args.push_back(std::move(addrOf));
    
    // Argument 2: fill value
    call->args.push_back(std::make_unique<IntegerLiteral>(range.constantValue, loc));
    
    // Argument 3: count (number of elements)
    int64_t count = range.endIndex - range.startIndex;
    call->args.push_back(std::make_unique<IntegerLiteral>(count, loc));
    
    return std::make_unique<ExprStmt>(std::move(call), loc);
}

StmtPtr MemCpyOptPass::createMemcpyForRange(const MemoryRange& range, SourceLocation loc) {
    // Create: __builtin_memcpy(&dest[start], &src[srcStart], count)
    
    auto callee = std::make_unique<Identifier>("__builtin_memcpy", loc);
    auto call = std::make_unique<CallExpr>(std::move(callee), loc);
    
    // Argument 1: &dest[startIndex]
    auto destId = std::make_unique<Identifier>(range.arrayName, loc);
    auto destIdx = std::make_unique<IntegerLiteral>(range.startIndex, loc);
    auto destExpr = std::make_unique<IndexExpr>(std::move(destId), std::move(destIdx), loc);
    auto destAddr = std::make_unique<AddressOfExpr>(std::move(destExpr), loc);
    call->args.push_back(std::move(destAddr));
    
    // Argument 2: &src[srcStartIndex]
    auto srcId = std::make_unique<Identifier>(range.sourceArray, loc);
    auto srcIdx = std::make_unique<IntegerLiteral>(range.sourceStartIndex, loc);
    auto srcExpr = std::make_unique<IndexExpr>(std::move(srcId), std::move(srcIdx), loc);
    auto srcAddr = std::make_unique<AddressOfExpr>(std::move(srcExpr), loc);
    call->args.push_back(std::move(srcAddr));
    
    // Argument 3: count
    int64_t count = range.endIndex - range.startIndex;
    call->args.push_back(std::make_unique<IntegerLiteral>(count, loc));
    
    return std::make_unique<ExprStmt>(std::move(call), loc);
}

void MemCpyOptPass::removeDeadMemcpy(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ) {
        if (isMemcpyDead(stmts, i)) {
            stmts.erase(stmts.begin() + i);
            ++stats_.deadMemcpyRemoved;
        } else {
            ++i;
        }
    }
}

bool MemCpyOptPass::isMemcpyDead(const std::vector<StmtPtr>& stmts, size_t memcpyIndex) {
    // Check if this is a memcpy call
    auto* exprStmt = dynamic_cast<ExprStmt*>(stmts[memcpyIndex].get());
    if (!exprStmt) return false;
    
    auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get());
    if (!call) return false;
    
    auto* callee = dynamic_cast<Identifier*>(call->callee.get());
    if (!callee || (callee->name != "__builtin_memcpy" && callee->name != "memcpy")) {
        return false;
    }
    
    // Get destination from first argument
    if (call->args.empty()) return false;
    
    // For now, we don't do complex dead memcpy analysis
    // This would require tracking all reads/writes to the destination
    // and checking if the memcpy result is overwritten before being read
    
    return false;
}

ExprPtr MemCpyOptPass::cloneExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(lit->value, lit->location);
    }
    if (auto* lit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(lit->value, lit->location);
    }
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return std::make_unique<Identifier>(id->name, id->location);
    }
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpression(bin->left.get()),
            bin->op,
            cloneExpression(bin->right.get()),
            bin->location);
    }
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            un->op,
            cloneExpression(un->operand.get()),
            un->location);
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpression(index->object.get()),
            cloneExpression(index->index.get()),
            index->location);
    }
    
    return nullptr;
}

} // namespace tyl
