CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
PYTHON ?= python3
BIN := dtp_ca_sim
SC_CFLAGS ?= $(shell pkg-config --cflags systemc 2>/dev/null)
SC_LIBS ?= $(shell pkg-config --libs systemc 2>/dev/null || echo -lsystemc)
ifneq ($(strip $(SYSTEMC)),)
SC_CFLAGS := -I$(SYSTEMC)/include
SC_LIBS := -L$(SYSTEMC)/lib -L$(SYSTEMC)/lib-linux64 -Wl,-rpath,$(SYSTEMC)/lib -Wl,-rpath,$(SYSTEMC)/lib-linux64 -lsystemc
endif

.PHONY: all bench test profile wave rtl sc test-models clean
all: $(BIN)
$(BIN): ca/dtp_ca_main.cpp ca/dtp_ca.h
	$(CXX) $(CXXFLAGS) -o $@ $<
bench: $(BIN)
	./$(BIN) 20000 2048 8 1
test: $(BIN)
	$(PYTHON) scripts/verify.py
wave: $(BIN)
	./$(BIN) 500 128 4 1 --vcd
profile:
	$(CXX) $(CXXFLAGS) -pg -o $(BIN)_pg ca/dtp_ca_main.cpp
	./$(BIN)_pg 60000 2048 8 12 --poll
	mv gmon.out gmon_poll.out
	./$(BIN)_pg 60000 2048 8 12 --dispatch
	mv gmon.out gmon_dispatch.out
	gprof -b $(BIN)_pg gmon_poll.out > gprof_poll.txt
	gprof -b $(BIN)_pg gmon_dispatch.out > gprof_dispatch.txt
rtl:
	iverilog -g2012 -o rtl/dtp_sim.vvp rtl/dtp_core.v rtl/dtp_tb.v
	vvp rtl/dtp_sim.vvp
sc: dtp_systemc
dtp_systemc: ca/dtp_systemc.cpp
	$(CXX) $(CXXFLAGS) $(SC_CFLAGS) -o $@ $< $(SC_LIBS)
test-models: dtp_systemc
	iverilog -g2012 -o rtl/dtp_sim.vvp rtl/dtp_core.v rtl/dtp_tb.v
	$(PYTHON) scripts/verify_models.py
clean:
	rm -f $(BIN) $(BIN)_pg dtp_systemc gmon*.out gprof_*.txt *.vcd rtl/*.vvp
