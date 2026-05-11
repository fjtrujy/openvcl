// Runner for openvcl unit tests.  Iterates the registry that TEST_CASE
// macros populate at static-initialization time, prints per-test status
// and a final summary, returns non-zero exit if anything failed.

#include "test_harness.h"

int main(int /*argc*/, char** /*argv*/)
{
    int passed = 0;
    int failed = 0;

    printf("[==========] Running %zu test case(s)\n",
           ::test::registry().size());

    for (size_t i = 0; i < ::test::registry().size(); ++i) {
        const ::test::Case& c = ::test::registry()[i];
        ::test::current_test_name() = c.name;
        int before = ::test::failures_in_current_test();
        printf("[ RUN      ] %s\n", c.name);
        c.fn();
        int delta = ::test::failures_in_current_test() - before;
        if (delta == 0) {
            printf("[       OK ] %s\n", c.name);
            ++passed;
        } else {
            printf("[  FAILED  ] %s (%d failed assertion(s))\n", c.name, delta);
            ++failed;
        }
    }

    printf("[==========] %d passed, %d failed, %d assertion(s) total\n",
           passed, failed, ::test::total_checks());

    return failed > 0 ? 1 : 0;
}
