BIN := ../shit
FAILED := failed.diff

%: expected/%.out
	@cat input/$@.shit | $(BIN) -A - | diff -wu $< - >> $(FAILED) && echo "\t[$@\t\t] ok." || echo "\t[$@\t\t] FAILED."

test: $(BIN)
	@rm -f $(FAILED)
	@$(MAKE) -B batch1

clean:
	rm -f $(FAILED)

batch1: expression parens_limit error_zero_division error_two_line error_multi_line
