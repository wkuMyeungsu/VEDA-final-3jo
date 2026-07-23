`timescale 1ns / 1ps

module tb_event_logger;

    reg clk;
    reg rst_n;

    reg        event_valid;
    reg [7:0]  event_code;
    reg [7:0]  event_detail;
    reg [31:0] timestamp_ms;

    reg [1:0] effective_risk;
    reg [2:0] warning_state;

    reg estop_active;
    reg comm_error;
    reg latch_active;

    reg checksum_error_latched;
    reg protocol_error_latched;
    reg timeout_error_latched;

    reg self_test_active;

    reg [1:0] led_active_pattern;
    reg       buzzer_active;

    reg       clear_log_req;
    reg [3:0] read_index;

    wire [63:0] read_data;
    wire        read_valid;

    wire [4:0] log_count;
    wire [3:0] write_pointer;
    wire       log_full;

    event_logger dut (
        .clk                    (clk),
        .rst_n                  (rst_n),

        .event_valid            (event_valid),
        .event_code             (event_code),
        .event_detail           (event_detail),

        .timestamp_ms           (timestamp_ms),

        .effective_risk         (effective_risk),
        .warning_state          (warning_state),

        .estop_active           (estop_active),
        .comm_error             (comm_error),
        .latch_active           (latch_active),

        .checksum_error_latched (checksum_error_latched),
        .protocol_error_latched (protocol_error_latched),
        .timeout_error_latched  (timeout_error_latched),

        .self_test_active       (self_test_active),

        .led_active_pattern     (led_active_pattern),
        .buzzer_active          (buzzer_active),

        .clear_log_req          (clear_log_req),

        .read_index             (read_index),
        .read_data              (read_data),
        .read_valid             (read_valid),

        .log_count              (log_count),
        .write_pointer          (write_pointer),
        .log_full               (log_full)
    );

    initial begin
        clk = 1'b0;
        forever #5 clk = ~clk;
    end

    task push_event;
        input [7:0]  code;
        input [7:0]  detail;
        input [31:0] timestamp;
        begin
            @(negedge clk);

            event_code  = code;
            event_detail = detail;
            timestamp_ms = timestamp;
            event_valid = 1'b1;

            @(negedge clk);

            event_valid = 1'b0;
        end
    endtask

    task print_log;
        input [3:0] index;
        begin
            read_index = index;
            #1;

            if (read_valid) begin
                $display(
                    "index=%0d timestamp=%0d code=%02X detail=%02X",
                    index,
                    read_data[31:0],
                    read_data[39:32],
                    read_data[47:40]
                );
            end
            else begin
                $display(
                    "index=%0d invalid",
                    index
                );
            end
        end
    endtask

    integer i;

    initial begin
        $dumpfile("event_logger.vcd");
        $dumpvars(0, tb_event_logger);

        rst_n = 1'b0;

        event_valid  = 1'b0;
        event_code   = 8'd0;
        event_detail = 8'd0;
        timestamp_ms = 32'd0;

        effective_risk = 2'd0;
        warning_state  = 3'd0;

        estop_active = 1'b0;
        comm_error   = 1'b0;
        latch_active = 1'b0;

        checksum_error_latched = 1'b0;
        protocol_error_latched = 1'b0;
        timeout_error_latched  = 1'b0;

        self_test_active = 1'b0;

        led_active_pattern = 2'b00;
        buzzer_active      = 1'b0;

        clear_log_req = 1'b0;
        read_index    = 4'd0;

        #30;
        rst_n = 1'b1;

        /*
         * 세 이벤트 저장
         */
        push_event(8'h06, 8'h00, 32'd100);
        push_event(8'h09, 8'h02, 32'd200);
        push_event(8'h02, 8'h00, 32'd300);

        #10;

        $display(
            "log_count=%0d write_pointer=%0d",
            log_count,
            write_pointer
        );

        print_log(0);
        print_log(1);
        print_log(2);
        print_log(3);

        /*
         * 총 18개가 되도록 추가 저장
         * 가장 오래된 2개는 덮어써져야 한다.
         */
        for (i = 4; i <= 18; i = i + 1) begin
            push_event(
                i[7:0],
                8'h00,
                i * 100
            );
        end

        #10;

        $display(
            "after overflow: count=%0d pointer=%0d full=%0d",
            log_count,
            write_pointer,
            log_full
        );

        /*
         * index 0은 덮어쓰기 후 가장 오래된 이벤트
         */
        print_log(0);
        print_log(15);

        /*
         * 로그 초기화
         */
        @(negedge clk);
        clear_log_req = 1'b1;

        @(negedge clk);
        clear_log_req = 1'b0;

        #10;

        $display(
            "after clear: count=%0d pointer=%0d",
            log_count,
            write_pointer
        );

        print_log(0);

        #20;
        $finish;
    end

endmodule