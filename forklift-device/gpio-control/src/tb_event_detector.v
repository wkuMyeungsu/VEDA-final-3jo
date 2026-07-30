`timescale 1ns / 1ps

module tb_event_detector;

    reg clk;
    reg rst_n;

    reg checksum_error;
    reg protocol_error;
    reg interbyte_timeout_error;
    reg framing_error;

    reg comm_error;
    reg estop_active;

    reg [1:0] risk_level;
    reg       risk_valid;

    reg self_test_active;
    reg clear_error_req;

    wire       event_valid;
    wire [7:0] event_code;
    wire [7:0] event_detail;

    event_detector dut (
        .clk                     (clk),
        .rst_n                   (rst_n),

        .checksum_error          (checksum_error),
        .protocol_error          (protocol_error),
        .interbyte_timeout_error (interbyte_timeout_error),
        .framing_error           (framing_error),

        .comm_error              (comm_error),
        .estop_active            (estop_active),

        .risk_level              (risk_level),
        .risk_valid              (risk_valid),

        .self_test_active        (self_test_active),
        .clear_error_req         (clear_error_req),

        .event_valid             (event_valid),
        .event_code              (event_code),
        .event_detail            (event_detail)
    );

    /*
     * 100MHz simulation clock
     */
    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    initial begin
        $dumpfile("event_detector.vcd");
        $dumpvars(0, tb_event_detector);

        rst_n = 1'b0;

        checksum_error          = 1'b0;
        protocol_error          = 1'b0;
        interbyte_timeout_error = 1'b0;
        framing_error           = 1'b0;

        comm_error  = 1'b1;
        estop_active = 1'b0;

        risk_level = 2'd0;
        risk_valid = 1'b0;

        self_test_active = 1'b0;
        clear_error_req  = 1'b0;

        #30;
        rst_n = 1'b1;

        /*
         * 통신 복구
         */
        #20;
        comm_error = 1'b0;

        /*
         * Protocol 오류
         */
        #20;
        protocol_error = 1'b1;

        #10;
        protocol_error = 1'b0;

        /*
         * Risk 0 → 2
         */
        #20;
        risk_level = 2'd2;
        risk_valid = 1'b1;

        #10;
        risk_valid = 1'b0;

        /*
         * E-stop 활성화
         */
        #20;
        estop_active = 1'b1;

        /*
         * E-stop 해제
         */
        #20;
        estop_active = 1'b0;

        /*
         * Watchdog timeout
         */
        #20;
        comm_error = 1'b1;

        #50;
        $finish;
    end

    /*
     * 이벤트가 발생하면 콘솔에 출력
     */
    always @(negedge clk) begin
        if (event_valid) begin
            $display(
                "time=%0t code=0x%02X detail=0x%02X",
                $time,
                event_code,
                event_detail
            );
        end
    end

endmodule