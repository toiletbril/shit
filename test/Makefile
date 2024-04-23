.PHONY: test clean test_list

ifeq ($(OS), Windows_NT)
BIN := ../shit.exe
else
BIN := ../shit
endif

FAILED := failed.diff

%: expected/%.out
	@cat input/$@.shit | $(BIN) -Ae - | diff -wu $< - >> $(FAILED) && echo "\t[$@\t\t] ok." || echo "\t[$@\t\t] FAILED."

test: $(BIN)
	@rm -f $(FAILED)
	@$(MAKE) -B test_list

clean:
	rm -f $(FAILED)

test_list: \
expression error_parens_limit error_zero_division error_two_line \
error_multi_line if_single_line error_unterminated_if_with_note
