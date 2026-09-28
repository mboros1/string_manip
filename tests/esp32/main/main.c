// Runs every test suite on the device and reports over the serial console.
// Each test file's main() is renamed <file>_main at compile time (see
// CMakeLists.txt); tools/esp32_run.py watches for the FAF_TESTS_* lines.

#include "faf_string_mem.h"
#include "faf_test.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

#define TEST_FILE(name) int name##_main(int argc, char **argv);
#include "test_list.h"
#undef TEST_FILE

void app_main(void) {
  static char *argv[] = {"faf_tests", NULL};
  int suites = 0, failed = 0;

  // Boards on the chip's own USB drop output until the host reopens the port
  // after the reset: give the runner a moment to start listening
  vTaskDelay(pdMS_TO_TICKS(1000));
  printf("FAF_TESTS_BEGIN pools=%d slots=%d\n", FAF_NPOOLS, FAF_POOL_SLOTS);
#define TEST_FILE(name)                                                        \
  clear_test_suites();                                                         \
  suites++;                                                                    \
  if (name##_main(1, argv) != 0) {                                             \
    failed++;                                                                  \
    printf("FAF_TESTS_FAILED_FILE %s\n", #name);                               \
  }
#include "test_list.h"
#undef TEST_FILE
  printf("FAF_TESTS_END suites=%d failed=%d\n", suites, failed);
}
