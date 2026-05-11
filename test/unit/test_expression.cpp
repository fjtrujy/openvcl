// Expression evaluator unit tests.
//
// The Expression class is the constant-folding engine used by openvcl for
// immediate values in instructions and address offsets.  These tests cover
// the basic arithmetic surface, with one deliberate regression test for the
// left-associativity fix in commit bc41a56.

#include "test_harness.h"

#include "../../src/Expression.h"
#include "../../src/Math.h"

using namespace vcl;

// Helper: parse and evaluate a textual expression, return result().
// Pushes a failure (without aborting) if parse or solve fails so the
// caller's subsequent CHECK_APPROX line still has something to compare.
static double eval(const std::string& expr)
{
    Expression e;
    e.setCustomOperators(Math::mathOperators());
    if (!e.process(expr)) {
        fprintf(stderr, "  eval(): process failed for '%s'\n", expr.c_str());
        return 0.0;
    }
    if (!e.solve()) {
        fprintf(stderr, "  eval(): solve failed for '%s'\n", expr.c_str());
        return 0.0;
    }
    return e.result();
}

TEST_CASE("Expression: simple addition")
{
    CHECK_APPROX(eval("1 + 2"), 3.0);
}

TEST_CASE("Expression: precedence respects * over +")
{
    CHECK_APPROX(eval("2 * 3 + 4"), 10.0);
    CHECK_APPROX(eval("4 + 2 * 3"), 10.0);
}

TEST_CASE("Expression: subtraction is left-associative (regression for bc41a56)")
{
    // Before bc41a56, the operator-priority compare used > instead of >=,
    // so "a - b + c" parsed as "a - (b + c)".  With the fix it parses as
    // "(a - b) + c", which is the standard arithmetic interpretation.
    CHECK_APPROX(eval("1 - 2 + 3"), 2.0);   // (1-2)+3 = 2,   NOT 1-(2+3) = -4
    CHECK_APPROX(eval("10 - 5 - 2"), 3.0);  // (10-5)-2 = 3,  NOT 10-(5-2) = 7
}

TEST_CASE("Expression: parentheses override precedence")
{
    CHECK_APPROX(eval("(1 + 2) * 3"),  9.0);
    CHECK_APPROX(eval("1 - (2 + 3)"), -4.0);
}
