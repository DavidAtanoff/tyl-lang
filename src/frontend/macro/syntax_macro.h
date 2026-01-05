// Tyl Compiler - Syntax Macro System
#ifndef TYL_SYNTAX_MACRO_H
#define TYL_SYNTAX_MACRO_H

#include "frontend/token/token.h"
#include "frontend/ast/ast.h"
#include <functional>
#include <regex>
#include <set>

namespace tyl {

struct PatternElement {
    enum class Kind { LITERAL, IDENT, EXPR, BLOCK, TOKENS, OPTIONAL, REPEAT, REPEAT_ONE };
    Kind kind;
    std::string value;
    std::string capture;
    std::vector<PatternElement> children;
};

struct SyntaxMacro {
    std::string name;
    std::vector<PatternElement> pattern;
    std::string templateStr;
    std::function<StmtPtr(const std::map<std::string, std::vector<Token>>&)> customParser;
    int precedence = 0;
    bool isOperator = false;
    bool isBlockSyntax = false;
};

struct UserInfixOperator {
    std::string symbol;
    int precedence;
    std::string leftParam;
    std::string rightParam;
    std::vector<std::unique_ptr<Statement>>* body;
    bool isRightAssoc = false;
};

struct UserDSLTransformer {
    std::string name;
    std::string transformExpr;
    std::vector<std::unique_ptr<Statement>>* body;
};

class SyntaxMacroRegistry {
public:
    static SyntaxMacroRegistry& instance();
    void registerMacro(const SyntaxMacro& macro);
    void registerDSL(const std::string& name, std::function<StmtPtr(const std::vector<Token>&)> parser);
    void registerOperator(const std::string& op, int precedence, const std::string& transform);
    void registerStatementMacro(const std::string& name);
    bool isStatementMacro(const std::string& name) const;
    const std::set<std::string>& getStatementMacros() const { return statementMacros_; }
    void registerDSLName(const std::string& name);
    bool isDSLName(const std::string& name) const;
    const std::set<std::string>& getDSLNames() const { return dslNames_; }
    bool matchesMacro(const std::vector<Token>& tokens, size_t start, SyntaxMacro*& outMacro, size_t& outEnd, std::map<std::string, std::vector<Token>>& captures);
    bool isDSL(const std::string& name) const;
    std::function<StmtPtr(const std::vector<Token>&)> getDSLParser(const std::string& name) const;
    const std::vector<SyntaxMacro>& getMacros() const { return macros_; }
    void registerUserInfixOperator(const std::string& symbol, int precedence, const std::string& leftParam, const std::string& rightParam, std::vector<std::unique_ptr<Statement>>* body);
    bool isUserInfixOperator(const std::string& symbol) const;
    const UserInfixOperator* getUserInfixOperator(const std::string& symbol) const;
    const std::map<std::string, UserInfixOperator>& getUserInfixOperators() const { return userInfixOps_; }
    void registerUserDSLTransformer(const std::string& name, const std::string& transformExpr, std::vector<std::unique_ptr<Statement>>* body = nullptr);
    bool hasUserDSLTransformer(const std::string& name) const;
    const UserDSLTransformer* getUserDSLTransformer(const std::string& name) const;
    void clear() { macros_.clear(); dslParsers_.clear(); statementMacros_.clear(); dslNames_.clear(); userInfixOps_.clear(); userDSLTransformers_.clear(); }
    
private:
    SyntaxMacroRegistry() = default;
    std::vector<SyntaxMacro> macros_;
    std::map<std::string, std::function<StmtPtr(const std::vector<Token>&)>> dslParsers_;
    std::set<std::string> statementMacros_;
    std::set<std::string> dslNames_;
    std::map<std::string, UserInfixOperator> userInfixOps_;
    std::map<std::string, UserDSLTransformer> userDSLTransformers_;
};

namespace dsl {
    StmtPtr parseAsm(const std::vector<Token>& tokens);
    StmtPtr parseSQL(const std::vector<Token>& tokens);
    StmtPtr parseHTML(const std::vector<Token>& tokens);
    StmtPtr parseJSON(const std::vector<Token>& tokens);
    StmtPtr parseRegex(const std::vector<Token>& tokens);
}

} // namespace tyl

#endif // TYL_SYNTAX_MACRO_H
