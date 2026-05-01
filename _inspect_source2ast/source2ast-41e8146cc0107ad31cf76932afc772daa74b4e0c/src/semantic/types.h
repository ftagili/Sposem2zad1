#pragma once
#include "../ast/ast.h"

typedef struct TypeEnv TypeEnv;

typedef struct FieldInfo {
    char *name;
    char *type_name;
    int offset;
} FieldInfo;

typedef struct MethodInfo {
    char *name;
    char *ret_type;
    int slot;
    char *impl_label;
} MethodInfo;

typedef struct ClassInfo {
    char *name;
    char *base_name;
    struct ClassInfo *base;

    FieldInfo *fields;
    int n_fields;

    MethodInfo *vtable;
    int n_slots;

    int size_bytes;
} ClassInfo;

TypeEnv *types_build_from_ast(ASTNode *root);
void types_free(TypeEnv *env);

const ClassInfo *types_find_class(const TypeEnv *env, const char *name);

int types_field_offset(const TypeEnv *env, const char *class_name,
                       const char *field_name, int *out_off);

int types_method_slot_and_label(const TypeEnv *env, const char *clss_name,
                                const char *method_name, int *out_slot,
                                const char **out_impl_label);