# make MODE=dbg/rel/prof/cosmo/cosmo_dbg
MODE ?= dbg

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

DIRS := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

# Translate a list of <...>.cpp files to a list of paths like
# o/$MODE/<...>.o.
# The mingw cross objects go in a separate tree so a Windows build does not
# clobber the POSIX objects that share the same MODE, and the reverse.
ifeq ($(WIN), 1)
OBJ_DIR := o/$(MODE)-win
else
OBJ_DIR := o/$(MODE)
endif

OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)
# One dependency file per object, written by the compiler, so a header edit
# rebuilds every source that includes it.
DEPS := $(OBJECTS:.o=.d)

# TARGET_WINDOWS is set for a native Windows build and for a mingw cross-compile
# from a POSIX host with WIN=1, so the compile and link flags target Windows
# while the host-side build commands keep the POSIX shell and /dev/null.
TARGET_WINDOWS :=
ifeq ($(OS), Windows_NT)
TARGET_WINDOWS := 1
endif
ifeq ($(WIN), 1)
TARGET_WINDOWS := 1
endif

ifneq ($(TARGET_WINDOWS), 1)
UNAME_S := $(shell uname -s)
TARGET_MACOS :=
ifeq ($(UNAME_S), Darwin)
  TARGET_MACOS := 1
else
all:
	$(error Unable to decipher your OS)
endif
endif

ifeq ($(TARGET_WINDOWS), 1)
OUT := ../shit.exe
else
OUT := ../shit
endif

ifeq ($(OS), Windows_NT)
DEVNULL := "NUL"
else
DEVNULL := "/dev/null"
endif

ifeq ($(MODE), cosmo)
CXX := cosmoc++
else ifeq ($(MODE), cosmo_dbg)
CXX := cosmoc++
else ifeq ($(OS), Windows_NT)
CXX := clang++
endif

ifeq ($(WIN), 1)
CXX := x86_64-w64-mingw32-g++
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
ifeq ($(TARGET_WINDOWS), 1)
USE_LD :=
else ifneq ($(shell which mold > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=mold
else ifneq ($(shell which lld > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif

ifeq ($(MODE), rel)
CXXFLAGS += \
	-s \
	-DNDEBUG \
	-flto \
	-static-pie \
	-O0
ifneq ($(TARGET_WINDOWS), 1)
ifneq ($(TARGET_MACOS), 1)
# Linux
CXXFLAGS += \
	-O3 \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
else ifeq ($(TARGET_MACOS), 1)
# MacOS
CXXFLAGS += \
	-O3
else
# Sindows
CXXFLAGS += \
	-O0
endif
else ifeq ($(MODE), dbg)
CXXFLAGS += \
	-fno-omit-frame-pointer \
	-g3 \
	-O0
ifneq ($(TARGET_WINDOWS), 1)
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
	-O3
# The same static link and libstdc++ drop as rel, so a profile measures the
# shipped binary and the static link lets the profiled binary run in a container
# whose glibc is older than the host's. Link-time optimization joins this block
# so the Windows cross path skips its tens-of-gigabytes whole-program pass.
ifneq ($(TARGET_WINDOWS), 1)
CXXFLAGS += \
	-flto \
	-static-pie \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
else ifeq ($(MODE), cov)
# Mirror prof, but instrument for clang source-based coverage instead of a
# sampling profiler. Build at -O0 and without LTO so a coverage region maps to a
# single source branch, and keep -DNDEBUG so a compiled-out assert does not read
# as a permanently uncovered branch. Unlike prof the line editor stays in, since
# coverage has no timing to perturb and the editor backs the UTF-8 width logic
# the error caret uses, so the suite reproduces the golden output and passes.
CXXFLAGS += \
	-g3 \
	-DNDEBUG \
	-fno-omit-frame-pointer \
	-O0 \
	-fprofile-instr-generate \
	-fcoverage-mapping
ifneq ($(TARGET_WINDOWS), 1)
CXXFLAGS += \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "prof", "cov", "cosmo")
endif

# The command-substitution drain runs on a pthread, so every POSIX build links
# libpthread. Native Windows clang has no libpthread and uses the Win32 thread
# API instead, so the flag stays off there.
ifneq ($(TARGET_WINDOWS), 1)
CXXFLAGS += \
	-pthread
LINK_LIBS += -lpthread
endif

# Missing Windows libraries.
ifeq ($(TARGET_WINDOWS), 1)
CXXFLAGS += \
	-luserenv \
	-ladvapi32
endif

# g++ on the mingw cross path does not know the clang loop pragmas the
# containers carry, so the unknown-pragma warning is silenced for that build.
# The mingw runtime is linked statically so the .exe carries libgcc and
# libstdc++ rather than depending on libgcc_s_seh-1.dll and libstdc++-6.dll at
# run time, which keeps it a single standalone binary the way the POSIX build is.
ifeq ($(WIN), 1)
CXXFLAGS += \
	-Wno-unknown-pragmas \
	-static \
	-static-libgcc \
	-static-libstdc++
endif

# Making objects in o/$MODE/<...>.o. -MMD writes the header dependency
# list next to each object, and -MP guards against a removed header. The
# Makefile is a prerequisite so a change to the flags, such as the link mode,
# forces a recompile rather than relinking stale objects built with old flags.
$(OBJ_DIR)/%.o: %.cpp Makefile
	printf "\t%-42s $@\n" $<
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $(CWD)$@

# Linking.
$(OUT): $(OBJECTS)
	printf "\t$(OBJECTS) <-> $(OUT)\n"
	$(CXX) $(CXXFLAGS) $(USE_LD) $(OBJECTS) $(LINK_LIBS) -o $@

dirs: $(DIRS)
	mkdir -p $(OBJ_DIR)/
	mkdir -p $(OBJ_DIR)/$<

shit: dirs
	$(MAKE) $(OUT)

install: shit
ifneq ($(TARGET_WINDOWS), 1)
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
	rm -rf ../*.profraw ../*.profdata ../coverage # Coverage

.PHONY: dirs shit install tidy fmt clean

# Pull in the generated header dependencies. Missing on the first build, which
# is fine since every object is compiled then anyway.
-include $(DEPS)
