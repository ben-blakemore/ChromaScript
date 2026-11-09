# Unit tests

Run the tests from the dissertation directory:

```sh
make -C dev test
```

The tests use [doctest](https://github.com/doctest/doctest), a small C++ framework.
Its header is included in `third_party/doctest/`, so there is nothing extra to
install. Tests need a C++17 compiler, Lua and FFTW development packages. They
run without an OpenGL window or sound server.

## Adding a test

Add a `TEST_CASE` to an existing `test_*.cpp` file or create a new one in this
directory. The Makefile finds the files automatically and doctest registers
the tests. For example:

```cpp
#include "doctest/doctest.h"
#include "dsl/utils.hpp"

TEST_CASE("zero brightness produces black")
{
    const auto rgb = hsv_to_rgb(0.5f, 1.0f, 0.0f);
    CHECK(rgb[0] == 0.0f);
    CHECK(rgb[1] == 0.0f);
    CHECK(rgb[2] == 0.0f);
}
```

Use `CHECK` to report a failure and continue, or `REQUIRE` when later checks
need the assertion to pass, such as checking a scene exists before reading a
shape. `SUBCASE` runs each input independently in tests that cover several
values. Failed checks show the test name, source line and actual values.

The files are grouped by subject: `test_dsl.cpp`, `test_colour.cpp`,
`test_chroma.cpp` and `test_band_config.cpp`. Band configuration tests cover
custom ranges, invalid input and the current user configuration file.
`test_main.cpp` asks doctest to provide the test runner.
`audio_stub.cpp` supplies fixed readings for DSL tests. The Lua fixture creates
a temporary script and captures expected error messages.

## Complete suite and sanitised run

`make -C dev test` runs every test case, including low-register tones, pure noise,
quiet tones and tones mixed with noise. No accuracy checks are skipped by default.
The analyser currently misclassifies C2 and D2 at three amplitudes each and
establishes a pitch class for nine pure-noise inputs. These are real failures:
the command returns a non-zero status until the implementation satisfies them.

To run the same suite with memory and undefined-behaviour sanitisers enabled:

```sh
make -C dev test-sanitise
```

To run one test while investigating a failure, use doctest's built-in filter
from `dev/`:

```sh
./build/unit_tests --test-case="ISCM defaults"
```

The suite covers declarations, audio bindings, colour conversions, numeric
boundaries, scene replacement, Lua callback errors, synthetic tones across
registers, noise rejection and the eight example scripts. It does not establish
GPU appearance, frame rate, live capture, file polling or accuracy on complete
musical recordings.
