#ifndef AST_AST_H
#define AST_AST_H

#include <stdio.h>

typedef struct ASTNode ASTNode;

struct ASTNode {
    char *label;           /* человекопонятная метка для узла */
    ASTNode **children;    /* динамический массив указателей на дочерние узлы */
    int numChildren;       /* количество дочерних узлов в данный момент */
    int capacity;          /* выделенная емкость массива дочерних узлов */
};

ASTNode *ast_create_node(const char *label);
void ast_add_child(ASTNode *parent, ASTNode *child);
ASTNode *ast_create_leaf_token(const char *kind, const char *lexeme);
void ast_free(ASTNode *node);

/* экспорт в Graphviz .dot формат */
void ast_print_dot(FILE *out, const ASTNode *root);

/* глобальный доступ к корню, устанавливаемый парсером */
void ast_set_root(ASTNode *root);
ASTNode *ast_get_root(void);

#endif /* AST_AST_H */


