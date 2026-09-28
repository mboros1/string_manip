// Runs the benchmarks on the device and reports over the serial console;
// tools/esp32_run.py watches for the FAF_BENCH_* lines.

#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

int bench_main(int argc, char **argv);

// Space-separated groups to run (default: all), set at build time:
// `make esp32_bench BENCH_GROUPS="kernels"`
#ifndef FAF_BENCH_GROUPS
#define FAF_BENCH_GROUPS ""
#endif

void app_main(void) {
  static char groups[] = FAF_BENCH_GROUPS;
  char *argv[8] = {"faf_bench"};
  int argc = 1;
  for (char *g = strtok(groups, " "); g && argc < 7; g = strtok(NULL, " "))
    argv[argc++] = g;

  // Boards on the chip's own USB drop output until the host reopens the port
  // after the reset: give the runner a moment to start listening
  vTaskDelay(pdMS_TO_TICKS(1000));
  printf("FAF_BENCH_BEGIN\n");
  // what tools/bench_track.py records about the machine
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  printf("FAF_BENCH_META chip=%s revision=%d cores=%d mhz=%d idf=%s\n",
         CONFIG_IDF_TARGET, chip.revision, chip.cores,
         CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, esp_get_idf_version());
  bench_main(argc, argv);
  printf("FAF_BENCH_END\n");
}
