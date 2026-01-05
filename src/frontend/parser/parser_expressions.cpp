// Tyl Compiler - Pratt Parser Expression Implementation
// Uses Pratt parsing (top-down operator precedence) for clean, extensible expression parsing

#include "parser_base.h"
#include "frontend/macro/syntax_macro.h"
#include "frontend/lexer/lexer.h"
#include "common/errors.h"
#include <unordered_set>

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
        case TokenType::DOTDOT: return Parser::Precedence::RANGE;  // .. inclusive range
        case TokenType::PLUS:
        case TokenType::MINUS: return Parser::Precedence::TERM;
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::PERCENT: return Parser::Precedence::FACTOR;
        case TokenType::CUSTOM_OP: return Parser::Precedence::FACTOR; // Custom ops get factor precedence by default
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

// Check if token is an infix operator
static bool isInfixOperator(TokenType type) {
    return getInfixPrecedence(type) != Parser::Precedence::NONE;
}

// Main Pratt parser entry point
ExprPtr Parser::expression() {
    return parsePrecedence(Precedence::ASSIGNMENT);
}

// Core Pratt parsing loop
ExprPtr Parser::parsePrecedence(Precedence minPrec) {
    // Parse prefix (includes primary expressions)
    ExprPtr left = parsePrefix();
    
    // Parse infix operators while they have higher precedence
    while (!isAtEnd()) {
        Precedence prec = getInfixPrecedence(peek().type);
        
        // Special case: { is only a postfix operator for record construction
        // when left is an identifier (type name). Otherwise, stop parsing.
        // This allows: if x > 0 { ... }
        if (check(TokenType::LBRACE) && !dynamic_cast<Identifier*>(left.get())) {
            break;
        }
        
        // Check for user-defined infix operators
        if (check(TokenType::IDENTIFIER)) {
            auto& registry = SyntaxMacroRegistry::instance();
            if (registry.isUserInfixOperator(peek().lexeme)) {
                prec = Precedence::COMPARISON; // User operators get comparison precedence
            }
        }
        
        // Check for ternary (expr if cond else other)
        if (check(TokenType::IF) && minPrec <= Precedence::TERNARY) {
            left = parseTernary(std::move(left));
            continue;
        }
        
        // Check for 'as' cast
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "as") {
            left = parseCast(std::move(left));
            continue;
        }
        
        if (prec == Precedence::NONE || prec < minPrec) break;
        
        left = parseInfix(std::move(left), prec);
    }
    
    return left;
}

// Parse prefix expressions (unary operators and primary)
ExprPtr Parser::parsePrefix() {
    auto loc = peek().location;
    
    // Unary operators
    if (match({TokenType::MINUS, TokenType::NOT, TokenType::BANG, TokenType::TILDE})) {
        auto op = previous().type;
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<UnaryExpr>(op, std::move(operand), loc);
    }
    
    // Address-of / Borrow
    if (match(TokenType::AMP)) {
        bool isMut = match(TokenType::MUT);
        auto operand = parsePrecedence(Precedence::UNARY);
        // Create BorrowExpr for safe borrows (& and &mut)
        return std::make_unique<BorrowExpr>(std::move(operand), isMut, loc);
    }
    
    // Dereference
    if (match(TokenType::STAR)) {
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<DerefExpr>(std::move(operand), loc);
    }
    
    // Await
    if (match(TokenType::AWAIT)) {
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<AwaitExpr>(std::move(operand), loc);
    }
    
    // Spawn
    if (match(TokenType::SPAWN)) {
        auto operand = parsePrecedence(Precedence::UNARY);
        return std::make_unique<SpawnExpr>(std::move(operand), loc);
    }
    
    // Channel receive: <- ch
    if (match(TokenType::CHAN_SEND)) {
        auto channel = parsePrecedence(Precedence::UNARY);
        return std::make_unique<ChanRecvExpr>(std::move(channel), loc);
    }
    
    // New expression
    if (match(TokenType::NEW)) {
        return parseNew(loc);
    }
    
    return primary();
}

// Parse infix expressions
ExprPtr Parser::parseInfix(ExprPtr left, Precedence prec) {
    auto loc = peek().location;
    TokenType op = peek().type;
    
    // Handle CUSTOM_OP tokens (like **)
    if (check(TokenType::CUSTOM_OP)) {
        std::string opSymbol = peek().lexeme;
        advance();
        auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
        
        // Check if there's a user-defined infix macro for this operator
        auto& registry = SyntaxMacroRegistry::instance();
        if (registry.isUserInfixOperator(opSymbol)) {
            auto opIdent = std::make_unique<Identifier>("__infix_" + opSymbol, loc);
            auto call = std::make_unique<CallExpr>(std::move(opIdent), loc);
            call->args.push_back(std::move(left));
            call->args.push_back(std::move(right));
            return call;
        }
        
        // No macro defined - create a call to a function named after the operator
        // e.g., ** becomes __op_starstar
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
    
    // Handle user-defined infix operators (identifier-based)
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
    
    advance(); // consume operator
    
    // Postfix operators (left-associative, special handling)
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
        // Record construction with type name: Point{x: 10, y: 20}
        // Only treat { as postfix if left is an identifier (type name)
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
        // Not an identifier - this { is not for us (e.g., if x > 0 { ... })
        // Don't consume it, just return the left expression
        current--;  // Put back the { we consumed in advance()
        return left;
    }
    
    // Standard Ternary: condition ? then : else
    // OR Postfix error propagation: expr?
    if (op == TokenType::QUESTION) {
        // Check if this is postfix ? (error propagation) or ternary ?:
        // If the next token cannot start an expression, it's postfix ?
        // Common cases: newline, ), ], }, ;, ,, operators, EOF
        TokenType next = peek().type;
        bool isPostfix = (next == TokenType::NEWLINE || next == TokenType::RPAREN ||
                          next == TokenType::RBRACKET || next == TokenType::RBRACE ||
                          next == TokenType::SEMICOLON || next == TokenType::COMMA ||
                          next == TokenType::END_OF_FILE || next == TokenType::DEDENT ||
                          next == TokenType::COLON);  // : alone means postfix, not ternary
        
        if (isPostfix) {
            // Postfix ? - error propagation operator
            return std::make_unique<PropagateExpr>(std::move(left), loc);
        }
        
        // Ternary: condition ? then : else
        auto thenExpr = parsePrecedence(Precedence::TERNARY);
        consume(TokenType::COLON, "Expected ':' in ternary expression");
        auto elseExpr = parsePrecedence(Precedence::TERNARY);
        return std::make_unique<TernaryExpr>(std::move(left), std::move(thenExpr), 
                                              std::move(elseExpr), loc);
    }
    
    // Postfix !
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
        // Could be placeholder lambda: _ => expr
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
    
    // Pipe operator (special: transforms into function call)
    if (op == TokenType::PIPE_GT) {
        auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
        return parsePipe(std::move(left), std::move(right), loc);
    }
    
    // Range operator: 1..5 is inclusive (1,2,3,4,5)
    if (op == TokenType::DOTDOT) {
        auto end = parsePrecedence(static_cast<Precedence>(static_cast<int>(Precedence::RANGE) + 1));
        ExprPtr step = nullptr;
        if (match(TokenType::BY)) {
            step = parsePrecedence(static_cast<Precedence>(static_cast<int>(Precedence::RANGE) + 1));
        }
        return std::make_unique<RangeExpr>(std::move(left), std::move(end), std::move(step), loc);
    }
    
    // Type check operator: value is Type
    if (op == TokenType::IS) {
        auto typeName = parseType();
        return std::make_unique<TypeCheckExpr>(std::move(left), typeName, loc);
    }
    
    // Spaceship operator with user override check
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
    
    // Standard binary operators (left-associative)
    auto right = parsePrecedence(static_cast<Precedence>(static_cast<int>(prec) + 1));
    
    // Normalize OR/AND variants
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

// Parse ternary: value if condition else other
ExprPtr Parser::parseTernary(ExprPtr thenExpr) {
    advance(); // consume 'if'
    auto condition = parsePrecedence(Precedence::TERNARY);
    consume(TokenType::ELSE, "Expected 'else' in ternary expression");
    auto elseExpr = parsePrecedence(Precedence::TERNARY);
    return std::make_unique<TernaryExpr>(std::move(condition), std::move(thenExpr), 
                                          std::move(elseExpr), thenExpr->location);
}

// Parse cast: expr as Type
ExprPtr Parser::parseCast(ExprPtr expr) {
    auto loc = peek().location;
    advance(); // consume 'as'
    auto targetType = parseType();
    return std::make_unique<CastExpr>(std::move(expr), targetType, loc);
}

// Parse member access: expr.member or expr.method()
ExprPtr Parser::parseMemberAccess(ExprPtr object, SourceLocation loc) {
    auto member = consume(TokenType::IDENTIFIER, "Expected member name after '.'").lexeme;
    
    // Check for method call
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

// Parse function call: expr(args) or expr[TypeArgs](args)
ExprPtr Parser::parseCall(ExprPtr callee, SourceLocation loc) {
    auto call = std::make_unique<CallExpr>(std::move(callee), loc);
    parseCallArgs(call.get());
    consume(TokenType::RPAREN, "Expected ')' after arguments");
    return call;
}

// Parse index access or explicit type arguments followed by call
ExprPtr Parser::parseIndexAccess(ExprPtr object, SourceLocation loc) {
    // Check if this might be explicit type arguments for a generic call
    // Pattern: identifier[Type1, Type2](args)
    // We need to look ahead to see if there's a ( after the ]
    
    // First, check if the object is an identifier (potential generic function)
    bool mightBeTypeArgs = dynamic_cast<Identifier*>(object.get()) != nullptr;
    
    if (mightBeTypeArgs) {
        // Save position to potentially backtrack
        size_t savedPos = current;
        
        // Try to parse as type arguments
        std::vector<std::string> typeArgs;
        bool validTypeArgs = true;
        
        // Parse type arguments
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
            advance(); // consume ]
            
            // Check if followed by (
            if (check(TokenType::LPAREN)) {
                advance(); // consume (
                auto call = std::make_unique<CallExpr>(std::move(object), loc);
                call->typeArgs = std::move(typeArgs);
                parseCallArgs(call.get());
                consume(TokenType::RPAREN, "Expected ')' after arguments");
                return call;
            }
        }
        
        // Not type arguments - restore position and parse as index
        current = savedPos;
    }
    
    // Regular index access
    auto index = expression();
    consume(TokenType::RBRACKET, "Expected ']' after index");
    return std::make_unique<IndexExpr>(std::move(object), std::move(index), loc);
}

// Parse pipe: left |> right  ->  right(left) or right.call(left, existing_args...)
ExprPtr Parser::parsePipe(ExprPtr left, ExprPtr right, SourceLocation loc) {
    if (auto* existingCall = dynamic_cast<CallExpr*>(right.get())) {
        // Insert left as first argument
        std::vector<ExprPtr> newArgs;
        newArgs.push_back(std::move(left));
        for (auto& arg : existingCall->args) {
            newArgs.push_back(std::move(arg));
        }
        existingCall->args = std::move(newArgs);
        return right;
    }
    
    // Create new call with left as argument
    auto call = std::make_unique<CallExpr>(std::move(right), loc);
    call->args.push_back(std::move(left));
    return call;
}

// Parse new expression: new Type(args) or new Type{args}
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


// Primary expressions (literals, identifiers, grouping)
ExprPtr Parser::primary() {
    auto loc = peek().location;
    
    // Algebraic Effects - handle expression
    // handle expr:
    //     Effect.op(params) => body
    if (match(TokenType::HANDLE)) {
        auto expr = expression();
        consume(TokenType::COLON, "Expected ':' after handle expression");
        match(TokenType::NEWLINE);
        
        auto handleExpr = std::make_unique<HandleExpr>(std::move(expr), loc);
        
        consume(TokenType::INDENT, "Expected indented handler block");
        skipNewlines();
        
        // Parse handler cases
        while (!check(TokenType::DEDENT) && !isAtEnd()) {
            // Parse Effect.op(params) => body
            std::string effectName = consume(TokenType::IDENTIFIER, "Expected effect name").lexeme;
            consume(TokenType::DOT, "Expected '.' after effect name");
            std::string opName = consume(TokenType::IDENTIFIER, "Expected operation name").lexeme;
            
            EffectHandlerCase handlerCase(effectName, opName);
            
            // Parse parameter bindings
            consume(TokenType::LPAREN, "Expected '(' after operation name");
            if (!check(TokenType::RPAREN)) {
                do {
                    handlerCase.paramNames.push_back(
                        consume(TokenType::IDENTIFIER, "Expected parameter name").lexeme);
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RPAREN, "Expected ')' after parameters");
            
            // Check for resume parameter: => |k| body
            if (match(TokenType::DOUBLE_ARROW)) {
                if (match(TokenType::PIPE)) {
                    handlerCase.resumeParam = consume(TokenType::IDENTIFIER, "Expected resume parameter").lexeme;
                    consume(TokenType::PIPE, "Expected '|' after resume parameter");
                }
            } else {
                consume(TokenType::ARROW, "Expected '=>' or '->' after handler pattern");
            }
            
            // Parse handler body
            if (check(TokenType::NEWLINE)) {
                match(TokenType::NEWLINE);
                handlerCase.body = block();
            } else {
                auto bodyExpr = expression();
                handlerCase.body = std::make_unique<ExprStmt>(std::move(bodyExpr), loc);
                match(TokenType::NEWLINE);
            }
            
            handleExpr->handlers.push_back(std::move(handlerCase));
            skipNewlines();
        }
        
        consume(TokenType::DEDENT, "Expected end of handler block");
        return handleExpr;
    }
    
    // Algebraic Effects - perform expression
    // perform Effect.op(args)
    if (match(TokenType::PERFORM)) {
        std::string effectName = consume(TokenType::IDENTIFIER, "Expected effect name").lexeme;
        consume(TokenType::DOT, "Expected '.' after effect name");
        std::string opName = consume(TokenType::IDENTIFIER, "Expected operation name").lexeme;
        
        auto performExpr = std::make_unique<PerformEffectExpr>(effectName, opName, loc);
        
        // Parse arguments
        consume(TokenType::LPAREN, "Expected '(' after operation name");
        if (!check(TokenType::RPAREN)) {
            do {
                performExpr->args.push_back(expression());
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RPAREN, "Expected ')' after arguments");
        
        return performExpr;
    }
    
    // Algebraic Effects - resume expression
    // resume(value)
    if (match(TokenType::RESUME)) {
        consume(TokenType::LPAREN, "Expected '(' after resume");
        ExprPtr value = nullptr;
        if (!check(TokenType::RPAREN)) {
            value = expression();
        }
        consume(TokenType::RPAREN, "Expected ')' after resume value");
        return std::make_unique<ResumeExpr>(std::move(value), loc);
    }
    
    // Channel creation: chan[T] or chan[T, N]
    if (match(TokenType::CHAN)) {
        consume(TokenType::LBRACKET, "Expected '[' after chan");
        std::string elemType = parseType();
        int64_t bufSize = 0;
        if (match(TokenType::COMMA)) {
            auto sizeTok = consume(TokenType::INTEGER, "Expected buffer size");
            bufSize = std::get<int64_t>(sizeTok.literal);
        }
        consume(TokenType::RBRACKET, "Expected ']' after channel type");
        return std::make_unique<MakeChanExpr>(elemType, bufSize, loc);
    }
    
    // Mutex creation: Mutex[T]
    if (match(TokenType::MUTEX)) {
        consume(TokenType::LBRACKET, "Expected '[' after Mutex");
        std::string elemType = parseType();
        consume(TokenType::RBRACKET, "Expected ']' after Mutex type");
        return std::make_unique<MakeMutexExpr>(elemType, loc);
    }
    
    // RWLock creation: RWLock[T]
    if (match(TokenType::RWLOCK)) {
        consume(TokenType::LBRACKET, "Expected '[' after RWLock");
        std::string elemType = parseType();
        consume(TokenType::RBRACKET, "Expected ']' after RWLock type");
        return std::make_unique<MakeRWLockExpr>(elemType, loc);
    }
    
    // Cond creation: Cond or Cond()
    if (match(TokenType::COND)) {
        if (match(TokenType::LPAREN)) {
            consume(TokenType::RPAREN, "Expected ')' after Cond");
        }
        return std::make_unique<MakeCondExpr>(loc);
    }
    
    // Semaphore creation: Semaphore(initial, max)
    if (match(TokenType::SEMAPHORE)) {
        consume(TokenType::LPAREN, "Expected '(' after Semaphore");
        auto initTok = consume(TokenType::INTEGER, "Expected initial count");
        int64_t initialCount = std::get<int64_t>(initTok.literal);
        int64_t maxCount = initialCount;  // Default max = initial
        if (match(TokenType::COMMA)) {
            auto maxTok = consume(TokenType::INTEGER, "Expected max count");
            maxCount = std::get<int64_t>(maxTok.literal);
        }
        consume(TokenType::RPAREN, "Expected ')' after Semaphore arguments");
        return std::make_unique<MakeSemaphoreExpr>(initialCount, maxCount, loc);
    }
    
    // Atomic creation: Atomic[T](value)
    if (match(TokenType::ATOMIC)) {
        consume(TokenType::LBRACKET, "Expected '[' after Atomic");
        std::string elemType = parseType();
        consume(TokenType::RBRACKET, "Expected ']' after Atomic type");
        consume(TokenType::LPAREN, "Expected '(' after Atomic[T]");
        auto initValue = expression();
        consume(TokenType::RPAREN, "Expected ')' after Atomic initial value");
        return std::make_unique<MakeAtomicExpr>(elemType, std::move(initValue), loc);
    }
    
    // Box creation: Box(value) or Box[T](value)
    if (match(TokenType::BOX)) {
        std::string elemType;
        if (match(TokenType::LBRACKET)) {
            elemType = parseType();
            consume(TokenType::RBRACKET, "Expected ']' after Box type");
        }
        consume(TokenType::LPAREN, "Expected '(' after Box");
        auto initValue = expression();
        consume(TokenType::RPAREN, "Expected ')' after Box value");
        return std::make_unique<MakeBoxExpr>(elemType, std::move(initValue), loc);
    }
    
    // Rc creation: Rc(value) or Rc[T](value)
    if (match(TokenType::RC)) {
        std::string elemType;
        if (match(TokenType::LBRACKET)) {
            elemType = parseType();
            consume(TokenType::RBRACKET, "Expected ']' after Rc type");
        }
        consume(TokenType::LPAREN, "Expected '(' after Rc");
        auto initValue = expression();
        consume(TokenType::RPAREN, "Expected ')' after Rc value");
        return std::make_unique<MakeRcExpr>(elemType, std::move(initValue), loc);
    }
    
    // Arc creation: Arc(value) or Arc[T](value)
    if (match(TokenType::ARC)) {
        std::string elemType;
        if (match(TokenType::LBRACKET)) {
            elemType = parseType();
            consume(TokenType::RBRACKET, "Expected ']' after Arc type");
        }
        consume(TokenType::LPAREN, "Expected '(' after Arc");
        auto initValue = expression();
        consume(TokenType::RPAREN, "Expected ')' after Arc value");
        return std::make_unique<MakeArcExpr>(elemType, std::move(initValue), loc);
    }
    
    // Cell creation: Cell(value) or Cell[T](value)
    if (match(TokenType::CELL)) {
        std::string elemType;
        if (match(TokenType::LBRACKET)) {
            elemType = parseType();
            consume(TokenType::RBRACKET, "Expected ']' after Cell type");
        }
        consume(TokenType::LPAREN, "Expected '(' after Cell");
        auto initValue = expression();
        consume(TokenType::RPAREN, "Expected ')' after Cell value");
        return std::make_unique<MakeCellExpr>(elemType, std::move(initValue), loc);
    }
    
    // RefCell creation: RefCell(value) or RefCell[T](value)
    if (match(TokenType::REFCELL)) {
        std::string elemType;
        if (match(TokenType::LBRACKET)) {
            elemType = parseType();
            consume(TokenType::RBRACKET, "Expected ']' after RefCell type");
        }
        consume(TokenType::LPAREN, "Expected '(' after RefCell");
        auto initValue = expression();
        consume(TokenType::RPAREN, "Expected ')' after RefCell value");
        return std::make_unique<MakeRefCellExpr>(elemType, std::move(initValue), loc);
    }
    
    // Integer literal (may have type suffix like i32, u64, etc.)
    if (match(TokenType::INTEGER)) {
        Token tok = previous();
        int64_t value = std::get<int64_t>(tok.literal);
        // Extract type suffix from lexeme (e.g., "42i32" -> suffix is "i32")
        std::string suffix;
        const std::string& lexeme = tok.lexeme;
        size_t i = 0;
        // Skip digits and optional leading minus
        while (i < lexeme.size() && (std::isdigit(lexeme[i]) || lexeme[i] == '-')) i++;
        if (i < lexeme.size()) suffix = lexeme.substr(i);
        return std::make_unique<IntegerLiteral>(value, loc, suffix);
    }
    
    // Float literal (may have type suffix like f32, f64, etc.)
    if (match(TokenType::FLOAT)) {
        Token tok = previous();
        double value = std::get<double>(tok.literal);
        // Extract type suffix from lexeme (e.g., "3.14f32" -> suffix is "f32")
        std::string suffix;
        const std::string& lexeme = tok.lexeme;
        size_t i = 0;
        // Skip digits, decimal point, and exponent notation
        while (i < lexeme.size() && (std::isdigit(lexeme[i]) || lexeme[i] == '.' || 
               lexeme[i] == 'e' || lexeme[i] == 'E' || lexeme[i] == '+' || lexeme[i] == '-')) i++;
        if (i < lexeme.size()) suffix = lexeme.substr(i);
        return std::make_unique<FloatLiteral>(value, loc, suffix);
    }
    
    // Placeholder _ for lambda shorthand (e.g., _ * 2)
    if (match(TokenType::UNDERSCORE)) {
        return std::make_unique<PlaceholderExpr>(loc);
    }
    
    // String literal (may be interpolated)
    if (match(TokenType::STRING)) {
        std::string value = std::get<std::string>(previous().literal);
        
        // Check for interpolation markers
        if (value.find('\x01') != std::string::npos) {
            auto interp = std::make_unique<InterpolatedString>(loc);
            
            std::string currentPart;
            size_t i = 0;
            while (i < value.length()) {
                if (value[i] == '\x01') {
                    if (!currentPart.empty()) {
                        interp->parts.push_back(currentPart);
                        currentPart.clear();
                    }
                    i++;
                    std::string exprStr;
                    while (i < value.length() && value[i] != '\x02') {
                        exprStr += value[i++];
                    }
                    if (i < value.length()) i++;
                    
                    // Parse the expression string properly instead of just creating an identifier
                    // This handles function calls, binary expressions, etc.
                    Lexer exprLexer(exprStr, "<interpolation>");
                    auto exprTokens = exprLexer.tokenize();
                    Parser exprParser(exprTokens);
                    auto parsedExpr = exprParser.expression();
                    interp->parts.push_back(std::move(parsedExpr));
                } else {
                    currentPart += value[i++];
                }
            }
            if (!currentPart.empty()) {
                interp->parts.push_back(currentPart);
            }
            return interp;
        }
        
        return std::make_unique<StringLiteral>(value, loc);
    }
    
    // Character literal: 'A', '\n', '\u{1F600}'
    if (match(TokenType::CHAR)) {
        uint32_t value = static_cast<uint32_t>(std::get<int64_t>(previous().literal));
        return std::make_unique<CharLiteral>(value, loc);
    }
    
    // Byte string literal: b"hello"
    if (match(TokenType::BYTE_STRING)) {
        std::string strValue = std::get<std::string>(previous().literal);
        std::vector<uint8_t> bytes(strValue.begin(), strValue.end());
        return std::make_unique<ByteStringLiteral>(bytes, false, loc);
    }
    
    // Raw byte string literal: br"\x00\xFF"
    if (match(TokenType::RAW_BYTE_STRING)) {
        std::string strValue = std::get<std::string>(previous().literal);
        std::vector<uint8_t> bytes(strValue.begin(), strValue.end());
        return std::make_unique<ByteStringLiteral>(bytes, true, loc);
    }
    
    // Boolean literals
    if (match(TokenType::TRUE)) {
        return std::make_unique<BoolLiteral>(true, loc);
    }
    if (match(TokenType::FALSE)) {
        return std::make_unique<BoolLiteral>(false, loc);
    }
    
    // Nil literal
    if (match(TokenType::NIL)) {
        return std::make_unique<NilLiteral>(loc);
    }
    
    // Handle 'self' keyword as an identifier in method contexts
    if (match(TokenType::SELF)) {
        return std::make_unique<Identifier>("self", previous().location);
    }
    
    // Identifier (may be DSL block or builtin function)
    if (match(TokenType::IDENTIFIER)) {
        std::string name = previous().lexeme;
        auto idLoc = previous().location;
        
        // Advanced Concurrency - Future/Promise
        if (name == "make_future") {
            consume(TokenType::LBRACKET, "Expected '[' after make_future");
            std::string elemType = parseType();
            consume(TokenType::RBRACKET, "Expected ']' after type");
            consume(TokenType::LPAREN, "Expected '(' after make_future[T]");
            consume(TokenType::RPAREN, "Expected ')' after make_future[T](");
            return std::make_unique<MakeFutureExpr>(elemType, idLoc);
        }
        
        if (name == "future_get") {
            consume(TokenType::LPAREN, "Expected '(' after future_get");
            auto futureExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after future_get argument");
            return std::make_unique<FutureGetExpr>(std::move(futureExpr), idLoc);
        }
        
        if (name == "future_set") {
            consume(TokenType::LPAREN, "Expected '(' after future_set");
            auto futureExpr = expression();
            consume(TokenType::COMMA, "Expected ',' after future argument");
            auto valueExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after future_set arguments");
            return std::make_unique<FutureSetExpr>(std::move(futureExpr), std::move(valueExpr), idLoc);
        }
        
        if (name == "future_is_ready") {
            consume(TokenType::LPAREN, "Expected '(' after future_is_ready");
            auto futureExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after future_is_ready argument");
            return std::make_unique<FutureIsReadyExpr>(std::move(futureExpr), idLoc);
        }
        
        // Advanced Concurrency - Thread Pool
        if (name == "make_thread_pool") {
            consume(TokenType::LPAREN, "Expected '(' after make_thread_pool");
            auto numWorkers = expression();
            consume(TokenType::RPAREN, "Expected ')' after make_thread_pool argument");
            return std::make_unique<MakeThreadPoolExpr>(std::move(numWorkers), idLoc);
        }
        
        if (name == "thread_pool_submit") {
            consume(TokenType::LPAREN, "Expected '(' after thread_pool_submit");
            auto poolExpr = expression();
            consume(TokenType::COMMA, "Expected ',' after pool argument");
            auto taskExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after thread_pool_submit arguments");
            return std::make_unique<ThreadPoolSubmitExpr>(std::move(poolExpr), std::move(taskExpr), idLoc);
        }
        
        if (name == "thread_pool_shutdown") {
            consume(TokenType::LPAREN, "Expected '(' after thread_pool_shutdown");
            auto poolExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after thread_pool_shutdown argument");
            return std::make_unique<ThreadPoolShutdownExpr>(std::move(poolExpr), idLoc);
        }
        
        // Advanced Concurrency - Cancellation Token
        if (name == "make_cancel_token") {
            consume(TokenType::LPAREN, "Expected '(' after make_cancel_token");
            consume(TokenType::RPAREN, "Expected ')' after make_cancel_token(");
            return std::make_unique<MakeCancelTokenExpr>(idLoc);
        }
        
        if (name == "cancel") {
            consume(TokenType::LPAREN, "Expected '(' after cancel");
            auto tokenExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after cancel argument");
            return std::make_unique<CancelExpr>(std::move(tokenExpr), idLoc);
        }
        
        if (name == "is_cancelled") {
            consume(TokenType::LPAREN, "Expected '(' after is_cancelled");
            auto tokenExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after is_cancelled argument");
            return std::make_unique<IsCancelledExpr>(std::move(tokenExpr), idLoc);
        }
        
        // Async Runtime - Event Loop and Task Management
        if (name == "async_init") {
            consume(TokenType::LPAREN, "Expected '(' after async_init");
            auto numWorkers = expression();
            consume(TokenType::RPAREN, "Expected ')' after async_init argument");
            return std::make_unique<AsyncRuntimeInitExpr>(std::move(numWorkers), idLoc);
        }
        
        if (name == "async_run") {
            consume(TokenType::LPAREN, "Expected '(' after async_run");
            consume(TokenType::RPAREN, "Expected ')' after async_run(");
            return std::make_unique<AsyncRuntimeRunExpr>(idLoc);
        }
        
        if (name == "async_shutdown") {
            consume(TokenType::LPAREN, "Expected '(' after async_shutdown");
            consume(TokenType::RPAREN, "Expected ')' after async_shutdown(");
            return std::make_unique<AsyncRuntimeShutdownExpr>(idLoc);
        }
        
        if (name == "async_spawn") {
            consume(TokenType::LPAREN, "Expected '(' after async_spawn");
            auto taskExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after async_spawn argument");
            return std::make_unique<AsyncSpawnExpr>(std::move(taskExpr), idLoc);
        }
        
        if (name == "async_sleep") {
            consume(TokenType::LPAREN, "Expected '(' after async_sleep");
            auto durationExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after async_sleep argument");
            return std::make_unique<AsyncSleepExpr>(std::move(durationExpr), idLoc);
        }
        
        if (name == "async_yield") {
            consume(TokenType::LPAREN, "Expected '(' after async_yield");
            consume(TokenType::RPAREN, "Expected ')' after async_yield(");
            return std::make_unique<AsyncYieldExpr>(idLoc);
        }
        
        // Advanced Concurrency - Channel Timeout
        if (name == "chan_recv_timeout") {
            consume(TokenType::LPAREN, "Expected '(' after chan_recv_timeout");
            auto channelExpr = expression();
            consume(TokenType::COMMA, "Expected ',' after channel argument");
            auto timeoutExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after chan_recv_timeout arguments");
            return std::make_unique<ChanRecvTimeoutExpr>(std::move(channelExpr), std::move(timeoutExpr), idLoc);
        }
        
        if (name == "chan_send_timeout") {
            consume(TokenType::LPAREN, "Expected '(' after chan_send_timeout");
            auto channelExpr = expression();
            consume(TokenType::COMMA, "Expected ',' after channel argument");
            auto valueExpr = expression();
            consume(TokenType::COMMA, "Expected ',' after value argument");
            auto timeoutExpr = expression();
            consume(TokenType::RPAREN, "Expected ')' after chan_send_timeout arguments");
            return std::make_unique<ChanSendTimeoutExpr>(std::move(channelExpr), std::move(valueExpr), std::move(timeoutExpr), idLoc);
        }
        
        // Check for builtin functions that can be called without parentheses in expressions
        // These are functions like str, len, int, float, bool, type, etc.
        static const std::unordered_set<std::string> exprBuiltins = {
            "str", "len", "int", "float", "bool", "type", "abs", "not"
        };
        
        if (exprBuiltins.count(name) && !check(TokenType::LPAREN) && !isAtStatementBoundary() &&
            !check(TokenType::ASSIGN) && !check(TokenType::COLON) && !check(TokenType::NEWLINE) &&
            !check(TokenType::COMMA) && !check(TokenType::RPAREN) && !check(TokenType::RBRACKET) &&
            !check(TokenType::PLUS) && !check(TokenType::MINUS) && !check(TokenType::STAR) &&
            !check(TokenType::SLASH) && !check(TokenType::PERCENT)) {
            // Parse as function call with single argument
            auto callee = std::make_unique<Identifier>(name, idLoc);
            auto call = std::make_unique<CallExpr>(std::move(callee), idLoc);
            call->args.push_back(parsePrecedence(Precedence::UNARY));
            return call;
        }
        
        // Check for DSL block: name:\n INDENT content DEDENT
        if (check(TokenType::COLON)) {
            size_t saved = current;
            advance();
            
            if (check(TokenType::NEWLINE)) {
                advance();
                skipNewlines();
                
                if (check(TokenType::INDENT)) {
                    bool isDSL = SyntaxMacroRegistry::instance().isDSLName(name) ||
                                 name == "sql" || name == "html" || name == "json" || 
                                 name == "regex" || name == "asm" || name == "css" ||
                                 name == "xml" || name == "yaml" || name == "toml" ||
                                 name == "graphql" || name == "markdown" || name == "query";
                    
                    if (isDSL) {
                        return parseDSLBlock(name, idLoc);
                    }
                }
            }
            current = saved;
        }
        
        return std::make_unique<Identifier>(name, idLoc);
    }
    
    // List literal or comprehension
    if (match(TokenType::LBRACKET)) {
        return listLiteral();
    }
    
    // Record literal
    if (match(TokenType::LBRACE)) {
        return recordLiteral();
    }
    
    // Grouped expression, tuple, or arrow lambda parameters
    if (match(TokenType::LPAREN)) {
        // Check for empty parens: () => expr
        if (check(TokenType::RPAREN)) {
            advance(); // consume )
            if (check(TokenType::DOUBLE_ARROW)) {
                advance(); // consume =>
                auto lam = std::make_unique<LambdaExpr>(loc);
                lam->body = expression();
                return lam;
            }
            // Empty tuple or unit - return nil for now
            return std::make_unique<NilLiteral>(loc);
        }
        
        // Check if this looks like lambda parameters: (a, b) => or (a) =>
        // We need to look ahead to see if => follows the )
        size_t savedPos = current;
        std::vector<std::string> potentialParams;
        bool couldBeLambdaParams = true;
        
        // Try to parse as identifier list
        if (check(TokenType::IDENTIFIER)) {
            potentialParams.push_back(advance().lexeme);
            
            while (match(TokenType::COMMA)) {
                if (check(TokenType::IDENTIFIER)) {
                    potentialParams.push_back(advance().lexeme);
                } else {
                    couldBeLambdaParams = false;
                    break;
                }
            }
            
            if (couldBeLambdaParams && check(TokenType::RPAREN)) {
                advance(); // consume )
                if (check(TokenType::DOUBLE_ARROW)) {
                    advance(); // consume =>
                    auto lam = std::make_unique<LambdaExpr>(loc);
                    for (const auto& param : potentialParams) {
                        lam->params.emplace_back(param, "");
                    }
                    lam->body = expression();
                    return lam;
                }
            }
        }
        
        // Not a lambda - restore position and parse as expression
        current = savedPos;
        
        auto expr = expression();
        
        // Tuple: (a, b, c)
        if (match(TokenType::COMMA)) {
            auto list = std::make_unique<ListExpr>(loc);
            list->elements.push_back(std::move(expr));
            do {
                list->elements.push_back(expression());
            } while (match(TokenType::COMMA));
            consume(TokenType::RPAREN, "Expected ')' after tuple elements");
            return list;
        }
        
        consume(TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }
    
    // Lambda: |params| => body
    if (match(TokenType::PIPE)) {
        return lambda();
    }
    
    // Try expression: try expr else default
    if (match(TokenType::TRY)) {
        auto tryExpr = expression();
        consume(TokenType::ELSE, "Expected 'else' after try expression");
        auto elseExpr = expression();
        return std::make_unique<TernaryExpr>(std::move(tryExpr), std::move(tryExpr), 
                                              std::move(elseExpr), loc);
    }
    
    auto diag = errors::expectedExpression(tokenTypeToString(peek().type), loc);
    throw TylDiagnosticError(diag);
}

// List literal or list comprehension
ExprPtr Parser::listLiteral() {
    auto loc = previous().location;
    auto list = std::make_unique<ListExpr>(loc);
    
    skipNewlines();
    if (!check(TokenType::RBRACKET)) {
        auto first = expression();
        
        // List comprehension: [expr for var in iterable if condition]
        if (match(TokenType::FOR)) {
            auto var = consume(TokenType::IDENTIFIER, "Expected variable in comprehension").lexeme;
            consume(TokenType::IN, "Expected 'in' in comprehension");
            auto iterable = expression();
            ExprPtr condition = nullptr;
            if (match(TokenType::IF)) {
                condition = expression();
            }
            skipNewlines();
            consume(TokenType::RBRACKET, "Expected ']' after list comprehension");
            return std::make_unique<ListCompExpr>(std::move(first), var, std::move(iterable), 
                                                   std::move(condition), loc);
        }
        
        list->elements.push_back(std::move(first));
        
        while (match(TokenType::COMMA)) {
            skipNewlines();
            if (check(TokenType::RBRACKET)) break;
            list->elements.push_back(expression());
        }
    }
    
    skipNewlines();
    consume(TokenType::RBRACKET, "Expected ']' after list");
    return list;
}

// Record literal: {field: value, ...} or Map literal: {"key": value, ...}
ExprPtr Parser::recordLiteral() {
    auto loc = previous().location;
    
    skipNewlines();
    
    // Empty braces - return empty record
    if (check(TokenType::RBRACE)) {
        advance();
        return std::make_unique<RecordExpr>(loc);
    }
    
    // Peek at first token to determine if this is a map (string key) or record (identifier key)
    if (check(TokenType::STRING)) {
        // This is a map literal: {"key": value, ...}
        auto map = std::make_unique<MapExpr>(loc);
        do {
            skipNewlines();
            if (check(TokenType::RBRACE)) break;
            
            // Parse key (must be a string for maps)
            auto keyToken = consume(TokenType::STRING, "Expected string key in map");
            auto key = std::make_unique<StringLiteral>(std::get<std::string>(keyToken.literal), keyToken.location);
            
            consume(TokenType::COLON, "Expected ':' after map key");
            auto value = expression();
            map->entries.emplace_back(std::move(key), std::move(value));
        } while (match(TokenType::COMMA));
        
        skipNewlines();
        consume(TokenType::RBRACE, "Expected '}' after map");
        return map;
    }
    
    // This is a record literal: {field: value, ...}
    auto rec = std::make_unique<RecordExpr>(loc);
    do {
        skipNewlines();
        if (check(TokenType::RBRACE)) break;
        
        auto name = consume(TokenType::IDENTIFIER, "Expected field name").lexeme;
        consume(TokenType::COLON, "Expected ':' after field name");
        auto value = expression();
        rec->fields.emplace_back(name, std::move(value));
    } while (match(TokenType::COMMA));
    
    skipNewlines();
    consume(TokenType::RBRACE, "Expected '}' after record");
    return rec;
}

// Lambda expression: |params| => body
ExprPtr Parser::lambda() {
    auto loc = previous().location;
    auto lam = std::make_unique<LambdaExpr>(loc);
    
    // Parse parameters
    if (!check(TokenType::PIPE)) {
        do {
            auto name = consume(TokenType::IDENTIFIER, "Expected parameter name").lexeme;
            std::string type;
            if (match(TokenType::COLON)) {
                type = parseType();
            }
            lam->params.emplace_back(name, type);
        } while (match(TokenType::COMMA));
    }
    
    consume(TokenType::PIPE, "Expected '|' after lambda parameters");
    match(TokenType::DOUBLE_ARROW); // Optional =>
    
    lam->body = expression();
    return lam;
}

// Arrow lambda syntax: x => expr, (x, y) => expr, () => expr
ExprPtr Parser::parseArrowLambda(const std::string& firstParam, SourceLocation loc) {
    auto lam = std::make_unique<LambdaExpr>(loc);
    lam->params.emplace_back(firstParam, "");
    lam->body = expression();
    return lam;
}

// Legacy compatibility wrappers (redirect to Pratt parser)
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
