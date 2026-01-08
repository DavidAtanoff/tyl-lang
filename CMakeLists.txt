cmake_minimum_required(VERSION 3.16)
project(tyl VERSION 1.0.0 LANGUAGES CXX)

# Use C++17 for VS 2022 Insiders compatibility
# VS 18 Insiders has bugs with C++20 ranges/concepts in STL headers
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX- /Zc:__cplusplus /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Release optimizations for MinGW
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "")
        add_compile_options(-O2 -DNDEBUG)
        add_link_options(-s)  # Strip symbols
    endif()
endif()

# Source files - Modular structure
set(FRONTEND_SOURCES
    # AST
    src/frontend/ast/ast.cpp
    # Syntax macros
    src/frontend/macro/syntax_macro.cpp
    # Modular parser - Core
    src/frontend/parser/parser_core.cpp
    src/frontend/parser/parser_types.cpp
    # Modular parser - Declarations
    src/frontend/parser/parser_declarations.cpp
    src/frontend/parser/parser_decl_traits.cpp
    # Modular parser - Statements
    src/frontend/parser/parser_statements.cpp
    # Modular parser - Expressions (Pratt parser)
    src/frontend/parser/parser_expressions.cpp
    # Modular lexer
    src/frontend/lexer/lexer_core.cpp
    src/frontend/lexer/lexer_scan.cpp
)

set(SEMANTIC_SOURCES
    # Types
    src/semantic/types/types.cpp
    # Symbols
    src/semantic/symbols/symbol_table.cpp
    # Ownership system
    src/semantic/ownership/ownership.cpp
    # Module system
    src/semantic/module/module_system.cpp
    # Generics / Monomorphization
    src/semantic/generics/monomorphizer.cpp
    src/semantic/generics/ast_cloner.cpp
    # CTFE (Compile-Time Function Evaluation)
    src/semantic/ctfe/ctfe_interpreter.cpp
    # Modular type checker
    src/semantic/checker/checker_core.cpp
    src/semantic/checker/checker_expr.cpp
    src/semantic/checker/checker_stmt.cpp
    src/semantic/checker/checker_decl.cpp
    # Modular macro expander
    src/semantic/expander/expander_core.cpp
    src/semantic/expander/expander_expand.cpp
    src/semantic/expander/expander_clone.cpp
    # Optimizer passes (Tier 2-5)
    src/semantic/optimizer/optimizer.cpp
    src/semantic/optimizer/constant_folding.cpp
    src/semantic/optimizer/constant_propagation.cpp
    src/semantic/optimizer/dead_code.cpp
    src/semantic/optimizer/inlining.cpp
    src/semantic/optimizer/tail_call.cpp
    src/semantic/optimizer/ctfe.cpp
    src/semantic/optimizer/ssa.cpp
    src/semantic/optimizer/loop_optimizer.cpp
    src/semantic/optimizer/instruction_scheduler.cpp
    src/semantic/optimizer/cse.cpp
    src/semantic/optimizer/gvn.cpp
    src/semantic/optimizer/algebraic.cpp
    src/semantic/optimizer/pgo.cpp
)

set(CLI_SOURCES
    src/cli/ast_printer.cpp
)

set(BACKEND_SOURCES
    # x64 native code generation - Core
    src/backend/x64/x64_assembler.cpp
    src/backend/x64/pe_generator.cpp
    src/backend/x64/peephole.cpp
    # Object file format
    src/backend/object/object_file.cpp
    # Garbage collection
    src/backend/gc/gc.cpp
    src/backend/gc/allocator.cpp
    # Modular native codegen - Core
    src/backend/codegen/core/codegen_compile.cpp
    src/backend/codegen/core/codegen_helpers.cpp
    src/backend/codegen/core/codegen_print.cpp
    src/backend/codegen/core/codegen_const_eval.cpp
    src/backend/codegen/core/codegen_type_utils.cpp
    src/backend/codegen/core/codegen_record_layout.cpp
    # Modular native codegen - Expressions
    src/backend/codegen/expr/codegen_expr_literals.cpp
    src/backend/codegen/expr/codegen_expr_binary.cpp
    src/backend/codegen/expr/codegen_expr_collections.cpp
    src/backend/codegen/expr/codegen_expr_misc.cpp
    src/backend/codegen/expr/codegen_expr_assign.cpp
    src/backend/codegen/expr/codegen_expr_lambda.cpp
    src/backend/codegen/expr/codegen_expr_pointer.cpp
    src/backend/codegen/expr/codegen_expr_async.cpp
    src/backend/codegen/expr/codegen_expr_channel.cpp
    src/backend/codegen/expr/codegen_expr_sync.cpp
    src/backend/codegen/expr/codegen_expr_advanced_concurrency.cpp
    src/backend/codegen/expr/codegen_expr_index.cpp
    src/backend/codegen/expr/codegen_expr_list.cpp
    src/backend/codegen/expr/codegen_expr_record.cpp
    src/backend/codegen/expr/codegen_expr_smart_ptr.cpp
    # Modular native codegen - Calls
    src/backend/codegen/call/codegen_call_core.cpp
    src/backend/codegen/call/codegen_call_dispatch.cpp
    src/backend/codegen/call/codegen_call_builtins_string.cpp
    src/backend/codegen/call/codegen_call_builtins_io.cpp
    src/backend/codegen/call/codegen_call_builtins_math.cpp
    src/backend/codegen/call/codegen_call_builtins_conv.cpp
    src/backend/codegen/call/codegen_call_builtins_system.cpp
    src/backend/codegen/call/codegen_call_builtins_list.cpp
    src/backend/codegen/call/codegen_call_builtins_result.cpp
    src/backend/codegen/call/codegen_call_builtins_memory.cpp
    src/backend/codegen/call/codegen_call_builtins_gc.cpp
    # Extended builtins
    src/backend/codegen/call/codegen_call_builtins_string_ext.cpp
    src/backend/codegen/call/codegen_call_builtins_math_ext.cpp
    src/backend/codegen/call/codegen_call_builtins_list_ext.cpp
    src/backend/codegen/call/codegen_call_builtins_time_ext.cpp
    src/backend/codegen/call/codegen_call_builtins_system_ext.cpp
    src/backend/codegen/call/codegen_call_builtins_complex.cpp
    src/backend/codegen/call/codegen_call_builtins_numeric.cpp
    # Modular native codegen - Statements
    src/backend/codegen/stmt/codegen_stmt_control.cpp
    src/backend/codegen/stmt/codegen_stmt_vars.cpp
    src/backend/codegen/stmt/codegen_stmt_misc.cpp
    src/backend/codegen/stmt/codegen_stmt_vardecl.cpp
    src/backend/codegen/stmt/codegen_stmt_assign.cpp
    # Modular native codegen - Declarations
    src/backend/codegen/decl/codegen_decl_fn.cpp
    src/backend/codegen/decl/codegen_decl_types.cpp
    src/backend/codegen/decl/codegen_decl_program.cpp
    # Modular native codegen - Support
    src/backend/codegen/codegen_builtins.cpp
    src/backend/codegen/codegen_gc.cpp
    src/backend/codegen/codegen_traits.cpp
    src/backend/codegen/codegen_ffi.cpp
    src/backend/codegen/register_allocator.cpp
    src/backend/codegen/global_register_allocator.cpp
    src/backend/codegen/vectorizer.cpp
    # Modular linker
    src/backend/linker/linker_core.cpp
    src/backend/linker/linker_symbols.cpp
    src/backend/linker/linker_layout.cpp
    src/backend/linker/linker_output.cpp
    src/backend/linker/linker_dll.cpp
)

set(ALL_SOURCES
    ${FRONTEND_SOURCES}
    ${SEMANTIC_SOURCES}
    ${BACKEND_SOURCES}
    ${CLI_SOURCES}
    src/main.cpp
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    # Frontend
    ${CMAKE_SOURCE_DIR}/src/frontend
    ${CMAKE_SOURCE_DIR}/src/frontend/ast
    ${CMAKE_SOURCE_DIR}/src/frontend/token
    ${CMAKE_SOURCE_DIR}/src/frontend/parser
    ${CMAKE_SOURCE_DIR}/src/frontend/lexer
    ${CMAKE_SOURCE_DIR}/src/frontend/macro
    # Semantic
    ${CMAKE_SOURCE_DIR}/src/semantic
    ${CMAKE_SOURCE_DIR}/src/semantic/types
    ${CMAKE_SOURCE_DIR}/src/semantic/symbols
    ${CMAKE_SOURCE_DIR}/src/semantic/checker
    ${CMAKE_SOURCE_DIR}/src/semantic/expander
    ${CMAKE_SOURCE_DIR}/src/semantic/module
    ${CMAKE_SOURCE_DIR}/src/semantic/optimizer
    ${CMAKE_SOURCE_DIR}/src/semantic/generics
    ${CMAKE_SOURCE_DIR}/src/semantic/ownership
    ${CMAKE_SOURCE_DIR}/src/semantic/ctfe
    # Backend
    ${CMAKE_SOURCE_DIR}/src/backend
    ${CMAKE_SOURCE_DIR}/src/backend/x64
    ${CMAKE_SOURCE_DIR}/src/backend/object
    ${CMAKE_SOURCE_DIR}/src/backend/runtime
    ${CMAKE_SOURCE_DIR}/src/backend/gc
    ${CMAKE_SOURCE_DIR}/src/backend/codegen
    ${CMAKE_SOURCE_DIR}/src/backend/codegen/core
    ${CMAKE_SOURCE_DIR}/src/backend/codegen/expr
    ${CMAKE_SOURCE_DIR}/src/backend/codegen/call
    ${CMAKE_SOURCE_DIR}/src/backend/codegen/stmt
    ${CMAKE_SOURCE_DIR}/src/backend/codegen/decl
    ${CMAKE_SOURCE_DIR}/src/backend/linker
    # CLI
    ${CMAKE_SOURCE_DIR}/src/cli
)

# Main executable
add_executable(tyl ${ALL_SOURCES})

# Installation
install(TARGETS tyl DESTINATION bin)

# Tests (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    # Add test targets here
endif()
