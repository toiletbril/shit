.PHONY: all shit clean

include ../Common.mk

MODULES := Main Expressions Lexer Parser Tokens
OBJECTS := $(addsuffix .o, $(MODULES))
OUT := ../shit

shit: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(OUT)

clean:
	@rm -f $(OBJECTS)
	@rm -f $(OUT)
