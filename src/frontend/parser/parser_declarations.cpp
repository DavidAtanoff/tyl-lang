// Tyl Compiler - Parser Declaration Implementations
// Handles: fn, record, enum, trait, impl, use, import, extern, macro, syntax, layer, unsafe, var

#include "parser_base.h"
#include "common/errors.h"

namespace tyl {

// Check if a cfg condition is satisfied
static bool evaluateCfg(const std::string& condition) {
    // Platform checks - these should match the TARGET platform, not the host
    // For now, we assume Windows target since we're generating Windows PE files
    if (condition == "windows") return true;
    if (condition == "linux") return false;
    if (condition == "macos") return false;
    if (condition == "unix") return false;
    
    // Architecture checks - assume x86_64 for now
    if (condition == "x86_64") return true;
    if (condition == "x86") return false;
    
    // Build mode checks
    #ifdef NDEBUG
    if (condition == "debug") return false;
    if (condition == "release") return true;
    #else
    if (condition == "debug") return true;
    if (condition == "release") return false;
    #endif
    
    // Feature checks - could be extended with compiler flags
    // For now, return false for unknown conditions
    return false;
}

StmtPtr Parser::declaration() {
    skipNewlines();
    
    // Parse attributes: #[repr(C)], #[repr(packed)], #[repr(align(N))], #[cdecl], #[stdcall], #[export], #[hidden], #[weak], #[cfg(...)], @derive(...), etc.
    bool reprC = false;
    bool reprPacked = false;
    int reprAlign = 0;
    CallingConvention callingConv = CallingConvention::Default;
    bool isNaked = false;
    bool isExport = false;
    bool isHidden = false;
    bool isWeak = false;
    bool skipDeclaration = false;  // For cfg that evaluates to false
    std::vector<std::string> deriveTraits;  // @derive(Debug, Clone, Eq)
    
    while (check(TokenType::ATTRIBUTE)) {
        auto attrTok = advance();
        std::string attr = std::get<std::string>(attrTok.literal);
        
        // Parse cfg(...) for conditional compilation
        if (attr.find("cfg(") == 0) {
            std::string condition = attr.substr(4, attr.length() - 5);  // Extract content between cfg( and )
            bool result = evaluateCfg(condition);
            if (!result) {
                skipDeclaration = true;
            }
        }
        // Parse derive(...) for automatic trait implementation
        else if (attr.find("derive(") == 0) {
            std::string deriveArg = attr.substr(7, attr.length() - 8);  // Extract content between derive( and )
            // Parse comma-separated trait names
            size_t pos = 0;
            while (pos < deriveArg.length()) {
                // Skip whitespace
                while (pos < deriveArg.length() && (deriveArg[pos] == ' ' || deriveArg[pos] == '\t')) pos++;
                if (pos >= deriveArg.length()) break;
                
                // Find end of trait name
                size_t start = pos;
                while (pos < deriveArg.length() && deriveArg[pos] != ',' && deriveArg[pos] != ' ') pos++;
                
                if (pos > start) {
                    deriveTraits.push_back(deriveArg.substr(start, pos - start));
                }
                
                // Skip comma
                while (pos < deriveArg.length() && (deriveArg[pos] == ',' || deriveArg[pos] == ' ')) pos++;
            }
        }
        // Parse repr(...) attributes
        else if (attr.find("repr(") == 0) {
            std::string reprArg = attr.substr(5, attr.length() - 6);  // Extract content between repr( and )
            if (reprArg == "C") {
                reprC = true;
            } else if (reprArg == "packed") {
                reprPacked = true;
            } else if (reprArg.find("align(") == 0) {
                std::string alignStr = reprArg.substr(6, reprArg.length() - 7);
                reprAlign = std::stoi(alignStr);
            }
        }
        // Parse calling convention attributes
        else if (attr == "cdecl") {
            callingConv = CallingConvention::Cdecl;
        } else if (attr == "stdcall") {
            callingConv = CallingConvention::Stdcall;
        } else if (attr == "fastcall") {
            callingConv = CallingConvention::Fastcall;
        } else if (attr == "win64") {
            callingConv = CallingConvention::Win64;
        } else if (attr == "naked") {
            isNaked = true;
        }
        // Parse visibility and linkage attributes
        else if (attr == "export") {
            isExport = true;
        } else if (attr == "hidden") {
            isHidden = true;
        } else if (attr == "visible") {
            isHidden = false;  // Explicitly visible (default)
        } else if (attr == "weak") {
            isWeak = true;
        }
        skipNewlines();
    }
    
    // If cfg evaluated to false, skip this declaration
    if (skipDeclaration) {
        // We need to skip the entire declaration that follows
        // First, skip any pub/priv/async modifiers
        while (match(TokenType::PUB) || match(TokenType::PRIV) || match(TokenType::ASYNC)) {}
        
        // Now skip the declaration based on what keyword we see
        if (match(TokenType::FN)) {
            // Skip function: name, params, return type, and body
            if (check(TokenType::IDENTIFIER)) advance(); // name
            // Skip until we find the body start (: or { or =>)
            while (!isAtEnd() && !check(TokenType::COLON) && !check(TokenType::LBRACE) && 
                   !check(TokenType::DOUBLE_ARROW) && !check(TokenType::ASSIGN)) {
                advance();
            }
            // Skip the body
            if (match(TokenType::COLON)) {
                match(TokenType::NEWLINE);
                if (match(TokenType::INDENT)) {
                    int depth = 1;
                    while (!isAtEnd() && depth > 0) {
                        if (match(TokenType::INDENT)) depth++;
                        else if (match(TokenType::DEDENT)) depth--;
                        else advance();
                    }
                } else {
                    // Single line body
                    while (!isAtEnd() && !check(TokenType::NEWLINE)) advance();
                    match(TokenType::NEWLINE);
                }
            } else if (match(TokenType::LBRACE)) {
                int depth = 1;
                while (!isAtEnd() && depth > 0) {
                    if (match(TokenType::LBRACE)) depth++;
                    else if (match(TokenType::RBRACE)) depth--;
                    else advance();
                }
            } else if (match(TokenType::DOUBLE_ARROW) || match(TokenType::ASSIGN)) {
                // Single expression body
                while (!isAtEnd() && !check(TokenType::NEWLINE)) advance();
                match(TokenType::NEWLINE);
            }
        } else if (match(TokenType::RECORD) || match(TokenType::ENUM) || match(TokenType::TRAIT) || 
                   match(TokenType::UNION) || match(TokenType::IMPL)) {
            // Skip struct-like declaration with indented body
            while (!isAtEnd() && !check(TokenType::COLON)) advance();
            if (match(TokenType::COLON)) {
                match(TokenType::NEWLINE);
                if (match(TokenType::INDENT)) {
                    int depth = 1;
                    while (!isAtEnd() && depth > 0) {
                        if (match(TokenType::INDENT)) depth++;
                        else if (match(TokenType::DEDENT)) depth--;
                        else advance();
                    }
                }
            }
        } else {
            // Unknown declaration type, skip to next line
            while (!isAtEnd() && !check(TokenType::NEWLINE)) advance();
            match(TokenType::NEWLINE);
        }
        
        // Skip any trailing newlines
        while (match(TokenType::NEWLINE)) {}
        
        // Return an empty block as a no-op
        return std::make_unique<Block>(peek().location);
    }
    
    bool isPublic = match(TokenType::PUB);
    bool isPrivate = !isPublic && match(TokenType::PRIV);
    bool isAsync = match(TokenType::ASYNC);
    bool isComptime = match(TokenType::COMPTIME);
    
    // Handle comptime assert at declaration level
    if (isComptime && match(TokenType::ASSERT)) {
        auto loc = previous().location;
        return comptimeAssertStatement(loc);
    }
    
    if (match(TokenType::FN)) {
        auto fn = fnDeclaration();
        auto* fnDecl = static_cast<FnDecl*>(fn.get());
        if (isAsync) fnDecl->isAsync = true;
        if (isComptime) fnDecl->isComptime = true;
        fnDecl->isPublic = isPublic;
        fnDecl->callingConv = callingConv;
        fnDecl->isNaked = isNaked;
        fnDecl->isExport = isExport;
        fnDecl->isHidden = isHidden;
        fnDecl->isWeak = isWeak;
        return fn;
    }
    if (match(TokenType::RECORD)) {
        auto rec = recordDeclaration();
        auto* recDecl = static_cast<RecordDecl*>(rec.get());
        recDecl->isPublic = isPublic;
        recDecl->reprC = reprC;
        recDecl->reprPacked = reprPacked;
        recDecl->reprAlign = reprAlign;
        recDecl->deriveTraits = deriveTraits;
        return rec;
    }
    if (match(TokenType::UNION)) {
        auto un = unionDeclaration();
        auto* unDecl = static_cast<UnionDecl*>(un.get());
        unDecl->isPublic = isPublic;
        unDecl->reprC = reprC;
        unDecl->reprAlign = reprAlign;
        return un;
    }
    if (match(TokenType::ENUM)) return enumDeclaration();
    if (match(TokenType::TYPE)) return typeAliasDeclaration();
    if (match(TokenType::TRAIT)) return traitDeclaration();
    if (match(TokenType::CONCEPT)) return conceptDeclaration();
    if (match(TokenType::IMPL)) return implDeclaration();
    if (match(TokenType::USE)) return useStatement();
    if (match(TokenType::IMPORT)) return importStatement();
    if (match(TokenType::MODULE)) return moduleDeclaration();
    if (match(TokenType::EXTERN)) return externDeclaration();
    if (match(TokenType::MACRO)) return macroDeclaration();
    if (match(TokenType::SYNTAX)) return syntaxMacroDeclaration();
    if (match(TokenType::LAYER)) return layerDeclaration();
    if (match(TokenType::UNSAFE)) return unsafeBlock();
    if (match(TokenType::ASM)) return asmStatement();
    if (match({TokenType::LET, TokenType::MUT, TokenType::CONST})) return varDeclaration();
    
    (void)isPublic;
    (void)isPrivate;
    (void)isAsync;
    (void)isComptime;
    return statement();
}

StmtPtr Parser::varDeclaration() {
    auto loc = previous().location;
    TokenType declType = previous().type;
    
    // Tuple destructuring: let (a, b) = expr
    if (check(TokenType::LPAREN)) {
        advance();
        std::vector<std::string> names;
        
        if (!check(TokenType::RPAREN)) {
            do {
                names.push_back(consume(TokenType::IDENTIFIER, "Expected variable name in destructuring").lexeme);
            } while (match(TokenType::COMMA));
        }
        
        consume(TokenType::RPAREN, "Expected ')' after destructuring pattern");
        consume(TokenType::ASSIGN, "Expected '=' after destructuring pattern");
        auto init = expression();
        match(TokenType::NEWLINE);
        
        auto decl = std::make_unique<DestructuringDecl>(
            DestructuringDecl::Kind::TUPLE, std::move(names), std::move(init), loc);
        decl->isMutable = (declType == TokenType::MUT);
        return decl;
    }
    
    // Record destructuring: let {x, y} = expr
    if (check(TokenType::LBRACE)) {
        advance();
        std::vector<std::string> names;
        
        if (!check(TokenType::RBRACE)) {
            do {
                names.push_back(consume(TokenType::IDENTIFIER, "Expected field name in destructuring").lexeme);
            } while (match(TokenType::COMMA));
        }
        
        consume(TokenType::RBRACE, "Expected '}' after destructuring pattern");
        consume(TokenType::ASSIGN, "Expected '=' after destructuring pattern");
        auto init = expression();
        match(TokenType::NEWLINE);
        
        auto decl = std::make_unique<DestructuringDecl>(
            DestructuringDecl::Kind::RECORD, std::move(names), std::move(init), loc);
        decl->isMutable = (declType == TokenType::MUT);
        return decl;
    }
    
    // Regular variable declaration - check for chained multi-assign: mut x = mut y = mut z = value
    auto name = consume(TokenType::IDENTIFIER, "Expected variable name").lexeme;
    
    std::string typeName;
    if (match(TokenType::COLON)) {
        typeName = parseType();
    }
    
    ExprPtr init = nullptr;
    if (match(TokenType::ASSIGN)) {
        // Check for chained mutable multi-assign: mut x = mut y = mut z = value
        if (declType == TokenType::MUT && check(TokenType::MUT)) {
            std::vector<std::string> names;
            names.push_back(name);
            
            // Parse chain: mut y = mut z = value
            while (match(TokenType::MUT)) {
                auto nextName = consume(TokenType::IDENTIFIER, "Expected variable name").lexeme;
                names.push_back(nextName);
                
                if (!match(TokenType::ASSIGN)) {
                    // Error: expected = after variable name
                    break;
                }
                
                // Check if next is another 'mut' (chain continues) or value
                if (!check(TokenType::MUT)) {
                    // This is the value expression
                    break;
                }
            }
            
            init = expression();
            match(TokenType::NEWLINE);
            
            auto decl = std::make_unique<MultiVarDecl>(std::move(names), std::move(init), loc);
            decl->isMutable = true;
            return decl;
        }
        
        init = expression();
    }
    
    match(TokenType::NEWLINE);
    
    auto decl = std::make_unique<VarDecl>(name, typeName, std::move(init), loc);
    decl->isMutable = (declType == TokenType::MUT);
    decl->isConst = (declType == TokenType::CONST);
    return decl;
}

StmtPtr Parser::fnDeclaration(bool requireBody) {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected function name").lexeme;
    
    auto fn = std::make_unique<FnDecl>(name, loc);
    
    // Generic type parameters and lifetime parameters: fn name[T, U, 'a, 'b, F[_]]
    if (match(TokenType::LBRACKET)) {
        do {
            // Check for lifetime parameter: 'a, 'static, etc.
            if (check(TokenType::LIFETIME)) {
                fn->lifetimeParams.push_back(advance().lexeme);
            } else {
                std::string paramName = consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme;
                
                // Check for HKT syntax: F[_] or F[_, _]
                if (check(TokenType::LBRACKET)) {
                    advance();  // consume [
                    std::string hktParam = paramName + "[";
                    bool first = true;
                    do {
                        if (!first) hktParam += ", ";
                        first = false;
                        if (check(TokenType::UNDERSCORE) || (check(TokenType::IDENTIFIER) && peek().lexeme == "_")) {
                            advance();  // consume _
                            hktParam += "_";
                        }
                    } while (match(TokenType::COMMA));
                    consume(TokenType::RBRACKET, "Expected ']' after type constructor arity");
                    hktParam += "]";
                    fn->typeParams.push_back(hktParam);
                } else if (match(TokenType::COLON)) {
                    // Type constraint: T: Numeric or T: Numeric + Orderable
                    std::string constraint = paramName + ": ";
                    constraint += consume(TokenType::IDENTIFIER, "Expected concept name").lexeme;
                    // Support multiple constraints with +
                    while (match(TokenType::PLUS)) {
                        constraint += " + ";
                        constraint += consume(TokenType::IDENTIFIER, "Expected concept name").lexeme;
                    }
                    fn->typeParams.push_back(constraint);
                } else {
                    fn->typeParams.push_back(paramName);
                }
            }
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    fn->params = parseParamsWithDefaults(fn->paramDefaults);
    
    if (match(TokenType::ARROW)) {
        fn->returnType = parseType();
    }
    
    // Support :, =>, =, and { for function body
    if (match(TokenType::DOUBLE_ARROW)) {
        auto expr = expression();
        auto ret = std::make_unique<ReturnStmt>(std::move(expr), loc);
        auto blk = std::make_unique<Block>(loc);
        blk->statements.push_back(std::move(ret));
        fn->body = std::move(blk);
        match(TokenType::NEWLINE);
    } else if (match(TokenType::LBRACE)) {
        // Brace block: fn add(a, b) { return a + b }
        fn->body = braceBlock();
    } else if (match(TokenType::COLON)) {
        match(TokenType::NEWLINE);
        
        if (check(TokenType::INDENT)) {
            fn->body = block();
        } else {
            auto expr = expression();
            auto ret = std::make_unique<ReturnStmt>(std::move(expr), loc);
            auto blk = std::make_unique<Block>(loc);
            blk->statements.push_back(std::move(ret));
            fn->body = std::move(blk);
            match(TokenType::NEWLINE);
        }
    } else if (match(TokenType::ASSIGN)) {
        auto expr = expression();
        auto ret = std::make_unique<ReturnStmt>(std::move(expr), loc);
        auto blk = std::make_unique<Block>(loc);
        blk->statements.push_back(std::move(ret));
        fn->body = std::move(blk);
        match(TokenType::NEWLINE);
    } else if (!requireBody) {
        // No body required (e.g., trait method signature)
        // Just consume the newline if present
        match(TokenType::NEWLINE);
    } else {
        auto diag = errors::expectedFunctionBody(peek().location);
        throw TylDiagnosticError(diag);
    }
    
    return fn;
}

StmtPtr Parser::recordDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected record name").lexeme;
    
    auto rec = std::make_unique<RecordDecl>(name, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            rec->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after record name");
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented record fields");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        auto fieldName = consume(TokenType::IDENTIFIER, "Expected field name").lexeme;
        std::string fieldType;
        BitfieldSpec bitfield;
        
        if (match(TokenType::COLON)) {
            fieldType = parseType();
            
            // Check for bitfield specification: field: int : 4
            if (match(TokenType::COLON)) {
                auto bitWidthTok = consume(TokenType::INTEGER, "Expected bit width for bitfield");
                bitfield.bitWidth = static_cast<int>(std::get<int64_t>(bitWidthTok.literal));
            }
        }
        rec->fields.emplace_back(fieldName, fieldType);
        rec->bitfields.push_back(bitfield);
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of record");
    return rec;
}

StmtPtr Parser::unionDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected union name").lexeme;
    
    auto un = std::make_unique<UnionDecl>(name, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            un->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after union name");
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented union fields");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        auto fieldName = consume(TokenType::IDENTIFIER, "Expected field name").lexeme;
        std::string fieldType;
        if (match(TokenType::COLON)) {
            fieldType = parseType();
        }
        un->fields.emplace_back(fieldName, fieldType);
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of union");
    return un;
}

StmtPtr Parser::enumDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected enum name").lexeme;
    
    auto en = std::make_unique<EnumDecl>(name, loc);
    
    if (match(TokenType::LBRACKET)) {
        do {
            en->typeParams.push_back(consume(TokenType::IDENTIFIER, "Expected type parameter").lexeme);
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::COLON, "Expected ':' after enum name");
    match(TokenType::NEWLINE);
    
    consume(TokenType::INDENT, "Expected indented enum variants");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        auto variantName = consume(TokenType::IDENTIFIER, "Expected variant name").lexeme;
        std::optional<int64_t> value;
        if (match(TokenType::ASSIGN)) {
            // Handle negative values: -1, -2, etc.
            bool isNegative = match(TokenType::MINUS);
            auto valTok = consume(TokenType::INTEGER, "Expected integer value");
            int64_t intValue = std::get<int64_t>(valTok.literal);
            value = isNegative ? -intValue : intValue;
        }
        en->variants.emplace_back(variantName, value);
        match(TokenType::NEWLINE);
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of enum");
    return en;
}

StmtPtr Parser::typeAliasDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected type name").lexeme;
    
    auto alias = std::make_unique<TypeAlias>(name, "", loc);
    
    // Parse type parameters: type Vector[T, N: int] = ...
    if (match(TokenType::LBRACKET)) {
        do {
            auto paramName = consume(TokenType::IDENTIFIER, "Expected type parameter name").lexeme;
            
            // Check if this is a value parameter: N: int
            if (match(TokenType::COLON)) {
                auto paramType = parseType();
                alias->typeParams.emplace_back(paramName, paramType, true);  // isValue = true
            } else {
                alias->typeParams.emplace_back(paramName, "type", false);  // Regular type param
            }
        } while (match(TokenType::COMMA));
        consume(TokenType::RBRACKET, "Expected ']' after type parameters");
    }
    
    consume(TokenType::ASSIGN, "Expected '=' after type name");
    
    // Check for opaque type: type Handle = opaque
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "opaque") {
        advance();  // consume 'opaque'
        match(TokenType::NEWLINE);
        alias->targetType = "opaque";
        return alias;
    }
    
    alias->targetType = parseType();
    
    // Parse where clause: type NonEmpty[T] = [T] where len(_) > 0
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "where") {
        advance();  // consume 'where'
        inConstraintContext_ = true;  // Don't transform placeholders in constraints
        alias->constraint = expression();
        inConstraintContext_ = false;
    }
    
    match(TokenType::NEWLINE);
    return alias;
}

// traitDeclaration and implDeclaration are defined in parser_decl_traits.cpp

StmtPtr Parser::useStatement() {
    auto loc = previous().location;
    
    // use layer "name"
    if (match(TokenType::LAYER)) {
        auto name = consume(TokenType::STRING, "Expected layer name string").lexeme;
        if (auto* str = std::get_if<std::string>(&previous().literal)) {
            name = *str;
        }
        match(TokenType::NEWLINE);
        auto stmt = std::make_unique<UseStmt>(name, loc);
        stmt->isLayer = true;
        return stmt;
    }
    
    // use "file.tyl" - file import
    if (check(TokenType::STRING)) {
        auto path = std::get<std::string>(advance().literal);
        
        // Check for alias: use "file.tyl" as name
        std::string alias;
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "as") {
            advance();  // consume 'as'
            alias = consume(TokenType::IDENTIFIER, "Expected alias name").lexeme;
        }
        
        match(TokenType::NEWLINE);
        auto stmt = std::make_unique<UseStmt>(path, loc);
        stmt->isFileImport = true;
        stmt->alias = alias;
        return stmt;
    }
    
    // use module::submodule or use module::{item1, item2}
    std::string path;
    path = consume(TokenType::IDENTIFIER, "Expected module name or string path").lexeme;
    
    while (match(TokenType::DOUBLE_COLON)) {
        // Check for selective import: use math::{sin, cos}
        if (match(TokenType::LBRACE)) {
            auto stmt = std::make_unique<UseStmt>(path, loc);
            
            // Parse import items
            if (!check(TokenType::RBRACE)) {
                do {
                    skipNewlines();
                    stmt->importItems.push_back(
                        consume(TokenType::IDENTIFIER, "Expected import item").lexeme
                    );
                } while (match(TokenType::COMMA));
            }
            skipNewlines();
            consume(TokenType::RBRACE, "Expected '}' after import items");
            match(TokenType::NEWLINE);
            return stmt;
        }
        
        // Check for wildcard: use math::*
        if (match(TokenType::STAR)) {
            auto stmt = std::make_unique<UseStmt>(path, loc);
            stmt->importItems.push_back("*");  // Wildcard marker
            match(TokenType::NEWLINE);
            return stmt;
        }
        
        path += "::" + consume(TokenType::IDENTIFIER, "Expected identifier").lexeme;
    }
    
    // Check for alias: use math as m
    std::string alias;
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "as") {
        advance();  // consume 'as'
        alias = consume(TokenType::IDENTIFIER, "Expected alias name").lexeme;
    }
    
    match(TokenType::NEWLINE);
    auto stmt = std::make_unique<UseStmt>(path, loc);
    stmt->alias = alias;
    return stmt;
}

StmtPtr Parser::moduleDeclaration() {
    auto loc = previous().location;
    
    // module name:
    std::string name = consume(TokenType::IDENTIFIER, "Expected module name").lexeme;
    
    // Allow nested module names: module math::calculus:
    while (match(TokenType::DOUBLE_COLON)) {
        name += "::" + consume(TokenType::IDENTIFIER, "Expected module name").lexeme;
    }
    
    consume(TokenType::COLON, "Expected ':' after module name");
    match(TokenType::NEWLINE);
    
    auto mod = std::make_unique<ModuleDecl>(name, loc);
    
    // Parse module body (indented block)
    if (match(TokenType::INDENT)) {
        while (!check(TokenType::DEDENT) && !isAtEnd()) {
            mod->body.push_back(declaration());
            skipNewlines();
        }
        match(TokenType::DEDENT);
    }
    
    return mod;
}

StmtPtr Parser::importStatement() {
    auto loc = previous().location;
    auto path = consume(TokenType::STRING, "Expected import path").lexeme;
    if (auto* str = std::get_if<std::string>(&previous().literal)) {
        path = *str;
    }
    
    auto imp = std::make_unique<ImportStmt>(path, loc);
    
    if (match(TokenType::IDENTIFIER) && previous().lexeme == "as") {
        imp->alias = consume(TokenType::IDENTIFIER, "Expected alias name").lexeme;
    }
    
    match(TokenType::NEWLINE);
    return imp;
}

StmtPtr Parser::externDeclaration() {
    auto loc = previous().location;
    
    std::string abi = "C";
    std::string library;
    
    // Parse ABI and/or library specification
    // extern "C":                    - C ABI, no specific library (link later)
    // extern "kernel32.dll":         - Windows DLL import
    // extern "C" "mylib.lib":        - C ABI with static library
    // extern "cdecl" "msvcrt.dll":   - Explicit calling convention
    if (check(TokenType::STRING)) {
        std::string first = std::get<std::string>(advance().literal);
        
        // Check if this is an ABI specifier or a library
        if (first == "C" || first == "cdecl" || first == "stdcall" || first == "fastcall" || first == "win64") {
            abi = first;
            // Check for optional library
            if (check(TokenType::STRING)) {
                library = std::get<std::string>(advance().literal);
            }
        } else {
            // It's a library name, ABI defaults to "C"
            library = first;
        }
    }
    
    consume(TokenType::COLON, "Expected ':' after extern");
    match(TokenType::NEWLINE);
    
    auto ext = std::make_unique<ExternDecl>(abi, library, loc);
    consume(TokenType::INDENT, "Expected indented extern block");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        if (match(TokenType::FN)) {
            auto fn = externFnDeclaration();
            ext->functions.push_back(std::unique_ptr<FnDecl>(static_cast<FnDecl*>(fn.release())));
        }
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of extern block");
    return ext;
}

StmtPtr Parser::externFnDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected function name").lexeme;
    
    auto fn = std::make_unique<FnDecl>(name, loc);
    fn->isExtern = true;
    
    // Parse parameters - support both styles:
    // New style with parens: fn printf(fmt: *str, ...) -> int
    // Old style without parens: fn GetStdHandle nStdHandle -> int
    if (match(TokenType::LPAREN)) {
        // New style with parentheses
        while (!check(TokenType::RPAREN) && !isAtEnd()) {
            // Check for variadic (...)
            if (match(TokenType::DOTDOT)) {
                if (match(TokenType::DOT)) {
                    // ... variadic marker
                    fn->params.emplace_back("...", "...");
                }
                break;
            }
            
            std::string paramName;
            std::string paramType;
            
            if (check(TokenType::IDENTIFIER)) {
                paramName = advance().lexeme;
                if (match(TokenType::COLON)) {
                    paramType = parseType();
                } else {
                    // Just a type, no name
                    paramType = paramName;
                    paramName = "_p" + std::to_string(fn->params.size());
                }
            } else if (check(TokenType::STAR)) {
                // Pointer type without name
                paramType = parseType();
                paramName = "_p" + std::to_string(fn->params.size());
            }
            
            fn->params.emplace_back(paramName, paramType);
            
            if (!match(TokenType::COMMA)) break;
        }
        consume(TokenType::RPAREN, "Expected ')' after parameters");
    } else {
        // Old style: space-separated params without parens
        fn->params = parseParams();
    }
    
    if (match(TokenType::ARROW)) {
        fn->returnType = parseType();
    }
    
    match(TokenType::NEWLINE);
    return fn;
}

StmtPtr Parser::macroDeclaration() {
    auto loc = previous().location;
    
    // Infix operator macro: macro infix "<=>" 50 left right: ...
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "infix") {
        advance();
        
        auto opSymbol = consume(TokenType::STRING, "Expected operator symbol string").lexeme;
        if (auto* str = std::get_if<std::string>(&previous().literal)) {
            opSymbol = *str;
        }
        
        int precedence = 50;
        if (check(TokenType::INTEGER)) {
            precedence = static_cast<int>(std::get<int64_t>(advance().literal));
        }
        
        auto mac = std::make_unique<MacroDecl>("infix_" + opSymbol, loc);
        mac->isOperator = true;
        mac->isInfix = true;
        mac->operatorSymbol = opSymbol;
        mac->precedence = precedence;
        
        if (check(TokenType::IDENTIFIER)) {
            mac->params.push_back(advance().lexeme);
        } else {
            mac->params.push_back("left");
        }
        if (check(TokenType::IDENTIFIER)) {
            mac->params.push_back(advance().lexeme);
        } else {
            mac->params.push_back("right");
        }
        
        consume(TokenType::COLON, "Expected ':' after infix macro signature");
        match(TokenType::NEWLINE);
        
        if (check(TokenType::INDENT)) {
            consume(TokenType::INDENT, "Expected indented macro body");
            skipNewlines();
            while (!check(TokenType::DEDENT) && !isAtEnd()) {
                mac->body.push_back(declaration());
                skipNewlines();
            }
            consume(TokenType::DEDENT, "Expected end of macro");
        } else {
            auto expr = expression();
            mac->body.push_back(std::make_unique<ExprStmt>(std::move(expr), loc));
            match(TokenType::NEWLINE);
        }
        
        return mac;
    }
    
    // Regular macro
    auto name = consume(TokenType::IDENTIFIER, "Expected macro name").lexeme;
    
    auto mac = std::make_unique<MacroDecl>(name, loc);
    
    while (check(TokenType::IDENTIFIER)) {
        mac->params.push_back(advance().lexeme);
    }
    
    consume(TokenType::COLON, "Expected ':' after macro signature");
    match(TokenType::NEWLINE);
    
    if (check(TokenType::INDENT)) {
        consume(TokenType::INDENT, "Expected indented macro body");
        skipNewlines();
        while (!check(TokenType::DEDENT) && !isAtEnd()) {
            mac->body.push_back(declaration());
            skipNewlines();
        }
        consume(TokenType::DEDENT, "Expected end of macro");
    }
    
    return mac;
}

StmtPtr Parser::syntaxMacroDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected syntax macro name").lexeme;
    
    auto syntaxMac = std::make_unique<SyntaxMacroDecl>(name, loc);
    
    if (match(TokenType::DOUBLE_ARROW)) {
        std::string transformExpr;
        while (!check(TokenType::NEWLINE) && !isAtEnd()) {
            transformExpr += peek().lexeme;
            advance();
        }
        syntaxMac->transformExpr = transformExpr;
        match(TokenType::NEWLINE);
        return syntaxMac;
    }
    
    if (match(TokenType::COLON)) {
        match(TokenType::NEWLINE);
        
        if (check(TokenType::INDENT)) {
            consume(TokenType::INDENT, "Expected indented syntax macro body");
            skipNewlines();
            
            if (check(TokenType::IDENTIFIER) && peek().lexeme == "transform") {
                advance();
                if (match(TokenType::DOUBLE_ARROW)) {
                    std::string transformExpr;
                    while (!check(TokenType::NEWLINE) && !isAtEnd()) {
                        if (!transformExpr.empty()) transformExpr += " ";
                        transformExpr += peek().lexeme;
                        advance();
                    }
                    syntaxMac->transformExpr = transformExpr;
                    match(TokenType::NEWLINE);
                    skipNewlines();
                }
            }
            
            while (!check(TokenType::DEDENT) && !isAtEnd()) {
                syntaxMac->body.push_back(declaration());
                skipNewlines();
            }
            consume(TokenType::DEDENT, "Expected end of syntax macro");
        }
    } else {
        match(TokenType::NEWLINE);
    }
    
    return syntaxMac;
}

StmtPtr Parser::layerDeclaration() {
    auto loc = previous().location;
    auto name = consume(TokenType::IDENTIFIER, "Expected layer name").lexeme;
    consume(TokenType::COLON, "Expected ':' after layer name");
    match(TokenType::NEWLINE);
    
    auto layer = std::make_unique<LayerDecl>(name, loc);
    consume(TokenType::INDENT, "Expected indented layer body");
    skipNewlines();
    
    while (!check(TokenType::DEDENT) && !isAtEnd()) {
        layer->declarations.push_back(declaration());
        skipNewlines();
    }
    
    consume(TokenType::DEDENT, "Expected end of layer");
    return layer;
}

StmtPtr Parser::unsafeBlock() {
    auto loc = previous().location;
    consume(TokenType::COLON, "Expected ':' after unsafe");
    match(TokenType::NEWLINE);
    auto body = block();
    return std::make_unique<UnsafeBlock>(std::move(body), loc);
}

StmtPtr Parser::asmStatement() {
    auto loc = previous().location;
    
    // Expect asm! { ... } or asm!: with indented block
    if (match(TokenType::BANG)) {
        // asm! { "code" } or asm!: block
        if (match(TokenType::LBRACE)) {
            // asm! { "mov rax, 1" }
            std::string code;
            
            // Parse the assembly code string(s)
            while (!check(TokenType::RBRACE) && !isAtEnd()) {
                if (check(TokenType::STRING)) {
                    if (!code.empty()) code += "\n";
                    code += std::get<std::string>(advance().literal);
                } else if (match(TokenType::COMMA) || match(TokenType::NEWLINE)) {
                    // Skip commas and newlines between strings
                    continue;
                } else {
                    break;
                }
            }
            consume(TokenType::RBRACE, "Expected '}' after asm block");
            
            return std::make_unique<AsmStmt>(code, loc);
        } else if (match(TokenType::COLON)) {
            // asm!:
            //     "mov rax, 1"
            //     "ret"
            match(TokenType::NEWLINE);
            consume(TokenType::INDENT, "Expected indented block after asm!:");
            
            std::string code;
            while (!check(TokenType::DEDENT) && !isAtEnd()) {
                if (check(TokenType::STRING)) {
                    if (!code.empty()) code += "\n";
                    code += std::get<std::string>(advance().literal);
                }
                match(TokenType::NEWLINE);
            }
            
            if (check(TokenType::DEDENT)) advance();
            
            return std::make_unique<AsmStmt>(code, loc);
        }
    }
    
    // If we get here, syntax is wrong - consume will throw
    consume(TokenType::BANG, "Expected '!' after asm");
    return nullptr;
}

} // namespace tyl
