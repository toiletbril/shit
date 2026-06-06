# make SHIT_MODE=dbg/rel/prof/nostdlibpp/cosmo/cosmo_dbg
SHIT_MODE ?= dbg

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

DIRS := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

# Translate a list of <...>.cpp files to a list of paths like
# o/$SHIT_MODE/<...>.o.
OBJECTS := $(SOURCES:%.cpp=o/$(SHIT_MODE)/%.o)
# One dependency file per object, written by the compiler, so a header edit
# rebuilds every source that includes it.
DEPS := $(OBJECTS:.o=.d)

ifeq ($(OS), Windows_NT)
OUT := ../shit.exe
DEVNULL := "NUL"
else
OUT := ../shit
DEVNULL := "/dev/null"
endif

ifeq ($(SHIT_MODE), cosmo)
CXX := cosmoc++
else ifeq ($(SHIT_MODE), cosmo_dbg)
CXX := cosmoc++
else ifeq ($(OS), Windows_NT)
CXX := clang++
endif

ifeq ($(SHIT_MODE), cosmo)
OS_INFO := "unknown-cosmo"
else ifeq ($(SHIT_MODE), cosmo_dbg)
OS_INFO := "unknown-cosmo-dbg"
else
OS_INFO := $(shell uname -rvom || echo "<no uname>")
endif

COMMIT_HASH := $(shell git rev-parse HEAD || echo "<no git>")

# Remember CXXFLAGS passed to us from CLI.
ENVCXXFLAGS := $(CXXFLAGS)
override undefine CXXFLAGS

DEFINES := \
	-DSHIT_COMPILER_COMMAND="\"$(CXX)\"" \
	-DSHIT_OS_INFO="\"$(OS_INFO)\"" \
	-DSHIT_COMMIT_HASH="\"$(COMMIT_HASH)\"" \
	-DSHIT_BUILD_MODE="\"$(SHIT_MODE)\"" \
	-DSHIT_ENVCXXFLAGS="\"$(ENVCXXFLAGS)\""

CXXFLAGS := \
	-Wall \
	-Wextra \
	-Wdouble-promotion \
	-Wno-format \
	-Werror \
	-Wno-unused-command-line-argument \
	-Wno-deprecated \
	-std=c++23 \
	-Wno-unknown-warning-option \
	-Wno-error=maybe-uninitialized \
	-pthread \
	-pipe \
	-fvisibility=hidden \
	-fno-rtti \
	-fno-semantic-interposition \
	-Bsymbolic \
	-fno-strict-aliasing \
	$(DEFINES) \
	$(ENVCXXFLAGS)

# Speed up the linking a bit.
ifneq ($(shell which mold > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=mold
else ifneq ($(shell which lld > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif

ifeq ($(SHIT_MODE), rel)
# Link-time optimization merges the duplicated header-only template containers
# across translation units, which the per-unit compile cannot do on its own.
CXXFLAGS += \
	-s \
	-DNDEBUG \
	-O3 \
	-flto
# Link the runtime statically so a -c invocation or a script launch does not
# pay the dynamic linker on every startup. This nearly halves the startup time
# and brings it close to dash.
ifneq ($(OS), Windows_NT)
CXXFLAGS += \
	-static
endif
else ifeq ($(SHIT_MODE), dbg)
CXXFLAGS += \
	-fno-omit-frame-pointer \
	-g3 \
	-O0
ifneq ($(OS), Windows_NT)
ifneq ($(SHIT_MODE), cosmo)
CXXFLAGS += \
	-fsanitize=address \
	-fsanitize=undefined
endif
endif
else ifeq ($(SHIT_MODE), cosmo)
CXXFLAGS += \
	-mcosmo \
	-mtiny \
	-fexceptions \
	-g \
	-DNDEBUG \
	-O3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for SHIT_MODE=cosmo)
endif
else ifeq ($(SHIT_MODE), cosmo_dbg)
CXXFLAGS += \
	-mcosmo \
	-mdbg \
	-fexceptions \
	-g3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for SHIT_MODE=cosmo_dbg)
endif
else ifeq ($(SHIT_MODE), nostdlibpp)
# Optimized like rel, but link without libstdc++. libsupc++ supplies the
# exception runtime and operator new and delete, pthread the drain thread, and
# libc the rest, so the binary needs no C++ standard library. The libraries go
# in LINK_LIBS so the link rule places them after the objects, where a static
# archive is searched only for symbols the objects already reference.
CXXFLAGS += \
	-s \
	-DNDEBUG \
	-O3 \
	-flto \
	-nostdlib++
LINK_LIBS := \
	-lsupc++ \
	-lpthread
else ifeq ($(SHIT_MODE), prof)
# Optimized like rel, but keep frame pointers and symbols so a profiler can
# unwind, and drop the line editor so the terminal handling does not perturb
# the measurement.
CXXFLAGS += \
	-g3 \
	-DNDEBUG \
	-DSHIT_NO_TOILETLINE \
	-fno-omit-frame-pointer \
	-O3 \
	-flto
else
$(error Invalid SHIT_MODE=$(SHIT_MODE). Available modes are "dbg", "rel", "prof", "nostdlibpp", "cosmo")
endif

# Missing Windows libraries.
ifeq ($(OS), Windows_NT)
CXXFLAGS += \
	-luserenv \
	-ladvapi32
endif

# Making objects in o/$SHIT_MODE/<...>.o. -MMD writes the header dependency
# list next to each object, and -MP guards against a removed header.
o/$(SHIT_MODE)/%.o: %.cpp
	printf "\t%-42s $@\n" $<
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $(CWD)$@

# Linking.
$(OUT): $(OBJECTS)
	printf "\t$(OBJECTS) <-> $(OUT)\n"
	$(CXX) $(CXXFLAGS) $(USE_LD) $(OBJECTS) $(LINK_LIBS) -o $@

dirs: $(DIRS)
	mkdir -p o/$(SHIT_MODE)/
	mkdir -p o/$(SHIT_MODE)/$<

shit: dirs
	$(MAKE) $(OUT)

install: shit
ifneq ($(OS), Windows_NT)
	install -m 755 $(OUT) /usr/local/bin/shit
	echo "Installed in '/usr/local/bin/shit'."
else
	$(error Unable to figure out install directory for Windows. \
			Please put '$(realpath ..)' in your PATH manually)
endif

CLANG_TIDY ?= clang-tidy

tidy:
	echo "	CLANG_TIDY $(SOURCES) $(HEADERS)"
	$(CLANG_TIDY) $(SOURCES) $(HEADERS) --header-filter="*.hpp"

CLANG_FORMAT ?= clang-format

fmt:
	echo "	CLANG_FORMAT -i $(SOURCES) $(HEADERS)"
	$(CLANG_FORMAT) -i $(SOURCES) $(HEADERS)

clean:
	rm -rf ./o ../shit ../shit.exe
	rm -rf ../*.ilk ../*.pdb ../*.lib # Windows
	rm -rf ../shit.com.dbg ../shit.aarch64.elf # Cosmopolitan
	rm -rf ../perf*

.PHONY: dirs shit install tidy fmt clean

# Pull in the generated header dependencies. Missing on the first build, which
# is fine since every object is compiled then anyway.
-include $(DEPS)
