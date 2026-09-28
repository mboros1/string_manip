# ESP-IDF app that runs the benchmarks on an ESP32 and reports over serial.
# Build, flash and collect the output with `make esp32_bench`
# (tools/esp32_run.py).

cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(faf_bench)
