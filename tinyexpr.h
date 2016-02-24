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

#ifndef __TINYEXPR_H__
#define __TINYEXPR_H__


#ifdef __cplusplus
extern "C" {
#endif


typedef double (*te_fun0)(void);
typedef double (*te_fun1)(double);
typedef double (*te_fun2)(double, double);
typedef double (*te_fun3)(double, double, double);
typedef double (*te_fun4)(double, double, double, double);
typedef double (*te_fun5)(double, double, double, double, double);
typedef double (*te_fun6)(double, double, double, double, double, double);
typedef double (*te_fun7)(double, double, double, double, double, double, double);

typedef union
{
    te_fun0 f0; te_fun1 f1; te_fun2 f2; te_fun3 f3; te_fun4 f4; te_fun5 f5; te_fun6 f6; te_fun7 f7;
} te_fun;

typedef struct te_expr {
    int type;
    union {double value; const double *bound; te_fun fun; };
    int member_count;
    struct te_expr *members[];
} te_expr;


#define TE_MASK_ARIT 0x00000007 /* Three bits, Arity, max is 8 */
#define TE_FLAG_TYPE 0x00000018 /* Two bits, 1 = constant, 2 = variable, 3 = function */

enum { TE_CONST = 1 << 3, TE_VAR = 2 << 3, TE_FUN = 3 << 3};

typedef struct te_variable {
    const char *name;
    const void *value;
    int type;
} te_variable;



/* Parses the input expression, evaluates it, and frees it. */
/* Returns NaN on error. */
double te_interp(const char *expression, int *error);

/* Parses the input expression and binds variables. */
/* Returns NULL on error. */
te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error);

/* Evaluates the expression. */
double te_eval(const te_expr *n);

/* Prints debugging information on the syntax tree. */
void te_print(const te_expr *n);

/* Frees the expression. */
/* This is safe to call on NULL pointers. */
void te_free(te_expr *n);


#ifdef __cplusplus
}
#endif

#endif /*__TINYEXPR_H__*/
