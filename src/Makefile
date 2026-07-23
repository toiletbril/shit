# make MODE=dbg/rel/prof/cosmo/cosmo_dbg
MODE ?= dbg
TARGET ?= # Linux/Windows_NT/Darwin
ARCH ?= $(shell uname -m) # TODO: arm64/x86_64

ROUTED_SOURCES := PlatformPosix.cpp PlatformPosixExtra.cpp PlatformWin32.cpp
SOURCES := $(filter-out $(ROUTED_SOURCES),$(wildcard *.cpp))
HEADERS := $(wildcard *.hpp)

DIRS := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

# The host OS, Windows_NT on Windows and the uname elsewhere.
ifeq ($(OS), Windows_NT)
HOST_OS := Windows_NT
else
HOST_OS := $(shell uname -s)
endif

# Windows_NT/Linux/Darwin
ifeq ($(TARGET),)
TARGET := $(HOST_OS)
endif

# Validate TARGET
ifneq ($(TARGET), Linux)
ifneq ($(TARGET), Windows_NT)
ifneq ($(TARGET), Darwin)
$(error Unknown target: $(TARGET))
endif
endif
endif

ifneq ($(TARGET), $(HOST_OS))
ifneq ($(TARGET), Windows_NT)
ifneq ($(TARGET), Darwin)
ifneq ($(TARGET), Linux)
$(error Cross-compilation to '$(TARGET)' from '$(HOST_OS)' is not supported)
endif
endif
endif
endif

ifeq ($(TARGET), Linux)
ifneq ($(HOST_OS), Linux)
ifneq ($(MODE), rel)
$(error Linux cross-compilation only supports MODE=rel)
endif
ifeq ($(ARCH), arm64)
LINUX_ZIG_ARCH := aarch64
else ifeq ($(ARCH), aarch64)
LINUX_ZIG_ARCH := aarch64
else ifeq ($(ARCH), x86_64)
LINUX_ZIG_ARCH := x86_64
else
$(error Linux cross-compilation does not support ARCH='$(ARCH)')
endif
LINUX_CROSS_COMPILATION := 1
endif
endif

ifeq ($(LINUX_CROSS_COMPILATION), 1)
OBJ_DIR := o/$(TARGET)/$(LINUX_ZIG_ARCH)/$(MODE)
else
OBJ_DIR := o/$(TARGET)/$(MODE)
endif

OBJECTS := $(SOURCES:%.cpp=$(OBJ_DIR)/%.o)
DEPS := $(OBJECTS:.o=.d)

MODE_SUFFIX := $(if $(filter rel,$(MODE)),,-$(MODE))
ifeq ($(MODE), cosmo)
OUT := ../shit$(MODE_SUFFIX).com
else ifeq ($(TARGET), Windows_NT)
OUT := ../shit$(MODE_SUFFIX).exe
else
OUT := ../shit$(MODE_SUFFIX)
endif

LINKED_OUT := $(OUT)
ifeq ($(MODE), rel)
ifneq ($(TARGET), Windows_NT)
LINKED_OUT := $(OBJ_DIR)/shit
endif
endif

# Cosmopolitan/Clang.
CXX_ORIGIN := $(origin CXX)
ifeq ($(MODE), cosmo)
CXX := cosmoc++
else ifeq ($(MODE), cosmo_dbg)
CXX := cosmoc++
else ifeq ($(OS), Windows_NT)
ifeq ($(CXX_ORIGIN), default)
CXX := clang++
endif
else ifeq ($(CXX_ORIGIN), default)
CXX := clang++
endif

# If we're cross-compiling, use MingW.
ifeq ($(TARGET), Windows_NT)
ifneq ($(OS), Windows_NT)
CXX := x86_64-w64-mingw32-g++
endif
endif

WINDOWS_COMPILER_TARGET := $(shell $(CXX) -dumpmachine 2>/dev/null)
WINDOWS_GNU_ABI := $(or $(findstring mingw,$(WINDOWS_COMPILER_TARGET)),\
	$(findstring windows-gnu,$(WINDOWS_COMPILER_TARGET)))

ifeq ($(TARGET), Linux)
ifneq ($(HOST_OS), Linux)
CXX := zig c++ -target $(LINUX_ZIG_ARCH)-linux-musl
endif
endif

ifeq ($(TARGET), Darwin)
ifneq ($(HOST_OS), Darwin)
ifeq ($(CXX_ORIGIN), default)
ifeq ($(ARCH), x86_64)
$(error x86_64 Darwin build is not supported)
else
CXX := oa64-clang++
endif
endif
endif
endif

ifeq ($(MODE), cosmo)
OS_INFO := "unknown-cosmo"
LIBC_INFO := cosmopolitan libc
else ifeq ($(MODE), cosmo_dbg)
OS_INFO := "unknown-cosmo-dbg"
LIBC_INFO := cosmopolitan libc
else ifeq ($(LINUX_CROSS_COMPILATION), 1)
OS_INFO := Linux $(LINUX_ZIG_ARCH) cross
else
OS_INFO := $(shell test -f /etc/os-release && . /etc/os-release 2>/dev/null; printf '%s%s' "$${PRETTY_NAME:+$$PRETTY_NAME }" "$$(uname -rvom 2>/dev/null || echo '<no uname>')")
endif

ifndef LIBC_INFO
ifeq ($(TARGET), Windows_NT)
ifneq ($(WINDOWS_GNU_ABI),)
LIBC_INFO := mingw msvcrt
else
LIBC_INFO := msvc
endif
else ifeq ($(TARGET), Darwin)
LIBC_INFO := Apple libSystem
else ifneq ($(TARGET), $(HOST_OS))
LIBC_INFO := zig musl libc++
else
LIBC_INFO := $(shell getconf GNU_LIBC_VERSION 2>/dev/null || { command -v ldd >/dev/null 2>&1 && ldd --version 2>&1 | head -n 1; } || echo 'unknown libc')
endif
endif

COMMIT_HASH := $(shell git rev-parse HEAD || echo "<no git>")

# Remember CXXFLAGS passed to us from CLI.
ENVCXXFLAGS := $(CXXFLAGS)
CXXFLAGS := # nothing

DEFINES := \
	-DSHIT_COMPILER_COMMAND="\"$(CXX)\"" \
	-DSHIT_LIBC="\"$(LIBC_INFO)\"" \
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
	-Wno-unknown-pragmas \
	-fno-strict-aliasing \
	$(DEFINES) \
	$(ENVCXXFLAGS)

ifeq ($(LINUX_CROSS_COMPILATION), 1)
CXXFLAGS += -Wno-date-time
endif

ifeq ($(TARGET), Linux)
CXXFLAGS += -Wl,-Bsymbolic
endif

LINK_LIBS := # nothing

ifeq ($(TARGET), Windows_NT)
ifeq ($(WINDOWS_GNU_ABI),)
LINK_LIBS += --rtlib=compiler-rt
endif
endif

ifeq ($(TARGET), Windows_NT)
ifneq ($(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif
else ifeq ($(TARGET), Darwin)
# mold is ELF-only and lld-as-Mach-O is rare on macOS, so the system ld64
# links directly with no -fuse-ld flag.
USE_LD :=
else ifneq ($(shell command -v mold 2>/dev/null),)
USE_LD := -fuse-ld=mold
else ifneq ($(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null),)
USE_LD := -fuse-ld=lld
else
USE_LD :=
endif

# MODE=rel
ifeq ($(MODE), rel)
CXXFLAGS += \
	-DNDEBUG \
	-O3 \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2
ifneq ($(TARGET), Windows_NT)
CXXFLAGS += -flto
endif
ifeq ($(TARGET), Linux)
ifeq ($(LINUX_CROSS_COMPILATION), 1)
CXXFLAGS += -static
else
CXXFLAGS += \
	-static-pie \
	-nostdlib++
LINK_LIBS += -lsupc++
endif
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
	-finstrument-functions \
	-DNDEBUG \
	-fno-omit-frame-pointer \
	-flto
ifneq ($(TARGET), Windows_NT)
CXXFLAGS += \
	-static-pie \
	-O3 \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=2
else
CXXFLAGS += \
	-O2
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
	-Wno-unknown-pragmas
ifneq ($(WINDOWS_GNU_ABI),)
CXXFLAGS += \
	-mconsole \
	-static \
	-static-libgcc \
	-static-libstdc++
endif
endif

# A bare `make` builds the shell, the same as `make shit`, rather than the first
# object the rules below would otherwise name as the default goal.
.DEFAULT_GOAL := shit

# Every object is built into $(OBJ_DIR), so its directory tree must exist first.
# The order-only prerequisite creates it before any compile, so a plain
# `make MODE=rel` from a clean tree works without first running the shit target.
$(OBJECTS): | dirs

$(OBJ_DIR)/%.o: %.cpp Makefile
	printf "\tCompiling %-42s $@... %-12s\r" $< ' '
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $(CWD)$@

# Linking.
$(LINKED_OUT): $(OBJECTS)
	printf "\tLinking $(LINKED_OUT)... %-64s\r" ' '
	$(CXX) $(CXXFLAGS) $(USE_LD) $(OBJECTS) $(LINK_LIBS) -o $@

dirs: $(DIRS)
	mkdir -p $(OBJ_DIR)/
	for source_dir in $(DIRS); do mkdir -p $(OBJ_DIR)/$$source_dir; done

shit: dirs
	$(MAKE) $(LINKED_OUT)
ifneq ($(LINKED_OUT), $(OUT))
	cp $(LINKED_OUT) $(OUT)
endif

# INSTALL_MOOD_SYMLINKS="sh bash dash".
PREFIX ?= /usr/local
DESTDIR ?=
bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man/man1
bashcompdir ?= $(PREFIX)/share/bash-completion/completions
INSTALL_MOOD_SYMLINKS ?=

REL_BINARY := ../shit
MANPAGE := ../docs/shit.1
BASH_COMPLETION := ../completions/shit.bash

install:
ifeq ($(TARGET), Windows_NT)
	$(error Unable to figure out install directory for Windows. \
			Please put '$(realpath ..)' in your PATH manually)
else
	$(MAKE) MODE=rel shit
	mkdir -p "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)" "$(DESTDIR)$(bashcompdir)"
	install -m 755 $(REL_BINARY) "$(DESTDIR)$(bindir)/shit"
	install -m 644 $(MANPAGE) "$(DESTDIR)$(mandir)/shit.1"
	install -m 644 $(BASH_COMPLETION) "$(DESTDIR)$(bashcompdir)/shit"
	for mood in $(INSTALL_MOOD_SYMLINKS); do \
		ln -sf shit "$(DESTDIR)$(bindir)/$$mood"; \
	done
	echo "Installed shit, its man page, and its bash completion under '$(DESTDIR)$(PREFIX)'."
endif

uninstall:
	rm -f "$(DESTDIR)$(bindir)/shit"
	rm -f "$(DESTDIR)$(mandir)/shit.1"
	rm -f "$(DESTDIR)$(bashcompdir)/shit"
	for mood in $(INSTALL_MOOD_SYMLINKS); do \
		rm -f "$(DESTDIR)$(bindir)/$$mood"; \
	done
	echo "Removed shit, its man page, and its bash completion from '$(DESTDIR)$(PREFIX)'."

CLANG_TIDY ?= clang-tidy

tidy:
	echo "	CLANG_TIDY $(SOURCES) $(HEADERS)"
	$(CLANG_TIDY) $(SOURCES) $(HEADERS) --header-filter="*.hpp"

CLANG_FORMAT ?= clang-format

fmt:
	echo "	CLANG_FORMAT -i $(SOURCES) $(HEADERS)"
	$(CLANG_FORMAT) -i $(SOURCES) $(ROUTED_SOURCES) $(HEADERS)

clean:
	rm -rf ./o ../shit ../shit-$(MODE) ../shit.exe ../shit-$(MODE).exe
	rm -rf ../*.ilk ../*.pdb ../*.lib # Windows
	rm -rf ../shit.com ../shit.com.dbg ../shit.aarch64.elf # Cosmopolitan
	rm -rf ../perf*
	rm -rf ../*.profraw ../*.profdata ../coverage # Coverage

.PHONY: dirs shit install uninstall tidy fmt clean

-include $(DEPS)
