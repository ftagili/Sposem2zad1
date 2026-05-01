#include "../ast/ast.h"
#include "../../build/parser.tab.h"
#include <stdio.h>

extern int parse_error;
extern int yyparse(void);

int main() {
  printf("Parser started. Enter input (Ctrl+D to exit):\n");
  yyparse();
  return parse_error;
}
