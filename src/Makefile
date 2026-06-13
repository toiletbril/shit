# make MODE=dbg/rel/prof/cosmo/cosmo_dbg
MODE ?= dbg
TARGET ?= # Linux/Windows_NT/TODO: Darwin
ARCH ?= $(shell uname -m) # TODO: arm64/x86_64

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

DIRS := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

# Windows_NT/Linux/Darwin
ifeq ($(TARGET),)
ifeq ($(OS), Windows_NT)
TARGET := Windows_NT
else
TARGET := $(shell uname -s)
endif
endif

# Validate TARGET
ifneq ($(TARGET), Linux)
ifneq ($(TARGET), Windows_NT)
ifneq ($(TARGET), Darwin)
$(error Unknown target: $(TARGET))
endif
endif
endif

OBJ_DIR := o/$(TARGET)/$(MODE)

OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

# Linker target file.
ifeq ($(MODE), cosmo)
OUT := ../shit.com
else ifeq ($(TARGET), Windows_NT)
OUT := ../shit.exe
else
OUT := ../shit
endif

# /dev/null
ifeq ($(OS), Windows_NT)
DEVNULL := "NUL"
else
DEVNULL := "/dev/null"
endif

# Cosmopolitan/Clang.
ifeq ($(MODE), cosmo)
CXX := cosmoc++
else ifeq ($(MODE), cosmo_dbg)
CXX := cosmoc++
else ifeq ($(OS), Windows_NT)
CXX := clang++
endif

# If we're cross-compiling, use MingW.
ifeq ($(TARGET), Windows_NT)
ifneq ($(OS), Windows_NT)
CXX := x86_64-w64-mingw32-g++
endif
endif

ifeq ($(TARGET), Darwin)
ifneq ($(UNAME_S), Darwin)
$(error cross-compiling to macOS is not supported yet)
endif
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
CXXFLAGS := # nothing

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

LINK_LIBS :=

# Speed up the linking a bit.
ifneq ($(shell which mold > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=mold
else ifneq ($(shell which lld > $(DEVNULL) 2>&1),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif

# MODE=rel
ifeq ($(MODE), rel)
CXXFLAGS += \
	-s \
	-DNDEBUG \
	-flto
ifneq ($(TARGET), Windows_NT)
# TODO: windows optimization fucks up the RAM.
CXXFLAGS += \
	-static-pie \
	-O3
endif
ifeq ($(TARGET), Linux)
CXXFLAGS += \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
# MODE=dbg
else ifeq ($(MODE), dbg)
CXXFLAGS += \
	-fno-omit-frame-pointer \
	-g3 \
	-O0
ifneq ($(TARGET), Windows_NT)
ifneq ($(MODE), cosmo)
CXXFLAGS += \
	-fsanitize=address \
	-fsanitize=undefined
endif
endif
# MODE=cosmo
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
# MODE=cosmo_dbg
else ifeq ($(MODE), cosmo_dbg)
CXXFLAGS += \
	-mcosmo \
	-mdbg \
	-fexceptions \
	-g3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo_dbg)
endif
# MODE=prof
else ifeq ($(MODE), prof)
CXXFLAGS += \
	-g3 \
	-DNDEBUG \
	-DSHIT_NO_TOILETLINE \
	-fno-omit-frame-pointer \
	-flto \
	-O3
ifneq ($(TARGET), Windows_NT)
# TODO: windows optimization fucks up the RAM.
CXXFLAGS += \
	-static-pie \
	-O3
endif
ifeq ($(TARGET), Linux)
CXXFLAGS += \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
else ifeq ($(MODE), cov)
CXXFLAGS += \
	-g3 \
	-DNDEBUG \
	-fno-omit-frame-pointer \
	-O0 \
	-fprofile-instr-generate \
	-fcoverage-mapping
ifneq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-static-pie \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "prof", "cov", "cosmo", "cosmo_dbg")
endif

# The command-substitution drain runs on a pthread, so every POSIX build links
# libpthread. Native Windows clang has no libpthread and uses the Win32 thread
# API instead, so the flag stays off there.
ifneq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-pthread
LINK_LIBS += -lpthread
endif

# Missing Windows libraries.
ifeq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-luserenv \
	-ladvapi32
endif

ifeq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-Wno-unknown-pragmas \
	-static \
	-static-libgcc \
	-static-libstdc++
endif

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

-include $(DEPS)
