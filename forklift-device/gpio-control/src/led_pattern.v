module led_pattern #(
    parameter integer CLK_FREQ_HZ = 27_000_000
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire [2:0] warning_state,

    output reg  [1:0] led_pattern_out
);

    localparam [2:0]
        STATE_SAFE       = 3'd0,
        STATE_CAUTION    = 3'd1,
        STATE_DANGER     = 3'd2,
        STATE_CRITICAL   = 3'd3,
        STATE_COMM_ERROR = 3'd4,
        STATE_ESTOP      = 3'd5;

    /*
     * 100ms tick
     *
     * 27MHz 기준:
     * 27,000,000 / 10 = 2,700,000 clocks
     */
    localparam integer TICK_100MS_COUNT =
        CLK_FREQ_HZ / 10;

    localparam integer TICK_50MS_COUNT =
        CLK_FREQ_HZ / 20;

    reg [31:0] tick_100ms_counter;
    reg [31:0] tick_50ms_counter;

    reg [2:0] slow_tick_counter;



    reg       slow_phase;
    reg       fast_phase;
    reg         estop_phase;
    
     /*
     * 100ms 및 500ms phase 생성
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_100ms_counter  <= 32'd0;
            slow_tick_counter <= 3'd0;
            slow_phase      <= 1'b0;
            fast_phase      <= 1'b0;
        end
        else begin
            if (tick_100ms_counter >= TICK_100MS_COUNT - 1) begin
                tick_100ms_counter <= 32'd0;
                fast_phase         <= ~fast_phase;

                if (slow_tick_counter == 3'd4) begin
                    slow_tick_counter <= 3'd0;
                    slow_phase        <= ~slow_phase;
                end
                else begin
                    slow_tick_counter <= slow_tick_counter + 1'b1;
                end
            end
            else begin
                tick_100ms_counter <= tick_100ms_counter + 1'b1;
            end
        end
    end

    /*
     * ESTOP용 50ms phase
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_50ms_counter <= 32'd0;
            estop_phase       <= 1'b0;
        end
        else begin
            if (tick_50ms_counter >= TICK_50MS_COUNT - 1) begin
                tick_50ms_counter <= 32'd0;
                estop_phase       <= ~estop_phase;
            end
            else begin
                tick_50ms_counter <= tick_50ms_counter + 1'b1;
            end
        end
    end

    always @(*) begin
        case (warning_state)

            STATE_SAFE: begin
                led_pattern_out = 2'b00;
            end

            /*
             * LED0 느린 점멸
             */
            STATE_CAUTION: begin
                led_pattern_out = {
                    1'b0,
                    slow_phase
                };
            end

            /*
             * 두 LED 교차 점멸
             */
            STATE_DANGER: begin
                led_pattern_out = {
                    fast_phase,
                    ~fast_phase
                };
            end

            /*
             * 두 LED 계속 ON
             */
            STATE_CRITICAL: begin
                led_pattern_out = 2'b11;
            end

            /*
             * 두 LED 동시 빠른 점멸
             */
            STATE_COMM_ERROR: begin
                led_pattern_out = {
                    fast_phase,
                    fast_phase
                };
            end
            
            
               STATE_ESTOP: begin
                   led_pattern_out = {
                    estop_phase,
                    estop_phase
                };
            end

            default: begin
                led_pattern_out = 2'b00;
            end

        endcase
    end

endmodule