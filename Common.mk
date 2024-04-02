ifeq ($(OS),Windows_NT)
CXX := clang++
endif

CXXFLAGS := -g3 -O0 -std=c++17 -fsanitize=address

o/%.o : %.cpp
	@echo "\tCXX $< \t-o $(CWD)$@"
	$(CXX) $(CXXFLAGS) -c $< -o $@
