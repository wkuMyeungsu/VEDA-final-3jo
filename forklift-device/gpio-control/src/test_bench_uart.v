`timescale 1ns / 1ps

module tb_uart_led_top;

    /*
     * 시뮬레이션 속도를 높이기 위한 설정
     *
     * Clock = 10 MHz
     * Baud  = 1 Mbps
     *
     * CLKS_PER_BIT = 10
     */
    localparam integer CLK_FREQ_HZ = 10_000_000;
    localparam integer BAUD_RATE   = 1_000_000;

    localparam integer CLK_PERIOD_NS = 100;
    localparam integer BIT_PERIOD_NS = 1000;

    reg clk;
    reg rst_n;
    reg uart_rx_i;

    wire led;

    integer error_count;

    top #(
        .CLK_FREQ_HZ   (CLK_FREQ_HZ),
        .BAUD_RATE     (BAUD_RATE)
    ) dut (
        .clk      (clk),
        .rst_n    (rst_n),
        .uart_rx_i(uart_rx_i),
        .led      (led)
    );

    /*
     * 10 MHz clock 생성
     * 주기 100 ns
     */
    initial begin
        clk = 1'b0;

        forever begin
            #(CLK_PERIOD_NS / 2);
            clk = ~clk;
        end
    end

    /*
     * 정상 UART 1바이트 전송
     *
     * UART frame:
     * Idle  : 1
     * Start : 0
     * Data  : 8bit, LSB first
     * Stop  : 1
     */
    task uart_send_byte;
        input [7:0] data;

        integer i;

        begin
            $display(
                "[%0t ns] UART TX normal byte: 0x%02X",
                $time,
                data
            );

            // Start bit
            uart_rx_i = 1'b0;
            #(BIT_PERIOD_NS);

            // Data bits, LSB first
            for (i = 0; i < 8; i = i + 1) begin
                uart_rx_i = data[i];
                #(BIT_PERIOD_NS);
            end

            // Stop bit
            uart_rx_i = 1'b1;
            #(BIT_PERIOD_NS);

            // 프레임 사이 idle 시간
            uart_rx_i = 1'b1;
            #(BIT_PERIOD_NS);
        end
    endtask

    /*
     * Stop bit가 잘못된 UART 프레임 전송
     */
    task uart_send_bad_stop;
        input [7:0] data;

        integer i;

        begin
            $display(
                "[%0t ns] UART TX bad-stop byte: 0x%02X",
                $time,
                data
            );

            // Start bit
            uart_rx_i = 1'b0;
            #(BIT_PERIOD_NS);

            // Data bits
            for (i = 0; i < 8; i = i + 1) begin
                uart_rx_i = data[i];
                #(BIT_PERIOD_NS);
            end

            /*
             * 잘못된 Stop bit
             * 정상은 1이어야 하지만 0을 전송
             */
            uart_rx_i = 1'b0;
            #(BIT_PERIOD_NS);

            // 다시 idle 상태로 복귀
            uart_rx_i = 1'b1;
            #(BIT_PERIOD_NS * 2);
        end
    endtask

    /*
     * LED 결과 자동 검사
     */
    task check_led;
        input expected_led;
        input [255:0] test_name;

        begin
            // 내부 회로가 반영될 시간을 조금 기다린다.
            #(BIT_PERIOD_NS);

            if (led === expected_led) begin
                $display(
                    "[PASS] %0s | LED=%b",
                    test_name,
                    led
                );
            end
            else begin
                $display(
                    "[FAIL] %0s | expected=%b, actual=%b",
                    test_name,
                    expected_led,
                    led
                );

                error_count = error_count + 1;
            end
        end
    endtask

    /*
     * UART 수신 결과 모니터링
     *
     * dut 내부 신호를 계층적으로 확인한다.
     */
    always @(posedge clk) begin
        if (dut.rx_valid) begin
            $display(
                "[%0t ns] FPGA RX valid: data=0x%02X",
                $time,
                dut.rx_data
            );
        end

        if (dut.framing_error) begin
            $display(
                "[%0t ns] FPGA framing error detected",
                $time
            );
        end
    end

    initial begin
        /*
         * GTKWave나 VCD 지원 시 파형 파일 생성
         */
        $dumpfile("uart_led_test.vcd");
        $dumpvars(0, tb_uart_led_top);

        error_count = 0;

        // UART idle 상태는 HIGH
        uart_rx_i = 1'b1;

        // Reset
        rst_n = 1'b0;

        #(CLK_PERIOD_NS * 10);

        rst_n = 1'b1;

        #(BIT_PERIOD_NS * 2);

        $display("");
        $display("========================================");
        $display(" UART LED TEST START");
        $display("========================================");

        /*
         * Test 1
         * 0x01 수신 → LED ON
         */
        uart_send_byte(8'h01);
        check_led(1'b1, "Receive 0x01 -> LED ON");

        /*
         * Test 2
         * 0x00 수신 → LED OFF
         */
        uart_send_byte(8'h00);
        check_led(1'b0, "Receive 0x00 -> LED OFF");

        /*
         * Test 3
         * 0x02 수신 → LED OFF
         */
        uart_send_byte(8'h02);
        check_led(1'b0, "Receive 0x02 -> LED OFF");

        /*
         * Test 4
         * LED를 다시 ON
         */
        uart_send_byte(8'h01);
        check_led(1'b1, "Receive 0x01 -> LED ON again");

        /*
         * Test 5
         * Stop bit 오류가 있는 0x00
         *
         * 잘못된 프레임이므로 rx_valid가 발생하지 않고
         * LED는 기존 ON 상태를 유지해야 한다.
         */
        uart_send_bad_stop(8'h00);
        check_led(
            1'b1,
            "Bad stop bit discarded -> LED remains ON"
        );

        /*
         * Test 6
         * 정상 0x00으로 LED OFF
         */
        uart_send_byte(8'h00);
        check_led(1'b0, "Valid 0x00 -> LED OFF");

        $display("");
        $display("========================================");

        if (error_count == 0) begin
            $display(" ALL UART LED TESTS PASSED");
        end
        else begin
            $display(
                " UART LED TEST FAILED: %0d error(s)",
                error_count
            );
        end

        $display("========================================");
        $display("");

        #(BIT_PERIOD_NS * 2);

        $finish;
    end

endmodule