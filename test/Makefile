ifeq ($(OS), Windows_NT)
BIN := ../shit.exe
else
BIN := ../shit
endif

%: expected/%.out
	cat input/$@.shit | \
	$(BIN) -Ae - | \
	diff -wu $< - >> failed.diff && \
	echo "	[$@		] ok." || \
	echo "	[$@		] FAILED."

test: $(BIN)
	@rm -f $(FAILED)
	@$(MAKE) -B run_tests

clean:
	@rm -f $(FAILED)

INPUT_FILES := $(wildcard input/*.shit)

run_tests: $(INPUT_FILES:input/%.shit=%)

.PHONY: test clean test_list
