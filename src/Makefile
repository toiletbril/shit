# make MODE=dbg/rel/prof/cosmo/cosmo_dbg
MODE ?= dbg

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

DIRS := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

# Translate a list of <...>.cpp files to a list of paths like
# o/$MODE/<...>.o.
OBJECTS := $(SOURCES:%.cpp=o/$(MODE)/%.o)
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

ifeq ($(MODE), cosmo)
CXX := cosmoc++
else ifeq ($(MODE), cosmo_dbg)
CXX := cosmoc++
else ifeq ($(OS), Windows_NT)
CXX := clang++
endif

ifeq ($(MODE), cosmo)
OS_INFO := "unknown-cosmo"
else ifeq ($(MODE), cosmo_dbg)
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
	-DSHIT_BUILD_MODE="\"$(MODE)\"" \
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
	-pipe \
	-fvisibility=hidden \
	-fno-rtti \
	-fno-semantic-interposition \
	-Bsymbolic \
	-fno-strict-aliasing \
	$(DEFINES) \
	$(ENVCXXFLAGS)

# Libraries that must trail the objects on the link line, so a symbol an object
# needs is still unresolved when the linker reaches the archive. libsupc++ backs
# the exception runtime and operator new under -nostdlib++, and libpthread backs
# the command-substitution drain thread.
LINK_LIBS :=

# Speed up the linking a bit.
ifneq ($(shell which mold > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=mold
else ifneq ($(shell which lld > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif

ifeq ($(MODE), rel)
# Link-time optimization merges the duplicated header-only template containers
# across translation units, which the per-unit compile cannot do on its own.
CXXFLAGS += \
	-s \
	-DNDEBUG \
	-O3 \
	-flto
# Link the runtime statically so a -c invocation or a script launch does not
# pay the dynamic linker on every startup. This nearly halves the startup time
# and brings it close to dash. Drop libstdc++ with -nostdlib++ and keep only
# libsupc++ for the exception runtime, since the shell carries its own
# containers. Windows clang has no libsupc++, so the drop stays POSIX-only.
ifneq ($(OS), Windows_NT)
# Plain -static rather than -static-pie. The static PIE form gives the loader
# address-space randomization, but glibc's static-PIE startup lowers
# RLIMIT_CORE to one byte, which makes ulimit -c report 0 and disables core
# dumps, a shell-visible regression that broke dash parity. A dynamic -pie
# keeps the limit but pays the dynamic linker on every startup, which the speed
# goal forbids, so the correct ulimit and the fast static startup win over the
# randomization here.
CXXFLAGS += \
	-static \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
else ifeq ($(MODE), dbg)
CXXFLAGS += \
	-fno-omit-frame-pointer \
	-g3 \
	-O0
ifneq ($(OS), Windows_NT)
ifneq ($(MODE), cosmo)
CXXFLAGS += \
	-fsanitize=address \
	-fsanitize=undefined
endif
endif
else ifeq ($(MODE), cosmo)
CXXFLAGS += \
	-mcosmo \
	-mtiny \
	-fexceptions \
	-g \
	-DNDEBUG \
	-O3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo)
endif
else ifeq ($(MODE), cosmo_dbg)
CXXFLAGS += \
	-mcosmo \
	-mdbg \
	-fexceptions \
	-g3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo_dbg)
endif
else ifeq ($(MODE), prof)
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
# The same libstdc++ drop as rel, so a profile measures the shipped binary.
ifneq ($(OS), Windows_NT)
CXXFLAGS += \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "prof", "cosmo")
endif

# The command-substitution drain runs on a pthread, so every POSIX build links
# libpthread. Native Windows clang has no libpthread and uses the Win32 thread
# API instead, so the flag stays off there.
ifneq ($(OS), Windows_NT)
CXXFLAGS += \
	-pthread
LINK_LIBS += -lpthread
endif

# Missing Windows libraries.
ifeq ($(OS), Windows_NT)
CXXFLAGS += \
	-luserenv \
	-ladvapi32
endif

# Making objects in o/$MODE/<...>.o. -MMD writes the header dependency
# list next to each object, and -MP guards against a removed header. The
# Makefile is a prerequisite so a change to the flags, such as the link mode,
# forces a recompile rather than relinking stale objects built with old flags.
o/$(MODE)/%.o: %.cpp Makefile
	printf "\t%-42s $@\n" $<
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $(CWD)$@

# Linking.
$(OUT): $(OBJECTS)
	printf "\t$(OBJECTS) <-> $(OUT)\n"
	$(CXX) $(CXXFLAGS) $(USE_LD) $(OBJECTS) $(LINK_LIBS) -o $@

dirs: $(DIRS)
	mkdir -p o/$(MODE)/
	mkdir -p o/$(MODE)/$<

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
