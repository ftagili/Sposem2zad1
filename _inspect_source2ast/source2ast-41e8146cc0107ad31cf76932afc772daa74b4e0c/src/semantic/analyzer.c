#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* определения для кроссплатформенности */
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

#include "../ast/ast.h"
#include "../../build/parser.tab.h"

extern FILE *yyin;
extern int yyparse(void);
extern ASTNode *ast_get_root(void);

static int parse_stream(FILE *in, const char *vname) {
  (void)vname; /* подавляем предупреждение о неиспользуемом параметре */
  yyin = in;

  /* yyparse должен возвращать 0 при успехе */
  int rc = yyparse();
  return rc != 0; /* 0 => успех, 1 => синтаксическая ошибка */
}

int analyze_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return 1;
  }

  /* устанавливаем бинарный режим (для windows) */
#ifdef _WIN32
  _setmode(_fileno(f), _O_BINARY);
#endif

  int err = parse_stream(f, path);
  fclose(f);

  return err;
}

int analyze_file_to_dot(const char *input_path, const char *dot_output_path) {
  errno = 0; /* сбрасываем errno перед вызовом */
  FILE *f = fopen(input_path, "rb");
  if (!f) {
    /* errno установлен системой при ошибке fopen */
    return 1; /* ошибка открытия входного файла */
  }
#ifdef _WIN32
  _setmode(_fileno(f), _O_BINARY);
#endif
  int err = parse_stream(f, input_path);
  fclose(f);
  if (err != 0) {
    return 2; /* синтаксическая ошибка */
  }
  ASTNode *root = ast_get_root();
  if (!root) {
    return 3; /* нет AST root */
  }
  errno = 0; /* сбрасываем errno перед вызовом */
  FILE *out = fopen(dot_output_path, "wb");
  if (!out) {
    /* errno установлен системой при ошибке fopen */
    return 4; /* ошибка открытия выходного файла */
  }
  ast_print_dot(out, root);
  fclose(out);
  return 0;
}

int analyze_string(const char *text, const char *virtual_name) {
  /* кроссплатформенная обработка строк */
#if defined(__unix__) || defined(__APPLE__) ||                                 \
    (defined(_WIN32) && defined(__MINGW32__))
  /* используем fmemopen на nix-системах и mingw */
  FILE *mem = fmemopen((void *)text, strlen(text), "rb");
  if (!mem) {
    fprintf(stderr, "fatal: fmemopen failed\n");
    return 1;
  }
  int err = parse_stream(mem, virtual_name ? virtual_name : "<input>");
  fclose(mem);
  return err;
#else
  /* портативный резервный вариант для windows msvc и других систем */
  char tmpname[L_tmpnam];
  FILE *tmp = NULL;

  /* безопасное создание временного файла */
#ifdef _WIN32
  char temp_path[MAX_PATH];
  if (GetTempPath(MAX_PATH, temp_path) == 0) {
    fprintf(stderr, "fatal: cannot get temp path\n");
    return 1;
  }
  if (GetTempFileName(temp_path, "ast", 0, tmpname) == 0) {
    fprintf(stderr, "fatal: cannot create temp file\n");
    return 1;
  }
#else
  tmpnam(tmpname);
#endif

  tmp = fopen(tmpname, "wb");
  if (!tmp) {
    fprintf(stderr, "fatal: cannot create temp file\n");
    return 1;
  }

  /* записываем данные в бинарном режиме */
  size_t len = strlen(text);
  if (fwrite(text, 1, len, tmp) != len) {
    fprintf(stderr, "fatal: write error\n");
    fclose(tmp);
    remove(tmpname);
    return 1;
  }

  fclose(tmp);
  tmp = fopen(tmpname, "rb");
  if (!tmp) {
    fprintf(stderr, "fatal: cannot reopen temp file\n");
    remove(tmpname);
    return 1;
  }

  int err = parse_stream(tmp, virtual_name ? virtual_name : "<input>");
  fclose(tmp);
  remove(tmpname);
  return err;
#endif
}
