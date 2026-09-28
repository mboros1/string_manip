// Runs the benchmarks on the device and reports over the serial console;
// tools/esp32_run.py watches for the FAF_BENCH_* lines.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

int bench_main(int argc, char **argv);

void app_main(void) {
  static char *argv[] = {"faf_bench", NULL};

  // Boards on the chip's own USB drop output until the host reopens the port
  // after the reset: give the runner a moment to start listening
  vTaskDelay(pdMS_TO_TICKS(1000));
  printf("FAF_BENCH_BEGIN\n");
  bench_main(1, argv);
  printf("FAF_BENCH_END\n");
}
