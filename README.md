#TINYEXPR


TINYEXPR is a very small recursive descent parser and evaluation engine for
math expressions. It's handy when you want to add the ability to evaluation
math expressions at runtime without adding a bunch of cruft to you project.

In addition to the standard math operators and precedence, TINYEXPR also supports
the standard C math functions and runtime binding of variables.

##Features

- **ANSI C with no dependencies**.
- Single source file and header file.
- Simple and fast.
- Implements standard operators precedence.
- Exposes standard C math functions (sin, sqrt, ln, etc.).
- Can bind variables at eval-time.
- Released under the zlib license - free for nearly any use.
- Easy to use and integrate with your code
- Thread-safe, provided that your *malloc* is.

##Short Example

Here is a minimal example to evaluate an expression at runtime.

```C
    #include "tinyexpr.h"
    #include <stdio.h>

    int main(int argc, char *argv[])
    {
        const char *c = "sqrt(5^2+7^2+11^2+(8-2)^2)";
        double r = te_interp(c, 0);
        printf("The expression:\n\t%s\nevaluates to:\n\t%f\n", c, r);
        return 0;
    }
```


That produces the following output:

    The expression:
            sqrt(5^2+7^2+11^2+(8-2)^2)
    evaluates to:
            15.198684


##Longer Example

Here is an example that will evaluate an expression passed in from the command
line. It also does error checking and binds the variables *x* and *y*.

```C
    #include "tinyexpr.h"
    #include <stdio.h>

    int main(int argc, char *argv[])
    {
        if (argc < 2) {
            printf("Usage: example2 \"expression\"\n", argv[0]);
            return 0;
        }

        const char *expression = argv[1];
        printf("Evaluating:\n\t%s\n", expression);

        /* This shows an example where the variables
         * x and y are bound at eval-time. */
        double x, y;
        te_variable vars[] = {{"x", &x}, {"y", &y}};

        /* This will compile the expression and check for errors. */
        int err;
        te_expr *n = te_compile(expression, vars, 2, &err);

        if (!err) {
            /* The variables can be changed here, and eval can be called as many
             * times as you like. This is fairly efficient because the parsing has
             * already been done. */
            x = 3;
            y = 4;
            const double r = te_eval(n); printf("Result:\n\t%f\n", r); }
        else {
            /* Show the user where the error is at. */
            printf("\t%*s^\nError near here", err-1, "");
        }

        /* te_free should always be called after te_compile. */
        te_free(n);

        return 0;
    }
```


This produces the output:

    $ example2 "sqrt(x^2+y2)"
        Evaluating:
                sqrt(x^2+y2)
                          ^
        Error near here


    $ example2 "sqrt(x^2+y^2)"
        Evaluating:
                sqrt(x^2+y^2)
        Result:
                5.000000


##Usage

TINYEXPR defines only five functions:

```C
    double te_interp(const char *expression, int *error);
    te_expr *te_compile(const char *expression, const te_variable *lookup, int lookup_len, int *error);
    double te_eval(te_expr *n);
    void te_print(const te_expr *n);
    void te_free(te_expr *n);
```

**te_interp** takes an expression and immediately returns the result of it. If
an error pointer is passed in, *te_interp* will set it to 0 for success or
approximately the position of the error for failure. If you don't care about
errors, just pass in 0.

**te_interp example:**

```C
    double x = te_interp("5+5", 0);
```

**te_compile** will compile an expression with unbound variables, which will
be suitable to evaluate later. **te_eval** can then be called on the compiled
expression repeatedly to evaluate it with different variable values. **te_free**
should be called after you're finished.

**te_compile example:**

```C
    double x, y;
    te_variable vars[] = {{"x", &x}, {"y", &y}};

    int err;
    te_expr *expr = te_compile("sqrt(x^2+y^2)", vars, 2, &err);

    if (!err) {
        x = 3; y = 4;
        const double h1 = te_eval(expr);

        x = 5; y = 7;
        const double h2 = te_eval(expr);
    }

    te_free(expr);
```

**te_print** will display some (possibly not so) useful debugging
information about the return value of *te_compile*.


##How it works

**te_compile** uses a simple recursive descent parser to compile your
expression into a syntax tree. For example, the expression "sin x + 1/4"
parses as:

![example syntax tree](doc/e1.png?raw=true)

**te_compile** also automatically prunes constant branches. In this example,
the compiled expression returned by *te_compile* is:

![example syntax tree](doc/e2.png?raw=true)

**te_eval** will automatically load in any variables by their pointer, then evaluate
and return the result of the expression.

**te_free** should always be called when you're done with the compiled expression.


##Speed


TINYEXPR is pretty fast compared to C when the expression is short, when the
expression does hard calculations (e.g. exponentiation), and when some of the
work can be simplified by *te_compile*. TINYEXPR is slow compared to C when the
expression is long and involves only basic arithmetic.

Here is some example performance numbers taken from the included
*benchmark.c* program:

| Expression | te_eval time | native C time | slowdown  |
| ------------- |-------------| -----|
| sqrt(a^1.5+a^2.5) | 15,641 ms | 14,478 ms | 8% slower |
| a+5 | 765 ms | 563 ms | 36% slower |
| a+(5*2) | 765 ms | 563 ms | 36% slower |
| (a+5)*2 | 1422 ms | 563 ms | 153% slower |
| (1/(a+1)+2/(a+2)+3/(a+3)) | 5,516 ms | 1,266 ms | 336% slower |



##Grammar

TINYEXPR parses the following grammar:

    <expr>      =    <term> {("+" | "-") <term>}
    <term>      =    <factor> {("*" | "/" | "%") <factor>}
    <factor>    =    <power> {"^" <power>}
    <power>     =    {("-" | "+")} <base>
    <base>      =    <constant> | <variable> | <function> <power> | "(" <expr> ")"

In addition, whitespace between tokens is ignored.

Valid variable names are any combination of the lower case letters *a* through
*z*. Constants can be integers, decimal numbers, or in scientific notation
(e.g. *1e3* for *1000*). A leading zero is not required (e.g. *.5* for *0.5*)


##Functions supported

TINYEXPR supports addition (+), subtraction/negation (-), multiplication (\*),
division (/), exponentiation (^) and modulus (%) with the normal operator
precedence (the one exception being that exponentiation is evaluated
left-to-right).

In addition, the following C math functions are also supported:

- abs (calls to *fabs*), acos, asin, atan, ceil, cos, cosh, exp, floor, ln (calls to *log*), log (calls to *log10*), sin, sinh, sqrt, tan, tanh


##Hints

- All functions/types start with the letters *te*.

- Remember to always call *te_free* on the result of *te_compile*, even if
  there is an error.

- If there is an error, you can usually still evaluate the first part of the
  expression.  This may or may not be useful to you.

- To allow constant optimization, surround constant expressions in parentheses.
  For example "x+(1+5)" will evaluate the "(1+5)" expression at compile time and
  compile the entire expression as "x+6", saving a runtime calculation. The
  parentheses are important, because TINYEXPR will not change the order of
  evaluation. If you instead compiled "x+1+5" TINYEXPR will insist that "1" is
  added to "x" first, and "5" is added the result second.

