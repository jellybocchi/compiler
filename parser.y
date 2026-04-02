%code requires {
#include "ast.h"
}

%{
#include <stdio.h>

#include "ast.h"
#include "error.h"

AstNode *syntax_tree = NULL;
extern int current_token_line;

int yylex(void);
void yyerror(const char *message);
%}

%define parse.error verbose

%union {
    AstNode *node;
}

%token <node> INT FLOAT ID TYPE
%token <node> SEMI COMMA ASSIGNOP RELOP PLUS MINUS STAR DIV AND OR DOT NOT
%token <node> LP RP LB RB LC RC
%token <node> STRUCT RETURN IF ELSE WHILE

%type <node> Program ExtDefList ExtDef ExtDecList Specifier StructSpecifier OptTag Tag
%type <node> VarDec FunDec VarList ParamDec CompSt StmtList Stmt DefList Def DecList Dec
%type <node> Exp Args

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE
%right ASSIGNOP
%left OR
%left AND
%left RELOP
%left PLUS MINUS
%left STAR DIV
%right NOT UMINUS
%left LP RP LB RB DOT

%start Program

%%

Program
    : ExtDefList
      {
          syntax_tree = ast_new_nonterminal("Program", 1, $1);
          $$ = syntax_tree;
      }
    ;

ExtDefList
    : ExtDef ExtDefList
      { $$ = ast_new_nonterminal("ExtDefList", 2, $1, $2); }
    | %empty
      { $$ = NULL; }
    ;

ExtDef
    : Specifier ExtDecList SEMI
      { $$ = ast_new_nonterminal("ExtDef", 3, $1, $2, $3); }
    | Specifier SEMI
      { $$ = ast_new_nonterminal("ExtDef", 2, $1, $2); }
    | Specifier FunDec CompSt
      { $$ = ast_new_nonterminal("ExtDef", 3, $1, $2, $3); }
    | error SEMI
      {
          yyerrok;
          $$ = NULL;
      }
    ;

ExtDecList
    : VarDec
      { $$ = ast_new_nonterminal("ExtDecList", 1, $1); }
    | VarDec COMMA ExtDecList
      { $$ = ast_new_nonterminal("ExtDecList", 3, $1, $2, $3); }
    ;

Specifier
    : TYPE
      { $$ = ast_new_nonterminal("Specifier", 1, $1); }
    | StructSpecifier
      { $$ = ast_new_nonterminal("Specifier", 1, $1); }
    ;

StructSpecifier
    : STRUCT OptTag LC DefList RC
      { $$ = ast_new_nonterminal("StructSpecifier", 5, $1, $2, $3, $4, $5); }
    | STRUCT Tag
      { $$ = ast_new_nonterminal("StructSpecifier", 2, $1, $2); }
    ;

OptTag
    : ID
      { $$ = ast_new_nonterminal("OptTag", 1, $1); }
    | %empty
      { $$ = NULL; }
    ;

Tag
    : ID
      { $$ = ast_new_nonterminal("Tag", 1, $1); }
    ;

VarDec
    : ID
      { $$ = ast_new_nonterminal("VarDec", 1, $1); }
    | VarDec LB INT RB
      { $$ = ast_new_nonterminal("VarDec", 4, $1, $2, $3, $4); }
    ;

FunDec
    : ID LP VarList RP
      { $$ = ast_new_nonterminal("FunDec", 4, $1, $2, $3, $4); }
    | ID LP RP
      { $$ = ast_new_nonterminal("FunDec", 3, $1, $2, $3); }
    ;

VarList
    : ParamDec COMMA VarList
      { $$ = ast_new_nonterminal("VarList", 3, $1, $2, $3); }
    | ParamDec
      { $$ = ast_new_nonterminal("VarList", 1, $1); }
    ;

ParamDec
    : Specifier VarDec
      { $$ = ast_new_nonterminal("ParamDec", 2, $1, $2); }
    ;

CompSt
    : LC DefList StmtList RC
      { $$ = ast_new_nonterminal("CompSt", 4, $1, $2, $3, $4); }
    | LC error RC
      {
          yyerrok;
          $$ = NULL;
      }
    ;

StmtList
    : Stmt StmtList
      { $$ = ast_new_nonterminal("StmtList", 2, $1, $2); }
    | %empty
      { $$ = NULL; }
    ;

Stmt
    : Exp SEMI
      { $$ = ast_new_nonterminal("Stmt", 2, $1, $2); }
    | CompSt
      { $$ = ast_new_nonterminal("Stmt", 1, $1); }
    | RETURN Exp SEMI
      { $$ = ast_new_nonterminal("Stmt", 3, $1, $2, $3); }
    | IF LP Exp RP Stmt %prec LOWER_THAN_ELSE
      { $$ = ast_new_nonterminal("Stmt", 5, $1, $2, $3, $4, $5); }
    | IF LP Exp RP Stmt ELSE Stmt
      { $$ = ast_new_nonterminal("Stmt", 7, $1, $2, $3, $4, $5, $6, $7); }
    | WHILE LP Exp RP Stmt
      { $$ = ast_new_nonterminal("Stmt", 5, $1, $2, $3, $4, $5); }
    | error SEMI
      {
          yyerrok;
          $$ = NULL;
      }
    ;

DefList
    : Def DefList
      { $$ = ast_new_nonterminal("DefList", 2, $1, $2); }
    | %empty
      { $$ = NULL; }
    ;

Def
    : Specifier DecList SEMI
      { $$ = ast_new_nonterminal("Def", 3, $1, $2, $3); }
    | error SEMI
      {
          yyerrok;
          $$ = NULL;
      }
    ;

DecList
    : Dec
      { $$ = ast_new_nonterminal("DecList", 1, $1); }
    | Dec COMMA DecList
      { $$ = ast_new_nonterminal("DecList", 3, $1, $2, $3); }
    ;

Dec
    : VarDec
      { $$ = ast_new_nonterminal("Dec", 1, $1); }
    | VarDec ASSIGNOP Exp
      { $$ = ast_new_nonterminal("Dec", 3, $1, $2, $3); }
    ;

Exp
    : Exp ASSIGNOP Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp AND Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp OR Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp RELOP Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp PLUS Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp MINUS Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp STAR Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp DIV Exp
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | LP Exp RP
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | MINUS Exp %prec UMINUS
      { $$ = ast_new_nonterminal("Exp", 2, $1, $2); }
    | NOT Exp
      { $$ = ast_new_nonterminal("Exp", 2, $1, $2); }
    | ID LP Args RP
      { $$ = ast_new_nonterminal("Exp", 4, $1, $2, $3, $4); }
    | ID LP RP
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | Exp LB Exp RB
      { $$ = ast_new_nonterminal("Exp", 4, $1, $2, $3, $4); }
    | Exp DOT ID
      { $$ = ast_new_nonterminal("Exp", 3, $1, $2, $3); }
    | ID
      { $$ = ast_new_nonterminal("Exp", 1, $1); }
    | INT
      { $$ = ast_new_nonterminal("Exp", 1, $1); }
    | FLOAT
      { $$ = ast_new_nonterminal("Exp", 1, $1); }
    ;

Args
    : Exp COMMA Args
      { $$ = ast_new_nonterminal("Args", 3, $1, $2, $3); }
    | Exp
      { $$ = ast_new_nonterminal("Args", 1, $1); }
    ;

%%

void yyerror(const char *message) {
    (void)message;
    if(current_token_line <= 0) {
        current_token_line = 1;
    }
    report_syntax_error(current_token_line, "Syntax error");
}
