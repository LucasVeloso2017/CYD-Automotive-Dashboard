# Teste do historico da sessao no PC:  cd preview && make -f test_history.mk
SRC = ../wemos-portal/src
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -I host_stubs -I $(SRC)

dist/test_history: test_history.cpp $(SRC)/history.c $(SRC)/history.h
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) -x c++ -o $@ test_history.cpp $(SRC)/history.c
	./$@

.PHONY: clean
clean:
	rm -f dist/test_history
