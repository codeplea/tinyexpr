// SPDX-License-Identifier: Zlib
/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 *
 * Copyright (c) 2015-2026 Lewis Van Winkle
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

/* COMPILE TIME OPTIONS */

/* Exponentiation associativity:
For a^b^c = (a^b)^c and -a^b = (-a)^b do nothing.
For a^b^c = a^(b^c) and -a^b = -(a^b) uncomment the next line.*/
/* #define TE_POW_FROM_RIGHT */

/* Logarithms
For log = base 10 log do nothing
For log = natural log uncomment the next line. */
/* #define TE_NAT_LOG */

#include "tinyexpr.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif


enum {
    TOK_NULL = TE_CLOSURE7+1, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX
};

/* Binary operator precedence, lowest first. PREC_NONE marks tokens that
   are never a binary operator (e.g. unary "!"). */
enum {
    PREC_NONE, PREC_LIST, PREC_OR, PREC_AND, PREC_EQ,
    PREC_REL, PREC_SUM, PREC_TERM, PREC_POW
};


enum {TE_CONSTANT = 1};


typedef struct state {
    const char *next;
    int type;
    int prec;
    union {double value; const double *bound; const void *function;};
    void *context;

    const te_variable *lookup;
    int lookup_len;
    int applied_unary;
    int depth;
} state;


#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION0) != 0)
#define IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE0) != 0)
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )
#define NEW_EXPR(type, ...) new_expr((type), (const te_expr*[]){__VA_ARGS__})
#define CHECK_NULL(ptr, ...) if ((ptr) == NULL) { __VA_ARGS__; return NULL; }

#ifndef TE_MAX_DEPTH
#define TE_MAX_DEPTH 512
#endif

static te_expr *new_expr(const int type, const te_expr *parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(void*) * arity;
    const int size = sizeof(te_expr) + psize + (IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr *ret = malloc(size);
    CHECK_NULL(ret);

    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    return ret;
}


static te_expr *new_fun(int type, const void *function, te_expr *a, te_expr *b) {
    /* Builds a pure function node over a (and b, for arity 2), taking
       ownership of both: on any failure they are freed and NULL returned. */
    te_expr *ret = !a || (ARITY(type) == 2 && !b) ? NULL : NEW_EXPR(type, a, b);
    if (!ret) {
        te_free(a);
        te_free(b);
        return NULL;
    }
    ret->function = function;
    return ret;
}


void te_free_parameters(te_expr *n) {
    int i;
    if (!n) return;
    for (i = 0; i < ARITY(n->type); ++i) te_free(n->parameters[i]);
}


void te_free(te_expr *n) {
    if (n) te_free_parameters(n);
    free(n);
}


static double pi(void) {return 3.14159265358979323846;}
static double e(void) {return 2.71828182845904523536;}
static double fac(double a) {
    if (!(a >= 0.0))
        return NAN;
    if (a > UINT_MAX)
        return INFINITY;
    unsigned int ua = (unsigned int)(a);
    unsigned long long int result = 1, i;
    for (i = 1; i <= ua; i++) {
        if (i > ULLONG_MAX / result)
            return INFINITY;
        result *= i;
    }
    return (double)result;
}
static unsigned long long int gcd(unsigned long long int a, unsigned long long int b) {
    while (b) {
        unsigned long long int t = a % b;
        a = b;
        b = t;
    }
    return a;
}
static double ncr(double n, double r) {
    if (!(n >= 0.0) || !(r >= 0.0) || n < r) return NAN;
    if (n > UINT_MAX || r > UINT_MAX) return INFINITY;
    unsigned long long int un = (unsigned int)(n), ur = (unsigned int)(r), i;
    unsigned long long int result = 1;
    if (ur > un / 2) ur = un - ur;
    for (i = 1; i <= ur; i++) {
        /* result * m / i is an exact integer, so reduce by gcd(result, i)
         * first and the remaining divisor of i must divide m. */
        unsigned long long int m = un - ur + i;
        unsigned long long int g = gcd(result, i);
        result /= g;
        m /= i / g;
        if (result > ULLONG_MAX / m)
            return INFINITY;
        result *= m;
    }
    return result;
}
static double npr(double n, double r) {return ncr(n, r) * fac(r);}

#ifdef _MSC_VER
#pragma function (ceil)
#pragma function (floor)
#endif

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs,     TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"acos", acos,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"asin", asin,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan", atan,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan2", atan2,  TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"ceil", ceil,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cos", cos,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cosh", cosh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"e", e,          TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"exp", exp,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"fac", fac,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"floor", floor,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ln", log,       TE_FUNCTION1 | TE_FLAG_PURE, 0},
#ifdef TE_NAT_LOG
    {"log", log,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
#else
    {"log", log10,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
#endif
    {"log10", log10,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ncr", ncr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"npr", npr,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"pi", pi,        TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"pow", pow,      TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"sin", sin,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sinh", sinh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sqrt", sqrt,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tan", tan,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tanh", tanh,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {0, 0, 0, 0}
};

static const te_variable *find_builtin(const char *name, int len) {
    int imin = 0, imax = sizeof(functions) / sizeof(te_variable) - 2;

    while (imax >= imin) {
        const int i = imin + (imax - imin) / 2;
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) return functions + i;
        if (c > 0) imin = i + 1; else imax = i - 1;
    }
    return 0;
}

static const te_variable *find_lookup(const state *s, const char *name, int len) {
    int i;
    for (i = 0; s->lookup && i < s->lookup_len; ++i) {
        const te_variable *var = s->lookup + i;
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') return var;
    }
    return 0;
}


static double add(double a, double b) {return a + b;}
static double sub(double a, double b) {return a - b;}
static double mul(double a, double b) {return a * b;}
static double divide(double a, double b) {return a / b;}
static double negate(double a) {return -a;}
static double comma(double a, double b) {(void)a; return b;}

static double greater(double a, double b) {return a > b;}
static double greater_eq(double a, double b) {return a >= b;}
static double lower(double a, double b) {return a < b;}
static double lower_eq(double a, double b) {return a <= b;}
static double equal(double a, double b) {return a == b;}
static double not_equal(double a, double b) {return a != b;}
static double logical_and(double a, double b) {return a != 0.0 && b != 0.0;}
static double logical_or(double a, double b) {return a != 0.0 || b != 0.0;}
static double logical_not(double a) {return a == 0.0;}
static double logical_notnot(double a) {return a != 0.0;}
static double negate_logical_not(double a) {return -(a == 0.0);}
static double negate_logical_notnot(double a) {return -(a != 0.0);}


/* Powers of ten up to 1e22 are exact doubles, so dividing by one gives a
   correctly rounded literal where multiplying by pow(10, -n) was an ulp off. */
static const double exact_pow10[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

static double parse_number(state *s) {
    /* Parses a decimal or hex constant manually, so that the result does
       not depend on LC_NUMERIC. Sets s->type. */
    const char *p = s->next;
    double value = 0.0;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        isxdigit((unsigned char)p[2])) {
        p += 2;
        while (isxdigit((unsigned char)p[0])) {
            const char c = p[0];
            const int d = isdigit((unsigned char)c) ? c - '0' :
                          (c >= 'a' ? c - 'a' + 10 : c - 'A' + 10);
            value = value * 16.0 + d;
            p++;
        }
    } else {
        int exponent = 0, digits = 0;

        while (isdigit((unsigned char)p[0])) {
            value = value * 10.0 + (p[0] - '0');
            p++; digits++;
        }

        if (p[0] == '.') {
            p++;
            while (isdigit((unsigned char)p[0])) {
                value = value * 10.0 + (p[0] - '0');
                p++; digits++; exponent--;
            }
        }

        if (digits == 0) {
            /* A lone '.' is not a number. */
            s->type = TOK_ERROR;
            return 0.0;
        }

        if (p[0] == 'e' || p[0] == 'E') {
            /* Only consume the exponent if digits follow it, so that
               "1e" still parses as the number 1 then the variable e. */
            const char *ep = p + 1;
            int esign = 1, e = 0;

            if (ep[0] == '+') {
                ep++;
            } else if (ep[0] == '-') {
                esign = -1;
                ep++;
            }

            if (isdigit((unsigned char)ep[0])) {
                while (isdigit((unsigned char)ep[0])) {
                    if (e < 100000000) e = e * 10 + (ep[0] - '0');
                    ep++;
                }
                exponent += esign * e;
                p = ep;
            }
        }

        /* The zero check keeps a huge exponent from producing 0 * inf. */
        if (exponent && value != 0.0) {
            if (exponent < 0 && exponent >= -22) {
                value /= exact_pow10[-exponent];
            } else {
                value *= pow(10.0, exponent);
            }
        }
    }

    s->type = TOK_NUMBER;
    s->next = p;
    return value;
}


static void infix(state *s, const void *function, int prec) {
    s->type = TOK_INFIX;
    s->function = function;
    s->prec = prec;
}

static int match(state *s, char c) {
    if (s->next[0] != c) return 0;
    s->next++;
    return 1;
}

static void next_token(state *s) {
    s->type = TOK_NULL;
    s->prec = PREC_NONE;

    do {
        if (!*s->next) {
            s->type = TOK_END;
            return;
        }

        if (isdigit((unsigned char)s->next[0]) || s->next[0] == '.') {
            s->value = parse_number(s);
        } else if (isalpha((unsigned char)s->next[0])) {
            const char *start = s->next;
            while (isalnum((unsigned char)s->next[0]) || s->next[0] == '_') s->next++;

            const te_variable *var = find_lookup(s, start, s->next - start);
            if (!var) var = find_builtin(start, s->next - start);

            if (!var) {
                s->type = TOK_ERROR;
            } else if (TYPE_MASK(var->type) == TE_VARIABLE) {
                s->type = TOK_VARIABLE;
                s->bound = var->address;
            } else if (IS_FUNCTION(var->type) != IS_CLOSURE(var->type)) {
                if (IS_CLOSURE(var->type)) s->context = var->context;
                s->type = var->type;
                s->function = var->address;
            } else {
                /* Not a type the header defines. */
                s->type = TOK_ERROR;
            }
        } else {
            switch (s->next++[0]) {
                case '+': infix(s, add, PREC_SUM); break;
                case '-': infix(s, sub, PREC_SUM); break;
                case '*': infix(s, mul, PREC_TERM); break;
                case '/': infix(s, divide, PREC_TERM); break;
                case '^': infix(s, pow, PREC_POW); break;
                case '%': infix(s, fmod, PREC_TERM); break;
                case '!': if (match(s, '=')) infix(s, not_equal, PREC_EQ); else infix(s, logical_not, PREC_NONE); break;
                case '=': if (match(s, '=')) infix(s, equal, PREC_EQ); else s->type = TOK_ERROR; break;
                case '<': if (match(s, '=')) infix(s, lower_eq, PREC_REL); else infix(s, lower, PREC_REL); break;
                case '>': if (match(s, '=')) infix(s, greater_eq, PREC_REL); else infix(s, greater, PREC_REL); break;
                case '&': if (match(s, '&')) infix(s, logical_and, PREC_AND); else s->type = TOK_ERROR; break;
                case '|': if (match(s, '|')) infix(s, logical_or, PREC_OR); else s->type = TOK_ERROR; break;
                case '(': s->type = TOK_OPEN; break;
                case ')': s->type = TOK_CLOSE; break;
                case ',': s->type = TOK_SEP; s->function = comma; s->prec = PREC_LIST; break;
                case ' ': case '\t': case '\n': case '\r': break;
                default: s->type = TOK_ERROR; break;
            }
        }
    } while (s->type == TOK_NULL);
}


static te_expr *binary(state *s, int prec);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            CHECK_NULL(ret);

            ret->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            CHECK_NULL(ret);

            ret->bound = s->bound;
            next_token(s);
            break;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            arity = ARITY(s->type);

            ret = new_expr(s->type, 0);
            CHECK_NULL(ret);

            ret->function = s->function;
            if (IS_CLOSURE(s->type)) ret->parameters[arity] = s->context;
            next_token(s);

            if (arity == 1) {
                ret->parameters[0] = power(s);
                CHECK_NULL(ret->parameters[0], te_free(ret));
            } else if (s->type == TOK_OPEN) {
                int i = 0;
                next_token(s);
                while (i < arity) {
                    ret->parameters[i] = binary(s, PREC_OR);
                    CHECK_NULL(ret->parameters[i], te_free(ret));
                    i++;
                    if (i < arity) {
                        if (s->type != TOK_SEP) break;
                        next_token(s);
                    }
                }
                if (i != arity || s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            } else if (arity != 0) {
                /* Parentheses are optional only on a 0-arity function. */
                s->type = TOK_ERROR;
            }
            break;

        case TOK_OPEN:
            next_token(s);
            ret = binary(s, PREC_LIST);
            CHECK_NULL(ret);

            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            CHECK_NULL(ret);

            s->type = TOK_ERROR;
            ret->value = NAN;
            break;
    }

    return ret;
}


static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+" | "!")} <base> */
    static double (*const unary[2][3])(double) = {
        {NULL, logical_not, logical_notnot},
        {negate, negate_logical_not, negate_logical_notnot}
    };
    int sign = 1, logical = 0;

    /* A sign after a "!" is dropped, since !(-x) == !x. */
    while (s->type == TOK_INFIX && (s->function == add || s->function == sub || s->function == logical_not)) {
        if (s->function == logical_not) logical = logical == 1 ? 2 : 1;
        else if (!logical && s->function == sub) sign = -sign;
        next_token(s);
    }

    double (*const f)(double) = unary[sign != 1][logical];

    if (s->depth >= TE_MAX_DEPTH) {
        s->type = TOK_ERROR;
        return NULL;
    }
    s->depth++;
    te_expr *ret = base(s);
    s->depth--;

    if (f) ret = new_fun(TE_FUNCTION1 | TE_FLAG_PURE, f, ret, NULL);

    /* Distinguishes "-1" from "(-1)" for factor(), which must only hoist
       unary operators actually written to the left of a "^". Set last so
       nested parsing inside base() can't clobber it. */
    s->applied_unary = f != NULL;

    return ret;
}


#ifdef TE_POW_FROM_RIGHT
static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);
    CHECK_NULL(ret);

    const void *left_function = NULL;

    if (s->applied_unary && ret->type == (TE_FUNCTION1 | TE_FLAG_PURE)) {
        left_function = ret->function;
        te_expr *se = ret->parameters[0];
        free(ret);
        ret = se;
    }

    /* Slot where the next exponent inserts, to make "^" go right-to-left.
       A unary operator on an exponent applies to the whole chain after it,
       so the slot moves inside the unary node. */
    void **slot = NULL;

    while (s->type == TOK_INFIX && s->function == pow) {
        next_token(s);

        te_expr *p = power(s);
        CHECK_NULL(p, te_free(ret));
        const int p_unary = s->applied_unary;

        if (slot) {
            te_expr *insert = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, *slot, p);
            CHECK_NULL(insert, te_free(p), te_free(ret));

            insert->function = pow;
            *slot = insert;
            slot = p_unary ? &p->parameters[0] : &insert->parameters[1];
        } else {
            ret = new_fun(TE_FUNCTION2 | TE_FLAG_PURE, pow, ret, p);
            CHECK_NULL(ret);
            slot = p_unary ? &p->parameters[0] : &ret->parameters[1];
        }
    }

    if (left_function) {
        ret = new_fun(TE_FUNCTION1 | TE_FLAG_PURE, left_function, ret, NULL);
        CHECK_NULL(ret);
    }

    return ret;
}
#else
static te_expr *factor(state *s) {
    return binary(s, PREC_POW);
}
#endif


static te_expr *operand(state *s, int prec) {
    return prec == PREC_POW ? power(s) :
           prec == PREC_TERM ? factor(s) : binary(s, prec + 1);
}

static te_expr *binary(state *s, int prec) {
    /* Parses one precedence level of left-associative binary operators:
       <list>      =    <or>     {"," <or>}
       <or>        =    <and>    {"||" <and>}
       <and>       =    <eq>     {"&&" <eq>}
       <eq>        =    <rel>    {("==" | "!=") <rel>}
       <rel>       =    <sum>    {("<" | "<=" | ">" | ">=") <sum>}
       <sum>       =    <term>   {("+" | "-") <term>}
       <term>      =    <factor> {("*" | "/" | "%") <factor>}
       <pow>       =    <power>  {"^" <power>}   (only without TE_POW_FROM_RIGHT) */
    te_expr *ret = operand(s, prec);
    CHECK_NULL(ret);

    while ((s->type == TOK_INFIX || s->type == TOK_SEP) && s->prec == prec) {
        const void *function = s->function;
        next_token(s);
        ret = new_fun(TE_FUNCTION2 | TE_FLAG_PURE, function, ret, operand(s, prec));
        CHECK_NULL(ret);
    }

    return ret;
}


#define TE_FUN(...) ((double(*)(__VA_ARGS__))n->function)
#define M(e) te_eval(n->parameters[e])


double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->value;
        case TE_VARIABLE: return *n->bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void)();
                case 1: return TE_FUN(double)(M(0));
                case 2: return TE_FUN(double, double)(M(0), M(1));
                case 3: return TE_FUN(double, double, double)(M(0), M(1), M(2));
                case 4: return TE_FUN(double, double, double, double)(M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(double, double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void*)(n->parameters[0]);
                case 1: return TE_FUN(void*, double)(n->parameters[1], M(0));
                case 2: return TE_FUN(void*, double, double)(n->parameters[2], M(0), M(1));
                case 3: return TE_FUN(void*, double, double, double)(n->parameters[3], M(0), M(1), M(2));
                case 4: return TE_FUN(void*, double, double, double, double)(n->parameters[4], M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(void*, double, double, double, double, double)(n->parameters[5], M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(void*, double, double, double, double, double, double)(n->parameters[6], M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(void*, double, double, double, double, double, double, double)(n->parameters[7], M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        default: return NAN;
    }

}

#undef TE_FUN
#undef M

static void optimize(te_expr *n) {
    /* Only functions flagged as pure can be folded. Constants and variables
       never carry the flag. */
    const int arity = ARITY(n->type);
    int known = 1, i;
    if (!IS_PURE(n->type)) return;

    for (i = 0; i < arity; ++i) {
        optimize(n->parameters[i]);
        if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) known = 0;
    }
    if (known) {
        const double value = te_eval(n);
        te_free_parameters(n);
        n->type = TE_CONSTANT;
        n->value = value;
    }
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    state s;
    s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;
    s.depth = 0;

    next_token(&s);
    te_expr *root = binary(&s, PREC_LIST);
    if (root == NULL) {
        if (error) *error = -1;
        return NULL;
    }

    if (s.type != TOK_END) {
        te_free(root);
        if (error) {
            *error = s.next - expression;
            if (*error == 0) *error = 1;
        }
        return NULL;
    }

    optimize(root);
    if (error) *error = 0;
    return root;
}


double te_interp(const char *expression, int *error) {
    te_expr *n = te_compile(expression, 0, 0, error);
    const double ret = n ? te_eval(n) : NAN;
    te_free(n);
    return ret;
}

static void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->value); break;
    case TE_VARIABLE: printf("bound %p\n", n->bound); break;

    default:
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
