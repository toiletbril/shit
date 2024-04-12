ifeq ($(OS),Windows_NT)
CXX := clang++
endif

CXXFLAGS := -g3 -O0 -std=c++17
ifneq ($(OS),Windows_NT)
CXXFLAGS += -fsanitize=address
endif

o/%.o : %.cpp
	@echo "\tCXX $< \t-o $(CWD)$@"
	$(CXX) $(CXXFLAGS) -c $< -o $@
