%option noyywrap yylineno noinput nounput

%{
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "error.h"
#include "parser.tab.h"

int current_token_line = 1;
static int comment_depth = 0;
static int comment_start_line = 1;

static int all_digits(const char *text) {
    size_t i;

    if(text == NULL || text[0] == '\0') {
        return 0;
    }

    for(i = 0; text[i] != '\0'; ++i) {
        if(!isdigit((unsigned char)text[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_valid_exponent_part(const char *text) {
    size_t i = 0;

    if(text[i] == '+' || text[i] == '-') {
        ++i;
    }
    return all_digits(text + i);
}

static int is_valid_float_literal(const char *text) {
    const char *dot = strchr(text, '.');
    const char *exp = strpbrk(text, "eE");
    size_t i;

    if(dot == NULL) {
        return 0;
    }

    if(exp == NULL) {
        if(dot == text || dot[1] == '\0') {
            return 0;
        }
        for(i = 0; text[i] != '\0'; ++i) {
            if(text[i] != '.' && !isdigit((unsigned char)text[i])) {
                return 0;
            }
        }
        return 1;
    }

    if(exp < dot) {
        return 0;
    }
    if(!is_valid_exponent_part(exp + 1)) {
        return 0;
    }

    if(dot == text) {
        if(exp == dot + 1) {
            return 0;
        }
        for(i = 1; text + i < exp; ++i) {
            if(!isdigit((unsigned char)text[i])) {
                return 0;
            }
        }
        return 1;
    }

    for(i = 0; text + i < dot; ++i) {
        if(!isdigit((unsigned char)text[i])) {
            return 0;
        }
    }
    for(i = (size_t)(dot - text) + 1; text + i < exp; ++i) {
        if(!isdigit((unsigned char)text[i])) {
            return 0;
        }
    }
    return 1;
}

static int return_simple_token(int token, const char *name) {
    current_token_line = yylineno;
    yylval.node = ast_new_terminal(name, yylineno);
    return token;
}

static int return_string_token(int token, const char *name) {
    current_token_line = yylineno;
    yylval.node = ast_new_terminal_string(name, yylineno, yytext);
    return token;
}

static int return_int_token(const char *text, int base) {
    current_token_line = yylineno;
    yylval.node = ast_new_terminal_int("INT", yylineno, (int)strtol(text, NULL, base));
    return INT;
}

static int return_float_token(const char *text) {
    current_token_line = yylineno;
    yylval.node = ast_new_terminal_float("FLOAT", yylineno, strtof(text, NULL));
    return FLOAT;
}
%}

%x COMMENT

DIGIT      [0-9]
LETTER     [_A-Za-z]
ID_BODY    [_A-Za-z0-9]

%%

[ \t\r]+                         ;
\n                               ;
"//".*                           ;
"/*"                             {
                                     comment_depth = 1;
                                     comment_start_line = yylineno;
                                     BEGIN(COMMENT);
                                 }

<COMMENT>"/*"                    {
                                     report_lexical_error(
                                         yylineno,
                                         "Nested comment"
                                     );
                                     ++comment_depth;
                                 }
<COMMENT>"*/"                    { if(--comment_depth == 0) BEGIN(INITIAL); }
<COMMENT>\n                      ;
<COMMENT>.                       ;
<COMMENT><<EOF>>                 {
                                     report_lexical_error(
                                         comment_start_line,
                                         "Unterminated comment"
                                     );
                                     BEGIN(INITIAL);
                                     return 0;
                                 }

"int"|"float"                    { return return_string_token(TYPE, "TYPE"); }
"struct"                         { return return_simple_token(STRUCT, "STRUCT"); }
"return"                         { return return_simple_token(RETURN, "RETURN"); }
"if"                             { return return_simple_token(IF, "IF"); }
"else"                           { return return_simple_token(ELSE, "ELSE"); }
"while"                          { return return_simple_token(WHILE, "WHILE"); }

";"                              { return return_simple_token(SEMI, "SEMI"); }
","                              { return return_simple_token(COMMA, "COMMA"); }
"="                              { return return_simple_token(ASSIGNOP, "ASSIGNOP"); }
">="|"<="|"=="|"!="|">"|"<"      { return return_string_token(RELOP, "RELOP"); }
"+"                              { return return_simple_token(PLUS, "PLUS"); }
"-"                              { return return_simple_token(MINUS, "MINUS"); }
"*"                              { return return_simple_token(STAR, "STAR"); }
"/"                              { return return_simple_token(DIV, "DIV"); }
"&&"                             { return return_simple_token(AND, "AND"); }
"||"                             { return return_simple_token(OR, "OR"); }
"."                              { return return_simple_token(DOT, "DOT"); }
"!"                              { return return_simple_token(NOT, "NOT"); }
"("                              { return return_simple_token(LP, "LP"); }
")"                              { return return_simple_token(RP, "RP"); }
"["                              { return return_simple_token(LB, "LB"); }
"]"                              { return return_simple_token(RB, "RB"); }
"{"                              { return return_simple_token(LC, "LC"); }
"}"                              { return return_simple_token(RC, "RC"); }

0[xX][A-Za-z0-9_]+               {
                                     size_t i;
                                     int valid = 1;
                                     for(i = 2; yytext[i] != '\0'; ++i) {
                                         if(!isxdigit((unsigned char)yytext[i])) {
                                             valid = 0;
                                             break;
                                         }
                                     }
                                     if(valid) {
                                         return return_int_token(yytext, 16);
                                     }
                                     report_lexical_error(yylineno, "Invalid hexadecimal integer");
                                 }
0[xX]                            { report_lexical_error(yylineno, "Invalid hexadecimal integer"); }
{DIGIT}+"."{DIGIT}*([eE][+-]?[A-Za-z0-9_]*)? {
                                     if(is_valid_float_literal(yytext)) {
                                         return return_float_token(yytext);
                                     }
                                     report_lexical_error(
                                         yylineno,
                                         "Invalid floating-point literal"
                                     );
                                 }
"."{DIGIT}+([eE][+-]?[A-Za-z0-9_]*)? {
                                     if(is_valid_float_literal(yytext)) {
                                         return return_float_token(yytext);
                                     }
                                     report_lexical_error(
                                         yylineno,
                                         "Invalid floating-point literal"
                                     );
                                 }
{DIGIT}+{LETTER}{ID_BODY}*       { report_lexical_error(yylineno, "Invalid identifier"); }
0{DIGIT}+                        {
                                     size_t i;
                                     int valid = 1;
                                     for(i = 1; yytext[i] != '\0'; ++i) {
                                         if(yytext[i] < '0' || yytext[i] > '7') {
                                             valid = 0;
                                             break;
                                         }
                                     }
                                     if(valid) {
                                         return return_int_token(yytext, 8);
                                     }
                                     report_lexical_error(yylineno, "Invalid octal integer");
                                 }
[1-9]{DIGIT}*|0                  { return return_int_token(yytext, 10); }
{LETTER}{ID_BODY}*               { return return_string_token(ID, "ID"); }

.                                {
                                     char message[64];
                                     snprintf(
                                         message,
                                         sizeof(message),
                                         "Mysterious character \"%s\"",
                                         yytext
                                     );
                                     report_lexical_error(yylineno, message);
                                 }

%%
