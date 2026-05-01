#include "analyzer.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <input-file> <output-file>\n", argv[0]);
    return 1;
  }

  const char *input_file = argv[1];
  const char *output_file = argv[2];

  errno = 0; /* сбрасываем errno перед вызовом */
  int result = analyze_file_to_dot(input_file, output_file);
  int saved_errno = errno; /* сохраняем errno сразу после вызова */

  if (result != 0) {
    if (result == 1) {
      // Ошибка открытия входного файла
      if (saved_errno != 0) {
        fprintf(stderr, "Error: cannot open input file '%s': %s\n", input_file,
                strerror(saved_errno));
      } else {
        fprintf(stderr, "Error: cannot open input file '%s'\n", input_file);
      }
    } else if (result == 2) {
      // Синтаксическая ошибка
      fprintf(stderr, "Error: syntax errors found in '%s'\n", input_file);
    } else if (result == 3) {
      // Нет AST root
      fprintf(stderr, "Error: no AST root produced\n");
    } else if (result == 4) {
      // Ошибка открытия выходного файла
      if (saved_errno != 0) {
        fprintf(stderr, "Error: cannot write to output file '%s': %s\n",
                output_file, strerror(saved_errno));
      } else {
        fprintf(stderr, "Error: cannot write to output file '%s'\n",
                output_file);
      }
    } else {
      // Неизвестная ошибка
      fprintf(stderr, "Error: unknown error occurred\n");
    }
    return result;
  }

  return 0;
}
