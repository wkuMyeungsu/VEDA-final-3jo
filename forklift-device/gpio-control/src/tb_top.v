`timescale 1ns/1ps

module tb_top;

    localparam integer CLK_FREQ_HZ = 27_000_000;
    localparam integer BAUD_RATE   = 115200;
    localparam real    BIT_NS      = 1_000_000_000.0 / BAUD_RATE;
    localparam real    CLK_NS      = 1_000_000_000.0 / CLK_FREQ_HZ;

    reg clk = 0;
    reg rst_n = 0;
    reg uart_rx_i = 1;
    reg estop_n = 1;
    reg manual_reset_n = 1;

    wire uart_tx_o;
    wire [1:0] led;
    wire buzzer_out;
    wire [3:0] warning_state_bcd;

    always #(CLK_NS/2.0) clk = ~clk;

    top #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .BAUD_RATE  (BAUD_RATE)
    ) dut (
        .clk              (clk),
        .rst_n            (rst_n),
        .uart_rx_i        (uart_rx_i),
        .uart_tx_o        (uart_tx_o),
        .estop_n          (estop_n),
        .manual_reset_n   (manual_reset_n),
        .led              (led),
        .buzzer_out       (buzzer_out),
        .warning_state_bcd(warning_state_bcd)
    );

    // warning_state_bcd가 dut 내부 warning_state와 항상 같은 값을
    // zero-extend해서 보여주는지 매 변화마다 확인 (2026-08-14 추가)
    always @(warning_state_bcd) begin
        #1; // dut.warning_state와의 조합논리 갱신 레이스 회피
        if (warning_state_bcd !== {1'b0, dut.warning_state})
            $display("[%0t ns] FAIL: warning_state_bcd=%0d != warning_state=%0d",
                      $time, warning_state_bcd, dut.warning_state);
        else
            $display("[%0t ns] warning_state_bcd=%0d (warning_state=%0d, OK)",
                      $time, warning_state_bcd, dut.warning_state);
    end

    // ---- Pi -> FPGA: 바이트 하나를 UART로 송신 ----
    task send_byte(input [7:0] data);
        integer i;
        begin
            uart_rx_i = 0; #(BIT_NS); // start bit
            for (i = 0; i < 8; i = i + 1) begin
                uart_rx_i = data[i];
                #(BIT_NS);
            end
            uart_rx_i = 1; #(BIT_NS); // stop bit
        end
    endtask

    task send_packet(input [7:0] cmd, input [7:0] data);
        reg [7:0] chksum;
        begin
            chksum = 8'hAA ^ cmd ^ data;
            send_byte(8'hAA);
            send_byte(cmd);
            send_byte(data);
            send_byte(chksum);
        end
    endtask

    // ---- FPGA -> Pi: uart_tx_o를 감시해서 바이트 디코딩 후 출력 ----
    reg [7:0] rxb;
    integer bi;
    always begin
        @(negedge uart_tx_o); // start bit edge
        #(BIT_NS * 1.5);      // 첫 데이터 비트 중앙으로 이동
        for (bi = 0; bi < 8; bi = bi + 1) begin
            rxb[bi] = uart_tx_o;
            #(BIT_NS);
        end
        $display("[%0t ns] FPGA->Pi byte = 0x%02x", $time, rxb);
    end

    initial begin
        #(CLK_NS*5);
        rst_n = 1;

        // 초기 comm_error=1(정상) 상태에서 잠시 대기
        #(BIT_NS*20);

        $display("---- SET_RISK(2) 전송 ----");
        send_packet(8'h01, 8'h02); // CMD_SET_RISK, risk=2

        #(BIT_NS*200);

        $display("---- CLEAR_ERROR 전송 ----");
        send_packet(8'h02, 8'h00); // CMD_CLEAR_ERROR

        #(BIT_NS*200);

        $display("---- SELF_TEST(LED) 전송 ----");
        send_packet(8'h03, 8'h01); // CMD_SELF_TEST, mode=LED

        #(BIT_NS*400);

        $display("---- 잘못된 checksum 패킷 전송 (에러 유발) ----");
        send_byte(8'hAA);
        send_byte(8'h01); // SET_RISK
        send_byte(8'h01); // risk=1
        send_byte(8'hFF); // 틀린 checksum

        #(BIT_NS*400);

        $display("TEST DONE");
        $finish;
    end

    initial begin
        #20_000_000; // 안전장치: 20ms 내 안 끝나면 강제 종료
        $display("TIMEOUT - forcing finish");
        $finish;
    end

endmodule
