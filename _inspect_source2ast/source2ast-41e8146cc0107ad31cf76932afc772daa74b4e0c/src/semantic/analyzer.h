#ifndef MODULE_ANALYZER_H
#define MODULE_ANALYZER_H

#ifdef __cplusplus
extern "C" {
#endif

int analyze_file(const char* path);
int analyze_string(const char* text, const char* virtual_name);
int analyze_file_to_dot(const char* input_path, const char* dot_output_path);

#ifdef __cplusplus
}
#endif

#endif
