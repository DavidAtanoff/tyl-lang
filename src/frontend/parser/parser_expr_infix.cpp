// Tyl Compiler - Pratt Parser Infix Operations
// Handles infix operator parsing and postfix operations

#include "parser_base.h"
#include "frontend/macro/syntax_macro.h"
#include "common/errors.h"

namespace tyl {

ExprPtr Parser::parseInfix(ExprPtr left, Precedence prec) {
    auto loc = peek().location;
    TokenType op = peek().type;
    
    // Handle CUSTOM_OP tokens
    if (check(TokenType::CUSTOM_OP)) {
        std::string opSymbol = peek().lexeme;
        advance();
        auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
        
        auto& registry = SyntaxMacroRegistry::instance();
        if (registry.isUserInfixOperator(opSymbol)) {
            auto opIdent = std::make_unique<Identifier>("__infix_" + opSymbol, loc);
            auto call = std::make_unique<CallExpr>(std::move(opIdent), loc);
            call->args.push_back(std::move(left));
            call->args.push_back(std::move(right));
            return call;
        }
        
        std::string funcName = "__op_";
        for (char c : opSymbol) {
            switch (c) {
                case '*': funcName += "star"; break;
                case '+': funcName += "plus"; break;
                case '-': funcName += "minus"; break;
                case '/': funcName += "slash"; break;
                case '%': funcName += "percent"; break;
                case '<': funcName += "lt"; break;
                case '>': funcName += "gt"; break;
                case '=': funcName += "eq"; break;
                case '!': funcName += "bang"; break;
                case '&': funcName += "amp"; break;
                case '|': funcName += "pipe"; break;
                case '^': funcName += "caret"; break;
                case '~': funcName += "tilde"; break;
                case '@': funcName += "at"; break;
                default: funcName += c; break;
            }
        }
        auto opIdent = std::make_unique<Identifier>(funcName, loc);
        auto call = std::make_unique<CallExpr>(std::move(opIdent), loc);
        call->args.push_back(std::move(left));
        call->args.push_back(std::move(right));
        return call;
    }
    
    // Handle user-defined infix operators
    if (check(TokenType::IDENTIFIER)) {
        auto& registry = SyntaxMacroRegistry::instance();
        std::string opSymbol = peek().lexeme;
        if (registry.isUserInfixOperator(opSymbol)) {
            advance();
            auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
            auto opIdent = std::make_unique<Identifier>("__infix_" + opSymbol, loc);
            auto call = std::make_unique<CallExpr>(std::move(opIdent), loc);
            call->args.push_back(std::move(left));
            call->args.push_back(std::move(right));
            return call;
        }
    }
    
    advance();
    
    // Postfix operators
    if (op == TokenType::DOT) {
        return parseMemberAccess(std::move(left), loc);
    }
    if (op == TokenType::QUESTION_DOT) {
        // Safe navigation: obj?.member
        auto member = consume(TokenType::IDENTIFIER, "Expected member name after '?.'").lexeme;
        return std::make_unique<SafeNavExpr>(std::move(left), member, loc);
    }
    if (op == TokenType::LBRACKET) {
        return parseIndexAccess(std::move(left), loc);
    }
    if (op == TokenType::LPAREN) {
        return parseCall(std::move(left), loc);
    }
    if (op == TokenType::LBRACE) {
        if (auto* id = dynamic_cast<Identifier*>(left.get())) {
            auto rec = std::make_unique<RecordExpr>(loc);
            rec->typeName = id->name;
            
            skipNewlines();
            if (!check(TokenType::RBRACE)) {
                do {
                    skipNewlines();
                    if (check(TokenType::RBRACE)) break;
                    
                    auto name = consume(TokenType::IDENTIFIER, "Expected field name").lexeme;
                    consume(TokenType::COLON, "Expected ':' after field name");
                    auto value = expression();
                    rec->fields.emplace_back(name, std::move(value));
                } while (match(TokenType::COMMA));
            }
            
            skipNewlines();
            consume(TokenType::RBRACE, "Expected '}' after record fields");
            return rec;
        }
        auto diag = errors::unexpectedToken(previous().lexeme, previous().location);
        throw TylDiagnosticError(diag);
    }
    
    // Ternary and postfix ?
    if (op == TokenType::QUESTION) {
        TokenType next = peek().type;
        bool isPostfix = (next == TokenType::NEWLINE || next == TokenType::RPAREN ||
                          next == TokenType::RBRACKET || next == TokenType::RBRACE ||
                          next == TokenType::SEMICOLON || next == TokenType::COMMA ||
                          next == TokenType::END_OF_FILE || next == TokenType::DEDENT ||
                          next == TokenType::COLON);
        
        if (isPostfix) {
            return std::make_unique<PropagateExpr>(std::move(left), loc);
        }
        
        auto thenExpr = parsePrecedence(Precedence::TERNARY);
        consume(TokenType::COLON, "Expected ':' in ternary expression");
        auto elseExpr = parsePrecedence(Precedence::TERNARY);
        return std::make_unique<TernaryExpr>(std::move(left), std::move(thenExpr), 
                                              std::move(elseExpr), loc);
    }
    
    if (op == TokenType::BANG) {
        return std::make_unique<UnaryExpr>(op, std::move(left), loc);
    }
    
    // Compound Assignment
    if (op == TokenType::ASSIGN || op == TokenType::PLUS_ASSIGN || op == TokenType::MINUS_ASSIGN ||
        op == TokenType::STAR_ASSIGN || op == TokenType::SLASH_ASSIGN || op == TokenType::PERCENT_ASSIGN) {
        auto right = parsePrecedence(Precedence::ASSIGNMENT);
        return std::make_unique<AssignExpr>(std::move(left), op, std::move(right), loc);
    }
    
    // Arrow lambda: identifier => expr
    if (op == TokenType::DOUBLE_ARROW) {
        if (auto* id = dynamic_cast<Identifier*>(left.get())) {
            return parseArrowLambda(id->name, loc);
        }
        // Could be placeholder lambda: _ => expr (but _ * 2 is handled differently)
        if (auto* ph = dynamic_cast<PlaceholderExpr*>(left.get())) {
            return parseArrowLambda("_", loc);
        }
        auto diag = errors::unexpectedToken("=>", loc);
        throw TylDiagnosticError(diag);
    }
    
    // Channel send: ch <- value
    if (op == TokenType::CHAN_SEND) {
        auto value = parsePrecedence(Precedence::ASSIGNMENT);
        return std::make_unique<ChanSendExpr>(std::move(left), std::move(value), loc);
    }
    
    // Pipe operator
    if (op == TokenType::PIPE_GT) {
        auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
        return parsePipe(std::move(left), std::move(right), loc);
    }
    
    // Range operator
    if (op == TokenType::DOTDOT) {
        auto end = parsePrecedence(static_cast<Precedence>(static_cast<int>(Precedence::RANGE) + 1));
        ExprPtr step = nullptr;
        if (match(TokenType::BY)) {
            step = parsePrecedence(static_cast<Precedence>(static_cast<int>(Precedence::RANGE) + 1));
        }
        return std::make_unique<RangeExpr>(std::move(left), std::move(end), std::move(step), loc);
    }
    
    // Inclusive range operator ..=
    if (op == TokenType::DOTDOT_EQ) {
        auto end = parsePrecedence(static_cast<Precedence>(static_cast<int>(Precedence::RANGE) + 1));
        ExprPtr step = nullptr;
        if (match(TokenType::BY)) {
            step = parsePrecedence(static_cast<Precedence>(static_cast<int>(Precedence::RANGE) + 1));
        }
        return std::make_unique<InclusiveRangeExpr>(std::move(left), std::move(end), std::move(step), loc);
    }
    
    // Type check operator: value is Type
    if (op == TokenType::IS) {
        auto typeName = parseType();
        return std::make_unique<TypeCheckExpr>(std::move(left), typeName, loc);
    }
    
    // Spaceship operator
    if (op == TokenType::SPACESHIP) {
        auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
        auto& registry = SyntaxMacroRegistry::instance();
        if (registry.isUserInfixOperator("<=>")) {
            auto opIdent = std::make_unique<Identifier>("__infix_<=>", loc);
            auto call = std::make_unique<CallExpr>(std::move(opIdent), loc);
            call->args.push_back(std::move(left));
            call->args.push_back(std::move(right));
            return call;
        }
        return std::make_unique<BinaryExpr>(std::move(left), op, std::move(right), loc);
    }
    
    // Standard binary operators
    auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
    
    if (op == TokenType::PIPE_PIPE) op = TokenType::OR;
    if (op == TokenType::AMP_AMP) op = TokenType::AND;
    
    // Check if this is a placeholder expression that should become a lambda
    // e.g., _ * 2 becomes |_it| _it * 2
    // But NOT in constraint contexts (refinement types)
    bool leftIsPlaceholder = dynamic_cast<PlaceholderExpr*>(left.get()) != nullptr;
    bool rightIsPlaceholder = dynamic_cast<PlaceholderExpr*>(right.get()) != nullptr;
    
    if (!inConstraintContext_ && (leftIsPlaceholder || rightIsPlaceholder)) {
        // Transform into a lambda: |_it| _it op right (or left op _it)
        std::string paramName = "_it";
        
        // Replace placeholder(s) with identifier
        ExprPtr newLeft, newRight;
        if (leftIsPlaceholder) {
            newLeft = std::make_unique<Identifier>(paramName, loc);
        } else {
            newLeft = std::move(left);
        }
        if (rightIsPlaceholder) {
            newRight = std::make_unique<Identifier>(paramName, loc);
        } else {
            newRight = std::move(right);
        }
        
        auto body = std::make_unique<BinaryExpr>(std::move(newLeft), op, std::move(newRight), loc);
        
        auto lambda = std::make_unique<LambdaExpr>(loc);
        lambda->params.push_back({paramName, ""});  // name, type (empty = inferred)
        lambda->body = std::move(body);
        return lambda;
    }
    
    return std::make_unique<BinaryExpr>(std::move(left), op, std::move(right), loc);
}

ExprPtr Parser::parseMemberAccess(ExprPtr object, SourceLocation loc) {
    auto member = consume(TokenType::IDENTIFIER, "Expected member name after '.'").lexeme;
    
    if (match(TokenType::LPAREN)) {
        auto memberExpr = std::make_unique<MemberExpr>(std::move(object), member, loc);
        auto call = std::make_unique<CallExpr>(std::move(memberExpr), loc);
        parseCallArgs(call.get());
        consume(TokenType::RPAREN, "Expected ')' after method arguments");
        return call;
    }
    
    // Check if object is a placeholder - transform _.field into |_it| _it.field
    // But NOT in constraint contexts (refinement types)
    if (!inConstraintContext_ && dynamic_cast<PlaceholderExpr*>(object.get()) != nullptr) {
        std::string paramName = "_it";
        auto paramIdent = std::make_unique<Identifier>(paramName, loc);
        auto body = std::make_unique<MemberExpr>(std::move(paramIdent), member, loc);
        auto lambda = std::make_unique<LambdaExpr>(loc);
        lambda->params.push_back({paramName, ""});
        lambda->body = std::move(body);
        return lambda;
    }
    
    return std::make_unique<MemberExpr>(std::move(object), member, loc);
}

ExprPtr Parser::parseCall(ExprPtr callee, SourceLocation loc) {
    auto call = std::make_unique<CallExpr>(std::move(callee), loc);
    parseCallArgs(call.get());
    consume(TokenType::RPAREN, "Expected ')' after arguments");
    return call;
}

ExprPtr Parser::parseIndexAccess(ExprPtr object, SourceLocation loc) {
    bool mightBeTypeArgs = dynamic_cast<Identifier*>(object.get()) != nullptr;
    
    if (mightBeTypeArgs) {
        size_t savedPos = current;
        std::vector<std::string> typeArgs;
        bool validTypeArgs = true;
        
        do {
            skipNewlines();
            if (check(TokenType::IDENTIFIER)) {
                std::string typeArg = parseType();
                if (!typeArg.empty()) {
                    typeArgs.push_back(typeArg);
                } else {
                    validTypeArgs = false;
                    break;
                }
            } else {
                validTypeArgs = false;
                break;
            }
        } while (match(TokenType::COMMA));
        
        if (validTypeArgs && check(TokenType::RBRACKET)) {
            advance();
            
            // Check for call with type args: Name[T](args)
            if (check(TokenType::LPAREN)) {
                advance();
                auto call = std::make_unique<CallExpr>(std::move(object), loc);
                call->typeArgs = std::move(typeArgs);
                parseCallArgs(call.get());
                consume(TokenType::RPAREN, "Expected ')' after arguments");
                return call;
            }
            
            // Check for record literal with type args: Name[T] { fields }
            if (check(TokenType::LBRACE)) {
                advance();
                auto* id = dynamic_cast<Identifier*>(object.get());
                auto rec = std::make_unique<RecordExpr>(loc);
                rec->typeName = id->name;
                rec->typeArgs = std::move(typeArgs);
                
                skipNewlines();
                if (!check(TokenType::RBRACE)) {
                    do {
                        skipNewlines();
                        if (check(TokenType::RBRACE)) break;
                        
                        auto name = consume(TokenType::IDENTIFIER, "Expected field name").lexeme;
                        consume(TokenType::COLON, "Expected ':' after field name");
                        auto value = expression();
                        rec->fields.emplace_back(name, std::move(value));
                    } while (match(TokenType::COMMA));
                }
                
                skipNewlines();
                consume(TokenType::RBRACE, "Expected '}' after record fields");
                return rec;
            }
        }
        
        current = savedPos;
    }
    
    auto index = expression();
    consume(TokenType::RBRACKET, "Expected ']' after index");
    return std::make_unique<IndexExpr>(std::move(object), std::move(index), loc);
}

ExprPtr Parser::parsePipe(ExprPtr left, ExprPtr right, SourceLocation loc) {
    if (auto* existingCall = dynamic_cast<CallExpr*>(right.get())) {
        std::vector<ExprPtr> newArgs;
        newArgs.push_back(std::move(left));
        for (auto& arg : existingCall->args) {
            newArgs.push_back(std::move(arg));
        }
        existingCall->args = std::move(newArgs);
        return right;
    }
    
    auto call = std::make_unique<CallExpr>(std::move(right), loc);
    call->args.push_back(std::move(left));
    return call;
}

} // namespace tyl
