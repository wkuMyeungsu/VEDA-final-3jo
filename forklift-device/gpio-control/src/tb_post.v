`timescale 1ns/1ps

module tb_post;

    localparam integer CLK_FREQ_HZ = 27_000_000;
    localparam real    CLK_NS      = 1_000_000_000.0 / CLK_FREQ_HZ;

    reg clk = 0;
    reg rst_n = 0;
    reg uart_rx_i = 1;
    reg estop_n = 1;
    reg manual_reset_n = 1;

    wire uart_tx_o;
    wire [1:0] led;
    wire buzzer_out;

    always #(CLK_NS/2.0) clk = ~clk;

    top #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .BAUD_RATE  (115200),
        .POST_DELAY_MS(1)   // 테스트용으로 짧게: 100ms -> 1ms
    ) dut (
        .clk           (clk),
        .rst_n         (rst_n),
        .uart_rx_i     (uart_rx_i),
        .uart_tx_o     (uart_tx_o),
        .estop_n       (estop_n),
        .manual_reset_n(manual_reset_n),
        .led           (led),
        .buzzer_out    (buzzer_out)
    );

    reg [7:0] rxb;
    integer bi;
    localparam real BIT_NS = 1_000_000_000.0 / 115200;
    always begin
        @(negedge uart_tx_o);
        #(BIT_NS * 1.5);
        for (bi = 0; bi < 8; bi = bi + 1) begin
            rxb[bi] = uart_tx_o;
            #(BIT_NS);
        end
        $display("[%0t ns] FPGA->Pi byte = 0x%02x", $time, rxb);
    end

    initial begin
        #(CLK_NS*5);
        rst_n = 1;

        $display("comm_error(default)=1 상태에서 POST가 정상 진입하는지 확인");
        $display("dut.comm_error = %b (1이어야 정상: Pi 미연결 기본 상태)", dut.comm_error);

        #(1_100_000); // POST_DELAY_MS=1ms(27,000 clocks) 경과 대기
        $display("dut.self_test_active = %b (POST_DELAY_MS 경과 후 1이어야 함)", dut.self_test_active);
        $display("dut.self_test_active_mode = %0d (3=TEST_ALL 이어야 함)", dut.self_test_active_mode);
        $display("dut.led = %b, dut.buzzer_out = %b", dut.led, dut.buzzer_out);

        #(1_000_000); // 1ms 남짓 더 대기하며 이벤트 push 확인
        $display("TEST DONE");
        $finish;
    end

    initial begin
        #5_000_000;
        $display("TIMEOUT - forcing finish");
        $finish;
    end

endmodule
