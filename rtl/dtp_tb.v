// ============================================================================
// dtp_tb.v — DTP 通道 testbench（iverilog 仿真，输出每周期累加结果）
//
// 使用与 CA 模型相同的单调递增数据包激励（en=1, data=pkt, mode=pkt&3），
// $display 打印累加器序列，供与 CA 模型比对（功能对齐）。
// ============================================================================
`timescale 1ns / 1ps

module dtp_tb;
    reg         clk = 0;
    reg         rst_n = 0;
    reg         en = 0;
    reg [15:0]  data_in = 0;
    reg [1:0]   mode = 0;
    wire [31:0] data_out;
    wire [7:0]  status;

    dtp_core #(.DATA_W(16)) dut (
        .clk(clk), .rst_n(rst_n), .en(en),
        .data_in(data_in), .mode(mode),
        .data_out(data_out), .status(status)
    );

    always #5 clk = ~clk;   // 10ns 周期

    integer pkt;
    integer cyc;
    initial begin
        $dumpfile("wave_rtl.vcd");
        $dumpvars(0, dtp_tb);
        // 复位
        rst_n = 0; #20; rst_n = 1;
        // 与 CA 模型一致的单调激励
        pkt = 1;
        for (cyc = 0; cyc < 20; cyc = cyc + 1) begin
            @(negedge clk);
            en = 1;
            data_in = pkt[15:0];
            mode = pkt[1:0];
            pkt = pkt + 1;
            @(posedge clk);
            #1;
            $display("TRACE %0d %0d", cyc + 1, dut.accum);
        end
        @(negedge clk);
        en = 0;
        #30;
        $display("RTL 仿真结束，最终 accum = %0d (0x%08h)", dut.accum, dut.accum);
        $finish;
    end
endmodule
