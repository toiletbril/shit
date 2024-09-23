# make MODE=dbg/rel/prof/cosmo
MODE ?= dbg

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)

DIRS := $(wildcard */.)

SOURCES += $(wildcard */**.cpp)
HEADERS += $(wildcard */**.hpp)

OBJECTS := $(SOURCES:%.cpp=o/$(MODE)/%.o)

ifeq ($(OS), Windows_NT)
OUT := ../shit.exe
else
OUT := ../shit
endif

ifeq ($(MODE), cosmo)
CXX := cosmoc++
else ifeq ($(OS), Windows_NT)
CXX := clang++
endif

ifeq ($(MODE), cosmo)
OS_INFO := "unknown-cosmo"
else
OS_INFO := $(shell uname -rvom || echo "<no uname>")
endif

COMMIT_HASH := $(shell git rev-parse HEAD || echo "<no git>")

DEFINES := \
	-DSHIT_OS_INFO="\"$(OS_INFO)\"" \
	-DSHIT_COMMIT_HASH="\"$(COMMIT_HASH)\"" \
	-DSHIT_BUILD_MODE="\"$(MODE)\""

# Use $CPPFLAGS or $CFLAGS to append new flags on command line.
CXXFLAGS := \
	-Wall \
	-Wextra \
	-Wdouble-promotion \
	-Wno-format \
	-Werror \
	-Wno-unused-command-line-argument \
	-Wno-deprecated \
	-std=c++17 \
	$(DEFINES) \
	$(CPPFLAGS) \
	$(CFLAGS)

ifeq ($(MODE), rel)
CXXFLAGS += \
	-s \
	-DNDEBUG \
	-O3
else ifeq ($(MODE), dbg)
CXXFLAGS += \
	-g3 \
	-O0
ifneq ($(OS), Windows_NT)
ifneq ($(MODE), cosmo)
CXXFLAGS += -fsanitize=address -fsanitize=undefined
endif
endif
else ifeq ($(MODE), cosmo)
CXXFLAGS += -fexceptions -g -O3
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo)
endif
else ifeq ($(MODE), prof)
CXXFLAGS += \
	-g3 \
	-O3
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "prof", "cosmo")
endif

ifeq ($(OS), Windows_NT)
CXXFLAGS += \
	-Wno-unused-command-line-argument \
	-Wno-deprecated-declarations
CXXFLAGS += \
	-luserenv \
	-ladvapi32
endif

o/$(MODE)/%.o: %.cpp
	printf "\t%-24s 🡲  $@\n" $<
	$(CXX) $(CXXFLAGS) -c $< -o $(CWD)$@

$(OUT): $(OBJECTS)
	printf "\t$(OBJECTS) 🡲  $(OUT)\n"
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

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

.PHONY: dirs shit install tidy fmt clean
