#pragma once

#include <vector>

namespace test_flags {
enum TestFlagBits : uint32_t
{
  TEST_TA        = 1 << 0,  /* TA/Binner behavior */
  TEST_ISP       = 1 << 1,  /* ISP/rasterizer behavior */
  TEST_BENCHMARK = 1 << 20, /* Measure some system component's performance */
};
}

class TestContext;

/* Function invoked for actual test logic */
typedef void (*TestFunc)(TestContext &);

/* Test definition registered at startup. Can be iterated with get_test_list. */
struct TestDefinition {
  uint32_t flags;
  const char *name;
  TestFunc func;
  const char *description;
};

/* Iterate through a list of all registered tests. */
std::vector<TestDefinition> &get_test_list();

/* Test context passed to a test. Used to create log and dump files, report errors, etc.
 */
class TestContext {
public:
  TestContext(const char *name) : _name(name)
  {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "/pc/%s.log", _name);
    _log_file = fopen(buffer, "w");
  }

  TestContext(const TestContext &) = delete;

  ~TestContext()
  {
    if (_log_file) {
      fclose(_log_file);
    }
  }

  const char *name() const
  {
    return _name;
  }

  void set_abort_on_failure(bool abort_on_failure)
  {
    _abort_on_failure = abort_on_failure;
  }

  void assert(bool condition, const char *msg);

  void pvr_reg_vram_dump(const char *dump_name);

  void host_printf(const char *fmt, ...);

  void log(const char *fmt, ...);

  FILE *log_file()
  {
    return _log_file;
  }

private:
  void create_test_folder();

  FILE *_log_file = nullptr;
  const char *_name;
  bool _abort_on_failure = true;
};

extern std::vector<TestDefinition> _g_test_list;

// Define a test with the given flags, append to the global test list via global
// constructor attributed function.
#define REGISTER_TEST(name, flags, description)                                          \
  void test_##name(TestContext &);                                                       \
  static TestDefinition _test_##name##_def = { flags, #name, test_##name, description }; \
  __attribute__((constructor)) void _register_test_##name()                              \
  {                                                                                      \
    _g_test_list.push_back(_test_##name##_def);                                          \
  }                                                                                      \
  void test_##name(TestContext &ctx)
