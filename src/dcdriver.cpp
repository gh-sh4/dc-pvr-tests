/* This program is a slight modification of the libdream/ta example program.
   The big difference in this program is that we add in a small bit of user
   input code, and that we support render-to-texture mode. */

#include <array>
#include <cstdlib>
#include <stdio.h>
#include <atomic>
#include <span>
#include <thread>

#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#include <arch/types.h>
#include <arch/timer.h>
#include <arch/wdt.h>

// #include <kos/init.h>
#include <kos/oneshot_timer.h>

#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include "test.h"

// KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NET);

/* Microsecond-based units */
#define MSEC 1000
#define SEC (1000 * MSEC)

/* Test configuration constants */
#define WDT_PET_COUNT 4000
#define WDT_INTERVAL (500 * MSEC)
#define WDT_SECONDS 10
#define WDT_COUNT_MAX ((WDT_SECONDS * SEC) / WDT_INTERVAL)

pvr_init_params_t pvr_params = {
  { PVR_BINSIZE_8, PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_0 },
  //   .opb_overflow_count = 16,
};

/////

std::atomic<bool> running;
oneshot_timer_t *safety_timer = nullptr;

const uint64_t safety_timeout_ms  = 5000;
const uint64_t safety_interval_ms = 250;
uint64_t last_safe_time_ms        = 0;

static void
timer_callback(void *data)
{
  const uint64_t now = timer_ms_gettime64();

  if (now - last_safe_time_ms > safety_timeout_ms) {
    printf("Safety timer expired, exiting\n");
    exit(-1);
  }

  last_safe_time_ms = now;
  oneshot_timer_start(safety_timer);
}

bool
is_start_pressed()
{
  maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
  if (cont != NULL) {
    cont_state_t *state = (cont_state_t *)maple_dev_status(cont);

    if (state != NULL) {
      if (state->buttons & CONT_START)
        return true;
    }
  }
  return false;
}

// define test_func_t as function pointer to test acceptiong void*

void
run_single_test(const char *name, TestFunc func, const char *description)
{
  // Reset the safety timeout
  last_safe_time_ms = timer_ms_gettime64();

  printf("START_TEST %s\n", name);
  const uint64_t time_start = timer_ns_gettime64();
  TestContext context(name);
  func(context);
  const uint64_t time_end = timer_ns_gettime64();
  const uint64_t time_ns  = time_end - time_start;
  printf("END_TEST %s time_ns %llu\n", name, time_ns);
}

void
run_all_tests()
{
  auto &test_list = get_test_list();

  std::sort(test_list.begin(),
            test_list.end(),
            [](const TestDefinition &a, const TestDefinition &b) {
              return strcmp(a.name, b.name) < 0;
            });

  // Run all tests
  for (const TestDefinition &test : get_test_list()) {

    if (strcmp(test.name, "isp_simple_render") != 0)
      continue;

    run_single_test(test.name, test.func, test.description);
  }
}

int
main(int argc, char **argv)
{
  // pvr_init(&pvr_params);
  // pvr_set_bg_color(0.1f, 0.1f, 0.2f);

  // Setup 'safety' timer to ensure the program doesn't hang
  last_safe_time_ms = timer_ms_gettime64();
  safety_timer      = oneshot_timer_create(timer_callback, nullptr, safety_interval_ms);
  oneshot_timer_start(safety_timer);

  run_all_tests();

  // pvr_shutdown();
  oneshot_timer_destroy(safety_timer);

  return 0;
}
