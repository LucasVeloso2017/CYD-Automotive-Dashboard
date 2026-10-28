# Teste do protocolo serial CYD <-> Wemos no PC:  cd preview && make -f test_link.mk
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -I ../src/net

dist/test_link: test_link.cpp ../src/net/link_proto.h ../wemos-portal/src/link_proto.h
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) -o $@ test_link.cpp
	./$@

.PHONY: clean
clean:
	rm -f dist/test_link
