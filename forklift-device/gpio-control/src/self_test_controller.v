module self_test_controller #(
    parameter integer CLK_FREQ_HZ  = 27_000_000,
    parameter integer AUTO_STOP_MS = 5000
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire [1:0] self_test_mode,
    input  wire       self_test_valid,

    input  wire       comm_error,
    input  wire       estop_active,

    output reg        self_test_active,
    output reg  [1:0] test_led_out,
    output reg        test_buzzer_enable,

    output reg        self_test_done,
    output reg        self_test_rejected
);

    localparam [1:0]
        TEST_STOP   = 2'd0,
        TEST_LED    = 2'd1,
        TEST_BUZZER = 2'd2,
        TEST_ALL    = 2'd3;

    localparam integer TICK_100MS_COUNT_RAW = CLK_FREQ_HZ / 10;
    localparam integer TICK_100MS_COUNT =
        (TICK_100MS_COUNT_RAW < 2) ? 2 : TICK_100MS_COUNT_RAW;

    localparam integer AUTO_STOP_COUNT_RAW =
        (CLK_FREQ_HZ / 1000) * AUTO_STOP_MS;
    localparam integer AUTO_STOP_COUNT =
        (AUTO_STOP_COUNT_RAW < 2) ? 2 : AUTO_STOP_COUNT_RAW;

    reg [31:0] auto_stop_counter;
    reg [31:0] tick_100ms_counter;

    reg [1:0] active_mode;

    reg [2:0] led_tick_counter;
    reg [1:0] led_phase;

    reg buzzer_phase;
    reg buzzer_tick_count;

    /*
     * 안전 우선순위:
     * ESTOP/COMM_ERROR > SELF_TEST 명령 > 자동 종료
     *
     * 통신 오류 상태에서는 self-test를 시작하지 않고 거부한다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            self_test_active   <= 1'b0;
            active_mode        <= TEST_STOP;
            auto_stop_counter  <= 32'd0;
            self_test_done     <= 1'b0;
            self_test_rejected <= 1'b0;
        end
        else begin
            self_test_done     <= 1'b0;
            self_test_rejected <= 1'b0;

            if (estop_active || comm_error) begin
                if (self_test_active)
                    self_test_done <= 1'b1;

                if (self_test_valid &&
                    (self_test_mode != TEST_STOP))
                    self_test_rejected <= 1'b1;

                self_test_active  <= 1'b0;
                active_mode       <= TEST_STOP;
                auto_stop_counter <= 32'd0;
            end
            else if (self_test_valid) begin
                if (self_test_mode == TEST_STOP) begin
                    if (self_test_active)
                        self_test_done <= 1'b1;

                    self_test_active  <= 1'b0;
                    active_mode       <= TEST_STOP;
                    auto_stop_counter <= 32'd0;
                end
                else begin
                    self_test_active  <= 1'b1;
                    active_mode       <= self_test_mode;
                    auto_stop_counter <= 32'd0;
                end
            end
            else if (self_test_active) begin
                if (auto_stop_counter >= AUTO_STOP_COUNT - 1) begin
                    self_test_active  <= 1'b0;
                    active_mode       <= TEST_STOP;
                    auto_stop_counter <= 32'd0;
                    self_test_done    <= 1'b1;
                end
                else begin
                    auto_stop_counter <= auto_stop_counter + 1'b1;
                end
            end
            else begin
                auto_stop_counter <= 32'd0;
            end
        end
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_100ms_counter <= 32'd0;
            led_tick_counter   <= 3'd0;
            led_phase          <= 2'd0;
            buzzer_phase       <= 1'b1;
            buzzer_tick_count  <= 1'b0;
        end
        else if (!self_test_active) begin
            tick_100ms_counter <= 32'd0;
            led_tick_counter   <= 3'd0;
            led_phase          <= 2'd0;
            buzzer_phase       <= 1'b1;
            buzzer_tick_count  <= 1'b0;
        end
        else if (tick_100ms_counter >= TICK_100MS_COUNT - 1) begin
            tick_100ms_counter <= 32'd0;

            if (led_tick_counter == 3'd4) begin
                led_tick_counter <= 3'd0;
                led_phase        <= led_phase + 1'b1;
            end
            else begin
                led_tick_counter <= led_tick_counter + 1'b1;
            end

            if (buzzer_tick_count) begin
                buzzer_tick_count <= 1'b0;
                buzzer_phase      <= ~buzzer_phase;
            end
            else begin
                buzzer_tick_count <= 1'b1;
            end
        end
        else begin
            tick_100ms_counter <= tick_100ms_counter + 1'b1;
        end
    end

    always @(*) begin
        test_led_out = 2'b00;

        if (self_test_active &&
            ((active_mode == TEST_LED) ||
             (active_mode == TEST_ALL))) begin
            case (led_phase)
                2'd0:    test_led_out = 2'b01;
                2'd1:    test_led_out = 2'b10;
                2'd2:    test_led_out = 2'b11;
                2'd3:    test_led_out = 2'b00;
                default: test_led_out = 2'b00;
            endcase
        end
    end

    always @(*) begin
        test_buzzer_enable = 1'b0;

        if (self_test_active &&
            ((active_mode == TEST_BUZZER) ||
             (active_mode == TEST_ALL))) begin
            test_buzzer_enable = buzzer_phase;
        end
    end

endmodule
