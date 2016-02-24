/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 *
 * Copyright (c) 2015, 2016 Lewis Van Winkle
 *
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgement in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "tinyexpr.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>


enum {TOK_NULL, TOK_END, TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_INFIX,
    TOK_VARIABLE, TOK_SEP, TOK_ERROR, TOK_FUNCTION0, TOK_FUNCTION1,
    TOK_FUNCTION2, TOK_FUNCTION3, TOK_FUNCTION4, TOK_FUNCTION5, TOK_FUNCTION6,
    TOK_FUNCTION7
};



typedef struct state {
    const char *start;
    const char *next;
    int type;
    union {double value; te_fun fun; const double *bound;};

    const te_variable *lookup;
    int lookup_len;
} state;


static int te_get_type(const int type) {
    if(type == 0) return TE_VAR;
    return type & TE_FLAG_TYPE;
}


static int te_get_arity(const int type) {
    return type & TE_MASK_ARIT;
}


static te_expr *new_expr(const int type, const te_expr *members[]) {
    int member_count = te_get_arity(type);
    size_t member_size = sizeof(te_expr*) * member_count;
    te_expr *ret = malloc(sizeof(te_expr) + member_size);
    ret->member_count = member_count;
    if(!members) {
        memset(ret->members, 0, member_size);
    } else {
        memcpy(ret->members, members, member_size);
    }
    ret->type = type;
    ret->bound = 0;
    return ret;
}


void te_free(te_expr *n) {
    int i;
    if (!n) return;
    for(i = n->member_count - 1; i >= 0; i--) te_free(n->members[i]);
    free(n);
}


static const double pi = 3.14159265358979323846;
static const double e  = 2.71828182845904523536;

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs,     TE_FUN | 1},
    {"acos", acos,    TE_FUN | 1},
    {"asin", asin,    TE_FUN | 1},
    {"atan", atan,    TE_FUN | 1},
    {"atan2", atan2,  TE_FUN | 2},
    {"ceil", ceil,    TE_FUN | 1},
    {"cos", cos,      TE_FUN | 1},
    {"cosh", cosh,    TE_FUN | 1},
    {"e", &e,         TE_VAR},
    {"exp", exp,      TE_FUN | 1},
    {"floor", floor,  TE_FUN | 1},
    {"ln", log,       TE_FUN | 1},
    {"log", log10,    TE_FUN | 1},
    {"pi", &pi,       TE_VAR},
    {"pow", pow,      TE_FUN | 2},
    {"sin", sin,      TE_FUN | 1},
    {"sinh", sinh,    TE_FUN | 1},
    {"sqrt", sqrt,    TE_FUN | 1},
    {"tan", tan,      TE_FUN | 1},
    {"tanh", tanh,    TE_FUN | 1},
    {0}
};

static const te_variable *find_function(const char *name, int len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax-imin)/2));
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        } else if (c > 0) {
            imin = i + 1;
        } else {
            imax = i - 1;
        }
    }

    return 0;
}


static const te_variable *find_var(const state *s, const char *name, int len) {
    int i;
    if (!s->lookup) return 0;
    for (i = 0; i < s->lookup_len; ++i) {
        if (strncmp(name, s->lookup[i].name, len) == 0 && s->lookup[i].name[len] == '\0') {
            return s->lookup + i;
        }
    }
    return 0;
}



static double add(double a, double b) {return a + b;}
static double sub(double a, double b) {return a - b;}
static double mul(double a, double b) {return a * b;}
static double divide(double a, double b) {return a / b;}
static double negate(double a) {return -a;}
static double comma(double a, double b) {return b;}


void next_token(state *s) {
    s->type = TOK_NULL;

    if (!*s->next){
        s->type = TOK_END;
        return;
    }

    do {

        /* Try reading a number. */
        if ((s->next[0] >= '0' && s->next[0] <= '9') || s->next[0] == '.') {
            s->value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } else {
            /* Look for a variable or builtin function call. */
            if (s->next[0] >= 'a' && s->next[0] <= 'z') {
                int arity, type;
                const char *start;
                start = s->next;
                while ((s->next[0] >= 'a' && s->next[0] <= 'z') || (s->next[0] >= '0' && s->next[0] <= '9')) s->next++;

                const te_variable *var = find_var(s, start, s->next - start);
                if (!var) var = find_function(start, s->next - start);

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    type = te_get_type(var->type);
                    arity = te_get_arity(var->type);
                    switch(type)
                    {
                        case TE_VAR:
                            s->type = TOK_VARIABLE;
                            s->bound = var->value; break;
                        case TE_FUN:
                            s->type = TOK_FUNCTION0 + arity;
                            s->fun.f0 = (void*)var->value;
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_INFIX; s->fun.f2 = add; break;
                    case '-': s->type = TOK_INFIX; s->fun.f2 = sub; break;
                    case '*': s->type = TOK_INFIX; s->fun.f2 = mul; break;
                    case '/': s->type = TOK_INFIX; s->fun.f2 = divide; break;
                    case '^': s->type = TOK_INFIX; s->fun.f2 = pow; break;
                    case '%': s->type = TOK_INFIX; s->fun.f2 = fmod; break;
                    case '(': s->type = TOK_OPEN; break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ',': s->type = TOK_SEP; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}


static te_expr *list(state *s);
static te_expr *expr(state *s);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-2> "(" <expr> "," <expr> ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (s->type) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONST, 0);
            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VAR, 0);
            ret->bound = s->bound;
            next_token(s);
            break;

        case TOK_FUNCTION0:
            ret = new_expr(TE_FUN, 0);
            ret->fun.f0 = s->fun.f0;
            next_token(s);
            if (s->type == TOK_OPEN) {
                next_token(s);
                if (s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }
            break;

        case TOK_FUNCTION1:
            ret = new_expr(TE_FUN | 1, 0);
            ret->fun.f0 = s->fun.f0;

            next_token(s);
            ret->members[0] = power(s);
            break;

        case TOK_FUNCTION2: case TOK_FUNCTION3: case TOK_FUNCTION4:
        case TOK_FUNCTION5: case TOK_FUNCTION6: case TOK_FUNCTION7:
            arity = s->type - TOK_FUNCTION0;

            ret = new_expr(TE_FUN | arity, 0);
            ret->fun.f0 = s->fun.f0;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->members[i] = expr(s);
                    if(s->type != TOK_SEP) {
                        break;
                    }
                }
                if(s->type != TOK_CLOSE || i < arity - 1) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }

            break;

        case TOK_OPEN:
            next_token(s);
            ret = list(s);
            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            s->type = TOK_ERROR;
            ret->value = 0.0/0.0;
            break;
    }

    return ret;
}


static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->fun.f2 == add || s->fun.f2 == sub)) {
        if (s->fun.f2 == sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        ret = new_expr(TE_FUN | 1, (const te_expr*[]){base(s)});
        ret->fun.f1 = negate;
    }

    return ret;
}


static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    while (s->type == TOK_INFIX && (s->fun.f2 == pow)) {
        te_fun2 t = s->fun.f2;
        next_token(s);
        ret = new_expr(TE_FUN | 2, (const te_expr*[]){ret, power(s)});
        ret->fun.f2 = t;
    }

    return ret;
}


static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);

    while (s->type == TOK_INFIX && (s->fun.f2 == mul || s->fun.f2 == divide || s->fun.f2 == fmod)) {
        te_fun2 t = s->fun.f2;
        next_token(s);
        ret = new_expr(TE_FUN | 2, (const te_expr*[]){ret, factor(s)});
        ret->fun.f2 = t;
    }

    return ret;
}


static te_expr *expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);

    while (s->type == TOK_INFIX && (s->fun.f2 == add || s->fun.f2 == sub)) {
        te_fun2 t = s->fun.f2;
        next_token(s);
        ret = new_expr(TE_FUN | 2, (const te_expr*[]){ret, term(s)});
        ret->fun.f2 = t;
    }

    return ret;
}


static te_expr *list(state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);

    while (s->type == TOK_SEP) {
        next_token(s);
        ret = new_expr(TE_FUN | 2, (const te_expr*[]){ret, term(s)});
        ret->fun.f2 = comma;
    }

    return ret;
}


double te_eval(const te_expr *n) {
    switch(te_get_type(n->type)) {
        case TE_CONST: return n->value;
        case TE_VAR: return *n->bound;
        case TE_FUN:
        switch(te_get_arity(n->type)) {
            #define m(e) te_eval(n->members[e])
            case 0: return n->fun.f0();
            case 1: return n->fun.f1(m(0));
            case 2: return n->fun.f2(m(0), m(1));
            case 3: return n->fun.f3(m(0), m(1), m(2));
            case 4: return n->fun.f4(m(0), m(1), m(2), m(3));
            case 5: return n->fun.f5(m(0), m(1), m(2), m(3), m(4));
            case 6: return n->fun.f6(m(0), m(1), m(2), m(3), m(4), m(5));
            case 7: return n->fun.f7(m(0), m(1), m(2), m(3), m(4), m(5), m(6));
            default: return 0.0/0.0;
            #undef m
        }
        default: return 0.0/0.0;
    }
}


static void optimize(te_expr *n) {
    /* Evaluates as much as possible. */

    /*
    if (n->bound) return;

    if (n->left) optimize(n->left);
    if (n->right) optimize(n->right);

    if (n->left && n->right)
    {
        if (n->left->left == 0 && n->left->right == 0 && n->right->left == 0 && n->right->right == 0 && n->right->bound == 0 && n->left->bound == 0)
        {
            const double r = n->f2(n->left->value, n->right->value);
            free(n->left); free(n->right);
            n->left = 0; n->right = 0;
            n->value = r;
        }
    } else if (n->left && !n->right) {
        if (n->left->left == 0 && n->left->right == 0 && n->left->bound == 0) {
            const double r = n->f1(n->left->value);
            free(n->left);
            n->left = 0;
            n->value = r;
        }
    }
    */
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *root = list(&s);

    if (s.type != TOK_END) {
        te_free(root);
        if (error) {
            *error = (s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } else {
        optimize(root);
        if (error) *error = 0;
        return root;
    }
}


double te_interp(const char *expression, int *error) {
    te_expr *n = te_compile(expression, 0, 0, error);
    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    } else {
        ret = 0.0/0.0;
    }
    return ret;
}

static void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(te_get_type(n->type)) {
    case TE_CONST: printf("%f\n", n->value); break;
    case TE_VAR: printf("bound %p\n", n->bound); break;
    case TE_FUN:
         arity = te_get_arity(n->type);
         printf("f%d", arity);
         for(i = 0; i < arity; i++) {
             printf(" %p", n->members[i]);
         }
         printf("\n");
         for(i = 0; i < arity; i++) {
             pn(n->members[i], depth + 1);
         }
         break;
    }
}


void te_print(const te_expr *n) {
    pn(n, 0);
}
