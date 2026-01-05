// Tyl Compiler - Pratt Parser Core
// Core Pratt parsing implementation for expressions

#include "parser_base.h"
#include "frontend/macro/syntax_macro.h"
#include "frontend/lexer/lexer.h"
#include "common/errors.h"

namespace tyl {

static Parser::Precedence getInfixPrecedence(TokenType type) {
    switch (type) {
        case TokenType::ASSIGN:
        case TokenType::PLUS_ASSIGN:
        case TokenType::MINUS_ASSIGN:
        case TokenType::STAR_ASSIGN:
        case TokenType::SLASH_ASSIGN:
        case TokenType::PERCENT_ASSIGN: return Parser::Precedence::ASSIGNMENT;
        case TokenType::CHAN_SEND: return Parser::Precedence::ASSIGNMENT;  // ch <- value has low precedence
        case TokenType::QUESTION_QUESTION: return Parser::Precedence::NULL_COALESCE;
        case TokenType::OR:
        case TokenType::PIPE_PIPE: return Parser::Precedence::OR;
        case TokenType::AND:
        case TokenType::AMP_AMP: return Parser::Precedence::AND;
        case TokenType::PIPE: return Parser::Precedence::BIT_OR;
        case TokenType::CARET: return Parser::Precedence::BIT_XOR;
        case TokenType::AMP: return Parser::Precedence::BIT_AND;
        case TokenType::EQ:
        case TokenType::NE: return Parser::Precedence::EQUALITY;
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::LE:
        case TokenType::GE:
        case TokenType::SPACESHIP:
        case TokenType::IS: return Parser::Precedence::COMPARISON;  // is for type checking
        case TokenType::DOTDOT:
        case TokenType::DOTDOT_EQ: return Parser::Precedence::RANGE;  // ..= inclusive range
        case TokenType::PLUS:
        case TokenType::MINUS: return Parser::Precedence::TERM;
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::PERCENT: return Parser::Precedence::FACTOR;
        case TokenType::CUSTOM_OP: return Parser::Precedence::FACTOR;
        case TokenType::PIPE_GT: return Parser::Precedence::PIPE;
        case TokenType::QUESTION: return Parser::Precedence::TERNARY;
        case TokenType::DOUBLE_ARROW: return Parser::Precedence::ASSIGNMENT;  // => for arrow lambdas
        case TokenType::DOT:
        case TokenType::QUESTION_DOT:  // ?. safe navigation
        case TokenType::LBRACKET:
        case TokenType::LPAREN:
        case TokenType::LBRACE: return Parser::Precedence::POSTFIX;
        default: return Parser::Precedence::NONE;
    }
}

ExprPtr Parser::expression() {
    return parsePrecedence(Precedence::ASSIGNMENT);
}

ExprPtr Parser::parsePrecedence(Precedence minPrec) {
    ExprPtr left = parsePrefix();
    
    while (!isAtEnd()) {
        Precedence prec = getInfixPrecedence(peek().type);
        
        if (check(TokenType::IDENTIFIER)) {
            auto& registry = SyntaxMacroRegistry::instance();
            if (registry.isUserInfixOperator(peek().lexeme)) {
                prec = Precedence::COMPARISON;
            }
        }
        
        if (check(TokenType::IF) && minPrec <= Precedence::TERNARY) {
            left = parseTernary(std::move(left));
            continue;
        }
        
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "as") {
            left = parseCast(std::move(left));
            continue;
        }
        
        if (prec == Precedence::NONE || prec < minPrec) break;
        
        left = parseInfix(std::move(left), prec);
    }
    
    return left;
}

ExprPtr Parser::parsePrefix() {
    auto loc = peek().location;
    
    if (match({TokenType::MINUS, TokenType::NOT, TokenType::BANG, TokenType::TILDE})) {
        auto op = previous().type;
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<UnaryExpr>(op, std::move(operand), loc);
    }
    
    if (match(TokenType::AMP)) {
        bool isMut = match(TokenType::MUT);
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<BorrowExpr>(std::move(operand), isMut, loc);
    }
    
    if (match(TokenType::STAR)) {
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<DerefExpr>(std::move(operand), loc);
    }
    
    if (match(TokenType::AWAIT)) {
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<AwaitExpr>(std::move(operand), loc);
    }
    
    if (match(TokenType::SPAWN)) {
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<SpawnExpr>(std::move(operand), loc);
    }
    
    // Channel receive: <- ch
    if (match(TokenType::CHAN_SEND)) {
        auto channel = parsePrecedence(Precedence::UNARY);
        return std::make_unique<ChanRecvExpr>(std::move(channel), loc);
    }
    
    if (match(TokenType::NEW)) {
        return parseNew(loc);
    }
    
    return primary();
}

ExprPtr Parser::parseTernary(ExprPtr thenExpr) {
    advance();
    auto condition = parsePrecedence(Precedence::TERNARY);
    consume(TokenType::ELSE, "Expected 'else' in ternary expression");
    auto elseExpr = parsePrecedence(Precedence::TERNARY);
    return std::make_unique<TernaryExpr>(std::move(condition), std::move(thenExpr), 
                                          std::move(elseExpr), thenExpr->location);
}

ExprPtr Parser::parseCast(ExprPtr expr) {
    auto loc = peek().location;
    advance();
    auto targetType = parseType();
    return std::make_unique<CastExpr>(std::move(expr), targetType, loc);
}

ExprPtr Parser::parseNew(SourceLocation loc) {
    auto typeName = consume(TokenType::IDENTIFIER, "Expected type name after 'new'").lexeme;
    auto newExpr = std::make_unique<NewExpr>(typeName, loc);
    
    if (match(TokenType::LPAREN)) {
        if (!check(TokenType::RPAREN)) {
            do {
                newExpr->args.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RPAREN, "Expected ')' after new arguments");
    } else if (match(TokenType::LBRACE)) {
        if (!check(TokenType::RBRACE)) {
            do {
                newExpr->args.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RBRACE, "Expected '}' after new initializer");
    }
    
    return newExpr;
}

// Legacy compatibility wrappers
ExprPtr Parser::assignment() { return parsePrecedence(Precedence::ASSIGNMENT); }
ExprPtr Parser::ternary() { return parsePrecedence(Precedence::TERNARY); }
ExprPtr Parser::nullCoalesce() { return parsePrecedence(Precedence::NULL_COALESCE); }
ExprPtr Parser::userInfixExpr() { return parsePrecedence(Precedence::COMPARISON); }
ExprPtr Parser::pipeExpr() { return parsePrecedence(Precedence::PIPE); }
ExprPtr Parser::logicalOr() { return parsePrecedence(Precedence::OR); }
ExprPtr Parser::logicalAnd() { return parsePrecedence(Precedence::AND); }
ExprPtr Parser::bitwiseOr() { return parsePrecedence(Precedence::BIT_OR); }
ExprPtr Parser::bitwiseXor() { return parsePrecedence(Precedence::BIT_XOR); }
ExprPtr Parser::bitwiseAnd() { return parsePrecedence(Precedence::BIT_AND); }
ExprPtr Parser::equality() { return parsePrecedence(Precedence::EQUALITY); }
ExprPtr Parser::comparison() { return parsePrecedence(Precedence::COMPARISON); }
ExprPtr Parser::range() { return parsePrecedence(Precedence::RANGE); }
ExprPtr Parser::term() { return parsePrecedence(Precedence::TERM); }
ExprPtr Parser::factor() { return parsePrecedence(Precedence::FACTOR); }
ExprPtr Parser::unary() { return parsePrefix(); }
ExprPtr Parser::postfix() { return parsePrecedence(Precedence::POSTFIX); }

} // namespace tyl
