#!/bin/bash

gcc-14 str_len_test.c -O3 -I$(brew --prefix simde)/include/ -flax-vector-conversions
