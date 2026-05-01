#ifndef CFG_CFG_H
#define CFG_CFG_H

#include <stdio.h>
#include "../ast/ast.h"

/* Forward declarations */
typedef struct CFGProgram CFGProgram;
typedef struct CFGFile CFGFile;
typedef struct CFGFunction CFGFunction;
typedef struct CFGNode CFGNode;
typedef struct CFGOperation CFGOperation;
typedef struct CFGError CFGError;
typedef struct CallGraph CallGraph;
typedef struct CallGraphEdge CallGraphEdge;

/* ============================================================================
 * CFG OPERATION - represents an elementary operation in a basic block
 * ============================================================================ */

/* Operation kinds */
typedef enum {
    CFG_OP_ASSIGN,      /* assignment: var = expr */
    CFG_OP_BINOP,       /* binary operation: left op right */
    CFG_OP_UNOP,        /* unary operation: op expr */
    CFG_OP_CALL,        /* function call: func(args...) */
    CFG_OP_INDEX,       /* array indexing: base[index] */
    CFG_OP_VAR,         /* variable reference */
    CFG_OP_LITERAL,     /* literal value */
    CFG_OP_COND,        /* condition evaluation (for if/while) */
    CFG_OP_RETURN,      /* return statement */
    CFG_OP_BREAK,       /* break statement */
    CFG_OP_VARDECL,     /* variable declaration */
    CFG_OP_FIELD_ACCESS, /* field access: obj.field */
    CFG_OP_METHOD_CALL, /* method call: obj.method(args...) */
    CFG_OP_NEW          /* object instantiation: new Class(args...) */
} CFGOperationKind;

struct CFGOperation {
    CFGOperationKind kind;
    char *op_name;              /* operation name (e.g., "+", "=", "call") */
    ASTNode *ast_node;          /* reference to AST node (may be NULL) */
    CFGOperation **operands;    /* array of operand operations */
    int num_operands;
    int capacity;
};

/* ============================================================================
 * CFG NODE - represents a basic block in the control flow graph
 * ============================================================================ */

struct CFGNode {
    int id;                     /* unique numeric id within function */
    int is_entry;               /* 1 if this is the entry block */
    int is_exit;                /* 1 if this is the exit block */
    
    /* Outgoing edges */
    CFGNode *successor_true;    /* true branch (for conditional) */
    CFGNode *successor_false;   /* false branch (for conditional) */
    CFGNode *successor;         /* unconditional successor */
    
    /* Operations in this basic block */
    CFGOperation **operations;
    int num_operations;
    int operations_capacity;
};

/* ============================================================================
 * CFG FUNCTION - represents a function and its CFG
 * ============================================================================ */

struct CFGFunction {
    char *name;                 /* function name */
    char *return_type;          /* return type (may be NULL for void) */
    
    /* Parameters */
    struct {
        char *name;
        char *type;
    } *parameters;
    int num_parameters;
    int parameters_capacity;
    
    char *source_file;          /* source file containing this function */
    
    /* CFG structure */
    CFGNode *entry;             /* entry basic block */
    CFGNode *exit;              /* exit basic block */
    CFGNode **all_nodes;        /* all basic blocks */
    int num_nodes;
    int nodes_capacity;
};

/* ============================================================================
 * CFG FILE - represents a source file and its AST
 * ============================================================================ */

struct CFGFile {
    char *filename;              /* source file path */
    ASTNode *ast_root;           /* AST root for this file */
    CFGFunction **functions;     /* functions discovered in this file */
    int num_functions;
    int functions_capacity;
};

/* ============================================================================
 * CFG ERROR - represents an analysis error
 * ============================================================================ */

typedef enum {
    CFG_ERR_BREAK_OUTSIDE_LOOP,
    CFG_ERR_UNKNOWN_FUNCTION,
    CFG_ERR_INVALID_AST,
    CFG_ERR_PARSE_ERROR
} CFGErrorKind;

struct CFGError {
    CFGErrorKind kind;
    char *message;               /* error message */
    char *function_name;         /* function where error occurred (may be NULL) */
    char *source_file;           /* source file (may be NULL) */
    int line;                    /* line number (0 if unknown) */
    int column;                  /* column number (0 if unknown) */
};

/* ============================================================================
 * CALL GRAPH - represents function call relationships
 * ============================================================================ */

struct CallGraphEdge {
    CFGFunction *caller;        /* calling function */
    CFGFunction *callee;         /* called function (may be NULL if unresolved) */
    char *callee_name;           /* callee name (for unresolved calls) */
};

struct CallGraph {
    CallGraphEdge *edges;       /* array of call edges */
    int num_edges;
    int edges_capacity;
};

/* ============================================================================
 * CFG PROGRAM - root object for CFG analysis
 * ============================================================================ */

struct CFGProgram {
    CFGFile **files;            /* collection of input files */
    int num_files;
    int files_capacity;
    
    CFGFunction **all_functions; /* all functions across all files */
    int num_all_functions;
    int all_functions_capacity;
    
    CallGraph *call_graph;      /* global call graph */
    
    CFGError **errors;          /* collection of errors */
    int num_errors;
    int errors_capacity;
    
    int next_node_id;           /* counter for unique node IDs */
};

/* ============================================================================
 * CFG PROGRAM API - main interface
 * ============================================================================ */

/* Create and destroy CFGProgram */
CFGProgram *cfg_prog_create(void);
void cfg_prog_free(CFGProgram *prog);

/* Add a file to the program */
int cfg_prog_add_file(CFGProgram *prog, const char *filename, ASTNode *ast_root);

/* Build CFG for all functions in all files */
int cfg_prog_build(CFGProgram *prog);

/* ============================================================================
 * ACCESSORS - iterate over functions, nodes, operations, errors
 * ============================================================================ */

/* Get number of functions */
int cfg_prog_get_num_functions(CFGProgram *prog);

/* Get function by index */
CFGFunction *cfg_prog_get_function(CFGProgram *prog, int index);

/* Get function by name */
CFGFunction *cfg_prog_find_function(CFGProgram *prog, const char *name);

/* Get number of errors */
int cfg_prog_get_num_errors(CFGProgram *prog);

/* Get error by index */
CFGError *cfg_prog_get_error(CFGProgram *prog, int index);

/* Get call graph */
CallGraph *cfg_prog_get_call_graph(CFGProgram *prog);

/* ============================================================================
 * CFG FUNCTION ACCESSORS
 * ============================================================================ */

const char *cfg_function_get_name(CFGFunction *func);
const char *cfg_function_get_return_type(CFGFunction *func);
int cfg_function_get_num_parameters(CFGFunction *func);
const char *cfg_function_get_parameter_name(CFGFunction *func, int index);
const char *cfg_function_get_parameter_type(CFGFunction *func, int index);
const char *cfg_function_get_source_file(CFGFunction *func);

CFGNode *cfg_function_get_entry(CFGFunction *func);
CFGNode *cfg_function_get_exit(CFGFunction *func);
int cfg_function_get_num_nodes(CFGFunction *func);
CFGNode *cfg_function_get_node(CFGFunction *func, int index);

/* ============================================================================
 * CFG NODE ACCESSORS
 * ============================================================================ */

int cfg_node_get_id(CFGNode *node);
int cfg_node_is_entry(CFGNode *node);
int cfg_node_is_exit(CFGNode *node);
CFGNode *cfg_node_get_successor(CFGNode *node);
CFGNode *cfg_node_get_successor_true(CFGNode *node);
CFGNode *cfg_node_get_successor_false(CFGNode *node);
int cfg_node_get_num_operations(CFGNode *node);
CFGOperation *cfg_node_get_operation(CFGNode *node, int index);

/* ============================================================================
 * CFG OPERATION ACCESSORS
 * ============================================================================ */

CFGOperationKind cfg_operation_get_kind(CFGOperation *op);
const char *cfg_operation_get_name(CFGOperation *op);
int cfg_operation_get_num_operands(CFGOperation *op);
CFGOperation *cfg_operation_get_operand(CFGOperation *op, int index);
ASTNode *cfg_operation_get_ast_node(CFGOperation *op);

/* ============================================================================
 * CALL GRAPH ACCESSORS
 * ============================================================================ */

int cfg_call_graph_get_num_edges(CallGraph *cg);
CallGraphEdge *cfg_call_graph_get_edge(CallGraph *cg, int index);
CFGFunction *cfg_call_edge_get_caller(CallGraphEdge *edge);
CFGFunction *cfg_call_edge_get_callee(CallGraphEdge *edge);
const char *cfg_call_edge_get_callee_name(CallGraphEdge *edge);

/* ============================================================================
 * DOT EXPORT
 * ============================================================================ */

/* Export a single function's CFG to DOT */
void cfg_function_print_dot(FILE *out, CFGFunction *func, CFGProgram *prog);

/* Export call graph to DOT */
void cfg_call_graph_print_dot(FILE *out, CallGraph *cg, CFGProgram *prog);

#endif /* CFG_CFG_H */
