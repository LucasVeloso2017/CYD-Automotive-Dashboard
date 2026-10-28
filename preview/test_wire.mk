# Teste do lado CYD do fio para o Wemos, no PC:  cd preview && make -f test_wire.mk
SRC = ../src
CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -Wextra -I host_stubs_wire -I $(SRC)/net -I $(SRC)/gateway

dist/test_wire: test_wire.cpp $(SRC)/net/link.cpp $(SRC)/net/link.h $(SRC)/net/link_proto.h $(SRC)/gateway/dtc_describe.c $(SRC)/gateway/vehicle_profile.c
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) -o $@ test_wire.cpp $(SRC)/net/link.cpp -x c++ $(SRC)/gateway/dtc_describe.c $(SRC)/gateway/vehicle_profile.c
	./$@
	@python3 -c "import json; n=[json.loads(l) for l in open('dist/wire_json.txt')]; print('JSON valido:', len(n), 'objetos')"

.PHONY: clean
clean:
	rm -f dist/test_wire dist/wire_json.txt
