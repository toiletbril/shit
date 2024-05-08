.PHONY: shit compile fmt clean

MODULES := Main Errors Builtin Cli Toiletline Expressions Utils Lexer Parser Tokens
HEADERS := Errors Builtin Cli Toiletline Expressions Utils Lexer Parser Tokens Debug Common

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

# RELEASE=1 make
ifeq ($(RELEASE), 1)
CXXFLAGS := -g -O2 -std=c++17
else
ifneq ($(OS), Windows_NT)
	CXXFLAGS += -fsanitize=address
endif
CXXFLAGS := -g3 -O0 -std=c++17
endif

# Additional libraries
ifeq ($(OS), Windows_NT)
	CXXFLAGS += -luserenv -ladvapi32
endif

o/%.o : %.cpp
	@echo "\tCXX $< \t-o $(CWD)$@"
	$(CXX) $(CXXFLAGS) -c $< -o $@

shit:
	mkdir -p ./o
	$(MAKE) compile

compile: $(OBJECTS)
	@echo "\tCXX $(OBJECTS) -o $(OUT)"
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(OUT)

CLANG_FORMAT ?= clang-format

fmt:
	@echo "\tCLANG_FORMAT -i $(SOURCE_FILES)"
	@echo "\tCLANG_FORMAT -i $(HEADER_FILES)"
	$(CLANG_FORMAT) -i $(SOURCE_FILES) $(HEADER_FILES)

clean:
	rm -rf ./o $(OUT)
ifeq ($(OS), Windows_NT)
	rm -rf ../*.ilk ../*.pdb ../*.lib
endif
