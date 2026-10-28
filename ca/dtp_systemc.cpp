// Single-channel reference; independent from the N-channel benchmark.
#include <systemc.h>
#include <cstdlib>
SC_MODULE(DtpChannel) {
    sc_in<bool> clk, rst_n, en;
    sc_in<sc_uint<16>> data;
    sc_in<sc_uint<2>> mode;
    sc_out<sc_uint<32>> dout;
    sc_signal<sc_uint<32>> accum;
    void eval() {
        const sc_uint<32> a = accum.read();
        dout.write(en.read() ? sc_uint<32>(a + data.read() + mode.read()) : a);
    }
    void tick() {
        if (!rst_n.read()) accum.write(0);
        else if (en.read()) accum.write(dout.read());
    }
    SC_CTOR(DtpChannel) {
        SC_METHOD(eval);
        sensitive << data << mode << en << accum;
        SC_METHOD(tick);
        sensitive << clk.pos() << rst_n.neg();
    }
};
int sc_main(int argc, char** argv) {
    const int cycles = argc > 1 ? std::atoi(argv[1]) : 20;
    if (cycles <= 0) return 2;
    sc_signal<bool> clk("clk"), rst_n("rst_n"), en("en");
    sc_signal<sc_uint<16>> data("data");
    sc_signal<sc_uint<2>> mode("mode");
    sc_signal<sc_uint<32>> dout("dout");
    DtpChannel ch("ch");
    ch.clk(clk); ch.rst_n(rst_n); ch.en(en);
    ch.data(data); ch.mode(mode); ch.dout(dout);
    sc_trace_file* tf = sc_create_vcd_trace_file("wave_systemc");
    sc_trace(tf, clk, "clk"); sc_trace(tf, rst_n, "rst_n");
    sc_trace(tf, en, "en"); sc_trace(tf, data, "data");
    sc_trace(tf, mode, "mode"); sc_trace(tf, dout, "dout");
    sc_trace(tf, ch.accum, "accum");
    rst_n = false; clk = false; en = false; sc_start(1, SC_NS);
    clk = true; sc_start(1, SC_NS);
    clk = false; sc_start(1, SC_NS);
    rst_n = true;
    for (int c = 1; c <= cycles; ++c) {
        en = true; data = c; mode = c & 3;
        sc_start(1, SC_NS);
        clk = true; sc_start(1, SC_NS);
        std::cout << "TRACE " << c << " " << ch.accum.read().to_uint() << std::endl;
        clk = false; sc_start(1, SC_NS);
    }
    en = false; sc_start(1, SC_NS);
    std::cout << "SystemC final accum=" << ch.accum.read() << std::endl;
    sc_close_vcd_trace_file(tf);
    return 0;
}
