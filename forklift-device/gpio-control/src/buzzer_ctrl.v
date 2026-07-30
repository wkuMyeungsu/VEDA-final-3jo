module buzzer_ctrl #(
    parameter integer CLK_FREQ_HZ  = 27_000_000,
    parameter integer TONE_FREQ_HZ = 2_000
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire       test_override,
    input  wire       test_enable,

    input  wire [2:0] warning_state,

    output wire       buzzer_out,

    /* 실제 출력이 활성화되는 ON/OFF 상태 */
    output wire       buzzer_enable
);

    localparam [2:0]
        STATE_SAFE       = 3'd0,
        STATE_CAUTION    = 3'd1,
        STATE_DANGER     = 3'd2,
        STATE_CRITICAL   = 3'd3,
        STATE_COMM_ERROR = 3'd4,
        STATE_ESTOP      = 3'd5;

    localparam integer TICK_10MS_COUNT_RAW = CLK_FREQ_HZ / 100;
    localparam integer TICK_10MS_COUNT =
        (TICK_10MS_COUNT_RAW < 2) ? 2 : TICK_10MS_COUNT_RAW;

    localparam integer HALF_TONE_COUNT_RAW =
        CLK_FREQ_HZ / (2 * TONE_FREQ_HZ);
    localparam integer HALF_TONE_COUNT =
        (HALF_TONE_COUNT_RAW < 2) ? 2 : HALF_TONE_COUNT_RAW;

    reg [31:0] tick_10ms_counter;
    reg [31:0] pattern_counter;
    reg [31:0] pattern_limit;

    reg [31:0] tone_counter;
    reg        tone_phase;

    reg [2:0] previous_state;
    reg       previous_test_override;

    reg normal_pattern_enable;

    always @(*) begin
        case (warning_state)
            STATE_SAFE:       pattern_limit = 32'd1;
            STATE_CAUTION:    pattern_limit = 32'd100;
            STATE_DANGER:     pattern_limit = 32'd40;
            STATE_CRITICAL:   pattern_limit = 32'd40;
            STATE_COMM_ERROR: pattern_limit = 32'd100;
            STATE_ESTOP:      pattern_limit = 32'd1;
            default:          pattern_limit = 32'd1;
        endcase
    end

    /*
     * 상태 또는 self-test override가 바뀌면 정상 경고 패턴을
     * 처음부터 시작한다. Self-test 종료 뒤 중간 패턴부터
     * 재개되는 현상을 방지한다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_10ms_counter     <= 32'd0;
            pattern_counter       <= 32'd0;
            previous_state        <= STATE_SAFE;
            previous_test_override <= 1'b0;
        end
        else if ((warning_state != previous_state) ||
                 (test_override != previous_test_override)) begin
            tick_10ms_counter      <= 32'd0;
            pattern_counter        <= 32'd0;
            previous_state         <= warning_state;
            previous_test_override <= test_override;
        end
        else if (tick_10ms_counter >= TICK_10MS_COUNT - 1) begin
            tick_10ms_counter <= 32'd0;

            if (pattern_counter >= pattern_limit - 1)
                pattern_counter <= 32'd0;
            else
                pattern_counter <= pattern_counter + 1'b1;
        end
        else begin
            tick_10ms_counter <= tick_10ms_counter + 1'b1;
        end
    end

    always @(*) begin
        normal_pattern_enable = 1'b0;

        case (warning_state)
            STATE_SAFE: begin
                normal_pattern_enable = 1'b0;
            end

            /* 100ms ON / 900ms OFF */
            STATE_CAUTION: begin
                normal_pattern_enable = (pattern_counter < 32'd10);
            end

            /* 200ms ON / 200ms OFF */
            STATE_DANGER: begin
                normal_pattern_enable = (pattern_counter < 32'd20);
            end

            /* 300ms ON / 100ms OFF */
            STATE_CRITICAL: begin
                normal_pattern_enable = (pattern_counter < 32'd30);
            end

            /* 100ms ON, 100ms OFF, 100ms ON, 700ms OFF */
            STATE_COMM_ERROR: begin
                normal_pattern_enable =
                    (pattern_counter < 32'd10) ||
                    ((pattern_counter >= 32'd20) &&
                     (pattern_counter < 32'd30));
            end

            /* 연속음 */
            STATE_ESTOP: begin
                normal_pattern_enable = 1'b1;
            end

            default: begin
                normal_pattern_enable = 1'b0;
            end
        endcase
    end

    assign buzzer_enable =
        test_override ? test_enable : normal_pattern_enable;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tone_counter <= 32'd0;
            tone_phase   <= 1'b0;
        end
        else if (!buzzer_enable) begin
            tone_counter <= 32'd0;
            tone_phase   <= 1'b0;
        end
        else if (tone_counter >= HALF_TONE_COUNT - 1) begin
            tone_counter <= 32'd0;
            tone_phase   <= ~tone_phase;
        end
        else begin
            tone_counter <= tone_counter + 1'b1;
        end
    end

    assign buzzer_out = buzzer_enable ? tone_phase : 1'b0;

endmodule
