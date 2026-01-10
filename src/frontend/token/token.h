// Tyl Compiler - Token definitions
#ifndef TYL_TOKEN_H
#define TYL_TOKEN_H

#include "common/common.h"

namespace tyl {

enum class TokenType {
    INTEGER, FLOAT, STRING, CHAR, BYTE_STRING, RAW_BYTE_STRING, IDENTIFIER,
    LIFETIME,  // Lifetime annotation: 'a, 'static, etc.
    FN, IF, ELSE, ELIF, FOR, WHILE, MATCH, RETURN,
    TRUE, FALSE, NIL, AND, OR, NOT, IN, TO, BY,
    TRY, ELSE_KW, USE, LAYER, MACRO, IMPORT, MODULE,
    EXTERN, ASYNC, AWAIT, SPAWN, RECORD, ENUM, UNION,
    LET, MUT, CONST, VAR,
    UNSAFE, PTR, REF, NEW, DELETE, ASM,
    BREAK, CONTINUE,
    TYPE, ALIAS, SYNTAX,
    PUB, PRIV, SELF, SUPER, TRAIT, IMPL,
    CHAN,  // Channel keyword for chan[T] type
    MUTEX,  // Mutex keyword for Mutex[T] type
    RWLOCK,  // RWLock keyword for RWLock[T] type
    COND,  // Cond keyword for condition variable type
    SEMAPHORE,  // Semaphore keyword for semaphore type
    LOCK,  // lock keyword for scoped lock acquisition
    ATOMIC,  // Atomic keyword for Atomic[T] type
    // Smart pointer keywords
    BOX,        // Box[T] - unique ownership heap allocation
    RC,         // Rc[T] - reference counted (single-threaded)
    ARC,        // Arc[T] - atomic reference counted (thread-safe)
    WEAK_PTR,   // Weak[T] - weak reference (non-owning)
    CELL,       // Cell[T] - interior mutability (single-threaded)
    REFCELL,    // RefCell[T] - runtime borrow checking
    // New syntax redesign tokens
    LOOP,           // loop keyword for infinite loops
    UNLESS,         // unless keyword (alias for if not)
    UNDERSCORE,     // _ placeholder for lambdas
    DOTDOT_EQ,      // ..= inclusive range
    QUESTION_DOT,   // ?. safe navigation
    EXPORT,         // export keyword attribute
    INLINE,         // inline keyword attribute
    NOINLINE,       // noinline keyword attribute
    PACKED,         // packed keyword attribute
    ALIGN,          // align keyword attribute
    REPR,           // repr keyword attribute
    HIDDEN,         // hidden keyword attribute
    WEAK,           // weak keyword attribute
    CDECL,          // cdecl calling convention
    STDCALL,        // stdcall calling convention
    FASTCALL,       // fastcall calling convention
    NAKED,          // naked function attribute
    COMPTIME,       // comptime keyword for compile-time execution
    ASSERT,         // assert keyword for compile-time assertions
    REQUIRE,        // require keyword for contracts
    ENSURE,         // ensure keyword for contracts
    INVARIANT,      // invariant keyword for contracts
    SCOPE,          // scope keyword for structured concurrency
    WITH,           // with keyword for resource management
    IS,             // is keyword for type checking
    FROM,           // from keyword for imports
    EFFECT,         // effect keyword for algebraic effects
    HANDLE,         // handle keyword for effect handlers
    PERFORM,        // perform keyword for effect operations
    RESUME,         // resume keyword for continuing from handlers
    CONCEPT,        // concept keyword for type classes/constraints
    WHERE,          // where keyword for constraint clauses
    // New syntax enhancements
    END,            // end keyword for Lua-style block termination
    THEN,           // then keyword for if-then-end style
    DO,             // do keyword for while-do-end style
    WALRUS,         // := walrus operator for assignment expressions
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ, NE, LT, GT, LE, GE,
    ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN, PERCENT_ASSIGN,
    DOT, DOTDOT, ARROW, DOUBLE_ARROW,
    AMP, PIPE, CARET, TILDE,
    AMP_AMP, PIPE_PIPE,
    QUESTION, BANG, AT, DOUBLE_COLON, PIPE_GT, QUESTION_QUESTION, DOLLAR, SPACESHIP,
    COLON, COMMA, SEMICOLON, LPAREN, RPAREN,
    LBRACKET, RBRACKET, LBRACE, RBRACE,
    NEWLINE, INDENT, DEDENT,
    CUSTOM_OP,
    ATTRIBUTE,  // #[...] attribute
    CHAN_SEND,  // <- for channel send (ch <- value)
    CHAN_RECV,  // <- for channel receive (<- ch)
    END_OF_FILE, ERROR
};

inline std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::INTEGER: return "INTEGER"; case TokenType::FLOAT: return "FLOAT";
        case TokenType::STRING: return "STRING"; case TokenType::CHAR: return "CHAR";
        case TokenType::BYTE_STRING: return "BYTE_STRING"; case TokenType::RAW_BYTE_STRING: return "RAW_BYTE_STRING";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::LIFETIME: return "LIFETIME";
        case TokenType::FN: return "FN"; case TokenType::IF: return "IF"; case TokenType::ELSE: return "ELSE";
        case TokenType::ELIF: return "ELIF"; case TokenType::FOR: return "FOR"; case TokenType::WHILE: return "WHILE";
        case TokenType::MATCH: return "MATCH"; case TokenType::RETURN: return "RETURN";
        case TokenType::TRUE: return "TRUE"; case TokenType::FALSE: return "FALSE"; case TokenType::NIL: return "NIL";
        case TokenType::AND: return "AND"; case TokenType::OR: return "OR"; case TokenType::NOT: return "NOT";
        case TokenType::IN: return "IN"; case TokenType::TO: return "TO"; case TokenType::BY: return "BY";
        case TokenType::TRY: return "TRY"; case TokenType::ELSE_KW: return "ELSE_KW"; case TokenType::USE: return "USE";
        case TokenType::LAYER: return "LAYER"; case TokenType::MACRO: return "MACRO"; case TokenType::IMPORT: return "IMPORT";
        case TokenType::MODULE: return "MODULE";
        case TokenType::EXTERN: return "EXTERN"; case TokenType::ASYNC: return "ASYNC"; case TokenType::AWAIT: return "AWAIT";
        case TokenType::SPAWN: return "SPAWN"; case TokenType::RECORD: return "RECORD"; case TokenType::ENUM: return "ENUM"; case TokenType::UNION: return "UNION";
        case TokenType::LET: return "LET"; case TokenType::MUT: return "MUT"; case TokenType::CONST: return "CONST";
        case TokenType::VAR: return "VAR"; case TokenType::UNSAFE: return "UNSAFE"; case TokenType::PTR: return "PTR";
        case TokenType::REF: return "REF"; case TokenType::NEW: return "NEW"; case TokenType::DELETE: return "DELETE";
        case TokenType::ASM: return "ASM";
        case TokenType::BREAK: return "BREAK"; case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::TYPE: return "TYPE"; case TokenType::ALIAS: return "ALIAS"; case TokenType::SYNTAX: return "SYNTAX";
        case TokenType::PUB: return "PUB"; case TokenType::PRIV: return "PRIV"; case TokenType::SELF: return "SELF";
        case TokenType::SUPER: return "SUPER"; case TokenType::TRAIT: return "TRAIT"; case TokenType::IMPL: return "IMPL";
        case TokenType::CHAN: return "CHAN";
        case TokenType::MUTEX: return "MUTEX";
        case TokenType::RWLOCK: return "RWLOCK";
        case TokenType::COND: return "COND";
        case TokenType::SEMAPHORE: return "SEMAPHORE";
        case TokenType::LOCK: return "LOCK";
        case TokenType::ATOMIC: return "ATOMIC";
        // Smart pointer tokens
        case TokenType::BOX: return "BOX";
        case TokenType::RC: return "RC";
        case TokenType::ARC: return "ARC";
        case TokenType::WEAK_PTR: return "WEAK_PTR";
        case TokenType::CELL: return "CELL";
        case TokenType::REFCELL: return "REFCELL";
        // New syntax redesign tokens
        case TokenType::LOOP: return "LOOP";
        case TokenType::UNLESS: return "UNLESS";
        case TokenType::UNDERSCORE: return "UNDERSCORE";
        case TokenType::DOTDOT_EQ: return "DOTDOT_EQ";
        case TokenType::QUESTION_DOT: return "QUESTION_DOT";
        case TokenType::EXPORT: return "EXPORT";
        case TokenType::INLINE: return "INLINE";
        case TokenType::NOINLINE: return "NOINLINE";
        case TokenType::PACKED: return "PACKED";
        case TokenType::ALIGN: return "ALIGN";
        case TokenType::REPR: return "REPR";
        case TokenType::HIDDEN: return "HIDDEN";
        case TokenType::WEAK: return "WEAK";
        case TokenType::CDECL: return "CDECL";
        case TokenType::STDCALL: return "STDCALL";
        case TokenType::FASTCALL: return "FASTCALL";
        case TokenType::NAKED: return "NAKED";
        case TokenType::COMPTIME: return "COMPTIME";
        case TokenType::ASSERT: return "ASSERT";
        case TokenType::REQUIRE: return "REQUIRE";
        case TokenType::ENSURE: return "ENSURE";
        case TokenType::INVARIANT: return "INVARIANT";
        case TokenType::SCOPE: return "SCOPE";
        case TokenType::WITH: return "WITH";
        case TokenType::IS: return "IS";
        case TokenType::FROM: return "FROM";
        case TokenType::EFFECT: return "EFFECT";
        case TokenType::HANDLE: return "HANDLE";
        case TokenType::PERFORM: return "PERFORM";
        case TokenType::RESUME: return "RESUME";
        case TokenType::CONCEPT: return "CONCEPT";
        case TokenType::WHERE: return "WHERE";
        // New syntax enhancements
        case TokenType::END: return "END";
        case TokenType::THEN: return "THEN";
        case TokenType::DO: return "DO";
        case TokenType::WALRUS: return "WALRUS";
        case TokenType::PERCENT_ASSIGN: return "PERCENT_ASSIGN";
        case TokenType::PLUS: return "PLUS"; case TokenType::MINUS: return "MINUS"; case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH"; case TokenType::PERCENT: return "PERCENT";
        case TokenType::EQ: return "EQ"; case TokenType::NE: return "NE"; case TokenType::LT: return "LT";
        case TokenType::GT: return "GT"; case TokenType::LE: return "LE"; case TokenType::GE: return "GE";
        case TokenType::ASSIGN: return "ASSIGN"; case TokenType::PLUS_ASSIGN: return "PLUS_ASSIGN";
        case TokenType::MINUS_ASSIGN: return "MINUS_ASSIGN"; case TokenType::STAR_ASSIGN: return "STAR_ASSIGN";
        case TokenType::SLASH_ASSIGN: return "SLASH_ASSIGN"; case TokenType::DOT: return "DOT";
        case TokenType::DOTDOT: return "DOTDOT"; case TokenType::ARROW: return "ARROW";
        case TokenType::DOUBLE_ARROW: return "DOUBLE_ARROW"; case TokenType::AMP: return "AMP";
        case TokenType::PIPE: return "PIPE"; case TokenType::CARET: return "CARET"; case TokenType::TILDE: return "TILDE";
        case TokenType::AMP_AMP: return "AMP_AMP"; case TokenType::PIPE_PIPE: return "PIPE_PIPE";
        case TokenType::QUESTION: return "QUESTION"; case TokenType::BANG: return "BANG"; case TokenType::AT: return "AT";
        case TokenType::DOUBLE_COLON: return "DOUBLE_COLON"; case TokenType::PIPE_GT: return "PIPE_GT";
        case TokenType::QUESTION_QUESTION: return "QUESTION_QUESTION"; case TokenType::DOLLAR: return "DOLLAR";
        case TokenType::SPACESHIP: return "SPACESHIP"; case TokenType::COLON: return "COLON";
        case TokenType::COMMA: return "COMMA"; case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::LPAREN: return "LPAREN"; case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACKET: return "LBRACKET"; case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::LBRACE: return "LBRACE"; case TokenType::RBRACE: return "RBRACE";
        case TokenType::NEWLINE: return "NEWLINE"; case TokenType::INDENT: return "INDENT";
        case TokenType::DEDENT: return "DEDENT"; case TokenType::CUSTOM_OP: return "CUSTOM_OP";
        case TokenType::ATTRIBUTE: return "ATTRIBUTE";
        case TokenType::CHAN_SEND: return "CHAN_SEND";
        case TokenType::CHAN_RECV: return "CHAN_RECV";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::ERROR: return "ERROR"; default: return "UNKNOWN";
    }
}

class Token {
public:
    TokenType type;
    std::string lexeme;
    SourceLocation location;
    std::variant<std::monostate, int64_t, double, std::string> literal;
    
    Token(TokenType t, std::string lex, SourceLocation loc) : type(t), lexeme(std::move(lex)), location(loc) {}
    Token(TokenType t, std::string lex, SourceLocation loc, int64_t val) : type(t), lexeme(std::move(lex)), location(loc), literal(val) {}
    Token(TokenType t, std::string lex, SourceLocation loc, double val) : type(t), lexeme(std::move(lex)), location(loc), literal(val) {}
    Token(TokenType t, std::string lex, SourceLocation loc, std::string val) : type(t), lexeme(std::move(lex)), location(loc), literal(std::move(val)) {}
    
    std::string toString() const { return tokenTypeToString(type) + " '" + lexeme + "' at " + location.toString(); }
};

} // namespace tyl

#endif // TYL_TOKEN_H
