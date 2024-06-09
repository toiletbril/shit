# make MODE=dbg/rel/cosmo
MODE ?= dbg

SOURCES := $(wildcard *.cpp)
HEADERS := $(wildcard *.hpp)
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

CXXFLAGS := \
	-Wall \
	-Wextra \
	-Wdouble-promotion \
	-Wno-format \
	-Werror \
	-std=c++17

ifeq ($(MODE), rel)
CXXFLAGS += \
	-DNDEBUG \
	-g \
	-O2
else ifeq ($(MODE), dbg)
CXXFLAGS += \
	-g3 \
	-Og
ifneq ($(OS), Windows_NT)
ifneq ($(MODE), cosmo)
CXXFLAGS += -fsanitize=address -fsanitize=undefined # Windows' Asan is broken
endif
endif
else ifeq ($(MODE), cosmo)
CXXFLAGS += -fexceptions
ifneq ($(CXX), cosmoc++)
$(error Your CXX is "$(CXX)" instead of "cosmoc++" for MODE=cosmo)
endif
else
$(error Invalid MODE=$(MODE). Available modes are "dbg", "rel", "cosmo")
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
	echo "	CXX $< 	-o $@"
	$(CXX) $(CXXFLAGS) -c $< -o $(CWD)$@

shit:
	mkdir -p o/$(MODE)/
	$(MAKE) compile
	echo "Compilation succeeded."

compile: $(OBJECTS)
	echo "	CXX $(OBJECTS)	-o $(OUT)"
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(OUT)

install: shit
ifneq ($(OS), Windows_NT)
	install -m 755 $(OUT) /usr/local/bin/shit
	echo "Installed in '/usr/local/bin/shit'."
else
	$(error Unable to figure out install directory for Windows. \
			Please put '$(realpath ..)' in your PATH manually)
endif

CLANG_FORMAT ?= clang-format

fmt:
	echo "	CLANG-FORMAT -i $(SOURCES) $(HEADERS)"
	$(CLANG_FORMAT) -i $(SOURCES) $(HEADERS)

clean:
	rm -rf ./o $(OUT)
	rm -rf ../*.ilk ../*.pdb ../*.lib # Windows
	rm -rf $(OUT).com.dbg $(OUT).aarch64.elf # Cosmopolitan

.PHONY: sanity shit compile install fmt clean
