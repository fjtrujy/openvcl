#include "test_harness.h"

#include "../../src/Alias.h"

using namespace vcl;

TEST_CASE("Alias: ids are monotonic creation-order keys")
{
    Alias first(Alias::FLOAT);
    Alias second(Alias::INTEGER);
    Alias third(Alias::FLOAT);

    CHECK(first.id() < second.id());
    CHECK(second.id() < third.id());
}
