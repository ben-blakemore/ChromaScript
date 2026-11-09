# Audio-reactive visual DSL.
#
# All dependencies are ordinary system packages found via pkg-config; see
# README.md for the exact install commands per distribution. The only vendored
# application code is the glad OpenGL loader (third_party/glad). Tests also
# vendor the single doctest header, so no test framework installation is needed.

CC ?= cc

# Distributions disagree on the name of the Lua pkg-config package
# (Debian/Ubuntu: lua5.4, some others: lua-5.4 or plain lua), so probe for it.
LUA_PKG := $(shell for p in lua5.4 lua-5.4 lua54 lua; do \
                     pkg-config --exists $$p && { echo $$p; break; }; done)

# fftw3f is the single-precision build of FFTW, matching the float sample
# pipeline; the double-precision fftw3 package is a different library name.
PKGS := glfw3 $(LUA_PKG) libpulse-simple gl fftw3f

# Headless tests only need Lua and FFTW, not the application dependencies.
TEST_TARGETS := test test-sanitise
ifneq ($(MAKECMDGOALS),)
  ifeq ($(filter-out $(TEST_TARGETS) clean,$(MAKECMDGOALS)),)
    PKGS := $(LUA_PKG) fftw3f
  endif
endif

# Fail early with a clear message rather than mid-compile with a missing
# header, but not for `make clean`, which needs no dependencies.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
  ifeq ($(LUA_PKG),)
    $(error No Lua development package found via pkg-config. Install it first - see README.md)
  endif
  MISSING := $(strip $(foreach p,$(PKGS),\
               $(if $(shell pkg-config --exists $(p) && echo y),,$(p))))
  ifneq ($(MISSING),)
    $(error Missing development packages: $(MISSING). See README.md for install commands)
  endif
endif

# Include paths and warnings shared by both languages. -Isrc so headers are
# included by a path relative to src/ ("runtime/audio/audio_handler.hpp") rather
# than by a chain of ../.. that breaks whenever a directory moves.
COMMON_FLAGS := -O2 -Wall -Wextra -MMD -MP \
                -Isrc -Ithird_party -Ithird_party/glad/include \
                $(shell pkg-config --cflags $(PKGS))

# gnu11/gnu++17 rather than the strict standards: the source uses POSIX and glibc
# extensions (M_PI, popen).
CFLAGS   += -std=gnu11 $(COMMON_FLAGS)
CXXFLAGS += -std=gnu++17 $(COMMON_FLAGS)
LDLIBS   += $(shell pkg-config --libs $(PKGS)) -lm -lpthread

# The C++ sources under src/, plus the vendored glad loader, which is C and stays
# compiled as C. `find` rather than $(wildcard) because wildcard cannot recurse.
#
CXX_SRCS := $(shell find src -name '*.cpp')
C_SRCS   := third_party/glad/src/gl.c

OBJS := $(CXX_SRCS:%.cpp=build/%.o) $(C_SRCS:%.c=build/%.o)
DEPS := $(OBJS:.o=.d)

# Linked with $(CXX): the C++ objects need the standard library, which $(CC)
# would not pull in.
dsl: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

run: dsl
	./dsl scripts/circle.lua

# Tests use the real Lua parser and chroma analyser, with deterministic audio
# accessors in place of PulseAudio capture. No window or sound server is needed.
TEST_SRCS := $(wildcard tests/doctest/*.cpp) \
             src/dsl/dsl.cpp src/dsl/utils.cpp src/runtime/audio/chroma.cpp \
             src/runtime/audio/band_config.cpp
TEST_OBJS := $(TEST_SRCS:%.cpp=build/%.o)
TEST_LIBS := $(shell pkg-config --libs $(LUA_PKG) fftw3f) -lpthread

build/unit_tests: $(TEST_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(TEST_LIBS)

test: build/unit_tests
	./build/unit_tests

SANITISE_FLAGS := -O1 -g -fsanitize=address,undefined,float-cast-overflow \
                  -fno-sanitize-recover=all -fno-omit-frame-pointer
SANITISE_OBJS := $(TEST_SRCS:%.cpp=build/sanitised/%.o)

build/sanitised/%.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(filter-out -O%,$(CXXFLAGS)) $(SANITISE_FLAGS) -c -o $@ $<

build/sanitised/unit_tests: $(SANITISE_OBJS)
	$(CXX) $(LDFLAGS) $(SANITISE_FLAGS) -o $@ $^ $(TEST_LIBS)

test-sanitise: build/sanitised/unit_tests
	./build/sanitised/unit_tests

clean:
	rm -rf build dsl

-include $(DEPS) $(TEST_OBJS:.o=.d) $(SANITISE_OBJS:.o=.d)

.PHONY: run clean test test-sanitise
