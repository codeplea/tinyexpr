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


enum {TOK_NULL, TOK_END, TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_ADD, TOK_SUB, TOK_MUL, TOK_DIV, TOK_FUNCTION1, TOK_FUNCTION2, TOK_VARIABLE, TOK_ERROR};



typedef struct {
    const char *start;
    const char *next;
    int type;
    union {double value; te_fun1 f1; te_fun2 f2; const double *var;};

    const te_variable *lookup;
    int lookup_len;
} state;




static te_expr *new_expr(te_expr *l, te_expr *r) {
    te_expr *ret = malloc(sizeof(te_expr));
    ret->left = l;
    ret->right = r;
    ret->bound = 0;
    return ret;
}


void te_free(te_expr *n) {
    if (!n) return;
    if (n->left) te_free(n->left);
    if (n->right) te_free(n->right);
    free(n);
}


typedef struct {
    const char *name;
    te_fun1 f1;
} builtin;


static const builtin functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs},
    {"acos", acos},
    {"asin", asin},
    {"atan", atan},
    {"ceil", ceil},
    {"cos", cos},
    {"cosh", cosh},
    {"exp", exp},
    {"floor", floor},
    {"ln", log},
    {"log", log10},
    {"sin", sin},
    {"sinh", sinh},
    {"sqrt", sqrt},
    {"tan", tan},
    {"tanh", tanh},
    {0}
};


static const builtin *find_function(const char *name, int len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(builtin) - 2;

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


static const double *find_var(const state *s, const char *name, int len) {
    int i;
    if (!s->lookup) return 0;
    for (i = 0; i < s->lookup_len; ++i) {
        if (strncmp(name, s->lookup[i].name, len) == 0 && s->lookup[i].name[len] == '\0') {
            return s->lookup[i].value;
        }
    }
    return 0;
}



static double add(double a, double b) {return a + b;}
static double sub(double a, double b) {return a - b;}
static double mul(double a, double b) {return a * b;}
static double divide(double a, double b) {return a / b;}
static double negate(double a) {return -a;}


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
                while (s->next[0] >= 'a' && s->next[0] <= 'z') s->next++;

                const double *var = find_var(s, start, s->next - start);
                if (var) {
                    s->type = TOK_VARIABLE;
                    s->var = var;
                } else {
                    if (s->next - start > 15) {
                        s->type = TOK_ERROR;
                    } else {
                        s->type = TOK_FUNCTION1;
                        const builtin *f = find_function(start, s->next - start);
                        if (!f) {
                            s->type = TOK_ERROR;
                        } else {
                            s->f1 = f->f1;
                        }
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_FUNCTION2; s->f2 = add; break;
                    case '-': s->type = TOK_FUNCTION2; s->f2 = sub; break;
                    case '*': s->type = TOK_FUNCTION2; s->f2 = mul; break;
                    case '/': s->type = TOK_FUNCTION2; s->f2 = divide; break;
                    case '^': s->type = TOK_FUNCTION2; s->f2 = pow; break;
                    case '%': s->type = TOK_FUNCTION2; s->f2 = fmod; break;
                    case '(': s->type = TOK_OPEN; break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}


static te_expr *expr(state *s);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function> <power> | "(" <expr> ")" */
    te_expr *ret;

    switch (s->type) {
        case TOK_NUMBER:
            ret = new_expr(0, 0);
            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(0, 0);
            ret->bound = s->var;
            next_token(s);
            break;

        case TOK_FUNCTION1:
            ret = new_expr(0, 0);
            ret->f1 = s->f1;
            next_token(s);
            ret->left = power(s);
            break;

        case TOK_OPEN:
            next_token(s);
            ret = expr(s);
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
    while (s->type == TOK_FUNCTION2 && (s->f2 == add || s->f2 == sub)) {
        if (s->f2 == sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        ret = base(s);
    } else {
        ret = new_expr(base(s), 0);
        ret->f1 = negate;
    }

    return ret;
}


static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    while (s->type == TOK_FUNCTION2 && (s->f2 == pow)) {
        te_fun2 t = s->f2;
        next_token(s);
        ret = new_expr(ret, power(s));
        ret->f2 = t;
    }

    return ret;
}


static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);

    while (s->type == TOK_FUNCTION2 && (s->f2 == mul || s->f2 == divide || s->f2 == fmod)) {
        te_fun2 t = s->f2;
        next_token(s);
        ret = new_expr(ret, factor(s));
        ret->f2 = t;
    }

    return ret;
}


static te_expr *expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);

    while (s->type == TOK_FUNCTION2 && (s->f2 == add || s->f2 == sub)) {
        te_fun2 t = s->f2;
        next_token(s);
        ret = new_expr(ret, term(s));
        ret->f2 = t;
    }

    return ret;
}


double te_eval(const te_expr *n) {
    double ret;

    if (n->bound) {
        ret = *n->bound;
    } else if (n->left == 0 && n->right == 0) {
        ret = n->value;
    } else if (n->left && n->right == 0) {
        ret = n->f1(te_eval(n->left));
    } else {
        ret = n->f2(te_eval(n->left), te_eval(n->right));
    }
    return ret;
}


static void optimize(te_expr *n) {
    /* Evaluates as much as possible. */
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
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *root = expr(&s);

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
    printf("%*s", depth, "");

    if (n->bound) {
        printf("bound %p\n", n->bound);
    } else if (n->left == 0 && n->right == 0) {
        printf("%f\n", n->value);
    } else if (n->left && n->right == 0) {
        printf("f1 %p\n", n->left);
        pn(n->left, depth+1);
    } else {
        printf("f2 %p %p\n", n->left, n->right);
        pn(n->left, depth+1);
        pn(n->right, depth+1);
    }
}


void te_print(const te_expr *n) {
    pn(n, 0);
}
