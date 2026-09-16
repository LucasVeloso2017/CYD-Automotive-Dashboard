# Teste do parser de DTC no PC:  cd preview && make -f test_dtc.mk
SRC = ../src/gateway
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -DELM327_HOST_TEST -I host_stubs -I $(SRC)

dist/test_dtc: test_dtc.cpp $(SRC)/elm327_gateway.cpp $(SRC)/elm327_gateway.h $(SRC)/dtc_data.h
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) -o $@ test_dtc.cpp $(SRC)/elm327_gateway.cpp
	./$@

.PHONY: clean
clean:
	rm -f dist/test_dtc
