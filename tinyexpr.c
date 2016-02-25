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


typedef double (*te_fun0)(void);
typedef double (*te_fun1)(double);
typedef double (*te_fun2)(double, double);
typedef double (*te_fun3)(double, double, double);
typedef double (*te_fun4)(double, double, double, double);
typedef double (*te_fun5)(double, double, double, double, double);
typedef double (*te_fun6)(double, double, double, double, double, double);
typedef double (*te_fun7)(double, double, double, double, double, double, double);

enum {
    TOK_NULL, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX,
    TOK_FUNCTION0, TOK_FUNCTION1, TOK_FUNCTION2, TOK_FUNCTION3,
    TOK_FUNCTION4, TOK_FUNCTION5, TOK_FUNCTION6, TOK_FUNCTION7
};

enum { TE_CONSTANT = TE_FUNCTION7+1};


typedef struct state {
    const char *start;
    const char *next;
    int type;
    union {double value; const double *bound; const void *function;};

    const te_variable *lookup;
    int lookup_len;
} state;


#define ARITY(TYPE) (((TYPE) < TE_FUNCTION0 || (TYPE) > TE_FUNCTION7) ? 0 : ((TYPE)-TE_FUNCTION0))

static te_expr *new_expr(const int type, const te_expr *parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(te_expr*) * arity;
    te_expr *ret = malloc(sizeof(te_expr) + psize);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->bound = 0;
    return ret;
}


void te_free(te_expr *n) {
    if (!n) return;
    switch (n->type) {
        case TE_FUNCTION7: te_free(n->parameters[6]);
        case TE_FUNCTION6: te_free(n->parameters[5]);
        case TE_FUNCTION5: te_free(n->parameters[4]);
        case TE_FUNCTION4: te_free(n->parameters[3]);
        case TE_FUNCTION3: te_free(n->parameters[2]);
        case TE_FUNCTION2: te_free(n->parameters[1]);
        case TE_FUNCTION1: te_free(n->parameters[0]);
    }
    free(n);
}


static const double pi = 3.14159265358979323846;
static const double e  = 2.71828182845904523536;

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs,     TE_FUNCTION1},
    {"acos", acos,    TE_FUNCTION1},
    {"asin", asin,    TE_FUNCTION1},
    {"atan", atan,    TE_FUNCTION1},
    {"atan2", atan2,  TE_FUNCTION2},
    {"ceil", ceil,    TE_FUNCTION1},
    {"cos", cos,      TE_FUNCTION1},
    {"cosh", cosh,    TE_FUNCTION1},
    {"e", &e,         TE_VARIABLE},
    {"exp", exp,      TE_FUNCTION1},
    {"floor", floor,  TE_FUNCTION1},
    {"ln", log,       TE_FUNCTION1},
    {"log", log10,    TE_FUNCTION1},
    {"pi", &pi,       TE_VARIABLE},
    {"pow", pow,      TE_FUNCTION2},
    {"sin", sin,      TE_FUNCTION1},
    {"sinh", sinh,    TE_FUNCTION1},
    {"sqrt", sqrt,    TE_FUNCTION1},
    {"tan", tan,      TE_FUNCTION1},
    {"tanh", tanh,    TE_FUNCTION1},
    {0}
};

static const te_variable *find_builtin(const char *name, int len) {
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

static const te_variable *find_lookup(const state *s, const char *name, int len) {
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
                const char *start;
                start = s->next;
                while ((s->next[0] >= 'a' && s->next[0] <= 'z') || (s->next[0] >= '0' && s->next[0] <= '9')) s->next++;

                const te_variable *var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    switch(var->type)
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->bound = var->address; break;
                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
                            s->type = TOK_FUNCTION0 + ARITY(var->type);
                            s->function = var->address;
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_INFIX; s->function = add; break;
                    case '-': s->type = TOK_INFIX; s->function = sub; break;
                    case '*': s->type = TOK_INFIX; s->function = mul; break;
                    case '/': s->type = TOK_INFIX; s->function = divide; break;
                    case '^': s->type = TOK_INFIX; s->function = pow; break;
                    case '%': s->type = TOK_INFIX; s->function = fmod; break;
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
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (s->type) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            ret->bound = s->bound;
            next_token(s);
            break;

        case TOK_FUNCTION0:
            ret = new_expr(TE_FUNCTION0, 0);
            ret->function = s->function;
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
            ret = new_expr(TE_FUNCTION1, 0);
            ret->function = s->function;
            next_token(s);
            ret->parameters[0] = power(s);
            break;

        case TOK_FUNCTION2: case TOK_FUNCTION3: case TOK_FUNCTION4:
        case TOK_FUNCTION5: case TOK_FUNCTION6: case TOK_FUNCTION7:
            arity = s->type - TOK_FUNCTION0;

            ret = new_expr(TE_FUNCTION0 + arity, 0);
            ret->function = s->function;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->parameters[i] = expr(s);
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
    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) {
        if (s->function == sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        ret = new_expr(TE_FUNCTION1, (const te_expr*[]){base(s)});
        ret->function = negate;
    }

    return ret;
}


static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    while (s->type == TOK_INFIX && (s->function == pow)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = new_expr(TE_FUNCTION2, (const te_expr*[]){ret, power(s)});
        ret->function = t;
    }

    return ret;
}


static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);

    while (s->type == TOK_INFIX && (s->function == mul || s->function == divide || s->function == fmod)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = new_expr(TE_FUNCTION2, (const te_expr*[]){ret, factor(s)});
        ret->function = t;
    }

    return ret;
}


static te_expr *expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);

    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = new_expr(TE_FUNCTION2, (const te_expr*[]){ret, term(s)});
        ret->function = t;
    }

    return ret;
}


static te_expr *list(state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);

    while (s->type == TOK_SEP) {
        next_token(s);
        ret = new_expr(TE_FUNCTION2, (const te_expr*[]){ret, expr(s)});
        ret->function = comma;
    }

    return ret;
}


double te_eval(const te_expr *n) {
    if (!n) return 0.0/0.0;

    switch(n->type) {
        case TE_CONSTANT: return n->value;
        case TE_VARIABLE: return *n->bound;
        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        switch(ARITY(n->type)) {
            #define m(e) te_eval(n->parameters[e])
            case 0: return ((te_fun0)(n->function))();
            case 1: return ((te_fun1)(n->function))(m(0));
            case 2: return ((te_fun2)(n->function))(m(0), m(1));
            case 3: return ((te_fun3)(n->function))(m(0), m(1), m(2));
            case 4: return ((te_fun4)(n->function))(m(0), m(1), m(2), m(3));
            case 5: return ((te_fun5)(n->function))(m(0), m(1), m(2), m(3), m(4));
            case 6: return ((te_fun6)(n->function))(m(0), m(1), m(2), m(3), m(4), m(5));
            case 7: return ((te_fun7)(n->function))(m(0), m(1), m(2), m(3), m(4), m(5), m(6));
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

    switch(n->type) {
    case TE_CONSTANT: printf("%f\n", n->value); break;
    case TE_VARIABLE: printf("bound %p\n", n->bound); break;
    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
         arity = ARITY(n->type);
         printf("f%d", arity);
         for(i = 0; i < arity; i++) {
             printf(" %p", n->parameters[i]);
         }
         printf("\n");
         for(i = 0; i < arity; i++) {
             pn(n->parameters[i], depth + 1);
         }
         break;
    }
}


void te_print(const te_expr *n) {
    pn(n, 0);
}
