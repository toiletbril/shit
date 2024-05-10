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
	@rm -f $(FAILED)

test_list:
	@echo "Done."
