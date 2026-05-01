#include "../ast/ast.h"
#include "cfg.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Parser generated in build directory */
#include "../../build/parser.tab.h"

extern FILE *yyin;
extern int yyparse(void);

/* Extract base filename without extension */
static char *get_base_filename(const char *path) {
  const char *base = strrchr(path, '/');
  if (!base)
    base = path;
  else
    base++;

  const char *dot = strrchr(base, '.');
  size_t len = dot ? (size_t)(dot - base) : strlen(base);
  char *result = (char *)malloc(len + 1);
  if (result) {
    memcpy(result, base, len);
    result[len] = '\0';
  }
  return result;
}

/* Create output directory if it doesn't exist */
static int ensure_output_dir(const char *dir) {
  if (!dir)
    return 1;

  struct stat st;
  if (stat(dir, &st) == 0) {
    return S_ISDIR(st.st_mode) ? 0 : 1;
  }

  /* Try to create directory */
#ifdef _WIN32
  return mkdir(dir) == 0 ? 0 : 1;
#else
  return mkdir(dir, 0755) == 0 ? 0 : 1;
#endif
}

/* Build output file path */
static char *build_output_path(const char *output_dir, const char *base_name,
                               const char *func_name, const char *suffix) {
  size_t dir_len = output_dir ? strlen(output_dir) : 0;
  size_t base_len = strlen(base_name);
  size_t func_len = func_name ? strlen(func_name) : 0;
  size_t suffix_len = strlen(suffix);

  size_t total = dir_len + 1 + base_len + 1 + func_len + suffix_len + 1;
  if (dir_len > 0)
    total += 1; /* for '/' */

  char *path = (char *)malloc(total);
  if (!path)
    return NULL;

  path[0] = '\0';
  if (output_dir && dir_len > 0) {
    strcat(path, output_dir);
    strcat(path, "/");
  }
  strcat(path, base_name);
  if (func_name && func_len > 0) {
    strcat(path, ".");
    strcat(path, func_name);
  }
  strcat(path, suffix);

  return path;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <input-file>... [output-dir]\n", argv[0]);
    fprintf(stderr, "  If output-dir is omitted, DOT files are placed next to "
                    "input files.\n");
    return 1;
  }

  int num_input_files = argc - 1;
  const char *output_dir = NULL;

  /* Check if last argument is output directory */
  if (argc >= 3) {
    /* Check if it's a directory or a file */
    struct stat st;
    if (stat(argv[argc - 1], &st) == 0 && S_ISDIR(st.st_mode)) {
      output_dir = argv[argc - 1];
      num_input_files = argc - 2;
    }
  }

  if (num_input_files < 1) {
    fprintf(stderr, "Error: at least one input file required\n");
    return 1;
  }

  /* Create CFG program */
  CFGProgram *prog = cfg_prog_create();
  if (!prog) {
    fprintf(stderr, "Error: failed to create CFG program\n");
    return 1;
  }

  int parse_errors = 0;

  /* Parse all input files */
  for (int i = 1; i <= num_input_files; i++) {
    const char *input_file = argv[i];

    errno = 0;
    FILE *f = fopen(input_file, "rb");
    if (!f) {
      fprintf(stderr, "Error: cannot open input file '%s': %s\n", input_file,
              strerror(errno));
      parse_errors = 1;
      continue;
    }

    yyin = f;
    int parse_result = yyparse();
    fclose(f);

    if (parse_result != 0) {
      fprintf(stderr, "Error: syntax errors in '%s'\n", input_file);
      parse_errors = 1;
      continue;
    }

    ASTNode *root = ast_get_root();
    if (!root) {
      fprintf(stderr, "Error: no AST root produced for '%s'\n", input_file);
      parse_errors = 1;
      continue;
    }

    /* Add file to program */
    if (!cfg_prog_add_file(prog, input_file, root)) {
      fprintf(stderr, "Error: failed to add file '%s' to program\n",
              input_file);
      parse_errors = 1;
      continue;
    }
  }

  if (parse_errors) {
    cfg_prog_free(prog);
    return 1;
  }

  /* Build CFG for all functions */
  if (!cfg_prog_build(prog)) {
    fprintf(stderr, "Error: failed to build CFG\n");
    cfg_prog_free(prog);
    return 1;
  }

  /* Print errors if any */
  int num_errors = cfg_prog_get_num_errors(prog);
  if (num_errors > 0) {
    for (int i = 0; i < num_errors; i++) {
      CFGError *err = cfg_prog_get_error(prog, i);
      if (err) {
        fprintf(stderr, "Error");
        if (err->source_file)
          fprintf(stderr, " in %s", err->source_file);
        if (err->function_name)
          fprintf(stderr, " (function %s)", err->function_name);
        if (err->line > 0)
          fprintf(stderr, ":%d", err->line);
        fprintf(stderr, ": %s\n",
                err->message ? err->message : "unknown error");
      }
    }
  }

  /* Determine output directory */
  const char *actual_output_dir = output_dir;
  if (!actual_output_dir && num_input_files == 1) {
    /* Single file: use same directory as input */
    const char *input_file = argv[1];
    const char *last_slash = strrchr(input_file, '/');
    if (last_slash) {
      size_t dir_len = (size_t)(last_slash - input_file);
      actual_output_dir = (char *)malloc(dir_len + 1);
      if (actual_output_dir) {
        memcpy((char *)actual_output_dir, input_file, dir_len);
        ((char *)actual_output_dir)[dir_len] = '\0';
      }
    }
  }

  if (actual_output_dir && strcmp(actual_output_dir, "") != 0) {
    if (ensure_output_dir(actual_output_dir) != 0) {
      fprintf(stderr, "Error: cannot create output directory '%s'\n",
              actual_output_dir);
      cfg_prog_free(prog);
      return 1;
    }
  }

  int write_errors = 0;

  /* Write CFG for each function */
  int num_functions = cfg_prog_get_num_functions(prog);
  for (int i = 0; i < num_functions; i++) {
    CFGFunction *func = cfg_prog_get_function(prog, i);
    if (!func)
      continue;

    const char *func_name = cfg_function_get_name(func);
    const char *source_file = cfg_function_get_source_file(func);

    if (!func_name || !source_file)
      continue;

    /* Build output filename */
    char *base_name = get_base_filename(source_file);
    if (!base_name) {
      write_errors = 1;
      continue;
    }

    char *output_path =
        build_output_path(actual_output_dir, base_name, func_name, ".cfg.dot");
    free(base_name);

    if (!output_path) {
      write_errors = 1;
      continue;
    }

    /* Write CFG DOT file */
    errno = 0;
    FILE *out = fopen(output_path, "w");
    if (!out) {
      fprintf(stderr, "Error: cannot write to '%s': %s\n", output_path,
              strerror(errno));
      free(output_path);
      write_errors = 1;
      continue;
    }

    cfg_function_print_dot(out, func, prog);
    fclose(out);
    free(output_path);
  }

  /* Write call graph */
  CallGraph *cg = cfg_prog_get_call_graph(prog);
  if (cg && num_functions > 0) {
    /* Determine call graph filename */
    const char *first_file = num_input_files > 0 ? argv[1] : "program";
    char *base_name = get_base_filename(first_file);
    if (base_name) {
      char *cg_path = build_output_path(actual_output_dir, base_name, NULL,
                                        ".callgraph.dot");
      free(base_name);

      if (cg_path) {
        errno = 0;
        FILE *out = fopen(cg_path, "w");
        if (!out) {
          fprintf(stderr, "Error: cannot write to '%s': %s\n", cg_path,
                  strerror(errno));
          write_errors = 1;
        } else {
          cfg_call_graph_print_dot(out, cg, prog);
          fclose(out);
        }
        free(cg_path);
      }
    }
  }

  cfg_prog_free(prog);

  if (write_errors || num_errors > 0)
    return 1;

  return 0;
}
