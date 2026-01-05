// Tyl Compiler - Parser Core Implementation
// Token navigation, error recovery, and main parse entry point

#include "parser_base.h"
#include "frontend/macro/syntax_macro.h"
#include "common/errors.h"
#include <unordered_set>

namespace tyl {

Parser::Parser(std::vector<Token> toks) : tokens(std::move(toks)) {}

Token& Parser::peek() { return tokens[current]; }
Token& Parser::previous() { return tokens[current - 1]; }
bool Parser::isAtEnd() { return peek().type == TokenType::END_OF_FILE; }

Token& Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::check(TokenType type) {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    auto diag = errors::expectedToken(message, tokenTypeToString(peek().type), peek().location);
    throw TylDiagnosticError(diag);
}

void Parser::skipNewlines() {
    while (match(TokenType::NEWLINE)) {}
}

void Parser::synchronize() {
    advance();
    while (!isAtEnd()) {
        if (previous().type == TokenType::NEWLINE) return;
        switch (peek().type) {
            case TokenType::FN:
            case TokenType::LET:
            case TokenType::MUT:
            case TokenType::CONST:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::FOR:
            case TokenType::RETURN:
            case TokenType::RECORD:
            case TokenType::ENUM:
                return;
            default:
                advance();
        }
    }
}

bool Parser::isAtStatementBoundary() {
    return check(TokenType::NEWLINE) || check(TokenType::END_OF_FILE) || 
           check(TokenType::DEDENT) || check(TokenType::SEMICOLON);
}

void Parser::preScanSyntaxDeclarations() {
    size_t savedPos = current;
    
    while (!isAtEnd()) {
        if (check(TokenType::SYNTAX)) {
            advance();
            if (check(TokenType::IDENTIFIER)) {
                std::string dslName = advance().lexeme;
                SyntaxMacroRegistry::instance().registerDSLName(dslName);
            }
        } else if (check(TokenType::MACRO)) {
            advance();
            if (check(TokenType::IDENTIFIER) && peek().lexeme == "infix") {
                advance();
                if (check(TokenType::STRING)) {
                    std::string opSymbol = std::get<std::string>(advance().literal);
                    int precedence = 50;
                    if (check(TokenType::INTEGER)) {
                        precedence = static_cast<int>(std::get<int64_t>(advance().literal));
                    }
                    SyntaxMacroRegistry::instance().registerUserInfixOperator(
                        opSymbol, precedence, "left", "right", nullptr);
                }
            }
        } else {
            advance();
        }
    }
    
    current = savedPos;
}

std::unique_ptr<Program> Parser::parse() {
    auto program = std::make_unique<Program>(peek().location);
    
    preScanSyntaxDeclarations();
    
    skipNewlines();
    while (!isAtEnd()) {
        try {
            auto stmt = declaration();
            if (stmt) {
                program->statements.push_back(std::move(stmt));
            }
        } catch (const TylDiagnosticError& e) {
            e.render();
            synchronize();
        } catch (const TylError& e) {
            std::cerr << "Parse error: " << e.what() << "\n";
            synchronize();
        }
        skipNewlines();
    }
    
    return program;
}

} // namespace tyl
