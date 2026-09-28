
* TODO: define list of functions.
    * length - done
    * toupper - done
    * tolower - done
    * split - done (plus split_owned and the next_token iterator)
    * ltrim, rtrim, trim - done
    * concat - done
    * strmult(?) - done as faf_string_repeat
    * sort, both individual str and arr - done (counting sort for chars,
      introsort for arrays; pdqsort.h is C++ only, so it isn't used)
    * copy - done
    * reverse - done
    * compare - done
    * hash - done
    * format - done (%% %c %s %S %d %i %u %x; no width/precision/floats yet)
    * contains - done
* Ideas
    * format: width, precision, floating point
    * find_set for sets of more than 16 bytes on SIMD (currently scalar bitmap)
    * next_token: faster path for tokens of 16+ bytes (see bench)
    * parse_f64: correctly rounded slow path (e.g. Eisel-Lemire)
