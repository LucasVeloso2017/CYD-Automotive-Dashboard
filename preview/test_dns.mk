# Teste do DNS do portal cativo no PC:  cd preview && make -f test_dns.mk
SRC = ../wemos-portal/src
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -I $(SRC)

dist/test_dns: test_dns.cpp $(SRC)/captive_dns.c $(SRC)/captive_dns.h
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) -x c++ -o $@ test_dns.cpp $(SRC)/captive_dns.c
	./$@

.PHONY: clean
clean:
	rm -f dist/test_dns
