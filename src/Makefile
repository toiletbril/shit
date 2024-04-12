.PHONY: shit compile fmt clean

include ../Common.mk

MODULES := Main Errors Builtin Flags Expressions Utils Lexer Parser Tokens
HEADERS := Errors Builtin Flags Expressions Utils Lexer Parser Tokens Debug Common

SOURCE_FILES := $(addsuffix .cpp, $(MODULES))
HEADER_FILES := $(addsuffix .hpp, $(HEADERS))
OBJECTS := $(addprefix o/, $(addsuffix .o, $(MODULES)))

ifeq ($(OS), Windows_NT)
OUT := ../shit.exe
else
OUT := ../shit
endif

shit:
	mkdir -p ./o
	$(MAKE) compile

compile: $(OBJECTS)
	@echo "\tCXX $(OBJECTS) -o $(OUT)"
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(OUT)

CLANG_FORMAT ?= clang-format

fmt:
	@echo "CLANG_FORMAT -i $(SOURCE_FILES) $(HEADER_FILES)"
	$(CLANG_FORMAT) -i $(SOURCE_FILES) $(HEADER_FILES)

clean:
	rm -rf ./o $(OUT)
ifeq ($(OS), Windows_NT)
	rm -rf ../*.ilk ../*.pdb ../*.lib
endif
