%{
#include "../ast/ast.h"
#include "../../build/parser.tab.h"
#include <string.h>
#include <stdlib.h>

static char* dup_yytext(const char* s) {
  size_t n = strlen(s) + 1;
  char* d = (char*)malloc(n);
  if (d) memcpy(d, s, n);
  return d;
}
%}

%option noyywrap
%option nounput
%option noinput

%%
"if"            return IF;
"else"          return ELSE;
"while"         return WHILE;
"do"            return DO;
"break"         return BREAK;
"return"        return RETURN;
"extern"        return EXTERN;
"class"         return CLASS;
"public"        return PUBLIC;
"private"       return PRIVATE;
"new"           return NEW;

"bool"|"byte"|"int"|"uint"|"long"|"ulong"|"char"|"string"  { yylval.str = dup_yytext(yytext); return BUILTIN_TYPE; }

"true"|"false"  { yylval.str = dup_yytext(yytext); return BOOL_LITERAL; }
0[xX][0-9A-Fa-f]+       { yylval.str = dup_yytext(yytext); return HEX_LITERAL; }
0[bB][01]+              { yylval.str = dup_yytext(yytext); return BITS_LITERAL; }
[0-9]+                  { yylval.str = dup_yytext(yytext); return DEC_LITERAL; }
\"([^\\"]|\\.)*\"  { yylval.str = dup_yytext(yytext); return STRING_LITERAL; }
'[^']'                  { yylval.str = dup_yytext(yytext); return CHAR_LITERAL; }

[a-zA-Z_][a-zA-Z_0-9]*  { yylval.str = dup_yytext(yytext); return IDENTIFIER; }

"=="            { yylval.str = dup_yytext(yytext); return EQEQ; }
"!="            { yylval.str = dup_yytext(yytext); return NEQ; }
"<="            { yylval.str = dup_yytext(yytext); return LE; }
">="            { yylval.str = dup_yytext(yytext); return GE; }
"<"             { yylval.str = dup_yytext(yytext); return LT; }
">"             { yylval.str = dup_yytext(yytext); return GT; }

"+="            { yylval.str = dup_yytext(yytext); return PLUS_ASSIGN; }
"-="            { yylval.str = dup_yytext(yytext); return MINUS_ASSIGN; }
"*="            { yylval.str = dup_yytext(yytext); return STAR_ASSIGN; }
"/="            { yylval.str = dup_yytext(yytext); return SLASH_ASSIGN; }
"%="            { yylval.str = dup_yytext(yytext); return PERCENT_ASSIGN; }

"+"             { yylval.str = dup_yytext(yytext); return PLUS; }
"-"             { yylval.str = dup_yytext(yytext); return MINUS; }
"*"             { yylval.str = dup_yytext(yytext); return STAR; }
"/"             { yylval.str = dup_yytext(yytext); return SLASH; }
"%"             { yylval.str = dup_yytext(yytext); return PERCENT; }

"="             return ASSIGN;
"&"             return AMPERSAND;

"{"     return LBRACE;
"}"     return RBRACE;
"("     return LPAREN;
")"     return RPAREN;
";"     return SEMICOLON;
","     return COMMA;
"["     return LBRACKET;
"]"     return RBRACKET;
"."     return DOT;
":"     return COLON;

"/*"([^*]|\*+[^*/])*\*+"/"  { /* multi-line comment */ }
"//"[^\n]*                   { /* single-line comment */ }

[[:space:]]+     { }
.                { printf("Unknown symbol: %s\n", yytext); }

%%