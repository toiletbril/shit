.PHONY: test clean test_list

ifeq ($(OS), Windows_NT)
BIN := ../shit.exe
else
BIN := ../shit
endif

FAILED := failed.diff

%: expected/%.out
	@ \
	cat input/$@.shit | \
	$(BIN) -Ae - | \
	diff -wu $< - >> $(FAILED) && \
	echo "	[$@		] ok." || \
	echo "	[$@		] FAILED."

test: $(BIN)
	@rm -f $(FAILED)
	@$(MAKE) -B test_list

clean:
	@rm -f $(FAILED)

TESTS := \
	hello

test_list: $(TESTS)
