#include "ast.h"
#include <stdlib.h>
#include <string.h>

static ASTNode *g_root = NULL;

static char *dup_cstr(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *d = (char *)malloc(n);
  if (d)
    memcpy(d, s, n);
  return d;
}

ASTNode *ast_create_node(const char *label) {
  ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
  if (!n)
    return NULL;
  n->label = dup_cstr(label ? label : "");
  n->children = NULL;
  n->numChildren = 0;
  n->capacity = 0;
  return n;
}

ASTNode *ast_create_leaf_token(const char *kind, const char *lexeme) {
  size_t kind_len = kind ? strlen(kind) : 0;
  size_t lex_len = lexeme ? strlen(lexeme) : 0;
  size_t total = kind_len + 1 + lex_len + 1; /* kind:lexeme + NUL */
  char *buf = (char *)malloc(total);
  if (!buf)
    return NULL;
  buf[0] = '\0';
  if (kind_len)
    strcat(buf, kind);
  strcat(buf, ":");
  if (lex_len)
    strcat(buf, lexeme);
  ASTNode *n = ast_create_node(buf);
  free(buf);
  return n;
}

void ast_add_child(ASTNode *parent, ASTNode *child) {
  if (!parent || !child)
    return;
  if (parent->numChildren == parent->capacity) {
    int newcap = parent->capacity == 0 ? 4 : parent->capacity * 2;
    ASTNode **nc = (ASTNode **)realloc(parent->children,
                                       (size_t)newcap * sizeof(ASTNode *));
    if (!nc)
      return;
    parent->children = nc;
    parent->capacity = newcap;
  }
  parent->children[parent->numChildren++] = child;
}

static void ast_free_rec(ASTNode *n) {
  if (!n)
    return;
  for (int i = 0; i < n->numChildren; ++i) {
    ast_free_rec(n->children[i]);
  }
  free(n->children);
  free(n->label);
  free(n);
}

void ast_free(ASTNode *node) { ast_free_rec(node); }

/* экспорт в dot формат */
typedef struct {
  const ASTNode *node;
  int id;
} NodeIdEntry;

static void print_dot_rec(FILE *out, const ASTNode *n, int *nextId) {
  if (!n)
    return;
  int myId = (*nextId)++;
  /* минимальное экранирование кавычек в метке */
  fprintf(out, "  n%d [label=\"", myId);
  for (const char *p = n->label ? n->label : ""; *p; ++p) {
    if (*p == '"' || *p == '\\')
      fputc('\\', out);
    fputc(*p, out);
  }
  fprintf(out, "\"];\n");
  for (int i = 0; i < n->numChildren; ++i) {
    int childId = *nextId;
    print_dot_rec(out, n->children[i], nextId);
    fprintf(out, "  n%d -> n%d;\n", myId, childId);
  }
}

void ast_print_dot(FILE *out, const ASTNode *root) {
  if (!out)
    return;
  fprintf(out, "digraph AST {\n");
  fprintf(out, "  node [shape=box, fontname=Helvetica];\n");
  int nextId = 0;
  print_dot_rec(out, root, &nextId);
  fprintf(out, "}\n");
}

void ast_set_root(ASTNode *root) { g_root = root; }
ASTNode *ast_get_root(void) { return g_root; }
