`timescale 1ns/1ps

module tb_cutoff;

    localparam integer CLK_FREQ_HZ = 27_000_000;
    localparam real    CLK_NS      = 1_000_000_000.0 / CLK_FREQ_HZ;
    localparam real    BIT_NS      = 1_000_000_000.0 / 115200;

    reg clk = 0;
    reg rst_n = 0;
    reg uart_rx_i = 1;
    reg estop_n = 1;
    reg manual_reset_n = 1;

    wire uart_tx_o;
    wire [1:0] led;
    wire buzzer_out;
    wire fwd_cutoff_relay_en;

    always #(CLK_NS/2.0) clk = ~clk;

    top #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .BAUD_RATE  (115200)
    ) dut (
        .clk                 (clk),
        .rst_n               (rst_n),
        .uart_rx_i           (uart_rx_i),
        .uart_tx_o           (uart_tx_o),
        .estop_n             (estop_n),
        .manual_reset_n      (manual_reset_n),
        .led                 (led),
        .buzzer_out          (buzzer_out),
        .fwd_cutoff_relay_en (fwd_cutoff_relay_en)
    );

    task send_byte(input [7:0] data);
        integer i;
        begin
            uart_rx_i = 0; #(BIT_NS);
            for (i = 0; i < 8; i = i + 1) begin
                uart_rx_i = data[i];
                #(BIT_NS);
            end
            uart_rx_i = 1; #(BIT_NS);
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

    reg [7:0] rxb;
    integer bi;
    always begin
        @(negedge uart_tx_o);
        #(BIT_NS * 1.5);
        for (bi = 0; bi < 8; bi = bi + 1) begin
            rxb[bi] = uart_tx_o;
            #(BIT_NS);
        end
        $display("[%0t ns] FPGA->Pi byte = 0x%02x  (fwd_cutoff_relay_en=%b)",
                  $time, rxb, fwd_cutoff_relay_en);
    end

    initial begin
        #(CLK_NS*5);
        rst_n = 1;
        #(BIT_NS*20);

        $display("==== 1) SET_RISK(0) SAFE : cutoff는 그대로 0이어야 함 ====");
        send_packet(8'h01, 8'h00);
        #(BIT_NS*200);
        $display("fwd_cutoff_relay_en = %b (기대값 0)", fwd_cutoff_relay_en);

        $display("==== 2) SET_RISK(3) CRITICAL : cutoff가 1로 래치되어야 함 ====");
        send_packet(8'h01, 8'h03);
        #(BIT_NS*300);
        $display("fwd_cutoff_relay_en = %b (기대값 1)", fwd_cutoff_relay_en);

        $display("==== 3) SET_RISK(0) SAFE로 복귀해도 cutoff는 유지되어야 함 (manual reset 전까지 래치) ====");
        send_packet(8'h01, 8'h00);
        #(BIT_NS*300);
        $display("fwd_cutoff_relay_en = %b (기대값 1, 여전히 래치)", fwd_cutoff_relay_en);

        $display("==== 4) manual_reset 버튼 눌러서 해제 ====");
        manual_reset_n = 0;
        #(1_000_000); // 20ms 디바운스 + 여유
        manual_reset_n = 1;
        #(1_000_000);
        $display("fwd_cutoff_relay_en = %b (기대값 0, 해제됨)", fwd_cutoff_relay_en);

        $display("==== 5) SET_RISK(3)로 다시 CRITICAL 만들고, comm_error는 cutoff에 영향 없어야 함 ====");
        send_packet(8'h01, 8'h03);
        #(BIT_NS*300);
        $display("fwd_cutoff_relay_en = %b (기대값 1)", fwd_cutoff_relay_en);

        $display("==== 6) manual reset 없이 700ms 방치 -> watchdog timeout(comm_error) 진입, cutoff는 CRITICAL 래치 그대로 유지 확인 ====");
        #(700_000_000);
        $display("dut.comm_error = %b, fwd_cutoff_relay_en = %b (기대값: comm_error=1, cutoff=1 그대로)",
                  dut.comm_error, fwd_cutoff_relay_en);

        $display("TEST DONE");
        $finish;
    end

    initial begin
        #900_000_000;
        $display("TIMEOUT");
        $finish;
    end

endmodule
