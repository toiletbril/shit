.PHONY: shit compile fmt clean

MODULES := \
	Main \
	Errors \
	Builtin \
	Cli \
	Toiletline \
	Expressions \
	Utils \
	Lexer \
	Parser \
	Tokens

HEADERS := \
	Errors \
	Builtin \
	Cli \
	Toiletline \
	Expressions \
	Utils \
	Lexer \
	Parser \
	Tokens \
	Debug \
	Common

SOURCE_FILES := $(addsuffix .cpp, $(MODULES))
HEADER_FILES := $(addsuffix .hpp, $(HEADERS))
OBJECTS := $(addprefix o/, $(addsuffix .o, $(MODULES)))

ifeq ($(OS), Windows_NT)
OUT := ../shit.exe
else
OUT := ../shit
endif

ifeq ($(OS),Windows_NT)
CXX := clang++
endif

CXXFLAGS := \
	-Wall \
	-Wextra \
	-Wdouble-promotion \
	-Wno-format \
	-Werror \
	-std=c++17

# RELEASE=1 make
ifeq ($(RELEASE), 1)
CXXFLAGS += \
	-DNDEBUG \
	-O2
else
ifneq ($(OS), Windows_NT)
	# Windows' Asan is broken
	CXXFLAGS += -fsanitize=address
endif
CXXFLAGS += \
	-g3 \
	-O0
endif

# Additional flags for Windows
ifeq ($(OS), Windows_NT)
	# Disable dumb errors
	CXXFLAGS += \
		-Wno-unused-command-line-argument \
		-Wno-deprecated-declarations
	# Libraries we depend on
	CXXFLAGS += \
		-luserenv \
		-ladvapi32
endif

o/%.o : %.cpp
	@echo "	CXX $< 	-o $(CWD)$@"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

shit:
	mkdir -p ./o
	@$(MAKE) compile

compile: $(OBJECTS)
	@echo "	CXX $(OBJECTS)	-o $(OUT)"
	@$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(OUT)

CLANG_FORMAT ?= clang-format

fmt:
	@echo "	CLANG_FORMAT -i $(SOURCE_FILES)"
	@echo "	CLANG_FORMAT -i $(HEADER_FILES)"
	@$(CLANG_FORMAT) -i $(SOURCE_FILES) $(HEADER_FILES)

clean:
	@rm -rf ./o $(OUT)
	@rm -rf $(OUT).com.dbg $(OUT).aarch64.elf # Cosmopolitan
	@rm -rf ../*.ilk ../*.pdb ../*.lib # Windows
