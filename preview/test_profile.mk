# Teste do perfil de veiculo no PC:  cd preview && make -f test_profile.mk
SRC = ../src/gateway
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -I host_stubs -I $(SRC)

dist/test_profile: test_profile.cpp $(SRC)/vehicle_profile.c $(SRC)/vehicle_profile.h
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) -x c++ -o $@ test_profile.cpp $(SRC)/vehicle_profile.c
	./$@

.PHONY: clean
clean:
	rm -f dist/test_profile
