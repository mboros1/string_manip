Goal of this library is to create a full suite of string functions that operate on `const char*` and two pointer strings using crossplatform
simd operations with no dependencies on the standard library, allowing it to work cross platform and in a free standing enviroment.

## Building and Testing

This project uses a Makefile build system that can be configured for your local environment.

### Configuration Options

You can configure the build process in several ways:

1. **Environment Variables**: Set these variables to customize your build:
   - `CC`: Compiler to use (default: gcc)
   - `SIMDE_INCLUDE`: Path to SIMDE library include directory (if needed)
   - `CFLAGS`: Compiler flags
   - `TEST_FLAGS`: Compiler flags for test builds

2. **Local Configuration File**: Create a `.env` file in the project root with your configuration.

3. **VSCode Configuration**: If you're using VSCode, the build settings are configured in `.vscode/settings.json`.

### Example Configuration

For macOS with Homebrew:
```bash
CC=gcc-14
SIMDE_INCLUDE=$(brew --prefix simde)/include/
```

### Building

```bash
# Build everything
make

# Build and run all tests
make all_tests

# Build and run a specific test
make test_faf_string
```

### Cleaning

```bash
make clean
```
