module timestamp_counter #(
    parameter integer CLK_FREQ_HZ = 27_000_000
)(
    input  wire        clk,
    input  wire        rst_n,

    /*
     * 1ms마다 1클럭 동안 HIGH
     */
    output reg         tick_1ms,

    /*
     * FPGA reset 해제 후 경과 시간
     */
    output reg [31:0]  timestamp_ms
);

    /*
     * 27MHz 기준:
     *
     * 27,000,000 / 1000
     * = 27,000 clocks per 1ms
     */
    localparam integer CLOCKS_PER_MS_RAW =
        CLK_FREQ_HZ / 1000;

    localparam integer CLOCKS_PER_MS =
        (CLOCKS_PER_MS_RAW < 2)
        ? 2
        : CLOCKS_PER_MS_RAW;

    reg [31:0] ms_counter;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ms_counter   <= 32'd0;
            timestamp_ms <= 32'd0;
            tick_1ms     <= 1'b0;
        end
        else begin
            /*
             * 기본적으로 LOW인 1클럭 펄스
             */
            tick_1ms <= 1'b0;

            if (ms_counter >= CLOCKS_PER_MS - 1) begin
                ms_counter   <= 32'd0;
                timestamp_ms <= timestamp_ms + 1'b1;
                tick_1ms     <= 1'b1;
            end
            else begin
                ms_counter <= ms_counter + 1'b1;
            end
        end
    end

endmodule