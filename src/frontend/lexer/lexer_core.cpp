// Tyl Compiler - Lexer Core
// Keywords, token creation, core methods

#include "lexer_base.h"

namespace tyl {

const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"fn", TokenType::FN}, {"if", TokenType::IF}, {"else", TokenType::ELSE},
    {"elif", TokenType::ELIF}, {"for", TokenType::FOR}, {"while", TokenType::WHILE},
    {"match", TokenType::MATCH}, {"return", TokenType::RETURN},
    {"true", TokenType::TRUE}, {"false", TokenType::FALSE}, {"nil", TokenType::NIL},
    {"null", TokenType::NIL},  // Alias for nil (C-style null pointer)
    {"and", TokenType::AND}, {"or", TokenType::OR}, {"not", TokenType::NOT},
    {"in", TokenType::IN}, {"to", TokenType::TO}, {"by", TokenType::BY},
    {"try", TokenType::TRY}, {"use", TokenType::USE}, {"layer", TokenType::LAYER},
    {"macro", TokenType::MACRO}, {"import", TokenType::IMPORT}, {"module", TokenType::MODULE}, {"extern", TokenType::EXTERN},
    {"async", TokenType::ASYNC}, {"await", TokenType::AWAIT}, {"spawn", TokenType::SPAWN},
    {"record", TokenType::RECORD}, {"enum", TokenType::ENUM}, {"union", TokenType::UNION},
    {"let", TokenType::LET}, {"mut", TokenType::MUT}, {"const", TokenType::CONST},
    {"unsafe", TokenType::UNSAFE}, {"ptr", TokenType::PTR}, {"ref", TokenType::REF},
    {"new", TokenType::NEW}, {"delete", TokenType::DELETE}, {"asm", TokenType::ASM},
    {"break", TokenType::BREAK}, {"continue", TokenType::CONTINUE},
    {"type", TokenType::TYPE}, {"alias", TokenType::ALIAS}, {"syntax", TokenType::SYNTAX},
    {"pub", TokenType::PUB}, {"priv", TokenType::PRIV},
    {"self", TokenType::SELF}, {"super", TokenType::SUPER},
    {"trait", TokenType::TRAIT}, {"impl", TokenType::IMPL},
    {"chan", TokenType::CHAN},
    {"Mutex", TokenType::MUTEX},
    {"RWLock", TokenType::RWLOCK},
    {"Cond", TokenType::COND},
    {"Semaphore", TokenType::SEMAPHORE},
    {"lock", TokenType::LOCK},
    {"Atomic", TokenType::ATOMIC},
    // Smart pointer keywords
    {"Box", TokenType::BOX},
    {"Rc", TokenType::RC},
    {"Arc", TokenType::ARC},
    {"Weak", TokenType::WEAK_PTR},
    {"Cell", TokenType::CELL},
    {"RefCell", TokenType::REFCELL},
    // New syntax redesign keywords
    {"loop", TokenType::LOOP},
    {"unless", TokenType::UNLESS},
    {"export", TokenType::EXPORT},
    {"inline", TokenType::INLINE},
    {"noinline", TokenType::NOINLINE},
    {"packed", TokenType::PACKED},
    {"align", TokenType::ALIGN},
    {"repr", TokenType::REPR},
    {"hidden", TokenType::HIDDEN},
    {"cdecl", TokenType::CDECL},
    {"stdcall", TokenType::STDCALL},
    {"fastcall", TokenType::FASTCALL},
    {"naked", TokenType::NAKED},
    {"comptime", TokenType::COMPTIME},
    {"require", TokenType::REQUIRE},
    {"ensure", TokenType::ENSURE},
    {"invariant", TokenType::INVARIANT},
    {"scope", TokenType::SCOPE},
    {"with", TokenType::WITH},
    {"is", TokenType::IS},
    {"from", TokenType::FROM},
    // Algebraic effects keywords
    {"effect", TokenType::EFFECT},
    {"handle", TokenType::HANDLE},
    {"perform", TokenType::PERFORM},
    {"resume", TokenType::RESUME},
    // Type classes / concepts keywords
    {"concept", TokenType::CONCEPT},
    {"where", TokenType::WHERE}
};

Lexer::Lexer(const std::string& src, const std::string& fname)
    : source(src), filename(fname) {
    indentStack.push(0);
}

char Lexer::advance() {
    char c = source[current++];
    if (c == '\n') {
        line++;
        column = 1;
        lineStart = current;
    } else {
        column++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source[current] != expected) return false;
    advance();
    return true;
}

void Lexer::addToken(TokenType type) {
    std::string text = source.substr(start, current - start);
    tokens.emplace_back(type, text, SourceLocation{filename, line, (int)(start - lineStart + 1)});
}

void Lexer::addToken(TokenType type, int64_t value) {
    std::string text = source.substr(start, current - start);
    tokens.emplace_back(type, text, SourceLocation{filename, line, (int)(start - lineStart + 1)}, value);
}

void Lexer::addToken(TokenType type, double value) {
    std::string text = source.substr(start, current - start);
    tokens.emplace_back(type, text, SourceLocation{filename, line, (int)(start - lineStart + 1)}, value);
}

void Lexer::addToken(TokenType type, const std::string& value) {
    std::string text = source.substr(start, current - start);
    tokens.emplace_back(type, text, SourceLocation{filename, line, (int)(start - lineStart + 1)}, value);
}

} // namespace tyl
