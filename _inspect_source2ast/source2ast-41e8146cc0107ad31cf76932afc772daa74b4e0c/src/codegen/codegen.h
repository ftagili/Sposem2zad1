#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration (реальный тип объявлен в ast.h)
typedef struct ASTNode ASTNode;

/**
 * Генерирует GNU as (GAS) asm для s390x из AST.
 *
 * out  — поток вывода (например, fopen("out.s","wb"))
 * root — корень AST (обычно ast_get_root())
 *
 * Возвращает 1 при успехе, 0 при ошибке.
 */
int codegen_s390x_from_ast(FILE *out, const ASTNode *root);

#ifdef __cplusplus
}
#endif
