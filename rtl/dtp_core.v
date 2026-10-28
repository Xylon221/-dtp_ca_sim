// ============================================================================
// dtp_core.v — DTP（数据搬移处理器）传输通道 RTL（可综合）
//
// 与 CA 模型中的 Channel 同构：使能时累加输入数据，输出累加结果。
//   out   = en ? (accum + data_in + mode) : accum   （组合）
//   accum <= out                                    （时钟沿时序）
// 该 RTL 用于与 CA 模型做"波形 / 功能对齐"：两者在相同激励下产生相同结果。
// ============================================================================
module dtp_core #(
    parameter DATA_W = 16
) (
    input  wire                clk,
    input  wire                rst_n,
    input  wire                en,          // 传输使能
    input  wire [DATA_W-1:0]   data_in,     // 输入数据
    input  wire [1:0]          mode,        // 传输模式
    output wire [31:0]         data_out,    // 累加输出
    output wire [7:0]          status       // 状态（低 8 位）
);
    reg [31:0] accum;

    // 组合：使能时累加
    wire [31:0] sum = en ? (accum + {{(32-DATA_W){1'b0}}, data_in} + {{30{1'b0}}, mode})
                          : accum;

    assign data_out = sum;
    assign status   = sum[7:0];

    // 时序：时钟沿锁存
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            accum <= 32'd0;
        else if (en)
            accum <= sum;
    end
endmodule
