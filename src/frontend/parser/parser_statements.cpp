// Tyl Compiler - Parser Statement Implementations
// Handles: if, while, for, match, return, break, continue, delete, block, expression statements

#include "parser_base.h"
#include <unordered_set>

namespace tyl {

StmtPtr Parser::statement() {
    if (match(TokenType::IF)) {
        // Check for 'if let' pattern
        if (check(TokenType::LET)) {
            return ifLetStatement();
        }
        return ifStatement();
    }
    if (match(TokenType::UNLESS)) return unlessStatement();
    if (match(TokenType::WHILE)) return whileStatement("");
    if (match(TokenType::FOR)) return forStatement("");
    if (match(TokenType::LOOP)) return loopStatement("");
    if (match(TokenType::MATCH)) return matchStatement();
    if (match(TokenType::RETURN)) return returnStatement();
    if (match(TokenType::BREAK)) return breakStatement();
    if (match(TokenType::CONTINUE)) return continueStatement();
    if (match(TokenType::DELETE)) return deleteStatement();
    if (match(TokenType::LOCK)) return lockStatement();
    if (match(TokenType::WITH)) return withStatement();
    if (match(TokenType::SCOPE)) return scopeStatement();
    if (match(TokenType::REQUIRE)) return requireStatement();
    if (match(TokenType::ENSURE)) return ensureStatement();
    if (match(TokenType::COMPTIME)) return comptimeBlock();
    if (match(TokenType::EFFECT)) return effectDeclaration();
    
    // Check for labeled loop: label: for/while/loop
    if (check(TokenType::IDENTIFIER)) {
        // Look ahead for colon followed by loop keyword
        size_t savedPos = current;
        std::string potentialLabel = advance().lexeme;
        if (match(TokenType::COLON)) {
            if (match(TokenType::FOR)) return forStatement(potentialLabel);
            if (match(TokenType::WHILE)) return whileStatement(potentialLabel);
            if (match(TokenType::LOOP)) return loopStatement(potentialLabel);
        }
        // Not a labeled loop, restore position
        current = savedPos;
    }
    
    // Check for tuple destructuring: (a, b, c) = expr
    if (check(TokenType::LPAREN)) {
        size_t savedPos = current;
        advance();  // consume (
        
        // Check if this looks like a destructuring pattern
        std::vector<std::string> names;
        bool isDestructuring = true;
        
        if (!check(TokenType::RPAREN)) {
            do {
                if (check(TokenType::IDENTIFIER)) {
                    names.push_back(advance().lexeme);
                } else {
                    isDestructuring = false;
                    break;
                }
            } while (match(TokenType::COMMA));
        }
        
        if (isDestructuring && match(TokenType::RPAREN) && match(TokenType::ASSIGN)) {
            auto loc = tokens[savedPos].location;
            auto init = expression();
            match(TokenType::NEWLINE);
            auto decl = std::make_unique<DestructuringDecl>(
                DestructuringDecl::Kind::TUPLE, std::move(names), std::move(init), loc);
            decl->isMutable = true;  // Default to mutable for bare destructuring
            return decl;
        }
        
        // Not destructuring, restore position
        current = savedPos;
    }
    
    // Check for record destructuring: {x, y} = expr
    if (check(TokenType::LBRACE)) {
        size_t savedPos = current;
        advance();  // consume {
        
        std::vector<std::string> names;
        bool isDestructuring = true;
        
        if (!check(TokenType::RBRACE)) {
            do {
                if (check(TokenType::IDENTIFIER)) {
                    names.push_back(advance().lexeme);
                    // Check for : which would indicate a record literal, not destructuring
                    if (check(TokenType::COLON)) {
                        isDestructuring = false;
                        break;
                    }
                } else {
                    isDestructuring = false;
                    break;
                }
            } while (match(TokenType::COMMA));
        }
        
        if (isDestructuring && match(TokenType::RBRACE) && match(TokenType::ASSIGN)) {
            auto loc = tokens[savedPos].location;
            auto init = expression();
            match(TokenType::NEWLINE);
            auto decl = std::make_unique<DestructuringDecl>(
                DestructuringDecl::Kind::RECORD, std::move(names), std::move(init), loc);
            decl->isMutable = true;
            return decl;
        }
        
        // Not destructuring, restore position
        current = savedPos;
    }
    
    return expressionStatement();
}

StmtPtr Parser::expressionStatement() {
    auto loc = peek().location;
    
    // Check for chained mutable multi-assign: mut x = mut y = mut z = 0
    if (check(TokenType::MUT)) {
        size_t savedPos = current;
        std::vector<std::string> names;
        
        // Try to parse: mut x = mut y = mut z = value
        while (match(TokenType::MUT)) {
            if (!check(TokenType::IDENTIFIER)) {
                // Not a valid pattern, restore
                current = savedPos;
                break;
            }
            std::string varName = advance().lexeme;
            names.push_back(varName);
            
            if (!match(TokenType::ASSIGN)) {
                // Not a valid pattern, restore
                current = savedPos;
                names.clear();
                break;
            }
            
            // Check if next is another 'mut' (chain continues) or value
            if (!check(TokenType::MUT)) {
                // This is the value expression
                auto init = expression();
                match(TokenType::NEWLINE);
                
                if (names.size() == 1) {
                    auto decl = std::make_unique<VarDecl>(names[0], "", std::move(init), loc);
                    decl->isMutable = true;
                    return decl;
                } else {
                    auto decl = std::make_unique<MultiVarDecl>(std::move(names), std::move(init), loc);
                    decl->isMutable = true;
                    return decl;
                }
            }
        }
        
        // If we get here with names, something went wrong - restore
        if (!names.empty()) {
            current = savedPos;
        }
    }
    
    auto expr = expression();
    
    if (auto* id = dynamic_cast<Identifier*>(expr.get())) {
        static const std::unordered_set<std::string> builtins = {
            "print", "println", "input", "exit",
            "gc_threshold", "gc_collect", "gc_enable", "gc_disable"
        };
        
        // Compile-time constant: NAME :: value
        // Also handles multi-constant: A :: B :: C :: value
        if (match(TokenType::DOUBLE_COLON)) {
            std::vector<std::string> names;
            names.push_back(id->name);
            
            // Check for chained constants: A :: B :: C :: value
            while (check(TokenType::IDENTIFIER)) {
                size_t savedPos = current;
                std::string nextName = advance().lexeme;
                if (match(TokenType::DOUBLE_COLON)) {
                    names.push_back(nextName);
                } else {
                    // Not a chain, restore and parse as value
                    current = savedPos;
                    break;
                }
            }
            
            auto init = expression();
            match(TokenType::NEWLINE);
            
            if (names.size() == 1) {
                auto decl = std::make_unique<VarDecl>(names[0], "", std::move(init), loc);
                decl->isMutable = false;
                decl->isConst = true;
                return decl;
            } else {
                auto decl = std::make_unique<MultiVarDecl>(std::move(names), std::move(init), loc);
                decl->isMutable = false;
                decl->isConst = true;
                return decl;
            }
        }
        
        // Built-in function call without parentheses
        if (builtins.count(id->name) && !isAtStatementBoundary() && 
            !check(TokenType::ASSIGN) && !check(TokenType::COLON)) {
            auto call = std::make_unique<CallExpr>(std::move(expr), loc);
            call->args.push_back(expression());
            match(TokenType::NEWLINE);
            return std::make_unique<ExprStmt>(std::move(call), loc);
        }
        
        // Check for multi-assignment: a = b = c = value
        // This needs to be checked BEFORE the regular assignment handling
        if (check(TokenType::ASSIGN)) {
            size_t savedPos = current;
            std::vector<std::string> names;
            names.push_back(id->name);
            
            // Try to parse chain: a = b = c = value
            while (match(TokenType::ASSIGN)) {
                if (check(TokenType::IDENTIFIER)) {
                    size_t peekPos = current;
                    std::string nextName = advance().lexeme;
                    if (check(TokenType::ASSIGN)) {
                        // This is part of the chain
                        names.push_back(nextName);
                        continue;
                    } else {
                        // This identifier is the start of the value expression
                        current = peekPos;
                        break;
                    }
                } else {
                    // Not an identifier, this is the value expression
                    break;
                }
            }
            
            if (names.size() > 1) {
                // Multi-assignment
                auto init = expression();
                match(TokenType::NEWLINE);
                auto decl = std::make_unique<MultiVarDecl>(std::move(names), std::move(init), loc);
                decl->isMutable = true;
                return decl;
            }
            
            // Not multi-assign, restore and continue to regular handling
            current = savedPos;
        }
        
        // name value  OR  name: type = value  OR  name := value
        if (!isAtStatementBoundary() && !check(TokenType::ASSIGN) && 
            !check(TokenType::PLUS_ASSIGN) && !check(TokenType::MINUS_ASSIGN)) {
            std::string name = id->name;
            std::string typeName;
            
            if (match(TokenType::COLON)) {
                // Check for := syntax (shorthand variable declaration)
                if (match(TokenType::ASSIGN)) {
                    auto init = expression();
                    match(TokenType::NEWLINE);
                    auto decl = std::make_unique<VarDecl>(name, "", std::move(init), loc);
                    decl->isMutable = true;  // := creates mutable variable
                    return decl;
                }
                // Otherwise it's name: type = value
                typeName = parseType();
                if (match(TokenType::ASSIGN)) {
                    auto init = expression();
                    match(TokenType::NEWLINE);
                    return std::make_unique<VarDecl>(name, typeName, std::move(init), loc);
                }
            } else {
                // Simple variable declaration: name value
                auto init = expression();
                match(TokenType::NEWLINE);
                return std::make_unique<VarDecl>(name, "", std::move(init), loc);
            }
        }
    }
    
    match(TokenType::NEWLINE);
    return std::make_unique<ExprStmt>(std::move(expr), loc);
}

StmtPtr Parser::block() {
    auto blk = std::make_unique<Block>(peek().location);
    
    consume(TokenType::INDENT, "Expected indented block");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        blk->statements.push_back(declaration());
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of block");
    return blk;
}

StmtPtr Parser::braceBlock() {
    // Brace-delimited block: { stmt; stmt; ... }
    // Already consumed the opening {
    auto blk = std::make_unique<Block>(previous().location);
    
    skipNewlines();
    // Skip any INDENT tokens that the lexer might have generated
    while (match(TokenType::INDENT)) {
        skipNewlines();
    }
    
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        // Skip DEDENT tokens inside brace blocks
        while (match(TokenType::DEDENT)) {
            skipNewlines();
        }
        if (check(TokenType::RBRACE)) break;
        
        blk->statements.push_back(declaration());
        skipNewlines();
        // Allow optional semicolons between statements
        match(TokenType::SEMICOLON);
        skipNewlines();
        
        // Skip any trailing DEDENT tokens
        while (match(TokenType::DEDENT)) {
            skipNewlines();
        }
    }
    
    // Skip any remaining DEDENT tokens before the closing brace
    while (match(TokenType::DEDENT)) {
        skipNewlines();
    }
    
    consume(TokenType::RBRACE, "Expected '}' after block");
    match(TokenType::NEWLINE);
    return blk;
}

StmtPtr Parser::endBlock() {
    // Lua-style block terminated by 'end'
    // Already consumed 'then' or 'do'
    auto blk = std::make_unique<Block>(previous().location);
    
    skipNewlines();
    // Skip any INDENT tokens that the lexer might have generated
    while (match(TokenType::INDENT)) {
        skipNewlines();
    }
    
    while (!check(TokenType::END) && !check(TokenType::ELSE) && !check(TokenType::ELIF) && !isAtEnd()) {
        // Skip DEDENT tokens inside end blocks
        while (match(TokenType::DEDENT)) {
            skipNewlines();
        }
        if (check(TokenType::END) || check(TokenType::ELSE) || check(TokenType::ELIF)) break;
        
        blk->statements.push_back(declaration());
        skipNewlines();
        // Allow optional semicolons between statements
        match(TokenType::SEMICOLON);
        skipNewlines();
        
        // Skip any trailing DEDENT tokens
        while (match(TokenType::DEDENT)) {
            skipNewlines();
        }
    }
    
    // Skip any remaining DEDENT tokens
    while (match(TokenType::DEDENT)) {
        skipNewlines();
    }
    
    return blk;
}

StmtPtr Parser::ifLetStatement() {
    // if let var = expr: body
    // if let var = expr and condition: body
    auto loc = previous().location;
    
    consume(TokenType::LET, "Expected 'let' after 'if'");
    auto varName = consume(TokenType::IDENTIFIER, "Expected variable name after 'if let'").lexeme;
    consume(TokenType::ASSIGN, "Expected '=' after variable name in 'if let'");
    auto value = expression();
    
    // Optional guard: if let x = expr and x > 0:
    ExprPtr guard = nullptr;
    if (match(TokenType::AND)) {
        guard = expression();
    }
    
    StmtPtr thenBranch;
    if (match(TokenType::LBRACE)) {
        thenBranch = braceBlock();
    } else if (match(TokenType::THEN)) {
        thenBranch = endBlock();
        consume(TokenType::END, "Expected 'end' after 'if let' block");
        match(TokenType::NEWLINE);
    } else {
        consume(TokenType::COLON, "Expected ':', '{', or 'then' after 'if let' condition");
        if (match(TokenType::NEWLINE)) {
            thenBranch = block();
        } else {
            auto blk = std::make_unique<Block>(loc);
            blk->statements.push_back(statement());
            thenBranch = std::move(blk);
        }
    }
    
    auto stmt = std::make_unique<IfLetStmt>(varName, std::move(value), std::move(thenBranch), loc);
    stmt->guard = std::move(guard);
    
    // Handle else branch
    skipNewlines();
    if (match(TokenType::ELSE)) {
        if (match(TokenType::LBRACE)) {
            stmt->elseBranch = braceBlock();
        } else if (check(TokenType::END)) {
            // else followed by end - empty else
            auto blk = std::make_unique<Block>(loc);
            stmt->elseBranch = std::move(blk);
        } else {
            consume(TokenType::COLON, "Expected ':' or '{' after else");
            if (match(TokenType::NEWLINE)) {
                stmt->elseBranch = block();
            } else {
                auto blk = std::make_unique<Block>(loc);
                blk->statements.push_back(statement());
                stmt->elseBranch = std::move(blk);
            }
        }
    }
    
    return stmt;
}

StmtPtr Parser::ifStatement() {
    auto loc = previous().location;
    auto condition = expression();
    
    StmtPtr thenBranch;
    bool usesEndStyle = false;
    
    if (match(TokenType::LBRACE)) {
        // Brace style: if condition { ... }
        thenBranch = braceBlock();
    } else if (match(TokenType::THEN)) {
        // Lua style: if condition then ... end
        usesEndStyle = true;
        thenBranch = endBlock();
    } else {
        consume(TokenType::COLON, "Expected ':', '{', or 'then' after if condition");
        if (match(TokenType::NEWLINE)) {
            // Multi-line: if condition:\n    body
            thenBranch = block();
        } else {
            // Single-line: if condition: statement
            auto blk = std::make_unique<Block>(loc);
            blk->statements.push_back(statement());
            thenBranch = std::move(blk);
        }
    }
    
    auto stmt = std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), loc);
    
    skipNewlines();
    while (match(TokenType::ELIF)) {
        auto elifCond = expression();
        StmtPtr elifBody;
        if (match(TokenType::LBRACE)) {
            elifBody = braceBlock();
        } else if (match(TokenType::THEN)) {
            usesEndStyle = true;
            elifBody = endBlock();
        } else {
            consume(TokenType::COLON, "Expected ':', '{', or 'then' after elif condition");
            if (match(TokenType::NEWLINE)) {
                elifBody = block();
            } else {
                auto blk = std::make_unique<Block>(loc);
                blk->statements.push_back(statement());
                elifBody = std::move(blk);
            }
        }
        stmt->elifBranches.emplace_back(std::move(elifCond), std::move(elifBody));
        skipNewlines();
    }
    
    if (match(TokenType::ELSE)) {
        if (match(TokenType::LBRACE)) {
            stmt->elseBranch = braceBlock();
        } else if (usesEndStyle) {
            // In Lua style, else is followed directly by statements until end
            stmt->elseBranch = endBlock();
        } else {
            consume(TokenType::COLON, "Expected ':' or '{' after else");
            if (match(TokenType::NEWLINE)) {
                stmt->elseBranch = block();
            } else {
                auto blk = std::make_unique<Block>(loc);
                blk->statements.push_back(statement());
                stmt->elseBranch = std::move(blk);
            }
        }
    }
    
    // Consume 'end' for Lua-style blocks
    if (usesEndStyle) {
        skipNewlines();
        // Skip any remaining DEDENT tokens
        while (match(TokenType::DEDENT)) {
            skipNewlines();
        }
        consume(TokenType::END, "Expected 'end' to close if statement");
        match(TokenType::NEWLINE);
    }
    
    return stmt;
}

StmtPtr Parser::whileStatement(const std::string& label) {
    auto loc = previous().location;
    auto condition = expression();
    
    StmtPtr body;
    bool usesEndStyle = false;
    
    if (match(TokenType::LBRACE)) {
        body = braceBlock();
    } else if (match(TokenType::DO)) {
        // Lua style: while condition do ... end
        usesEndStyle = true;
        body = endBlock();
        // Skip any remaining DEDENT tokens
        while (match(TokenType::DEDENT)) {
            skipNewlines();
        }
        consume(TokenType::END, "Expected 'end' to close while loop");
        match(TokenType::NEWLINE);
    } else {
        consume(TokenType::COLON, "Expected ':', '{', or 'do' after while condition");
        if (match(TokenType::NEWLINE)) {
            body = block();
        } else {
            // Single-line: while condition: statement
            auto blk = std::make_unique<Block>(loc);
            blk->statements.push_back(statement());
            body = std::move(blk);
        }
    }
    auto stmt = std::make_unique<WhileStmt>(std::move(condition), std::move(body), loc);
    stmt->label = label;
    return stmt;
}

StmtPtr Parser::forStatement(const std::string& label) {
    auto loc = previous().location;
    auto varName = consume(TokenType::IDENTIFIER, "Expected variable name").lexeme;
    consume(TokenType::IN, "Expected 'in' after for variable");
    auto iterable = expression();
    
    StmtPtr body;
    bool usesEndStyle = false;
    
    if (match(TokenType::LBRACE)) {
        body = braceBlock();
    } else if (match(TokenType::DO)) {
        // Lua style: for i in range do ... end
        usesEndStyle = true;
        body = endBlock();
        // Skip any remaining DEDENT tokens
        while (match(TokenType::DEDENT)) {
            skipNewlines();
        }
        consume(TokenType::END, "Expected 'end' to close for loop");
        match(TokenType::NEWLINE);
    } else {
        consume(TokenType::COLON, "Expected ':', '{', or 'do' after for iterable");
        if (match(TokenType::NEWLINE)) {
            body = block();
        } else {
            // Single-line: for i in range: statement
            auto blk = std::make_unique<Block>(loc);
            blk->statements.push_back(statement());
            body = std::move(blk);
        }
    }
    auto stmt = std::make_unique<ForStmt>(varName, std::move(iterable), std::move(body), loc);
    stmt->label = label;
    return stmt;
}

StmtPtr Parser::matchStatement() {
    auto loc = previous().location;
    auto value = expression();
    consume(TokenType::COLON, "Expected ':' after match value");
    match(TokenType::NEWLINE);
    
    auto stmt = std::make_unique<MatchStmt>(std::move(value), loc);
    consume(TokenType::INDENT, "Expected indented match cases");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        // Parse pattern - could be a variable binding, literal, range, or wildcard
        ExprPtr pattern;
        auto patternLoc = peek().location;
        
        // Handle wildcard _ specially - convert to Identifier for match patterns
        if (check(TokenType::UNDERSCORE)) {
            advance();
            pattern = std::make_unique<Identifier>("_", patternLoc);
        } else {
            pattern = primary();
        }
        
        // Check for range pattern: 90..100
        if (check(TokenType::DOTDOT)) {
            advance();  // consume ..
            auto endExpr = primary();
            pattern = std::make_unique<RangeExpr>(std::move(pattern), std::move(endExpr), nullptr, patternLoc);
        }
        
        // Check for guard: pattern if condition
        ExprPtr guard = nullptr;
        if (match(TokenType::IF)) {
            guard = expression();
        }
        
        if (!match(TokenType::ARROW)) {
            consume(TokenType::COLON, "Expected '->' or ':' after match pattern");
        }
        
        if (check(TokenType::NEWLINE)) {
            match(TokenType::NEWLINE);
            auto body = block();
            stmt->cases.emplace_back(std::move(pattern), std::move(guard), std::move(body));
        } else if (check(TokenType::RETURN)) {
            advance();
            auto retStmt = returnStatement();
            stmt->cases.emplace_back(std::move(pattern), std::move(guard), std::move(retStmt));
        } else if (check(TokenType::IDENTIFIER)) {
            static const std::unordered_set<std::string> builtins = {"print", "println", "input", "exit"};
            std::string name = peek().lexeme;
            if (builtins.count(name)) {
                auto loc = peek().location;
                advance();
                auto callee = std::make_unique<Identifier>(name, loc);
                auto call = std::make_unique<CallExpr>(std::move(callee), loc);
                call->args.push_back(expression());
                auto exprStmt = std::make_unique<ExprStmt>(std::move(call), loc);
                stmt->cases.emplace_back(std::move(pattern), std::move(guard), std::move(exprStmt));
                match(TokenType::NEWLINE);
            } else {
                auto expr = expression();
                auto exprStmt = std::make_unique<ExprStmt>(std::move(expr), peek().location);
                stmt->cases.emplace_back(std::move(pattern), std::move(guard), std::move(exprStmt));
                match(TokenType::NEWLINE);
            }
        } else {
            auto expr = expression();
            auto exprStmt = std::make_unique<ExprStmt>(std::move(expr), peek().location);
            stmt->cases.emplace_back(std::move(pattern), std::move(guard), std::move(exprStmt));
            match(TokenType::NEWLINE);
        }
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of match block");
    return stmt;
}

StmtPtr Parser::returnStatement() {
    auto loc = previous().location;
    ExprPtr value = nullptr;
    
    // Parse return value at higher precedence to avoid consuming 'if' as ternary
    // This allows: return 0 if x < 0  (inline conditional)
    if (!isAtStatementBoundary() && !check(TokenType::IF)) {
        // Parse at NULL_COALESCE precedence to skip ternary parsing
        value = parsePrecedence(Precedence::NULL_COALESCE);
    }
    
    // Inline conditional: return value if condition
    if (match(TokenType::IF)) {
        auto condition = expression();
        match(TokenType::NEWLINE);
        auto returnStmt = std::make_unique<ReturnStmt>(std::move(value), loc);
        auto thenBlock = std::make_unique<Block>(loc);
        thenBlock->statements.push_back(std::move(returnStmt));
        return std::make_unique<IfStmt>(std::move(condition), std::move(thenBlock), loc);
    }
    
    match(TokenType::NEWLINE);
    return std::make_unique<ReturnStmt>(std::move(value), loc);
}

StmtPtr Parser::breakStatement() {
    auto loc = previous().location;
    
    // Check for label: break outer
    std::string label;
    if (check(TokenType::IDENTIFIER) && !check(TokenType::IF)) {
        label = advance().lexeme;
    }
    
    // Inline conditional: break if condition
    if (match(TokenType::IF)) {
        auto condition = expression();
        match(TokenType::NEWLINE);
        auto breakStmt = std::make_unique<BreakStmt>(loc);
        breakStmt->label = label;
        auto thenBlock = std::make_unique<Block>(loc);
        thenBlock->statements.push_back(std::move(breakStmt));
        return std::make_unique<IfStmt>(std::move(condition), std::move(thenBlock), loc);
    }
    
    match(TokenType::NEWLINE);
    auto stmt = std::make_unique<BreakStmt>(loc);
    stmt->label = label;
    return stmt;
}

StmtPtr Parser::continueStatement() {
    auto loc = previous().location;
    
    // Check for label: continue outer
    std::string label;
    if (check(TokenType::IDENTIFIER) && !check(TokenType::IF)) {
        label = advance().lexeme;
    }
    
    // Inline conditional: continue if condition
    if (match(TokenType::IF)) {
        auto condition = expression();
        match(TokenType::NEWLINE);
        auto continueStmt = std::make_unique<ContinueStmt>(loc);
        continueStmt->label = label;
        auto thenBlock = std::make_unique<Block>(loc);
        thenBlock->statements.push_back(std::move(continueStmt));
        return std::make_unique<IfStmt>(std::move(condition), std::move(thenBlock), loc);
    }
    
    match(TokenType::NEWLINE);
    auto stmt = std::make_unique<ContinueStmt>(loc);
    stmt->label = label;
    return stmt;
}

StmtPtr Parser::deleteStatement() {
    auto loc = previous().location;
    auto expr = expression();
    match(TokenType::NEWLINE);
    return std::make_unique<DeleteStmt>(std::move(expr), loc);
}

StmtPtr Parser::lockStatement() {
    auto loc = previous().location;
    auto mutex = expression();
    consume(TokenType::COLON, "Expected ':' after lock expression");
    match(TokenType::NEWLINE);
    auto body = block();
    return std::make_unique<LockStmt>(std::move(mutex), std::move(body), loc);
}

// New syntax redesign statement implementations

StmtPtr Parser::unlessStatement() {
    // unless condition: body  =>  if not condition: body
    auto loc = previous().location;
    auto condition = expression();
    consume(TokenType::COLON, "Expected ':' after unless condition");
    match(TokenType::NEWLINE);
    
    auto thenBranch = block();
    
    // Wrap condition in NOT
    auto notCondition = std::make_unique<UnaryExpr>(TokenType::NOT, std::move(condition), loc);
    return std::make_unique<IfStmt>(std::move(notCondition), std::move(thenBranch), loc);
}

StmtPtr Parser::loopStatement(const std::string& label) {
    auto loc = previous().location;
    
    consume(TokenType::COLON, "Expected ':' after loop");
    match(TokenType::NEWLINE);
    auto body = block();
    
    auto stmt = std::make_unique<LoopStmt>(std::move(body), loc);
    stmt->label = label;
    return stmt;
}

StmtPtr Parser::withStatement() {
    auto loc = previous().location;
    auto resource = expression();
    
    std::string alias;
    if (match(TokenType::IDENTIFIER) && previous().lexeme == "as") {
        alias = consume(TokenType::IDENTIFIER, "Expected alias name after 'as'").lexeme;
    }
    
    consume(TokenType::COLON, "Expected ':' after with expression");
    match(TokenType::NEWLINE);
    auto body = block();
    
    return std::make_unique<WithStmt>(std::move(resource), alias, std::move(body), loc);
}

StmtPtr Parser::scopeStatement() {
    auto loc = previous().location;
    
    std::string label;
    ExprPtr timeout = nullptr;
    
    // Check for label or timeout
    if (check(TokenType::IDENTIFIER)) {
        std::string name = peek().lexeme;
        if (name == "timeout") {
            advance();
            timeout = expression();
        } else {
            label = name;
            advance();
        }
    }
    
    consume(TokenType::COLON, "Expected ':' after scope");
    match(TokenType::NEWLINE);
    auto body = block();
    
    auto stmt = std::make_unique<ScopeStmt>(std::move(body), loc);
    stmt->label = label;
    stmt->timeout = std::move(timeout);
    return stmt;
}

StmtPtr Parser::requireStatement() {
    auto loc = previous().location;
    auto condition = expression();
    
    std::string message;
    if (match(TokenType::COMMA)) {
        auto msgExpr = expression();
        if (auto* str = dynamic_cast<StringLiteral*>(msgExpr.get())) {
            message = str->value;
        }
    }
    
    match(TokenType::NEWLINE);
    return std::make_unique<RequireStmt>(std::move(condition), message, loc);
}

StmtPtr Parser::ensureStatement() {
    auto loc = previous().location;
    auto condition = expression();
    
    std::string message;
    if (match(TokenType::COMMA)) {
        auto msgExpr = expression();
        if (auto* str = dynamic_cast<StringLiteral*>(msgExpr.get())) {
            message = str->value;
        }
    }
    
    match(TokenType::NEWLINE);
    return std::make_unique<EnsureStmt>(std::move(condition), message, loc);
}

StmtPtr Parser::comptimeBlock() {
    auto loc = previous().location;
    
    // Check for comptime assert
    if (match(TokenType::ASSERT)) {
        return comptimeAssertStatement(loc);
    }
    
    consume(TokenType::COLON, "Expected ':' after comptime");
    match(TokenType::NEWLINE);
    auto body = block();
    return std::make_unique<ComptimeBlock>(std::move(body), loc);
}

StmtPtr Parser::comptimeAssertStatement(SourceLocation loc) {
    // comptime assert condition, "message"
    auto condition = expression();
    
    std::string message;
    if (match(TokenType::COMMA)) {
        auto msgExpr = expression();
        if (auto* str = dynamic_cast<StringLiteral*>(msgExpr.get())) {
            message = str->value;
        }
        // If not a string literal, just ignore the message (will be caught at type check time)
    }
    
    match(TokenType::NEWLINE);
    return std::make_unique<ComptimeAssertStmt>(std::move(condition), message, loc);
}

StmtPtr Parser::effectDeclaration() {
    // effect Error[E]:
    //     fn raise e: E -> never
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected effect name").lexeme;
    
    auto decl = std::make_unique<EffectDecl>(name, loc);
    
    // Parse type parameters [E], [S], etc.
    if (match(TokenType::LBRACKET)) {
        do {
            decl->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after effect declaration");
    match(TokenType::NEWLINE);
    consume(TokenType::INDENT, "Expected indented effect body");
    skipNewlines();
    
    // Parse effect operations
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        if (match(TokenType::FN)) {
            auto opName = consume(TokenType::IDENTIFIER, "Expected operation name").lexeme;
            EffectOpDecl op(opName);
            
            // Parse parameters
            while (!check(TokenType::ARROW) && !check(TokenType::NEWLINE) && !isAtEnd()) {
                auto paramName = consume(TokenType::IDENTIFIER, "Expected parameter name").lexeme;
                consume(TokenType::COLON, "Expected ':' after parameter name");
                auto paramType = parseType();
                op.params.emplace_back(paramName, paramType);
                match(TokenType::COMMA);
            }
            
            // Parse return type
            if (match(TokenType::ARROW)) {
                op.returnType = parseType();
            } else {
                op.returnType = "void";
            }
            
            decl->operations.push_back(std::move(op));
        }
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of effect block");
    return decl;
}

} // namespace tyl
