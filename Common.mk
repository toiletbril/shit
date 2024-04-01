ifeq ($(OS),Windows_NT)
CXX := clang++
endif

CXXFLAGS := -I$(PWD)/include -O2 -std=c++17
OBJFLAGS := -c

%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(OBJFLAGS) $< -o $@
