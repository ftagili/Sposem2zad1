%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ast/ast.h"

void yyerror(const char *s);
int yylex(void);
%}

/* Семантические типы */
%union {
    char* str;
    ASTNode* node;
}

/* Токены */
%token <str> IDENTIFIER
%token <str> BOOL_LITERAL STRING_LITERAL CHAR_LITERAL HEX_LITERAL BITS_LITERAL DEC_LITERAL
%token IF ELSE WHILE DO BREAK RETURN
%token EXTERN CLASS PUBLIC PRIVATE
%token <str> BUILTIN_TYPE
%token <str> PLUS MINUS STAR SLASH PERCENT
%token <str> LT GT LE GE EQEQ NEQ
%token LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA LBRACKET RBRACKET ASSIGN
%token <str> PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%token COLON DOT NEW AMPERSAND

/* Типы нетерминалов */
%type <node> source sourceItemList sourceItem funcDef funcSignature argList argDefList argDef
%type <node> typeRef statementBlock statementList statement varDecl varList varItemList optAssign
%type <node> ifStmt optElse whileStmt doWhileStmt breakStmt returnStmt exprStmt expr argExprList exprList literal
%type <node> importSpec optImportSpec classDef optBase member memberList field fieldList optModifier optTypeRef

%start source

/* Приоритеты (ниже -> ниже приоритет) */
%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%left  EQEQ NEQ LT GT LE GE
%left  PLUS MINUS
%left  STAR SLASH PERCENT
%right UPLUS UMINUS
%left  DOT

%%

source
    : sourceItemList
      { $$ = ast_create_node("source"); ast_add_child($$, $1); ast_set_root($$); }
    ;

sourceItemList
    : /* пусто */
      { $$ = ast_create_node("items"); }
    | sourceItemList sourceItem
      { $$ = $1; ast_add_child($$, $2); }
    ;

sourceItem
    : funcDef
      { $$ = $1; }
    | classDef
      { $$ = $1; }
    ;

funcDef
    : optImportSpec funcSignature statementBlock
      { $$ = ast_create_node("funcDef");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); ast_add_child($$, $3); }
    | optImportSpec funcSignature SEMICOLON
      { $$ = ast_create_node("funcDecl");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); }
    ;

optImportSpec
    : /* пусто */
      { $$ = NULL; }
    | importSpec
      { $$ = $1; }
    ;

importSpec
    : EXTERN LPAREN STRING_LITERAL RPAREN
      { $$ = ast_create_node("import");
        ASTNode* dll = ast_create_leaf_token("dll", $3); free($3);
        ast_add_child($$, dll); }
    | EXTERN LPAREN STRING_LITERAL COMMA STRING_LITERAL RPAREN
      { $$ = ast_create_node("import");
        ASTNode* dll = ast_create_leaf_token("dll", $3); free($3);
        ASTNode* entry = ast_create_leaf_token("entry", $5); free($5);
        ast_add_child($$, dll); ast_add_child($$, entry); }
    ;

funcSignature
    : typeRef IDENTIFIER LPAREN argList RPAREN
      { $$ = ast_create_node("signature");
        ast_add_child($$, $1);
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        ast_add_child($$, $4);
      }
    ;

argList
    : /* пусто */
      { $$ = ast_create_node("args"); }
    | argDefList
      { $$ = ast_create_node("args"); ast_add_child($$, $1); }
    ;

argDefList
    : argDef
      { $$ = ast_create_node("arglist"); ast_add_child($$, $1); }
    | argDefList COMMA argDef
      { $$ = $1; ast_add_child($$, $3); }
    ;

argDef
    : typeRef IDENTIFIER
      { $$ = ast_create_node("arg");
        ast_add_child($$, $1);
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
      }
    ;

typeRef
    : BUILTIN_TYPE
      { $$ = ast_create_leaf_token("type", $1); free($1); }
    | IDENTIFIER
      { $$ = ast_create_leaf_token("typeRef", $1); free($1); }
    | typeRef LBRACKET RBRACKET
      { $$ = ast_create_node("array"); ast_add_child($$, $1); }
    ;

statementBlock
    : LBRACE statementList RBRACE
      { $$ = ast_create_node("block"); ast_add_child($$, $2); }
    ;

statementList
    : /* пусто */
      { $$ = ast_create_node("stmts"); }
    | statementList statement
      { $$ = $1; ast_add_child($$, $2); }
    ;

statement
    : varDecl
      { $$ = $1; }
    | ifStmt
      { $$ = $1; }
    | whileStmt
      { $$ = $1; }
    | doWhileStmt
      { $$ = $1; }
    | breakStmt
      { $$ = $1; }
    | returnStmt
      { $$ = $1; }
    | exprStmt
      { $$ = $1; }
    | statementBlock
      { $$ = $1; }
    ;

varDecl
    : typeRef varList SEMICOLON
      { $$ = ast_create_node("vardecl"); ast_add_child($$, $1); ast_add_child($$, $2); }
    ;

varList
    : varItemList
      { $$ = $1; }
    ;

varItemList
    : IDENTIFIER optAssign
      { $$ = ast_create_node("vars");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id); ast_add_child($$, $2);
      }
    | varItemList COMMA IDENTIFIER optAssign
      { $$ = $1;
        ASTNode* id = ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, id); ast_add_child($$, $4);
      }
    ;

optAssign
    : /* пусто */
      { $$ = ast_create_node("noinit"); }
    | ASSIGN expr
      { $$ = ast_create_node("assign"); ast_add_child($$, $2); }
    ;

ifStmt
    : IF LPAREN expr RPAREN statement optElse
      { $$ = ast_create_node("if"); ast_add_child($$, $3); ast_add_child($$, $5); ast_add_child($$, $6); }
    ;

optElse
    : /* пусто */
      { $$ = ast_create_node("noelse"); }
    | ELSE statement
      { $$ = ast_create_node("else"); ast_add_child($$, $2); }
    ;

whileStmt
    : WHILE LPAREN expr RPAREN statement
      { $$ = ast_create_node("while"); ast_add_child($$, $3); ast_add_child($$, $5); }
    ;

doWhileStmt
    : DO statementBlock WHILE LPAREN expr RPAREN SEMICOLON
      { $$ = ast_create_node("doWhile"); ast_add_child($$, $2); ast_add_child($$, $5); }
    ;

breakStmt
    : BREAK SEMICOLON
      { $$ = ast_create_node("break"); }
    ;

returnStmt
    : RETURN expr SEMICOLON
      { $$ = ast_create_node("return"); ast_add_child($$, $2); }
    | RETURN SEMICOLON
      { $$ = ast_create_node("return"); }
    ;

exprStmt
    : expr SEMICOLON
      { $$ = ast_create_node("exprstmt"); ast_add_child($$, $1); }
    ;

/* --- выражения --- */
expr
    /* присваивание */
    : IDENTIFIER ASSIGN expr
      { $$ = ast_create_node("assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id); ast_add_child($$, $3); }

    /* составные присваивания */
    | IDENTIFIER PLUS_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER MINUS_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER STAR_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER SLASH_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }
    | IDENTIFIER PERCENT_ASSIGN expr
      { $$ = ast_create_node("compound_assign");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ASTNode* op = ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, id); ast_add_child($$, op); ast_add_child($$, $3); }

    /* арифметика */
    | expr STAR    expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr SLASH   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr PERCENT expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }

    | expr PLUS    expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr MINUS   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }

    /* сравнения */
    | expr LT   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr GT   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr LE   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr GE   expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr EQEQ expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }
    | expr NEQ  expr
      { $$ = ast_create_node("binop"); ast_add_child($$, $1);
        ASTNode* op=ast_create_leaf_token("op", $2); free($2);
        ast_add_child($$, op); ast_add_child($$, $3); }

    /* унарные + и - */
    | MINUS expr %prec UMINUS
      { $$ = ast_create_node("unop");
        ASTNode* op=ast_create_leaf_token("op", $1); free($1);
        ast_add_child($$, op); ast_add_child($$, $2); }
    | PLUS  expr %prec UPLUS
      { $$ = ast_create_node("unop");
        ASTNode* op=ast_create_leaf_token("op", $1); free($1);
        ast_add_child($$, op); ast_add_child($$, $2); }
    /* адрес переменной &var */
    | AMPERSAND IDENTIFIER
      { $$ = ast_create_node("address");
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id); }

    /* new ClassName(...) */
    | NEW IDENTIFIER LPAREN argExprList RPAREN
      { $$ = ast_create_node("new");
        ASTNode* id=ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        ast_add_child($$, $4);
      }
    | NEW IDENTIFIER
      { $$ = ast_create_node("new");
        ASTNode* id=ast_create_leaf_token("id", $2); free($2);
        ASTNode* args=ast_create_node("args");
        ast_add_child($$, id);
        ast_add_child($$, args);
      }

    /* доступ к членам: obj.field */
    | expr DOT IDENTIFIER
      { $$ = ast_create_node("fieldAccess");
        ASTNode* id=ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, $1);
        ast_add_child($$, id);
      }

    /* вызов метода: obj.method(args) */
    | expr DOT IDENTIFIER LPAREN argExprList RPAREN
      { $$ = ast_create_node("methodCall");
        ASTNode* id=ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, $1);
        ast_add_child($$, id);
        ast_add_child($$, $5);
      }

    /* индекс после точки (если вдруг понадобится): obj.arr[idx] */
    | expr DOT IDENTIFIER LBRACKET argExprList RBRACKET
      { $$ = ast_create_node("memberIndex");
        ASTNode* id=ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, $1);
        ast_add_child($$, id);
        ast_add_child($$, $5);
      }

    /* прочее */
    | LPAREN expr RPAREN
      { $$ = $2; }
    | IDENTIFIER
      { $$ = ast_create_leaf_token("id", $1); free($1); }
    | literal
      { $$ = $1; }
    | IDENTIFIER LPAREN argExprList RPAREN
      { $$ = ast_create_node("call");
        ASTNode* id=ast_create_leaf_token("id",$1); free($1);
        ast_add_child($$, id); ast_add_child($$, $3); }
    | IDENTIFIER LBRACKET argExprList RBRACKET
      { $$ = ast_create_node("index");
        ASTNode* id=ast_create_leaf_token("id",$1); free($1);
        ast_add_child($$, id); ast_add_child($$, $3); }
    ;

argExprList
    : /* пусто */
      { $$ = ast_create_node("args"); }
    | exprList
      { $$ = ast_create_node("args"); ast_add_child($$, $1); }
    ;

exprList
    : expr
      { $$ = ast_create_node("list"); ast_add_child($$, $1); }
    | exprList COMMA expr
      { $$ = $1; ast_add_child($$, $3); }
    ;

literal
    : BOOL_LITERAL
      { $$ = ast_create_leaf_token("bool", $1); free($1); }
    | STRING_LITERAL
      { $$ = ast_create_leaf_token("string", $1); free($1); }
    | CHAR_LITERAL
      { $$ = ast_create_leaf_token("char", $1); free($1); }
    | HEX_LITERAL
      { $$ = ast_create_leaf_token("hex", $1); free($1); }
    | BITS_LITERAL
      { $$ = ast_create_leaf_token("bits", $1); free($1); }
    | DEC_LITERAL
      { $$ = ast_create_leaf_token("dec", $1); free($1); }
    ;

/* --------- КЛАССЫ / НАСЛЕДОВАНИЕ --------- */

classDef
    : CLASS IDENTIFIER optBase LBRACE memberList RBRACE
      { $$ = ast_create_node("class");
        ASTNode* id = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, id);
        if ($3) ast_add_child($$, $3);
        ast_add_child($$, $5);
      }
    ;

/* optBase: ": BaseClass" или пусто */
optBase
    : /* пусто */
      { $$ = NULL; }
    | COLON IDENTIFIER
      { $$ = ast_create_node("extends");
        ASTNode* base = ast_create_leaf_token("id", $2); free($2);
        ast_add_child($$, base);
      }
    ;

memberList
    : /* пусто */
      { $$ = ast_create_node("members"); }
    | memberList member
      { $$ = $1; ast_add_child($$, $2); }
    ;

member
    : optModifier funcDef
      { $$ = ast_create_node("member");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); }
    | optModifier field
      { $$ = ast_create_node("member");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); }
    ;

optModifier
    : /* пусто */
      { $$ = NULL; }
    | PUBLIC
      { $$ = ast_create_leaf_token("modifier", "public"); }
    | PRIVATE
      { $$ = ast_create_leaf_token("modifier", "private"); }
    ;

field
    : optTypeRef fieldList SEMICOLON
      { $$ = ast_create_node("field");
        if ($1) ast_add_child($$, $1);
        ast_add_child($$, $2); }
    ;

optTypeRef
    : /* пусто */
      { $$ = NULL; }
    | typeRef
      { $$ = $1; }
    ;

fieldList
    : IDENTIFIER
      { $$ = ast_create_node("fieldlist");
        ASTNode* id = ast_create_leaf_token("id", $1); free($1);
        ast_add_child($$, id); }
    | fieldList COMMA IDENTIFIER
      { $$ = $1;
        ASTNode* id = ast_create_leaf_token("id", $3); free($3);
        ast_add_child($$, id); }
    ;

%%

int parse_error = 0;

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
    parse_error = 1;
}
