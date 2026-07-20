module buzzer_ctrl #(
    parameter integer CLK_FREQ_HZ  = 27_000_000,
    parameter integer TONE_FREQ_HZ = 2_000
)(
    input  wire       clk,
    input  wire       rst_n,

    /*
     * warning_fsm에서 출력되는 상태
     *
     * 0: SAFE
     * 1: CAUTION
     * 2: DANGER
     * 3: CRITICAL
     * 4: COMM_ERROR
     * 5: ESTOP
     */
    input  wire [2:0] warning_state,

    /*
     * Passive buzzer 또는 MOSFET gate 출력
     */
    output wire       buzzer_out,

    /*
     * 패턴 ON/OFF 상태 확인용
     */
    output reg        buzzer_enable
);

    localparam [2:0]
        STATE_SAFE       = 3'd0,
        STATE_CAUTION    = 3'd1,
        STATE_DANGER     = 3'd2,
        STATE_CRITICAL   = 3'd3,
        STATE_COMM_ERROR = 3'd4,
        STATE_ESTOP      = 3'd5;

    /*
     * 패턴의 최소 시간 단위는 10ms이다.
     *
     * 27MHz 기준:
     * 27,000,000 / 100 = 270,000 clocks
     */
    localparam integer TICK_10MS_COUNT =
        CLK_FREQ_HZ / 100;

    /*
     * Passive buzzer 음원 생성
     *
     * 2kHz 출력은 250us마다 출력을 반전한다.
     *
     * 27MHz 기준:
     * 27,000,000 / (2 × 2,000)
     * = 6,750 clocks
     */
    localparam integer HALF_TONE_COUNT =
        CLK_FREQ_HZ / (2 * TONE_FREQ_HZ);

    reg [31:0] tick_10ms_counter;
    reg [31:0] pattern_counter;
    reg [31:0] pattern_limit;

    reg [31:0] tone_counter;
    reg        tone_phase;

    reg [2:0] previous_state;

    /*
     * 각 상태의 패턴 길이
     *
     * pattern_counter 1카운트 = 10ms
     */
    always @(*) begin
        case (warning_state)

            /*
             * SAFE는 패턴을 사용하지 않는다.
             */
            STATE_SAFE: begin
                pattern_limit = 32'd1;
            end

            /*
             * CAUTION:
             * 100ms ON + 900ms OFF
             * 전체 주기 1000ms
             */
            STATE_CAUTION: begin
                pattern_limit = 32'd100;
            end

            /*
             * DANGER:
             * 200ms ON + 200ms OFF
             * 전체 주기 400ms
             */
            STATE_DANGER: begin
                pattern_limit = 32'd40;
            end

            /*
             * CRITICAL:
             * 300ms ON + 100ms OFF
             * 전체 주기 400ms
             */
            STATE_CRITICAL: begin
                pattern_limit = 32'd40;
            end

            /*
             * COMM_ERROR:
             * 100ms ON
             * 100ms OFF
             * 100ms ON
             * 700ms OFF
             *
             * 전체 주기 1000ms
             */
            STATE_COMM_ERROR: begin
                pattern_limit = 32'd100;
            end

            /*
             * ESTOP은 연속음이므로
             * 패턴 카운터를 사용하지 않는다.
             */
            STATE_ESTOP: begin
                pattern_limit = 32'd1;
            end

            default: begin
                pattern_limit = 32'd1;
            end

        endcase
    end

    /*
     * 10ms 단위 패턴 카운터
     *
     * warning_state가 변경되면 패턴을 처음부터 시작한다.
     * 따라서 새로운 위험 상태 진입 시 즉시 부저가 울린다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_10ms_counter <= 32'd0;
            pattern_counter   <= 32'd0;
            previous_state    <= STATE_SAFE;
        end
        else begin
            if (warning_state != previous_state) begin
                tick_10ms_counter <= 32'd0;
                pattern_counter   <= 32'd0;
                previous_state    <= warning_state;
            end
            else if (tick_10ms_counter >=
                     TICK_10MS_COUNT - 1) begin

                tick_10ms_counter <= 32'd0;

                if (pattern_counter >= pattern_limit - 1)
                    pattern_counter <= 32'd0;
                else
                    pattern_counter <= pattern_counter + 1'b1;
            end
            else begin
                tick_10ms_counter <=
                    tick_10ms_counter + 1'b1;
            end
        end
    end

    /*
     * 상태별 부저 ON/OFF 패턴
     */
    always @(*) begin
        buzzer_enable = 1'b0;

        case (warning_state)

            /*
             * 정상 상태: 무음
             */
            STATE_SAFE: begin
                buzzer_enable = 1'b0;
            end

            /*
             * CAUTION
             *
             * pattern_counter 0~9
             * = 10카운트 × 10ms
             * = 100ms ON
             */
            STATE_CAUTION: begin
                if (pattern_counter < 32'd10)
                    buzzer_enable = 1'b1;
                else
                    buzzer_enable = 1'b0;
            end

            /*
             * DANGER
             *
             * 0~19  : 200ms ON
             * 20~39 : 200ms OFF
             */
            STATE_DANGER: begin
                if (pattern_counter < 32'd20)
                    buzzer_enable = 1'b1;
                else
                    buzzer_enable = 1'b0;
            end

            /*
             * CRITICAL
             *
             * 0~29  : 300ms ON
             * 30~39 : 100ms OFF
             */
            STATE_CRITICAL: begin
                if (pattern_counter < 32'd30)
                    buzzer_enable = 1'b1;
                else
                    buzzer_enable = 1'b0;
            end

            /*
             * COMM_ERROR
             *
             * 0~9   : 100ms ON
             * 10~19 : 100ms OFF
             * 20~29 : 100ms ON
             * 30~99 : 700ms OFF
             */
            STATE_COMM_ERROR: begin
                if ((pattern_counter < 32'd10) ||
                    ((pattern_counter >= 32'd20) &&
                     (pattern_counter < 32'd30)))
                    buzzer_enable = 1'b1;
                else
                    buzzer_enable = 1'b0;
            end

            /*
             * ESTOP
             * 비상정지가 해제될 때까지 연속음
             */
            STATE_ESTOP: begin
                buzzer_enable = 1'b1;
            end

            default: begin
                buzzer_enable = 1'b0;
            end

        endcase
    end

    /*
     * 2kHz 음원 생성
     *
     * 부저가 꺼지는 구간에는 tone counter를 초기화한다.
     * 다음 ON 구간이 항상 LOW에서 시작되도록 한다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tone_counter <= 32'd0;
            tone_phase   <= 1'b0;
        end
        else if (!buzzer_enable) begin
            tone_counter <= 32'd0;
            tone_phase   <= 1'b0;
        end
        else begin
            if (tone_counter >= HALF_TONE_COUNT - 1) begin
                tone_counter <= 32'd0;
                tone_phase   <= ~tone_phase;
            end
            else begin
                tone_counter <= tone_counter + 1'b1;
            end
        end
    end

    /*
     * buzzer_enable이 HIGH인 동안에만
     * 2kHz 사각파 출력
     */
    assign buzzer_out =
        buzzer_enable ? tone_phase : 1'b0;

endmodule