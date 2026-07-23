`timescale 1ns / 1ps

module tb_timestamp_counter;

    /*
     * 시뮬레이션용 100kHz
     *
     * 100,000 / 1000
     * = 100 clocks per 1ms
     */
    localparam integer CLK_FREQ_HZ = 100_000;

    reg clk;
    reg rst_n;

    wire        tick_1ms;
    wire [31:0] timestamp_ms;

    timestamp_counter #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ)
    ) dut (
        .clk         (clk),
        .rst_n       (rst_n),
        .tick_1ms    (tick_1ms),
        .timestamp_ms(timestamp_ms)
    );

    /*
     * 100kHz clock
     *
     * 주기 = 10us
     * 반주기 = 5us
     */
    initial begin
        clk = 1'b0;

        forever begin
            #5000 clk = ~clk;
        end
    end

    initial begin
        rst_n = 1'b0;

        /*
         * 몇 클럭 동안 reset 유지
         */
        #30000;

        rst_n = 1'b1;

        /*
         * 약 6ms 동안 관찰
         */
        #6000000;

        $finish;
    end

    /*
     * 1ms tick 발생 시 결과 출력
     */
    always @(posedge clk) begin
        if (tick_1ms) begin
            $display(
                "time=%0t ns, timestamp_ms=%0d",
                $time,
                timestamp_ms
            );

        end
    end

endmodule