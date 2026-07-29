module self_test_controller #(
    parameter integer CLK_FREQ_HZ  = 27_000_000,
    parameter integer AUTO_STOP_MS = 5000
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire [1:0] self_test_mode,
    input  wire       self_test_valid,

    /*
     * 실제 출력에 적용되는 위험도.
     * SAFE 상태에서만 self-test를 허용한다.
     */
    input  wire [1:0] effective_risk,

    input  wire       comm_error,
    input  wire       estop_active,

    output reg        self_test_active,
    output reg  [1:0] active_mode,
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

    localparam integer TICK_100MS_COUNT_RAW =
        CLK_FREQ_HZ / 10;

    localparam integer TICK_100MS_COUNT =
        (TICK_100MS_COUNT_RAW < 2)
        ? 2
        : TICK_100MS_COUNT_RAW;

    localparam integer AUTO_STOP_COUNT_RAW =
        (CLK_FREQ_HZ / 1000) * AUTO_STOP_MS;

    localparam integer AUTO_STOP_COUNT =
        (AUTO_STOP_COUNT_RAW < 2)
        ? 2
        : AUTO_STOP_COUNT_RAW;

    wire risk_is_safe;
    wire self_test_allowed;

    assign risk_is_safe =
        (effective_risk == 2'd0);

    assign self_test_allowed =
        risk_is_safe &&
        !comm_error &&
        !estop_active;

    reg [31:0] auto_stop_counter;
    reg [31:0] tick_100ms_counter;

    reg [2:0] led_tick_counter;
    reg [1:0] led_phase;

    reg buzzer_phase;
    reg buzzer_tick_count;

    /*
     * 안전 우선순위:
     * 위험 상태 / 통신 오류 / E-stop > SELF_TEST
     *
     * Self-test는 SAFE이고 통신이 정상이며 E-stop이 해제된
     * 상태에서만 시작할 수 있다. 진행 중 안전 조건이 깨지면
     * 즉시 종료하고 실제 경고 출력으로 복귀한다.
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
            /*
             * 1클럭 이벤트 펄스
             */
            self_test_done     <= 1'b0;
            self_test_rejected <= 1'b0;

            /*
             * 진행 중 위험도가 상승하거나 통신 오류 또는
             * E-stop이 발생하면 self-test를 즉시 중단한다.
             */
            if (!self_test_allowed) begin
                if (self_test_active) begin
                    self_test_active  <= 1'b0;
                    active_mode       <= TEST_STOP;
                    auto_stop_counter <= 32'd0;
                    self_test_done    <= 1'b1;
                end
                else begin
                    active_mode       <= TEST_STOP;
                    auto_stop_counter <= 32'd0;
                end

                /*
                 * STOP 명령은 거부로 기록하지 않는다.
                 * 시작 명령만 거부한다.
                 */
                if (self_test_valid &&
                    (self_test_mode != TEST_STOP)) begin
                    self_test_rejected <= 1'b1;
                end
            end

            /*
             * 안전 조건을 만족할 때 SELF_TEST 명령 처리
             */
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

            /*
             * 지정 시간 후 자동 종료
             */
            else if (self_test_active) begin
                if (auto_stop_counter >=
                    AUTO_STOP_COUNT - 1) begin
                    self_test_active  <= 1'b0;
                    active_mode       <= TEST_STOP;
                    auto_stop_counter <= 32'd0;
                    self_test_done    <= 1'b1;
                end
                else begin
                    auto_stop_counter <=
                        auto_stop_counter + 1'b1;
                end
            end
            else begin
                active_mode       <= TEST_STOP;
                auto_stop_counter <= 32'd0;
            end
        end
    end

    /*
     * Self-test 공통 100ms 시간축
     */
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
        else if (tick_100ms_counter >=
                 TICK_100MS_COUNT - 1) begin
            tick_100ms_counter <= 32'd0;

            /*
             * LED: 500ms마다 다음 단계
             */
            if (led_tick_counter == 3'd4) begin
                led_tick_counter <= 3'd0;
                led_phase        <= led_phase + 1'b1;
            end
            else begin
                led_tick_counter <=
                    led_tick_counter + 1'b1;
            end

            /*
             * 부저: 200ms마다 ON/OFF 반전
             */
            if (buzzer_tick_count) begin
                buzzer_tick_count <= 1'b0;
                buzzer_phase      <= ~buzzer_phase;
            end
            else begin
                buzzer_tick_count <= 1'b1;
            end
        end
        else begin
            tick_100ms_counter <=
                tick_100ms_counter + 1'b1;
        end
    end

    /*
     * LED 테스트 패턴:
     * 01 -> 10 -> 11 -> 00
     */
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

    /*
     * 부저 테스트 패턴: 200ms ON / 200ms OFF
     */
    always @(*) begin
        test_buzzer_enable = 1'b0;

        if (self_test_active &&
            ((active_mode == TEST_BUZZER) ||
             (active_mode == TEST_ALL))) begin
            test_buzzer_enable = buzzer_phase;
        end
    end

endmodule
