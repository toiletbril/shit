BIN := ../shit
FAILED := failed.diff

%: expected/%.out
	@cat input/$@ | $(BIN) -i | diff -u $< - >> $(FAILED) && echo "\t[$@\t\t] ok." || echo "\t[$@\t\t] FAILED."

test: $(BIN)
	@rm -f $(FAILED)
	@$(MAKE) -B batch1

clean:
	rm -f $(FAILED)

batch1: regress long parens zero_division
