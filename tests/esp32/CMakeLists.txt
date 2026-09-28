# ESP-IDF app that runs every test suite on an ESP32 and reports over serial.
# Build, flash and collect the results with `make esp32_test` (see run.py).

cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(faf_tests)
