#include "cfg.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parser generated in build directory */
#include "../ast/ast.h"
#include "../../build/parser.tab.h"

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

static char *dup_cstr(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *d = (char *)malloc(n);
  if (d)
    memcpy(d, s, n);
  return d;
}

static void escape_dot_string(FILE *out, const char *s) {
  if (!s)
    return;
  for (const char *p = s; *p; ++p) {
    if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r') {
      if (*p == '\n')
        fputs("\\n", out);
      else if (*p == '\r')
        fputs("\\r", out);
      else {
        fputc('\\', out);
        fputc(*p, out);
      }
    } else {
      fputc(*p, out);
    }
  }
}

/* ============================================================================
 * CFG OPERATION IMPLEMENTATION
 * ============================================================================
 */

static CFGOperation *cfg_operation_create(CFGOperationKind kind,
                                          const char *op_name,
                                          ASTNode *ast_node) {
  CFGOperation *op = (CFGOperation *)calloc(1, sizeof(CFGOperation));
  if (!op)
    return NULL;
  op->kind = kind;
  op->op_name = dup_cstr(op_name);
  op->ast_node = ast_node;
  op->operands = NULL;
  op->num_operands = 0;
  op->capacity = 0;
  return op;
}

static void cfg_operation_add_operand(CFGOperation *op, CFGOperation *operand) {
  if (!op || !operand)
    return;
  if (op->num_operands == op->capacity) {
    int newcap = op->capacity == 0 ? 2 : op->capacity * 2;
    CFGOperation **no = (CFGOperation **)realloc(
        op->operands, (size_t)newcap * sizeof(CFGOperation *));
    if (!no)
      return;
    op->operands = no;
    op->capacity = newcap;
  }
  op->operands[op->num_operands++] = operand;
}

static void cfg_operation_free(CFGOperation *op) {
  if (!op)
    return;
  free(op->op_name);
  if (op->operands) {
    for (int i = 0; i < op->num_operands; i++) {
      cfg_operation_free(op->operands[i]);
    }
    free(op->operands);
  }
  free(op);
}

/* Extract token value from "kind:value" format */
static char *extract_token_value(const char *label) {
  if (!label)
    return dup_cstr("");
  const char *colon = strchr(label, ':');
  if (colon)
    return dup_cstr(colon + 1);
  return dup_cstr(label);
}

/* Decompose AST expression into operations */
static CFGOperation *decompose_expr_to_operation(ASTNode *expr) {
  if (!expr || !expr->label)
    return NULL;

  const char *label = expr->label;

  if (strcmp(label, "binop") == 0 && expr->numChildren >= 3) {
    /* Binary operation: left op right */
    ASTNode *left = expr->children[0];
    ASTNode *op_node = expr->children[1];
    ASTNode *right = expr->children[2];

    char *op_name = NULL;
    if (op_node && op_node->label) {
      op_name = extract_token_value(op_node->label);
    } else {
      op_name = dup_cstr("?");
    }

    CFGOperation *op = cfg_operation_create(CFG_OP_BINOP, op_name, expr);
    free(op_name);

    CFGOperation *left_op = decompose_expr_to_operation(left);
    CFGOperation *right_op = decompose_expr_to_operation(right);

    if (left_op)
      cfg_operation_add_operand(op, left_op);
    if (right_op)
      cfg_operation_add_operand(op, right_op);

    return op;
  } else if (strcmp(label, "unop") == 0 && expr->numChildren >= 2) {
    /* Unary operation: op expr */
    ASTNode *op_node = expr->children[0];
    ASTNode *operand = expr->children[1];

    char *op_name = NULL;
    if (op_node && op_node->label) {
      op_name = extract_token_value(op_node->label);
    } else {
      op_name = dup_cstr("?");
    }

    CFGOperation *op = cfg_operation_create(CFG_OP_UNOP, op_name, expr);
    free(op_name);

    CFGOperation *operand_op = decompose_expr_to_operation(operand);
    if (operand_op)
      cfg_operation_add_operand(op, operand_op);

    return op;
  } else if (strcmp(label, "address") == 0 && expr->numChildren >= 1) {
    /* Address-of operation: &var */
    ASTNode *id_node = expr->children[0];
    char *var_name = NULL;
    if (id_node && id_node->label) {
      var_name = extract_token_value(id_node->label);
    } else {
      var_name = dup_cstr("?");
    }

    CFGOperation *op = cfg_operation_create(CFG_OP_VAR, var_name, expr);
    free(var_name);
    // Mark as address-of operation by using a special name
    if (op && op->op_name) {
      char *addr_name = (char *)malloc(strlen(op->op_name) + 10);
      sprintf(addr_name, "&%s", op->op_name);
      free(op->op_name);
      op->op_name = addr_name;
    }

    return op;
  } else if (strcmp(label, "call") == 0 && expr->numChildren >= 2) {
    /* Function call: func(args...) */
    ASTNode *func_id = expr->children[0];
    ASTNode *args_node = expr->numChildren > 1 ? expr->children[1] : NULL;

    char *func_name = NULL;
    if (func_id && func_id->label) {
      func_name = extract_token_value(func_id->label);
    } else {
      func_name = dup_cstr("?");
    }

    CFGOperation *op = cfg_operation_create(CFG_OP_CALL, func_name, expr);
    free(func_name);

    /* Add function name as first operand */
    if (func_id && func_id->label) {
      char *name = extract_token_value(func_id->label);
      CFGOperation *name_op = cfg_operation_create(CFG_OP_VAR, name, func_id);
      free(name);
      cfg_operation_add_operand(op, name_op);
    }

    /* Add arguments as operands */
    if (args_node && strcmp(args_node->label, "args") == 0 &&
        args_node->numChildren > 0) {
      ASTNode *arglist = args_node->children[0];
      if (arglist && strcmp(arglist->label, "list") == 0) {
        for (int i = 0; i < arglist->numChildren; i++) {
          CFGOperation *arg_op =
              decompose_expr_to_operation(arglist->children[i]);
          if (arg_op)
            cfg_operation_add_operand(op, arg_op);
        }
      }
    }

    return op;
  } else if (strcmp(label, "index") == 0 && expr->numChildren >= 2) {
    /* Array indexing: base[index] */
    ASTNode *base_id = expr->children[0];
    ASTNode *indices_node = expr->numChildren > 1 ? expr->children[1] : NULL;

    CFGOperation *op = cfg_operation_create(CFG_OP_INDEX, "[]", expr);

    /* Base as first operand */
    if (base_id) {
      CFGOperation *base_op = decompose_expr_to_operation(base_id);
      if (base_op)
        cfg_operation_add_operand(op, base_op);
    }

    /* Indices as operands */
    if (indices_node && strcmp(indices_node->label, "args") == 0 &&
        indices_node->numChildren > 0) {
      ASTNode *indexlist = indices_node->children[0];
      if (indexlist && strcmp(indexlist->label, "list") == 0) {
        for (int i = 0; i < indexlist->numChildren; i++) {
          CFGOperation *idx_op =
              decompose_expr_to_operation(indexlist->children[i]);
          if (idx_op)
            cfg_operation_add_operand(op, idx_op);
        }
      }
    }

    return op;
  } else if (strcmp(label, "fieldAccess") == 0 && expr->numChildren >= 2) {
    /* Field access: obj.field */
    ASTNode *obj = expr->children[0];
    ASTNode *field_id = expr->children[1];

    char *field_name = NULL;
    if (field_id && field_id->label) {
      field_name = extract_token_value(field_id->label);
    } else {
      field_name = dup_cstr("?");
    }

    CFGOperation *op = cfg_operation_create(CFG_OP_FIELD_ACCESS, field_name, expr);
    free(field_name);

    CFGOperation *obj_op = decompose_expr_to_operation(obj);
    if (obj_op)
      cfg_operation_add_operand(op, obj_op);

    return op;
  } else if (strcmp(label, "methodCall") == 0 && expr->numChildren >= 3) {
    /* Method call: obj.method(args...) */
    ASTNode *obj = expr->children[0];
    ASTNode *method_id = expr->children[1];
    ASTNode *args_node = expr->numChildren > 2 ? expr->children[2] : NULL;

    char *method_name = NULL;
    if (method_id && method_id->label) {
      method_name = extract_token_value(method_id->label);
    } else {
      method_name = dup_cstr("?");
    }

    CFGOperation *op = cfg_operation_create(CFG_OP_METHOD_CALL, method_name, expr);
    free(method_name);

    /* Object as first operand */
    CFGOperation *obj_op = decompose_expr_to_operation(obj);
    if (obj_op)
      cfg_operation_add_operand(op, obj_op);

    /* Add arguments as operands */
    if (args_node && strcmp(args_node->label, "args") == 0 &&
        args_node->numChildren > 0) {
      ASTNode *arglist = args_node->children[0];
      if (arglist && strcmp(arglist->label, "list") == 0) {
        for (int i = 0; i < arglist->numChildren; i++) {
          CFGOperation *arg_op =
              decompose_expr_to_operation(arglist->children[i]);
          if (arg_op)
            cfg_operation_add_operand(op, arg_op);
        }
      }
    }

    return op;
  } else if (strcmp(label, "new") == 0 && expr->numChildren >= 1) {
    /* Object instantiation: new Class(args...) */
    ASTNode *class_id = expr->children[0];
    ASTNode *args_node = expr->numChildren > 1 ? expr->children[1] : NULL;

    char *class_name = NULL;
    if (class_id && class_id->label) {
      class_name = extract_token_value(class_id->label);
    } else {
      class_name = dup_cstr("?");
    }

    CFGOperation *op = cfg_operation_create(CFG_OP_NEW, class_name, expr);
    free(class_name);

    /* Add arguments as operands */
    if (args_node && strcmp(args_node->label, "args") == 0 &&
        args_node->numChildren > 0) {
      ASTNode *arglist = args_node->children[0];
      if (arglist && strcmp(arglist->label, "list") == 0) {
        for (int i = 0; i < arglist->numChildren; i++) {
          CFGOperation *arg_op =
              decompose_expr_to_operation(arglist->children[i]);
          if (arg_op)
            cfg_operation_add_operand(op, arg_op);
        }
      }
    }

    return op;
  } else if (label && strncmp(label, "id:", 3) == 0) {
    /* Identifier */
    char *name = extract_token_value(label);
    CFGOperation *op = cfg_operation_create(CFG_OP_VAR, name, expr);
    free(name);
    return op;
  } else if (label && (strncmp(label, "bool:", 5) == 0 ||
                       strncmp(label, "string:", 7) == 0 ||
                       strncmp(label, "char:", 5) == 0 ||
                       strncmp(label, "hex:", 4) == 0 ||
                       strncmp(label, "bits:", 5) == 0 ||
                       strncmp(label, "dec:", 4) == 0)) {
    /* Literal */
    char *value = extract_token_value(label);
    CFGOperation *op = cfg_operation_create(CFG_OP_LITERAL, value, expr);
    free(value);
    return op;
  }

  /* Default: treat as variable or unknown */
  return cfg_operation_create(CFG_OP_VAR,
                              label ? dup_cstr(label) : dup_cstr("?"), expr);
}

/* ============================================================================
 * CFG NODE IMPLEMENTATION
 * ============================================================================
 */

static CFGNode *cfg_node_create(int id, int is_entry, int is_exit) {
  CFGNode *node = (CFGNode *)calloc(1, sizeof(CFGNode));
  if (!node)
    return NULL;
  node->id = id;
  node->is_entry = is_entry;
  node->is_exit = is_exit;
  node->successor_true = NULL;
  node->successor_false = NULL;
  node->successor = NULL;
  node->operations = NULL;
  node->num_operations = 0;
  node->operations_capacity = 0;
  return node;
}

static void cfg_node_add_operation(CFGNode *node, CFGOperation *op) {
  if (!node || !op)
    return;
  if (node->num_operations == node->operations_capacity) {
    int newcap =
        node->operations_capacity == 0 ? 4 : node->operations_capacity * 2;
    CFGOperation **no = (CFGOperation **)realloc(
        node->operations, (size_t)newcap * sizeof(CFGOperation *));
    if (!no)
      return;
    node->operations = no;
    node->operations_capacity = newcap;
  }
  node->operations[node->num_operations++] = op;
}

static void cfg_node_free(CFGNode *node) {
  if (!node)
    return;
  if (node->operations) {
    for (int i = 0; i < node->num_operations; i++) {
      cfg_operation_free(node->operations[i]);
    }
    free(node->operations);
  }
  free(node);
}

/* ============================================================================
 * CFG FUNCTION IMPLEMENTATION
 * ============================================================================
 */

static CFGFunction *cfg_function_create(const char *name) {
  CFGFunction *func = (CFGFunction *)calloc(1, sizeof(CFGFunction));
  if (!func)
    return NULL;
  func->name = dup_cstr(name);
  func->return_type = NULL;
  func->parameters = NULL;
  func->num_parameters = 0;
  func->parameters_capacity = 0;
  func->source_file = NULL;
  func->entry = NULL;
  func->exit = NULL;
  func->all_nodes = NULL;
  func->num_nodes = 0;
  func->nodes_capacity = 0;
  return func;
}

static void cfg_function_add_node(CFGFunction *func, CFGNode *node) {
  if (!func || !node)
    return;
  if (func->num_nodes == func->nodes_capacity) {
    int newcap = func->nodes_capacity == 0 ? 8 : func->nodes_capacity * 2;
    CFGNode **nn = (CFGNode **)realloc(func->all_nodes,
                                       (size_t)newcap * sizeof(CFGNode *));
    if (!nn)
      return;
    func->all_nodes = nn;
    func->nodes_capacity = newcap;
  }
  func->all_nodes[func->num_nodes++] = node;
}

static void cfg_function_add_parameter(CFGFunction *func, const char *name,
                                       const char *type) {
  if (!func || !name)
    return;
  if (func->num_parameters == func->parameters_capacity) {
    int newcap =
        func->parameters_capacity == 0 ? 4 : func->parameters_capacity * 2;
    void *np =
        realloc(func->parameters, (size_t)newcap * sizeof(*func->parameters));
    if (!np)
      return;
    func->parameters = (typeof(func->parameters))np;
    func->parameters_capacity = newcap;
  }
  func->parameters[func->num_parameters].name = dup_cstr(name);
  func->parameters[func->num_parameters].type = dup_cstr(type ? type : "void");
  func->num_parameters++;
}

static void cfg_function_free(CFGFunction *func) {
  if (!func)
    return;
  free(func->name);
  free(func->return_type);
  if (func->parameters) {
    for (int i = 0; i < func->num_parameters; i++) {
      free(func->parameters[i].name);
      free(func->parameters[i].type);
    }
    free(func->parameters);
  }
  free(func->source_file);
  if (func->all_nodes) {
    for (int i = 0; i < func->num_nodes; i++) {
      cfg_node_free(func->all_nodes[i]);
    }
    free(func->all_nodes);
  }
  free(func);
}

/* Extract function name from AST */
static char *extract_func_name(ASTNode *func_def) {
  if (!func_def || strcmp(func_def->label, "funcDef") != 0)
    return NULL;
  if (func_def->numChildren < 1)
    return NULL;
  ASTNode *sig = func_def->children[0];
  if (!sig || strcmp(sig->label, "signature") != 0)
    return NULL;
  if (sig->numChildren < 2)
    return NULL;
  ASTNode *id_node = sig->children[1];
  if (!id_node || !id_node->label)
    return NULL;

  const char *colon = strchr(id_node->label, ':');
  if (colon)
    return dup_cstr(colon + 1);
  return dup_cstr(id_node->label);
}

/* Extract type from AST typeRef */
static char *extract_type(ASTNode *type_node) {
  if (!type_node || !type_node->label)
    return dup_cstr("void");

  const char *colon = strchr(type_node->label, ':');
  if (colon)
    return dup_cstr(colon + 1);
  return dup_cstr(type_node->label);
}

/* Extract function signature from AST */
static void extract_signature(CFGFunction *func, ASTNode *func_def) {
  if (!func || !func_def || strcmp(func_def->label, "funcDef") != 0)
    return;
  if (func_def->numChildren < 1)
    return;

  ASTNode *sig = func_def->children[0];
  if (!sig || strcmp(sig->label, "signature") != 0)
    return;

  /* Extract return type */
  if (sig->numChildren > 0) {
    ASTNode *return_type = sig->children[0];
    if (return_type) {
      func->return_type = extract_type(return_type);
    }
  }

  /* Extract parameters */
  if (sig->numChildren > 2) {
    ASTNode *args_node = sig->children[2];
    if (args_node && strcmp(args_node->label, "args") == 0 &&
        args_node->numChildren > 0) {
      ASTNode *arglist = args_node->children[0];
      if (arglist && strcmp(arglist->label, "arglist") == 0) {
        for (int i = 0; i < arglist->numChildren; i++) {
          ASTNode *arg = arglist->children[i];
          if (arg && strcmp(arg->label, "arg") == 0 && arg->numChildren >= 2) {
            ASTNode *arg_type = arg->children[0];
            ASTNode *arg_id = arg->children[1];

            char *param_type = extract_type(arg_type);
            char *param_name = NULL;

            if (arg_id && arg_id->label) {
              const char *colon = strchr(arg_id->label, ':');
              param_name = dup_cstr(colon ? colon + 1 : arg_id->label);
            }

            if (param_name) {
              cfg_function_add_parameter(func, param_name, param_type);
              free(param_name);
            }
            free(param_type);
          }
        }
      }
    }
  }
}

/* ============================================================================
 * CFG FILE IMPLEMENTATION
 * ============================================================================
 */

static CFGFile *cfg_file_create(const char *filename, ASTNode *ast_root) {
  CFGFile *file = (CFGFile *)calloc(1, sizeof(CFGFile));
  if (!file)
    return NULL;
  file->filename = dup_cstr(filename);
  file->ast_root = ast_root;
  file->functions = NULL;
  file->num_functions = 0;
  file->functions_capacity = 0;
  return file;
}

static void cfg_file_add_function(CFGFile *file, CFGFunction *func) {
  if (!file || !func)
    return;
  if (file->num_functions == file->functions_capacity) {
    int newcap =
        file->functions_capacity == 0 ? 4 : file->functions_capacity * 2;
    CFGFunction **nf = (CFGFunction **)realloc(
        file->functions, (size_t)newcap * sizeof(CFGFunction *));
    if (!nf)
      return;
    file->functions = nf;
    file->functions_capacity = newcap;
  }
  file->functions[file->num_functions++] = func;
}

static void cfg_file_free(CFGFile *file) {
  if (!file)
    return;
  free(file->filename);
  if (file->functions) {
    for (int i = 0; i < file->num_functions; i++) {
      cfg_function_free(file->functions[i]);
    }
    free(file->functions);
  }
  free(file);
}

/* ============================================================================
 * CFG ERROR IMPLEMENTATION
 * ============================================================================
 */

static CFGError *cfg_error_create(CFGErrorKind kind, const char *message,
                                  const char *function_name,
                                  const char *source_file, int line,
                                  int column) {
  CFGError *err = (CFGError *)calloc(1, sizeof(CFGError));
  if (!err)
    return NULL;
  err->kind = kind;
  err->message = dup_cstr(message);
  err->function_name = function_name ? dup_cstr(function_name) : NULL;
  err->source_file = source_file ? dup_cstr(source_file) : NULL;
  err->line = line;
  err->column = column;
  return err;
}

static void cfg_error_free(CFGError *err) {
  if (!err)
    return;
  free(err->message);
  free(err->function_name);
  free(err->source_file);
  free(err);
}

/* ============================================================================
 * CALL GRAPH IMPLEMENTATION
 * ============================================================================
 */

static CallGraph *call_graph_create(void) {
  CallGraph *cg = (CallGraph *)calloc(1, sizeof(CallGraph));
  if (!cg)
    return NULL;
  cg->edges = NULL;
  cg->num_edges = 0;
  cg->edges_capacity = 0;
  return cg;
}

static void call_graph_add_edge(CallGraph *cg, CFGFunction *caller,
                                CFGFunction *callee, const char *callee_name) {
  if (!cg || !caller)
    return;
  if (cg->num_edges == cg->edges_capacity) {
    int newcap = cg->edges_capacity == 0 ? 8 : cg->edges_capacity * 2;
    CallGraphEdge *ne = (CallGraphEdge *)realloc(
        cg->edges, (size_t)newcap * sizeof(CallGraphEdge));
    if (!ne)
      return;
    cg->edges = ne;
    cg->edges_capacity = newcap;
  }
  cg->edges[cg->num_edges].caller = caller;
  cg->edges[cg->num_edges].callee = callee;
  cg->edges[cg->num_edges].callee_name =
      callee_name ? dup_cstr(callee_name) : NULL;
  cg->num_edges++;
}

static void call_graph_free(CallGraph *cg) {
  if (!cg)
    return;
  if (cg->edges) {
    for (int i = 0; i < cg->num_edges; i++) {
      free(cg->edges[i].callee_name);
    }
    free(cg->edges);
  }
  free(cg);
}

/* Extract function name from call operation */
static char *extract_callee_name(CFGOperation *call_op) {
  if (!call_op || call_op->kind != CFG_OP_CALL || call_op->num_operands < 1)
    return NULL;
  CFGOperation *name_op = call_op->operands[0];
  if (name_op && name_op->op_name)
    return dup_cstr(name_op->op_name);
  return NULL;
}

/* ============================================================================
 * CFG PROGRAM IMPLEMENTATION
 * ============================================================================
 */

CFGProgram *cfg_prog_create(void) {
  CFGProgram *prog = (CFGProgram *)calloc(1, sizeof(CFGProgram));
  if (!prog)
    return NULL;
  prog->files = NULL;
  prog->num_files = 0;
  prog->files_capacity = 0;
  prog->all_functions = NULL;
  prog->num_all_functions = 0;
  prog->all_functions_capacity = 0;
  prog->call_graph = call_graph_create();
  prog->errors = NULL;
  prog->num_errors = 0;
  prog->errors_capacity = 0;
  prog->next_node_id = 0;
  return prog;
}

void cfg_prog_free(CFGProgram *prog) {
  if (!prog)
    return;
  if (prog->files) {
    for (int i = 0; i < prog->num_files; i++) {
      cfg_file_free(prog->files[i]);
    }
    free(prog->files);
  }
  /* Functions are freed with files, but we also have all_functions */
  if (prog->all_functions) {
    free(prog->all_functions);
  }
  if (prog->call_graph) {
    call_graph_free(prog->call_graph);
  }
  if (prog->errors) {
    for (int i = 0; i < prog->num_errors; i++) {
      cfg_error_free(prog->errors[i]);
    }
    free(prog->errors);
  }
  free(prog);
}

int cfg_prog_add_file(CFGProgram *prog, const char *filename,
                      ASTNode *ast_root) {
  if (!prog || !filename || !ast_root)
    return 0;

  if (prog->num_files == prog->files_capacity) {
    int newcap = prog->files_capacity == 0 ? 4 : prog->files_capacity * 2;
    CFGFile **nf =
        (CFGFile **)realloc(prog->files, (size_t)newcap * sizeof(CFGFile *));
    if (!nf)
      return 0;
    prog->files = nf;
    prog->files_capacity = newcap;
  }

  CFGFile *file = cfg_file_create(filename, ast_root);
  if (!file)
    return 0;

  prog->files[prog->num_files++] = file;
  return 1;
}

static void cfg_prog_add_error(CFGProgram *prog, CFGErrorKind kind,
                               const char *message, const char *function_name,
                               const char *source_file, int line, int column) {
  if (!prog)
    return;
  if (prog->num_errors == prog->errors_capacity) {
    int newcap = prog->errors_capacity == 0 ? 4 : prog->errors_capacity * 2;
    CFGError **ne =
        (CFGError **)realloc(prog->errors, (size_t)newcap * sizeof(CFGError *));
    if (!ne)
      return;
    prog->errors = ne;
    prog->errors_capacity = newcap;
  }
  CFGError *err =
      cfg_error_create(kind, message, function_name, source_file, line, column);
  if (err)
    prog->errors[prog->num_errors++] = err;
}

static void cfg_prog_add_function(CFGProgram *prog, CFGFunction *func) {
  if (!prog || !func)
    return;
  if (prog->num_all_functions == prog->all_functions_capacity) {
    int newcap = prog->all_functions_capacity == 0
                     ? 8
                     : prog->all_functions_capacity * 2;
    CFGFunction **nf = (CFGFunction **)realloc(
        prog->all_functions, (size_t)newcap * sizeof(CFGFunction *));
    if (!nf)
      return;
    prog->all_functions = nf;
    prog->all_functions_capacity = newcap;
  }
  prog->all_functions[prog->num_all_functions++] = func;
}

/* Find all function definitions in AST */
static void find_functions(ASTNode *node, ASTNode ***funcs, int *count,
                           int *capacity) {
  if (!node)
    return;

  if (strcmp(node->label, "funcDef") == 0) {
    if (*count == *capacity) {
      int newcap = *capacity == 0 ? 4 : *capacity * 2;
      ASTNode **nf =
          (ASTNode **)realloc(*funcs, (size_t)newcap * sizeof(ASTNode *));
      if (!nf)
        return;
      *funcs = nf;
      *capacity = newcap;
    }
    if (*funcs) {
      (*funcs)[(*count)++] = node;
    }
  } else {
    for (int i = 0; i < node->numChildren; i++) {
      find_functions(node->children[i], funcs, count, capacity);
    }
  }
}

/* Context for break handling */
typedef struct {
  CFGNode *loop_exit; /* node to jump to on break */
  int depth;          /* nesting depth */
} LoopContext;

/* Build CFG from statement, returning the last node */
static CFGNode *build_cfg_from_statement(CFGProgram *prog, CFGFunction *func,
                                         ASTNode *stmt, CFGNode *current,
                                         LoopContext *loop_ctx);

/* Build CFG from statement list */
static CFGNode *build_cfg_from_statements(CFGProgram *prog, CFGFunction *func,
                                          ASTNode *stmt_list, CFGNode *current,
                                          LoopContext *loop_ctx) {
  if (!stmt_list || !current)
    return current;

  if (strcmp(stmt_list->label, "stmts") == 0) {
    for (int i = 0; i < stmt_list->numChildren; i++) {
      ASTNode *stmt = stmt_list->children[i];
      current = build_cfg_from_statement(prog, func, stmt, current, loop_ctx);
    }
  }

  return current;
}

/* Build CFG from a single statement */
static CFGNode *build_cfg_from_statement(CFGProgram *prog, CFGFunction *func,
                                         ASTNode *stmt, CFGNode *current,
                                         LoopContext *loop_ctx) {
  if (!stmt || !current || !stmt->label)
    return current;

  const char *label = stmt->label;

  if (strcmp(label, "if") == 0) {
    /* if (expr) statement optElse */
    if (stmt->numChildren < 2)
      return current;

    ASTNode *condition = stmt->children[0];
    ASTNode *then_stmt = stmt->children[1];
    ASTNode *else_node = stmt->numChildren > 2 ? stmt->children[2] : NULL;

    /* Create condition node */
    CFGNode *cond_node = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, cond_node);

    /* Decompose condition into operations */
    CFGOperation *cond_op = decompose_expr_to_operation(condition);
    if (cond_op) {
      cond_op->kind = CFG_OP_COND;
      cfg_node_add_operation(cond_node, cond_op);
    }

    /* Link current to condition */
    current->successor = cond_node;

    /* Build then branch */
    CFGNode *then_end =
        build_cfg_from_statement(prog, func, then_stmt, cond_node, loop_ctx);

    /* Check if then branch ends with exit */
    int then_exits = (then_end == func->exit ||
                      (then_end && then_end->successor == func->exit));

    /* Build else branch if present */
    CFGNode *else_end = NULL;
    int else_exits = 0;
    if (else_node && strcmp(else_node->label, "else") == 0 &&
        else_node->numChildren > 0) {
      ASTNode *else_stmt = else_node->children[0];
      else_end =
          build_cfg_from_statement(prog, func, else_stmt, cond_node, loop_ctx);
      else_exits = (else_end == func->exit ||
                    (else_end && else_end->successor == func->exit));
    }

    /* Create merge node if needed */
    if (!then_exits || !else_exits) {
      CFGNode *merge_node = cfg_node_create(prog->next_node_id++, 0, 0);
      cfg_function_add_node(func, merge_node);

      if (!then_exits && then_end && then_end != func->exit) {
        then_end->successor = merge_node;
      }
      if (else_end && !else_exits && else_end != func->exit) {
        else_end->successor = merge_node;
      } else if (!else_end) {
        /* No else: false branch goes to merge */
        cond_node->successor_false = merge_node;
      }

      /* Set true/false edges from condition */
      cond_node->successor_true = then_end ? then_end : merge_node;
      if (!else_end && !else_exits) {
        cond_node->successor_false = merge_node;
      }

      return merge_node;
    } else {
      /* Both branches exit */
      cond_node->successor_true = then_end ? then_end : func->exit;
      if (else_end) {
        cond_node->successor_false = else_end;
      } else {
        cond_node->successor_false = func->exit;
      }
      return cond_node;
    }

  } else if (strcmp(label, "while") == 0) {
    /* while (expr) statement */
    if (stmt->numChildren < 2)
      return current;

    ASTNode *condition = stmt->children[0];
    ASTNode *body = stmt->children[1];

    /* Create loop header (condition node) */
    CFGNode *loop_header = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, loop_header);

    /* Decompose condition */
    CFGOperation *cond_op = decompose_expr_to_operation(condition);
    if (cond_op) {
      cond_op->kind = CFG_OP_COND;
      cfg_node_add_operation(loop_header, cond_op);
    }

    /* Link current to loop header */
    current->successor = loop_header;

    /* Create exit node for break */
    CFGNode *loop_exit = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, loop_exit);

    /* Build body with loop context */
    LoopContext body_ctx = {loop_exit, loop_ctx ? loop_ctx->depth + 1 : 1};
    CFGNode *body_end =
        build_cfg_from_statement(prog, func, body, loop_header, &body_ctx);

    /* Back edge from body to loop header */
    if (body_end && body_end != func->exit &&
        body_end->successor != func->exit) {
      body_end->successor = loop_header;
    }

    /* False edge from condition to exit */
    loop_header->successor_false = loop_exit;
    loop_header->successor_true = body_end ? body_end : loop_header;

    return loop_exit;

  } else if (strcmp(label, "doWhile") == 0) {
    /* do statementBlock while (expr) */
    if (stmt->numChildren < 2)
      return current;

    ASTNode *body = stmt->children[0];
    ASTNode *condition = stmt->children[1];

    /* Create loop exit node */
    CFGNode *loop_exit = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, loop_exit);

    /* Build body first */
    LoopContext body_ctx = {loop_exit, loop_ctx ? loop_ctx->depth + 1 : 1};
    CFGNode *body_end =
        build_cfg_from_statement(prog, func, body, current, &body_ctx);

    /* Create condition node */
    CFGNode *cond_node = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, cond_node);

    /* Decompose condition */
    CFGOperation *cond_op = decompose_expr_to_operation(condition);
    if (cond_op) {
      cond_op->kind = CFG_OP_COND;
      cfg_node_add_operation(cond_node, cond_op);
    }

    /* Link body to condition */
    if (body_end && body_end != func->exit &&
        body_end->successor != func->exit) {
      body_end->successor = cond_node;
    }

    /* True edge: back to body start (current) */
    cond_node->successor_true = current;
    /* False edge: to loop exit */
    cond_node->successor_false = loop_exit;

    return loop_exit;

  } else if (strcmp(label, "break") == 0) {
    /* break; */
    if (!loop_ctx || !loop_ctx->loop_exit) {
      cfg_prog_add_error(prog, CFG_ERR_BREAK_OUTSIDE_LOOP,
                         "break statement outside of loop", func->name,
                         func->source_file, 0, 0);
      return current;
    }

    CFGNode *break_node = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, break_node);

    CFGOperation *break_op = cfg_operation_create(CFG_OP_BREAK, "break", stmt);
    cfg_node_add_operation(break_node, break_op);

    current->successor = break_node;
    break_node->successor = loop_ctx->loop_exit;

    return break_node;

  } else if (strcmp(label, "return") == 0) {
    /* return expr; or return; */
    CFGNode *return_node = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, return_node);

    CFGOperation *return_op =
        cfg_operation_create(CFG_OP_RETURN, "return", stmt);
    if (stmt->numChildren > 0) {
      /* Has return value */
      ASTNode *ret_expr = stmt->children[0];
      CFGOperation *ret_val_op = decompose_expr_to_operation(ret_expr);
      if (ret_val_op)
        cfg_operation_add_operand(return_op, ret_val_op);
    }
    cfg_node_add_operation(return_node, return_op);

    current->successor = return_node;
    return_node->successor = func->exit;

    return return_node;

  } else if (strcmp(label, "block") == 0) {
    /* { statement* } */
    if (stmt->numChildren > 0) {
      ASTNode *stmt_list = stmt->children[0];
      return build_cfg_from_statements(prog, func, stmt_list, current,
                                       loop_ctx);
    }
    return current;

  } else if (strcmp(label, "vardecl") == 0) {
    /* typeRef varList; */
    CFGNode *decl_node = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, decl_node);

    if (stmt->numChildren >= 2) {
      ASTNode *var_list = stmt->children[1];
      if (var_list && strcmp(var_list->label, "vars") == 0) {
        for (int i = 0; i < var_list->numChildren; i += 2) {
          if (i < var_list->numChildren) {
            ASTNode *var_id = var_list->children[i];
            ASTNode *opt_assign = i + 1 < var_list->numChildren
                                      ? var_list->children[i + 1]
                                      : NULL;

            if (var_id && var_id->label) {
              const char *colon = strchr(var_id->label, ':');
              char *var_name = dup_cstr(colon ? colon + 1 : var_id->label);

              CFGOperation *decl_op =
                  cfg_operation_create(CFG_OP_VARDECL, var_name, stmt);
              free(var_name);

              if (opt_assign && strcmp(opt_assign->label, "assign") == 0 &&
                  opt_assign->numChildren > 0) {
                ASTNode *init_expr = opt_assign->children[0];
                CFGOperation *init_op = decompose_expr_to_operation(init_expr);
                if (init_op)
                  cfg_operation_add_operand(decl_op, init_op);
              }

              cfg_node_add_operation(decl_node, decl_op);
            }
          }
        }
      }
    }

    current->successor = decl_node;
    return decl_node;

  } else if (strcmp(label, "exprstmt") == 0) {
    /* expr; */
    CFGNode *expr_node = cfg_node_create(prog->next_node_id++, 0, 0);
    cfg_function_add_node(func, expr_node);

    if (stmt->numChildren > 0) {
      ASTNode *expr = stmt->children[0];
      CFGOperation *expr_op = decompose_expr_to_operation(expr);
      if (expr_op) {
        cfg_node_add_operation(expr_node, expr_op);

        /* Function calls will be processed in second pass after all functions
         * are built */
      }
    }

    current->successor = expr_node;
    return expr_node;
  }

  /* Unknown statement type */
  return current;
}

/* Build CFG for a function */
static CFGFunction *build_cfg_for_function(CFGProgram *prog, ASTNode *func_def,
                                           const char *source_file) {
  if (!func_def || strcmp(func_def->label, "funcDef") != 0)
    return NULL;

  char *func_name = extract_func_name(func_def);
  if (!func_name) {
    func_name = dup_cstr("unknown");
  }

  CFGFunction *func = cfg_function_create(func_name);
  free(func_name);

  if (!func)
    return NULL;

  func->source_file = dup_cstr(source_file);

  /* Extract signature */
  extract_signature(func, func_def);

  /* Create entry and exit nodes */
  func->entry = cfg_node_create(prog->next_node_id++, 1, 0);
  func->exit = cfg_node_create(prog->next_node_id++, 0, 1);
  cfg_function_add_node(func, func->entry);
  cfg_function_add_node(func, func->exit);

  if (func_def->numChildren < 2) {
    /* Function declaration only, no body */
    func->entry->successor = func->exit;
    return func;
  }

  ASTNode *body = func_def->children[1];
  if (!body || strcmp(body->label, "block") != 0) {
    func->entry->successor = func->exit;
    return func;
  }

  /* Build CFG from body */
  LoopContext loop_ctx = {NULL, 0};
  CFGNode *last = build_cfg_from_statements(prog, func, body->children[0],
                                            func->entry, &loop_ctx);

  /* Link last node to exit if it doesn't already exit */
  if (last && last != func->exit && last->successor != func->exit &&
      !last->successor_true && !last->successor_false) {
    if (!last->successor)
      last->successor = func->exit;
  }

  return func;
}

/* Recursively find all CALL operations in an operation tree */
static void find_call_operations(CFGOperation *op, CFGOperation ***calls,
                                 int *count, int *capacity) {
  if (!op)
    return;

  if (op->kind == CFG_OP_CALL) {
    /* Add to list */
    if (*count == *capacity) {
      int newcap = *capacity == 0 ? 4 : *capacity * 2;
      CFGOperation **nc = (CFGOperation **)realloc(
          *calls, (size_t)newcap * sizeof(CFGOperation *));
      if (!nc)
        return;
      *calls = nc;
      *capacity = newcap;
    }
    (*calls)[(*count)++] = op;
  }

  /* Recursively check operands */
  for (int i = 0; i < op->num_operands; i++) {
    find_call_operations(op->operands[i], calls, count, capacity);
  }
}

/* Extract call graph edges from all operations in a function */
static void extract_call_edges_from_function(CFGProgram *prog,
                                             CFGFunction *func) {
  if (!prog || !func)
    return;

  for (int i = 0; i < func->num_nodes; i++) {
    CFGNode *node = func->all_nodes[i];
    if (!node)
      continue;

    /* Find all CALL operations in this node (recursively) */
    CFGOperation **calls = NULL;
    int call_count = 0;
    int call_capacity = 0;

    for (int j = 0; j < node->num_operations; j++) {
      CFGOperation *op = node->operations[j];
      if (op) {
        find_call_operations(op, &calls, &call_count, &call_capacity);
      }
    }

    /* Process each call */
    for (int j = 0; j < call_count; j++) {
      CFGOperation *call_op = calls[j];
      if (!call_op)
        continue;

      /* Check if this call edge already exists */
      char *callee_name = extract_callee_name(call_op);
      if (!callee_name)
        continue;

      int edge_exists = 0;
      for (int k = 0; k < prog->call_graph->num_edges; k++) {
        CallGraphEdge *edge = &prog->call_graph->edges[k];
        if (edge->caller == func && edge->callee_name &&
            strcmp(edge->callee_name, callee_name) == 0) {
          edge_exists = 1;
          break;
        }
      }

      if (!edge_exists) {
        CFGFunction *callee = cfg_prog_find_function(prog, callee_name);
        call_graph_add_edge(prog->call_graph, func, callee, callee_name);
        if (!callee) {
          cfg_prog_add_error(prog, CFG_ERR_UNKNOWN_FUNCTION,
                             "unknown function called", func->name,
                             func->source_file, 0, 0);
        }
      }

      free(callee_name);
    }

    if (calls)
      free(calls);
  }
}

int cfg_prog_build(CFGProgram *prog) {
  if (!prog)
    return 0;

  /* First pass: Build all CFGs */
  for (int f = 0; f < prog->num_files; f++) {
    CFGFile *file = prog->files[f];
    if (!file || !file->ast_root)
      continue;

    /* Find all functions in this file */
    ASTNode **funcs = NULL;
    int func_count = 0;
    int func_capacity = 0;

    find_functions(file->ast_root, &funcs, &func_count, &func_capacity);

    /* Build CFG for each function */
    for (int i = 0; i < func_count; i++) {
      CFGFunction *func =
          build_cfg_for_function(prog, funcs[i], file->filename);
      if (func) {
        cfg_file_add_function(file, func);
        cfg_prog_add_function(prog, func);
      }
    }

    if (funcs)
      free(funcs);
  }

  /* Second pass: Extract call graph edges from all functions */
  for (int i = 0; i < prog->num_all_functions; i++) {
    CFGFunction *func = prog->all_functions[i];
    if (func) {
      extract_call_edges_from_function(prog, func);
    }
  }

  return 1;
}

/* ============================================================================
 * ACCESSORS
 * ============================================================================
 */

int cfg_prog_get_num_functions(CFGProgram *prog) {
  return prog ? prog->num_all_functions : 0;
}

CFGFunction *cfg_prog_get_function(CFGProgram *prog, int index) {
  if (!prog || index < 0 || index >= prog->num_all_functions)
    return NULL;
  return prog->all_functions[index];
}

CFGFunction *cfg_prog_find_function(CFGProgram *prog, const char *name) {
  if (!prog || !name)
    return NULL;
  for (int i = 0; i < prog->num_all_functions; i++) {
    if (prog->all_functions[i] && prog->all_functions[i]->name &&
        strcmp(prog->all_functions[i]->name, name) == 0) {
      return prog->all_functions[i];
    }
  }
  return NULL;
}

int cfg_prog_get_num_errors(CFGProgram *prog) {
  return prog ? prog->num_errors : 0;
}

CFGError *cfg_prog_get_error(CFGProgram *prog, int index) {
  if (!prog || index < 0 || index >= prog->num_errors)
    return NULL;
  return prog->errors[index];
}

CallGraph *cfg_prog_get_call_graph(CFGProgram *prog) {
  return prog ? prog->call_graph : NULL;
}

const char *cfg_function_get_name(CFGFunction *func) {
  return func ? func->name : NULL;
}

const char *cfg_function_get_return_type(CFGFunction *func) {
  return func ? func->return_type : NULL;
}

int cfg_function_get_num_parameters(CFGFunction *func) {
  return func ? func->num_parameters : 0;
}

const char *cfg_function_get_parameter_name(CFGFunction *func, int index) {
  if (!func || index < 0 || index >= func->num_parameters)
    return NULL;
  return func->parameters[index].name;
}

const char *cfg_function_get_parameter_type(CFGFunction *func, int index) {
  if (!func || index < 0 || index >= func->num_parameters)
    return NULL;
  return func->parameters[index].type;
}

const char *cfg_function_get_source_file(CFGFunction *func) {
  return func ? func->source_file : NULL;
}

CFGNode *cfg_function_get_entry(CFGFunction *func) {
  return func ? func->entry : NULL;
}

CFGNode *cfg_function_get_exit(CFGFunction *func) {
  return func ? func->exit : NULL;
}

int cfg_function_get_num_nodes(CFGFunction *func) {
  return func ? func->num_nodes : 0;
}

CFGNode *cfg_function_get_node(CFGFunction *func, int index) {
  if (!func || index < 0 || index >= func->num_nodes)
    return NULL;
  return func->all_nodes[index];
}

int cfg_node_get_id(CFGNode *node) { return node ? node->id : -1; }

int cfg_node_is_entry(CFGNode *node) { return node ? node->is_entry : 0; }

int cfg_node_is_exit(CFGNode *node) { return node ? node->is_exit : 0; }

CFGNode *cfg_node_get_successor(CFGNode *node) {
  return node ? node->successor : NULL;
}

CFGNode *cfg_node_get_successor_true(CFGNode *node) {
  return node ? node->successor_true : NULL;
}

CFGNode *cfg_node_get_successor_false(CFGNode *node) {
  return node ? node->successor_false : NULL;
}

int cfg_node_get_num_operations(CFGNode *node) {
  return node ? node->num_operations : 0;
}

CFGOperation *cfg_node_get_operation(CFGNode *node, int index) {
  if (!node || index < 0 || index >= node->num_operations)
    return NULL;
  return node->operations[index];
}

CFGOperationKind cfg_operation_get_kind(CFGOperation *op) {
  return op ? op->kind : CFG_OP_VAR;
}

const char *cfg_operation_get_name(CFGOperation *op) {
  return op ? op->op_name : NULL;
}

int cfg_operation_get_num_operands(CFGOperation *op) {
  return op ? op->num_operands : 0;
}

CFGOperation *cfg_operation_get_operand(CFGOperation *op, int index) {
  if (!op || index < 0 || index >= op->num_operands)
    return NULL;
  return op->operands[index];
}

ASTNode *cfg_operation_get_ast_node(CFGOperation *op) {
  return op ? op->ast_node : NULL;
}

int cfg_call_graph_get_num_edges(CallGraph *cg) {
  return cg ? cg->num_edges : 0;
}

CallGraphEdge *cfg_call_graph_get_edge(CallGraph *cg, int index) {
  if (!cg || index < 0 || index >= cg->num_edges)
    return NULL;
  return &cg->edges[index];
}

CFGFunction *cfg_call_edge_get_caller(CallGraphEdge *edge) {
  return edge ? edge->caller : NULL;
}

CFGFunction *cfg_call_edge_get_callee(CallGraphEdge *edge) {
  return edge ? edge->callee : NULL;
}

const char *cfg_call_edge_get_callee_name(CallGraphEdge *edge) {
  return edge ? edge->callee_name : NULL;
}

/* ============================================================================
 * DOT EXPORT
 * ============================================================================
 */

/* Format operation label for DOT (OP_KIND(args)@line:column format) */
static void format_operation_label(FILE *out, CFGOperation *op) {
  if (!op)
    return;

  const char *name = cfg_operation_get_name(op);
  CFGOperationKind kind = cfg_operation_get_kind(op);
  int line = 0; /* TODO: extract from AST if available */
  int column = 0;

  /* Determine operation kind string */
  const char *op_kind = "UNKNOWN";
  switch (kind) {
  case CFG_OP_ASSIGN:
    op_kind = "ASSIGN";
    break;
  case CFG_OP_BINOP:
    op_kind = "BINOP";
    break;
  case CFG_OP_UNOP:
    op_kind = "UNOP";
    break;
  case CFG_OP_CALL:
    op_kind = "CALL";
    break;
  case CFG_OP_INDEX:
    op_kind = "INDEX";
    break;
  case CFG_OP_VAR:
    op_kind = "READ";
    break;
  case CFG_OP_LITERAL:
    op_kind = "CONST";
    break;
  case CFG_OP_RETURN:
    op_kind = "RETURN";
    break;
  case CFG_OP_BREAK:
    op_kind = "BREAK";
    break;
  case CFG_OP_VARDECL:
    op_kind = "VARDECL";
    break;
  case CFG_OP_COND:
    op_kind = "COND";
    break;
  case CFG_OP_FIELD_ACCESS:
    op_kind = "FIELD_ACCESS";
    break;
  case CFG_OP_METHOD_CALL:
    op_kind = "METHOD_CALL";
    break;
  case CFG_OP_NEW:
    op_kind = "NEW";
    break;
  }

  fprintf(out, "%s(", op_kind);

  /* Format arguments based on operation type */
  switch (kind) {
  case CFG_OP_CALL:
    if (cfg_operation_get_num_operands(op) >= 1) {
      CFGOperation *name_op = cfg_operation_get_operand(op, 0);
      if (name_op && name_op->op_name) {
        escape_dot_string(out, name_op->op_name);
      } else {
        fputs("?", out);
      }
    }
    break;
  case CFG_OP_VAR:
    if (name) {
      escape_dot_string(out, name);
    } else {
      fputs("?", out);
    }
    break;
  case CFG_OP_LITERAL:
    if (name) {
      /* For literals, show the value */
      escape_dot_string(out, name);
    } else {
      fputs("?", out);
    }
    break;
  case CFG_OP_BINOP:
  case CFG_OP_UNOP:
    if (name) {
      escape_dot_string(out, name);
    } else {
      fputs("?", out);
    }
    break;
  case CFG_OP_VARDECL:
    if (name) {
      escape_dot_string(out, name);
    } else {
      fputs("?", out);
    }
    break;
  case CFG_OP_INDEX:
    if (cfg_operation_get_num_operands(op) >= 1) {
      CFGOperation *base_op = cfg_operation_get_operand(op, 0);
      if (base_op && base_op->op_name) {
        escape_dot_string(out, base_op->op_name);
      } else {
        fputs("?", out);
      }
    }
    break;
  case CFG_OP_RETURN:
    /* Show return value type if available */
    fputs("", out);
    break;
  case CFG_OP_BREAK:
    fputs("", out);
    break;
  case CFG_OP_COND:
    fputs("", out);
    break;
  case CFG_OP_ASSIGN:
    /* Show variable name if available */
    if (name) {
      escape_dot_string(out, name);
    }
    break;
  case CFG_OP_FIELD_ACCESS:
    if (name) {
      escape_dot_string(out, name);
    } else {
      fputs("?", out);
    }
    break;
  case CFG_OP_METHOD_CALL:
    if (name) {
      escape_dot_string(out, name);
    } else {
      fputs("?", out);
    }
    break;
  case CFG_OP_NEW:
    if (name) {
      escape_dot_string(out, name);
    } else {
      fputs("?", out);
    }
    break;
  }

  fprintf(out, ")@%d:%d", line, column);
}

void cfg_function_print_dot(FILE *out, CFGFunction *func, CFGProgram *prog) {
  if (!out || !func)
    return;

  fprintf(out, "digraph CFG_%s {\n", func->name ? func->name : "unknown");
  fprintf(out, "  label=\"CFG for function: ");
  if (func->name)
    escape_dot_string(out, func->name);
  fprintf(out, "\";\n");
  fprintf(out, "  node [fontname=\"Helvetica\"];\n");
  fprintf(out, "  rankdir=TB;\n");

  /* Print basic block nodes (white squares) */
  for (int i = 0; i < func->num_nodes; i++) {
    CFGNode *node = func->all_nodes[i];
    fprintf(out,
            "  block_%d [label=\"#%d\", shape=box, style=filled, "
            "fillcolor=white];\n",
            node->id, node->id);
  }

  /* Print operation nodes (green ovals, or red for errors) */
  int op_counter = 0;
  for (int i = 0; i < func->num_nodes; i++) {
    CFGNode *node = func->all_nodes[i];
    if (node->is_entry || node->is_exit)
      continue;

    for (int j = 0; j < node->num_operations; j++) {
      CFGOperation *op = node->operations[j];
      if (!op)
        continue;

      int op_id = 10000 + op_counter++;
      const char *fillcolor = "lightgreen";

      /* Check if this is an error operation */
      if (op->kind == CFG_OP_CALL && prog) {
        char *callee_name = extract_callee_name(op);
        if (callee_name) {
          CFGFunction *callee = cfg_prog_find_function(prog, callee_name);
          if (!callee) {
            fillcolor = "lightcoral";
          }
          free(callee_name);
        }
      }

      fprintf(out, "  op_%d [label=\"", op_id);
      format_operation_label(out, op);
      fprintf(out, "\", shape=ellipse, style=filled, fillcolor=%s];\n",
              fillcolor);

      /* Edge from block to operation */
      fprintf(out, "  block_%d -> op_%d [style=solid];\n", node->id, op_id);
    }
  }

  /* Print control-flow edges between blocks */
  for (int i = 0; i < func->num_nodes; i++) {
    CFGNode *node = func->all_nodes[i];

    if (node->successor) {
      fprintf(out, "  block_%d -> block_%d [style=solid];\n", node->id,
              node->successor->id);
    }
    if (node->successor_true) {
      fprintf(out, "  block_%d -> block_%d [label=\"true\", style=solid];\n",
              node->id, node->successor_true->id);
    }
    if (node->successor_false) {
      fprintf(out, "  block_%d -> block_%d [label=\"false\", style=solid];\n",
              node->id, node->successor_false->id);
    }
  }

  fprintf(out, "}\n");
}

void cfg_call_graph_print_dot(FILE *out, CallGraph *cg, CFGProgram *prog) {
  if (!out || !cg)
    return;

  fprintf(out, "digraph CallGraph {\n");
  fprintf(out, "  label=\"Call Graph\";\n");
  fprintf(out, "  node [shape=box, fontname=Helvetica];\n");

  /* Print function nodes */
  for (int i = 0; i < prog->num_all_functions; i++) {
    CFGFunction *func = prog->all_functions[i];
    if (func && func->name) {
      fprintf(out, "  \"");
      escape_dot_string(out, func->name);
      fprintf(out, "\" [label=\"");
      escape_dot_string(out, func->name);
      fprintf(out, "\"];\n");
    }
  }

  /* Print call edges */
  for (int i = 0; i < cg->num_edges; i++) {
    CallGraphEdge *edge = &cg->edges[i];
    if (edge->caller && edge->caller->name) {
      fprintf(out, "  \"");
      escape_dot_string(out, edge->caller->name);
      fprintf(out, "\" -> \"");
      if (edge->callee && edge->callee->name) {
        escape_dot_string(out, edge->callee->name);
        fprintf(out, "\";\n");
      } else if (edge->callee_name) {
        escape_dot_string(out, edge->callee_name);
        fprintf(out, "\" [style=dashed, color=red];\n");
      }
    }
  }

  fprintf(out, "}\n");
}
