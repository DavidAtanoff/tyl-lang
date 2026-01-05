// Tyl Compiler - Syntax Macro System Implementation
#include "syntax_macro.h"

namespace tyl {

SyntaxMacroRegistry& SyntaxMacroRegistry::instance() {
    static SyntaxMacroRegistry reg;
    return reg;
}

void SyntaxMacroRegistry::registerMacro(const SyntaxMacro& macro) { macros_.push_back(macro); }

void SyntaxMacroRegistry::registerDSL(const std::string& name, std::function<StmtPtr(const std::vector<Token>&)> parser) {
    dslParsers_[name] = parser;
}

void SyntaxMacroRegistry::registerOperator(const std::string& op, int precedence, const std::string& transform) {
    SyntaxMacro macro;
    macro.name = "op_" + op;
    macro.isOperator = true;
    macro.precedence = precedence;
    macro.templateStr = transform;
    PatternElement left; left.kind = PatternElement::Kind::EXPR; left.capture = "left";
    PatternElement opElem; opElem.kind = PatternElement::Kind::LITERAL; opElem.value = op;
    PatternElement right; right.kind = PatternElement::Kind::EXPR; right.capture = "right";
    macro.pattern = {left, opElem, right};
    macros_.push_back(macro);
}

void SyntaxMacroRegistry::registerStatementMacro(const std::string& name) { statementMacros_.insert(name); }
bool SyntaxMacroRegistry::isStatementMacro(const std::string& name) const { return statementMacros_.find(name) != statementMacros_.end(); }
void SyntaxMacroRegistry::registerDSLName(const std::string& name) { dslNames_.insert(name); }
bool SyntaxMacroRegistry::isDSLName(const std::string& name) const { return dslNames_.find(name) != dslNames_.end(); }

void SyntaxMacroRegistry::registerUserInfixOperator(const std::string& symbol, int precedence,
                                                     const std::string& leftParam, const std::string& rightParam,
                                                     std::vector<std::unique_ptr<Statement>>* body) {
    UserInfixOperator op; op.symbol = symbol; op.precedence = precedence;
    op.leftParam = leftParam; op.rightParam = rightParam; op.body = body;
    userInfixOps_[symbol] = std::move(op);
}

bool SyntaxMacroRegistry::isUserInfixOperator(const std::string& symbol) const { return userInfixOps_.find(symbol) != userInfixOps_.end(); }
const UserInfixOperator* SyntaxMacroRegistry::getUserInfixOperator(const std::string& symbol) const {
    auto it = userInfixOps_.find(symbol);
    return it != userInfixOps_.end() ? &it->second : nullptr;
}

void SyntaxMacroRegistry::registerUserDSLTransformer(const std::string& name, const std::string& transformExpr,
                                                      std::vector<std::unique_ptr<Statement>>* body) {
    UserDSLTransformer transformer; transformer.name = name; transformer.transformExpr = transformExpr; transformer.body = body;
    userDSLTransformers_[name] = std::move(transformer);
    dslNames_.insert(name);
}

bool SyntaxMacroRegistry::hasUserDSLTransformer(const std::string& name) const { return userDSLTransformers_.find(name) != userDSLTransformers_.end(); }
const UserDSLTransformer* SyntaxMacroRegistry::getUserDSLTransformer(const std::string& name) const {
    auto it = userDSLTransformers_.find(name);
    return it != userDSLTransformers_.end() ? &it->second : nullptr;
}

bool SyntaxMacroRegistry::matchesMacro(const std::vector<Token>& tokens, size_t start, SyntaxMacro*& outMacro, size_t& outEnd, std::map<std::string, std::vector<Token>>& captures) {
    for (auto& macro : macros_) {
        size_t pos = start; captures.clear(); bool matched = true;
        for (auto& elem : macro.pattern) {
            if (pos >= tokens.size()) { matched = false; break; }
            switch (elem.kind) {
                case PatternElement::Kind::LITERAL:
                    if (tokens[pos].lexeme != elem.value) matched = false; else pos++;
                    break;
                case PatternElement::Kind::IDENT:
                    if (tokens[pos].type == TokenType::IDENTIFIER) { captures[elem.capture].push_back(tokens[pos]); pos++; }
                    else matched = false;
                    break;
                case PatternElement::Kind::EXPR:
                    captures[elem.capture].push_back(tokens[pos]); pos++;
                    break;
                default: break;
            }
            if (!matched) break;
        }
        if (matched) { outMacro = &macro; outEnd = pos; return true; }
    }
    return false;
}

bool SyntaxMacroRegistry::isDSL(const std::string& name) const { return dslParsers_.find(name) != dslParsers_.end(); }
std::function<StmtPtr(const std::vector<Token>&)> SyntaxMacroRegistry::getDSLParser(const std::string& name) const {
    auto it = dslParsers_.find(name);
    return it != dslParsers_.end() ? it->second : nullptr;
}

namespace dsl {

StmtPtr parseAsm(const std::vector<Token>& tokens) {
    std::string asmCode;
    for (const auto& tok : tokens) { if (!asmCode.empty()) asmCode += " "; asmCode += tok.lexeme; }
    auto block = std::make_unique<Block>(tokens.empty() ? SourceLocation{} : tokens[0].location);
    return std::make_unique<UnsafeBlock>(std::move(block), tokens.empty() ? SourceLocation{} : tokens[0].location);
}

StmtPtr parseSQL(const std::vector<Token>& tokens) {
    std::string sql;
    for (const auto& tok : tokens) { if (!sql.empty()) sql += " "; sql += tok.lexeme; }
    auto sqlStr = std::make_unique<StringLiteral>(sql, tokens.empty() ? SourceLocation{} : tokens[0].location);
    auto dbIdent = std::make_unique<Identifier>("db", tokens.empty() ? SourceLocation{} : tokens[0].location);
    auto queryMember = std::make_unique<MemberExpr>(std::move(dbIdent), "query", tokens.empty() ? SourceLocation{} : tokens[0].location);
    auto call = std::make_unique<CallExpr>(std::move(queryMember), tokens.empty() ? SourceLocation{} : tokens[0].location);
    call->args.push_back(std::move(sqlStr));
    return std::make_unique<ExprStmt>(std::move(call), tokens.empty() ? SourceLocation{} : tokens[0].location);
}

StmtPtr parseHTML(const std::vector<Token>& tokens) {
    std::string html;
    for (const auto& tok : tokens) html += tok.lexeme;
    auto htmlStr = std::make_unique<StringLiteral>(html, tokens.empty() ? SourceLocation{} : tokens[0].location);
    return std::make_unique<ExprStmt>(std::move(htmlStr), tokens.empty() ? SourceLocation{} : tokens[0].location);
}

StmtPtr parseJSON(const std::vector<Token>& tokens) {
    std::string json;
    for (const auto& tok : tokens) json += tok.lexeme;
    auto jsonStr = std::make_unique<StringLiteral>(json, tokens.empty() ? SourceLocation{} : tokens[0].location);
    return std::make_unique<ExprStmt>(std::move(jsonStr), tokens.empty() ? SourceLocation{} : tokens[0].location);
}

StmtPtr parseRegex(const std::vector<Token>& tokens) {
    std::string pattern;
    for (const auto& tok : tokens) pattern += tok.lexeme;
    auto patternStr = std::make_unique<StringLiteral>(pattern, tokens.empty() ? SourceLocation{} : tokens[0].location);
    auto regexIdent = std::make_unique<Identifier>("Regex", tokens.empty() ? SourceLocation{} : tokens[0].location);
    auto call = std::make_unique<CallExpr>(std::move(regexIdent), tokens.empty() ? SourceLocation{} : tokens[0].location);
    call->args.push_back(std::move(patternStr));
    return std::make_unique<ExprStmt>(std::move(call), tokens.empty() ? SourceLocation{} : tokens[0].location);
}

} // namespace dsl
} // namespace tyl
