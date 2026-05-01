cmake_minimum_required(VERSION 3.15)
project(source2ast C)

# --- стандарт C ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- опции ---
option(BUILD_SEMANTIC "Build semantic analyzer" ON)
option(BUILD_CFG "Build CFG tool" ON)
option(BUILD_CODEGEN "Build linear code generator" ON)

# --- ищем bison / flex ---
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# --- bison: parser.y -> parser.tab.c / parser.tab.h ---
BISON_TARGET(Parser
    ${CMAKE_SOURCE_DIR}/src/parser/parser.y
    ${CMAKE_BINARY_DIR}/parser.tab.c
    DEFINES_FILE ${CMAKE_BINARY_DIR}/parser.tab.h
)

# --- flex: lexer.l -> lex.yy.c ---
FLEX_TARGET(Lexer
    ${CMAKE_SOURCE_DIR}/src/lexer/lexer.l
    ${CMAKE_BINARY_DIR}/lex.yy.c
)

# обеспечить правильный порядок генерации
ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# --- общие include-пути ---
include_directories(
    ${CMAKE_SOURCE_DIR}/src/ast
    ${CMAKE_SOURCE_DIR}/src/cfg
    ${CMAKE_BINARY_DIR}       # здесь лежат parser.tab.h и lex.yy.c
)

# --- флаги компилятора / дефайны ---

# GCC
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wno-sign-compare
        -Wno-unused-function
    )
endif()

# общие define'ы
add_compile_definitions(
    _POSIX_C_SOURCE=200809L
    _GNU_SOURCE
)

# _WIN32 для windows
if (WIN32)
    add_compile_definitions(_WIN32)
endif()

# --- библиотека AST ---
add_library(ast STATIC
    src/ast/ast.c
)

target_include_directories(ast PUBLIC
    ${CMAKE_SOURCE_DIR}/src/ast
    ${CMAKE_BINARY_DIR}
)

# --- исполняемый файл parser ---
add_executable(parser
    src/parser/main.c
    ${BISON_Parser_OUTPUT_SOURCE}
    ${FLEX_Lexer_OUTPUTS}
)

target_link_libraries(parser PRIVATE ast)

# --- исполняемый файл semantic ---
if (BUILD_SEMANTIC)
    add_executable(semantic
        src/semantic/main.c
        src/semantic/analyzer.c
        ${BISON_Parser_OUTPUT_SOURCE}
        ${FLEX_Lexer_OUTPUTS}
    )
    target_link_libraries(semantic PRIVATE ast)
endif()

# --- исполняемый файл cfg ---
if (BUILD_CFG)
    add_executable(cfg
        src/cfg/main.c
        src/cfg/cfg.c
        ${BISON_Parser_OUTPUT_SOURCE}
        ${FLEX_Lexer_OUTPUTS}
    )
    target_link_libraries(cfg PRIVATE ast)
endif()

# --- исполняемый файл codegen (linear code generator) ---
if (BUILD_CODEGEN)
    add_executable(codegen
        src/codegen/main.c
        src/codegen/codegen.c
        src/cfg/cfg.c
        ${BISON_Parser_OUTPUT_SOURCE}
        ${FLEX_Lexer_OUTPUTS}
    )
    target_include_directories(codegen PRIVATE
        ${CMAKE_SOURCE_DIR}/src/codegen
    )
    target_link_libraries(codegen PRIVATE ast)
endif()

# --- target для очистки директорий build и output ---
add_custom_target(clear
    COMMAND find ${CMAKE_BINARY_DIR} -mindepth 1 -delete
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/output
    COMMENT "Cleaning build and output directories"
)
