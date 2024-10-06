#include <cstdio>
#include <cstdlib>

#include "test.h"

static TestContext _current_context = {};

void
test_set_current(const TestContext &context)
{
  _current_context = context;
}

void
test_assert(bool condition, const char *msg)
{
  if (!condition) {
    printf("TEST ASSERT FAILED: %s\n", msg);
    exit(-1);
  }
}
