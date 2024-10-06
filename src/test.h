#pragma once

struct TestContext {
  const char *name;
};

typedef void (*TestFunc)(TestContext *);

void test_set_current(const TestContext &context);
void test_assert(bool condition, const char *msg);
