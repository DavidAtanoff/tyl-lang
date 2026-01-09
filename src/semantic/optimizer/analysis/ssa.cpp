// Tyl Compiler - SSA (Static Single Assignment) Implementation
// Converts AST to SSA form using Braun et al. algorithm
#include "ssa.h"
#include <sstream>
#include <algorithm>

namespace tyl {

// ============================================
// SSAValue Implementation
// ============================================

std::string SSAValue::toString() const {
    std::ostringstream oss;
    if (!name.empty()) {
        oss << name << "_" << version;
    } else {
        oss << "v" << id;
    }
    return oss.str();
}

// ============================================
// SSAInstruction Implementation
// ============================================

bool SSAInstruction::isTerminator() const {
    return opcode == SSAOpcode::BRANCH || 
           opcode == SSAOpcode::JUMP || 
           opcode == SSAOpcode::RETURN;
}

bool SSAInstruction::hasSideEffects() const {
    return opcode == SSAOpcode::CALL || 
           opcode == SSAOpcode::STORE ||
           opcode == SSAOpcode::RETURN;
}

std::string SSAInstruction::toString() const {
    std::ostringstream oss;
    
    if (result) {
        oss << result->toString() << " = ";
    }

    switch (opcode) {
        case SSAOpcode::CONST_INT: oss << "const.i64 " << intValue; break;
        case SSAOpcode::CONST_FLOAT: oss << "const.f64 " << floatValue; break;
        case SSAOpcode::CONST_BOOL: oss << "const.bool " << (boolValue ? "true" : "false"); break;
        case SSAOpcode::CONST_STRING: oss << "const.str \"" << stringValue << "\""; break;
        case SSAOpcode::ADD: oss << "add"; break;
        case SSAOpcode::SUB: oss << "sub"; break;
        case SSAOpcode::MUL: oss << "mul"; break;
        case SSAOpcode::DIV: oss << "div"; break;
        case SSAOpcode::MOD: oss << "mod"; break;
        case SSAOpcode::NEG: oss << "neg"; break;
        case SSAOpcode::EQ: oss << "eq"; break;
        case SSAOpcode::NE: oss << "ne"; break;
        case SSAOpcode::LT: oss << "lt"; break;
        case SSAOpcode::GT: oss << "gt"; break;
        case SSAOpcode::LE: oss << "le"; break;
        case SSAOpcode::GE: oss << "ge"; break;
        case SSAOpcode::AND: oss << "and"; break;
        case SSAOpcode::OR: oss << "or"; break;
        case SSAOpcode::NOT: oss << "not"; break;
        case SSAOpcode::PHI: oss << "phi"; break;
        case SSAOpcode::BRANCH: 
            oss << "br " << operands[0]->toString() 
                << ", " << trueTarget->label 
                << ", " << falseTarget->label; 
            break;
        case SSAOpcode::JUMP: oss << "jmp " << trueTarget->label; break;
        case SSAOpcode::RETURN: oss << "ret"; break;
        case SSAOpcode::LOAD: oss << "load"; break;
        case SSAOpcode::STORE: oss << "store"; break;
        case SSAOpcode::ALLOCA: oss << "alloca"; break;
        case SSAOpcode::CALL: oss << "call " << funcName; break;
        case SSAOpcode::PARAM: oss << "param"; break;
        case SSAOpcode::INT_TO_FLOAT: oss << "i2f"; break;
        case SSAOpcode::FLOAT_TO_INT: oss << "f2i"; break;
        case SSAOpcode::COPY: oss << "copy"; break;
        case SSAOpcode::NOP: oss << "nop"; break;
    }
    
    // Print operands for non-special instructions
    if (opcode != SSAOpcode::BRANCH && opcode != SSAOpcode::JUMP && 
        opcode != SSAOpcode::PHI && !operands.empty()) {
        oss << " ";
        for (size_t i = 0; i < operands.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << operands[i]->toString();
        }
    }
    
    // Print phi operands
    if (opcode == SSAOpcode::PHI) {
        oss << " [";
        for (size_t i = 0; i < phiOperands.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << phiOperands[i].first->label << ": " << phiOperands[i].second->toString();
        }
        oss << "]";
    }
    
    return oss.str();
}

// ============================================
// SSABasicBlock Implementation
// ============================================

void SSABasicBlock::addInstruction(SSAInstrPtr instr) {
    instr->parent = this;
    instructions.push_back(std::move(instr));
}

SSAInstruction* SSABasicBlock::getTerminator() {
    if (instructions.empty()) return nullptr;
    auto* last = instructions.back().get();
    return last->isTerminator() ? last : nullptr;
}

std::string SSABasicBlock::toString() const {
    std::ostringstream oss;
    oss << label << ":\n";
    for (const auto& instr : instructions) {
        oss << "  " << instr->toString() << "\n";
    }
    return oss.str();
}

// ============================================
// SSAFunction Implementation
// ============================================

SSAValuePtr SSAFunction::createValue(SSAType type, const std::string& name) {
    auto value = std::make_shared<SSAValue>(nextValueId++, type, name);
    return value;
}

SSABasicBlock* SSAFunction::createBlock(const std::string& label) {
    std::string blockLabel = label.empty() ? "bb" + std::to_string(nextBlockId) : label;
    auto block = std::make_unique<SSABasicBlock>(nextBlockId++, blockLabel);
    block->parent = this;
    SSABasicBlock* ptr = block.get();
    blocks.push_back(std::move(block));
    return ptr;
}

std::string SSAFunction::toString() const {
    std::ostringstream oss;
    oss << "function " << name << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << params[i]->toString();
    }
    oss << "):\n";
    for (const auto& block : blocks) {
        oss << block->toString();
    }
    return oss.str();
}

// ============================================
// SSAModule Implementation
// ============================================

SSAFunction* SSAModule::createFunction(const std::string& name) {
    auto func = std::make_unique<SSAFunction>(name);
    func->parent = this;
    SSAFunction* ptr = func.get();
    functions.push_back(std::move(func));
    return ptr;
}

SSAFunction* SSAModule::getFunction(const std::string& name) {
    for (auto& func : functions) {
        if (func->name == name) return func.get();
    }
    return nullptr;
}

int SSAModule::addString(const std::string& str) {
    auto it = stringPool.find(str);
    if (it != stringPool.end()) return it->second;
    int id = nextStringId++;
    stringPool[str] = id;
    return id;
}

std::string SSAModule::toString() const {
    std::ostringstream oss;
    oss << "; SSA Module\n";
    for (const auto& func : functions) {
        oss << func->toString() << "\n";
    }
    return oss.str();
}

// ============================================
// SSABuilder Implementation
// ============================================

SSABuilder::SSABuilder() : module_(nullptr), currentFunc_(nullptr), currentBlock_(nullptr) {}

std::unique_ptr<SSAModule> SSABuilder::build(Program& ast) {
    auto module = std::make_unique<SSAModule>();
    module_ = module.get();
    
    // First pass: collect all function declarations
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            buildFunction(*fn);
        }
    }
    
    // Build top-level code as _start function
    auto* startFunc = module_->createFunction("_start");
    currentFunc_ = startFunc;
    currentBlock_ = startFunc->createBlock("entry");
    startFunc->entryBlock = currentBlock_;
    
    for (auto& stmt : ast.statements) {
        if (!dynamic_cast<FnDecl*>(stmt.get())) {
            buildStatement(stmt.get());
        }
    }
    
    // Add return if not present
    if (!currentBlock_->getTerminator()) {
        emitReturn(nullptr);
    }
    
    sealBlock(currentBlock_);
    
    return module;
}

void SSABuilder::buildFunction(FnDecl& fn) {
    auto* func = module_->createFunction(fn.name);
    currentFunc_ = func;
    
    // Create entry block
    currentBlock_ = func->createBlock("entry");
    func->entryBlock = currentBlock_;
    
    // Add parameters
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const auto& param = fn.params[i];
        auto paramValue = func->createValue(SSAType::INT, param.first);
        func->params.push_back(paramValue);
        
        // Write parameter to variable
        writeVariable(param.first, currentBlock_, paramValue);
    }
    
    // Build function body
    buildStatement(fn.body.get());
    
    // Add implicit return if needed
    if (!currentBlock_->getTerminator()) {
        emitReturn(nullptr);
    }
    
    // Seal all blocks
    for (auto& block : func->blocks) {
        if (sealedBlocks_.find(block.get()) == sealedBlocks_.end()) {
            sealBlock(block.get());
        }
    }
    
    // Clear state for next function
    varVersions_.clear();
    varCounter_.clear();
    incompletePhis_.clear();
    sealedBlocks_.clear();
}

void SSABuilder::buildStatement(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            buildStatement(s.get());
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        SSAValuePtr value = nullptr;
        if (varDecl->initializer) {
            value = buildExpression(varDecl->initializer.get());
        } else {
            // Default initialization
            auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CONST_INT);
            instr->intValue = 0;
            value = currentFunc_->createValue(SSAType::INT, varDecl->name);
            instr->result = value;
            currentBlock_->addInstruction(std::move(instr));
        }
        writeVariable(varDecl->name, currentBlock_, value);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* id = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            SSAValuePtr value = buildExpression(assignStmt->value.get());
            
            // Handle compound assignment
            if (assignStmt->op != TokenType::ASSIGN) {
                SSAValuePtr oldValue = readVariable(id->name, currentBlock_);
                SSAOpcode op;
                switch (assignStmt->op) {
                    case TokenType::PLUS_ASSIGN: op = SSAOpcode::ADD; break;
                    case TokenType::MINUS_ASSIGN: op = SSAOpcode::SUB; break;
                    case TokenType::STAR_ASSIGN: op = SSAOpcode::MUL; break;
                    case TokenType::SLASH_ASSIGN: op = SSAOpcode::DIV; break;
                    default: op = SSAOpcode::ADD; break;
                }
                value = emitBinary(op, oldValue, value);
            }
            
            writeVariable(id->name, currentBlock_, value);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        buildExpression(exprStmt->expr.get());
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        SSAValuePtr cond = buildExpression(ifStmt->condition.get());
        
        auto* thenBlock = currentFunc_->createBlock("if.then");
        auto* elseBlock = ifStmt->elseBranch ? 
            currentFunc_->createBlock("if.else") : nullptr;
        auto* mergeBlock = currentFunc_->createBlock("if.merge");
        
        emitBranch(cond, thenBlock, elseBlock ? elseBlock : mergeBlock);
        
        // Then branch
        currentBlock_ = thenBlock;
        buildStatement(ifStmt->thenBranch.get());
        if (!currentBlock_->getTerminator()) {
            emitJump(mergeBlock);
        }
        sealBlock(thenBlock);
        
        // Else branch
        if (elseBlock) {
            currentBlock_ = elseBlock;
            buildStatement(ifStmt->elseBranch.get());
            if (!currentBlock_->getTerminator()) {
                emitJump(mergeBlock);
            }
            sealBlock(elseBlock);
        }
        
        currentBlock_ = mergeBlock;
        sealBlock(mergeBlock);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        auto* condBlock = currentFunc_->createBlock("while.cond");
        auto* bodyBlock = currentFunc_->createBlock("while.body");
        auto* exitBlock = currentFunc_->createBlock("while.exit");
        
        emitJump(condBlock);
        
        currentBlock_ = condBlock;
        SSAValuePtr cond = buildExpression(whileStmt->condition.get());
        emitBranch(cond, bodyBlock, exitBlock);
        
        currentBlock_ = bodyBlock;
        buildStatement(whileStmt->body.get());
        if (!currentBlock_->getTerminator()) {
            emitJump(condBlock);
        }
        sealBlock(bodyBlock);
        sealBlock(condBlock);
        
        currentBlock_ = exitBlock;
        sealBlock(exitBlock);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        SSAValuePtr value = nullptr;
        if (returnStmt->value) {
            value = buildExpression(returnStmt->value.get());
        }
        emitReturn(value);
    }
}

SSAValuePtr SSABuilder::buildExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CONST_INT);
        instr->intValue = intLit->value;
        auto value = currentFunc_->createValue(SSAType::INT);
        instr->result = value;
        currentBlock_->addInstruction(std::move(instr));
        return value;
    }
    
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CONST_FLOAT);
        instr->floatValue = floatLit->value;
        auto value = currentFunc_->createValue(SSAType::FLOAT);
        instr->result = value;
        currentBlock_->addInstruction(std::move(instr));
        return value;
    }
    
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CONST_BOOL);
        instr->boolValue = boolLit->value;
        auto value = currentFunc_->createValue(SSAType::BOOL);
        instr->result = value;
        currentBlock_->addInstruction(std::move(instr));
        return value;
    }
    
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CONST_STRING);
        instr->stringValue = strLit->value;
        auto value = currentFunc_->createValue(SSAType::STRING);
        instr->result = value;
        currentBlock_->addInstruction(std::move(instr));
        return value;
    }
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return readVariable(ident->name, currentBlock_);
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        SSAValuePtr left = buildExpression(binary->left.get());
        SSAValuePtr right = buildExpression(binary->right.get());
        
        SSAOpcode op;
        switch (binary->op) {
            case TokenType::PLUS: op = SSAOpcode::ADD; break;
            case TokenType::MINUS: op = SSAOpcode::SUB; break;
            case TokenType::STAR: op = SSAOpcode::MUL; break;
            case TokenType::SLASH: op = SSAOpcode::DIV; break;
            case TokenType::PERCENT: op = SSAOpcode::MOD; break;
            case TokenType::EQ: op = SSAOpcode::EQ; break;
            case TokenType::NE: op = SSAOpcode::NE; break;
            case TokenType::LT: op = SSAOpcode::LT; break;
            case TokenType::GT: op = SSAOpcode::GT; break;
            case TokenType::LE: op = SSAOpcode::LE; break;
            case TokenType::GE: op = SSAOpcode::GE; break;
            case TokenType::AND:
            case TokenType::AMP_AMP: op = SSAOpcode::AND; break;
            case TokenType::OR:
            case TokenType::PIPE_PIPE: op = SSAOpcode::OR; break;
            default: op = SSAOpcode::ADD; break;
        }
        
        return emitBinary(op, left, right);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        SSAValuePtr operand = buildExpression(unary->operand.get());
        
        SSAOpcode op;
        switch (unary->op) {
            case TokenType::MINUS: op = SSAOpcode::NEG; break;
            case TokenType::NOT:
            case TokenType::BANG: op = SSAOpcode::NOT; break;
            default: op = SSAOpcode::NEG; break;
        }
        
        return emitUnary(op, operand);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        std::vector<SSAValuePtr> args;
        for (auto& arg : call->args) {
            args.push_back(buildExpression(arg.get()));
        }
        
        std::string funcName;
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            funcName = id->name;
        }
        
        return emitCall(funcName, args);
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        SSAValuePtr cond = buildExpression(ternary->condition.get());
        
        auto* thenBlock = currentFunc_->createBlock("ternary.then");
        auto* elseBlock = currentFunc_->createBlock("ternary.else");
        auto* mergeBlock = currentFunc_->createBlock("ternary.merge");
        
        emitBranch(cond, thenBlock, elseBlock);
        
        currentBlock_ = thenBlock;
        SSAValuePtr thenValue = buildExpression(ternary->thenExpr.get());
        auto* thenExit = currentBlock_;
        emitJump(mergeBlock);
        sealBlock(thenBlock);
        
        currentBlock_ = elseBlock;
        SSAValuePtr elseValue = buildExpression(ternary->elseExpr.get());
        auto* elseExit = currentBlock_;
        emitJump(mergeBlock);
        sealBlock(elseBlock);
        
        currentBlock_ = mergeBlock;
        sealBlock(mergeBlock);
        
        // Create phi node
        auto phi = std::make_unique<SSAInstruction>(SSAOpcode::PHI);
        auto result = currentFunc_->createValue(thenValue->type);
        phi->result = result;
        phi->phiOperands.push_back({thenExit, thenValue});
        phi->phiOperands.push_back({elseExit, elseValue});
        currentBlock_->addInstruction(std::move(phi));
        
        return result;
    }
    
    // Default: return a dummy value
    auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CONST_INT);
    instr->intValue = 0;
    auto value = currentFunc_->createValue(SSAType::INT);
    instr->result = value;
    currentBlock_->addInstruction(std::move(instr));
    return value;
}

// ============================================
// SSA Construction (Braun et al. algorithm)
// ============================================

void SSABuilder::writeVariable(const std::string& name, SSABasicBlock* block, SSAValuePtr value) {
    varVersions_[name].push_back(value);
    value->name = name;
    value->version = varCounter_[name]++;
}

SSAValuePtr SSABuilder::readVariable(const std::string& name, SSABasicBlock* block) {
    auto it = varVersions_.find(name);
    if (it != varVersions_.end() && !it->second.empty()) {
        return it->second.back();
    }
    return readVariableRecursive(name, block);
}

SSAValuePtr SSABuilder::readVariableRecursive(const std::string& name, SSABasicBlock* block) {
    SSAValuePtr value;
    
    if (sealedBlocks_.find(block) == sealedBlocks_.end()) {
        // Block not sealed yet - create incomplete phi
        auto phi = std::make_unique<SSAInstruction>(SSAOpcode::PHI);
        value = currentFunc_->createValue(SSAType::INT, name);
        phi->result = value;
        incompletePhis_[block][name] = phi.get();
        block->instructions.insert(block->instructions.begin(), std::move(phi));
    }
    else if (block->predecessors.size() == 1) {
        // Single predecessor - no phi needed
        value = readVariable(name, block->predecessors[0]);
    }
    else if (block->predecessors.empty()) {
        // No predecessors (entry block) - undefined variable
        auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CONST_INT);
        instr->intValue = 0;
        value = currentFunc_->createValue(SSAType::INT, name);
        instr->result = value;
        block->instructions.insert(block->instructions.begin(), std::move(instr));
    }
    else {
        // Multiple predecessors - need phi
        auto phi = std::make_unique<SSAInstruction>(SSAOpcode::PHI);
        value = currentFunc_->createValue(SSAType::INT, name);
        phi->result = value;
        block->instructions.insert(block->instructions.begin(), std::move(phi));
        writeVariable(name, block, value);
        value = addPhiOperands(name, block->instructions[0].get());
    }
    
    writeVariable(name, block, value);
    return value;
}

SSAValuePtr SSABuilder::addPhiOperands(const std::string& name, SSAInstruction* phi) {
    for (auto* pred : phi->parent->predecessors) {
        SSAValuePtr value = readVariable(name, pred);
        phi->phiOperands.push_back({pred, value});
    }
    return tryRemoveTrivialPhi(phi);
}

SSAValuePtr SSABuilder::tryRemoveTrivialPhi(SSAInstruction* phi) {
    SSAValuePtr same = nullptr;
    
    for (auto& [block, value] : phi->phiOperands) {
        if (value == same || value == phi->result) {
            continue;  // Unique value or self-reference
        }
        if (same != nullptr) {
            return phi->result;  // Non-trivial phi
        }
        same = value;
    }
    
    if (same == nullptr) {
        // Phi is unreachable or in entry block
        return phi->result;
    }
    
    // Phi is trivial - replace with same
    // Note: In a full implementation, we'd replace all uses of phi->result with same
    return same;
}

void SSABuilder::sealBlock(SSABasicBlock* block) {
    auto it = incompletePhis_.find(block);
    if (it != incompletePhis_.end()) {
        for (auto& [name, phi] : it->second) {
            addPhiOperands(name, phi);
        }
        incompletePhis_.erase(it);
    }
    sealedBlocks_.insert(block);
}

// ============================================
// SSA Emission Helpers
// ============================================

SSAValuePtr SSABuilder::emitBinary(SSAOpcode op, SSAValuePtr left, SSAValuePtr right) {
    auto instr = std::make_unique<SSAInstruction>(op);
    instr->operands.push_back(left);
    instr->operands.push_back(right);
    
    SSAType resultType = SSAType::INT;
    if (left->type == SSAType::FLOAT || right->type == SSAType::FLOAT) {
        resultType = SSAType::FLOAT;
    }
    if (op >= SSAOpcode::EQ && op <= SSAOpcode::GE) {
        resultType = SSAType::BOOL;
    }
    
    auto result = currentFunc_->createValue(resultType);
    instr->result = result;
    currentBlock_->addInstruction(std::move(instr));
    return result;
}

SSAValuePtr SSABuilder::emitUnary(SSAOpcode op, SSAValuePtr operand) {
    auto instr = std::make_unique<SSAInstruction>(op);
    instr->operands.push_back(operand);
    
    SSAType resultType = operand->type;
    if (op == SSAOpcode::NOT) {
        resultType = SSAType::BOOL;
    }
    
    auto result = currentFunc_->createValue(resultType);
    instr->result = result;
    currentBlock_->addInstruction(std::move(instr));
    return result;
}

SSAValuePtr SSABuilder::emitCall(const std::string& name, const std::vector<SSAValuePtr>& args) {
    auto instr = std::make_unique<SSAInstruction>(SSAOpcode::CALL);
    instr->funcName = name;
    instr->operands = args;
    
    auto result = currentFunc_->createValue(SSAType::INT);
    instr->result = result;
    currentBlock_->addInstruction(std::move(instr));
    return result;
}

void SSABuilder::emitBranch(SSAValuePtr cond, SSABasicBlock* trueBlock, SSABasicBlock* falseBlock) {
    auto instr = std::make_unique<SSAInstruction>(SSAOpcode::BRANCH);
    instr->operands.push_back(cond);
    instr->trueTarget = trueBlock;
    instr->falseTarget = falseBlock;
    
    currentBlock_->successors.push_back(trueBlock);
    currentBlock_->successors.push_back(falseBlock);
    trueBlock->predecessors.push_back(currentBlock_);
    falseBlock->predecessors.push_back(currentBlock_);
    
    currentBlock_->addInstruction(std::move(instr));
}

void SSABuilder::emitJump(SSABasicBlock* target) {
    auto instr = std::make_unique<SSAInstruction>(SSAOpcode::JUMP);
    instr->trueTarget = target;
    
    currentBlock_->successors.push_back(target);
    target->predecessors.push_back(currentBlock_);
    
    currentBlock_->addInstruction(std::move(instr));
}

void SSABuilder::emitReturn(SSAValuePtr value) {
    auto instr = std::make_unique<SSAInstruction>(SSAOpcode::RETURN);
    if (value) {
        instr->operands.push_back(value);
    }
    currentBlock_->addInstruction(std::move(instr));
}

SSAType SSABuilder::getExprType(Expression* expr) {
    if (dynamic_cast<IntegerLiteral*>(expr)) return SSAType::INT;
    if (dynamic_cast<FloatLiteral*>(expr)) return SSAType::FLOAT;
    if (dynamic_cast<BoolLiteral*>(expr)) return SSAType::BOOL;
    if (dynamic_cast<StringLiteral*>(expr)) return SSAType::STRING;
    return SSAType::INT;
}

// ============================================
// SSA Optimizer Implementation
// ============================================

void SSAOptimizer::optimize(SSAModule& module) {
    for (auto& func : module.functions) {
        // Run optimization passes in order
        constantPropagation(*func);
        copyPropagation(*func);
        deadCodeElimination(*func);
        commonSubexpressionElimination(*func);
    }
}

void SSAOptimizer::deadCodeElimination(SSAFunction& func) {
    // Mark all values as potentially dead
    std::set<SSAValue*> liveValues;
    
    // Mark values used by terminators and side-effecting instructions as live
    std::function<void(SSAValuePtr)> markLive = [&](SSAValuePtr value) {
        if (!value || liveValues.count(value.get())) return;
        liveValues.insert(value.get());
        
        // Mark operands of defining instruction as live
        if (value->defInstr) {
            for (auto& op : value->defInstr->operands) {
                markLive(op);
            }
            for (auto& [block, val] : value->defInstr->phiOperands) {
                markLive(val);
            }
        }
    };
    
    // Find all live values
    for (auto& block : func.blocks) {
        for (auto& instr : block->instructions) {
            if (instr->hasSideEffects() || instr->isTerminator()) {
                for (auto& op : instr->operands) {
                    markLive(op);
                }
            }
        }
    }
    
    // Remove dead instructions
    for (auto& block : func.blocks) {
        block->instructions.erase(
            std::remove_if(block->instructions.begin(), block->instructions.end(),
                [&](const SSAInstrPtr& instr) {
                    if (instr->hasSideEffects() || instr->isTerminator()) return false;
                    if (!instr->result) return false;
                    return liveValues.find(instr->result.get()) == liveValues.end();
                }),
            block->instructions.end());
    }
}

void SSAOptimizer::constantPropagation(SSAFunction& func) {
    std::map<SSAValue*, int64_t> constants;
    
    // Find all constant values
    for (auto& block : func.blocks) {
        for (auto& instr : block->instructions) {
            if (instr->opcode == SSAOpcode::CONST_INT && instr->result) {
                constants[instr->result.get()] = instr->intValue;
            }
        }
    }
    
    // Fold constant operations
    for (auto& block : func.blocks) {
        for (auto& instr : block->instructions) {
            if (instr->operands.size() == 2) {
                auto it1 = constants.find(instr->operands[0].get());
                auto it2 = constants.find(instr->operands[1].get());
                
                if (it1 != constants.end() && it2 != constants.end()) {
                    int64_t result = 0;
                    bool canFold = true;
                    
                    switch (instr->opcode) {
                        case SSAOpcode::ADD: result = it1->second + it2->second; break;
                        case SSAOpcode::SUB: result = it1->second - it2->second; break;
                        case SSAOpcode::MUL: result = it1->second * it2->second; break;
                        case SSAOpcode::DIV: 
                            if (it2->second != 0) result = it1->second / it2->second;
                            else canFold = false;
                            break;
                        case SSAOpcode::MOD:
                            if (it2->second != 0) result = it1->second % it2->second;
                            else canFold = false;
                            break;
                        default: canFold = false; break;
                    }
                    
                    if (canFold && instr->result) {
                        instr->opcode = SSAOpcode::CONST_INT;
                        instr->intValue = result;
                        instr->operands.clear();
                        constants[instr->result.get()] = result;
                    }
                }
            }
        }
    }
}

void SSAOptimizer::copyPropagation(SSAFunction& func) {
    std::map<SSAValue*, SSAValuePtr> copies;
    
    // Find all copy instructions
    for (auto& block : func.blocks) {
        for (auto& instr : block->instructions) {
            if (instr->opcode == SSAOpcode::COPY && instr->result && !instr->operands.empty()) {
                copies[instr->result.get()] = instr->operands[0];
            }
        }
    }
    
    // Replace uses of copies with original values
    for (auto& block : func.blocks) {
        for (auto& instr : block->instructions) {
            for (auto& op : instr->operands) {
                auto it = copies.find(op.get());
                if (it != copies.end()) {
                    op = it->second;
                }
            }
        }
    }
}

void SSAOptimizer::commonSubexpressionElimination(SSAFunction& func) {
    // Simple CSE: find identical instructions and replace
    std::map<std::string, SSAValuePtr> expressions;
    
    auto getExprKey = [](SSAInstruction* instr) -> std::string {
        if (instr->operands.size() != 2) return "";
        std::ostringstream oss;
        oss << static_cast<int>(instr->opcode) << "_"
            << instr->operands[0]->id << "_"
            << instr->operands[1]->id;
        return oss.str();
    };
    
    for (auto& block : func.blocks) {
        for (auto& instr : block->instructions) {
            std::string key = getExprKey(instr.get());
            if (!key.empty() && instr->result) {
                auto it = expressions.find(key);
                if (it != expressions.end()) {
                    // Replace with existing value
                    instr->opcode = SSAOpcode::COPY;
                    instr->operands.clear();
                    instr->operands.push_back(it->second);
                } else {
                    expressions[key] = instr->result;
                }
            }
        }
    }
}

bool SSAOptimizer::isInstructionDead(SSAInstruction* instr) {
    return !instr->hasSideEffects() && !instr->isTerminator();
}

std::optional<int64_t> SSAOptimizer::tryEvalConstant(SSAInstruction* instr) {
    if (instr->opcode == SSAOpcode::CONST_INT) {
        return instr->intValue;
    }
    return std::nullopt;
}

} // namespace tyl
