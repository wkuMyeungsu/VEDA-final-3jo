module warning_latch #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer HOLD_TIME_MS = 2000
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire [1:0] risk_level,
    input  wire       risk_valid,

    output reg  [1:0] effective_risk,
    output reg        latch_active
);

    /*
     * 27MHz, 2000ms 기준:
     * 27,000 × 2000 = 54,000,000 clocks
     */
    localparam integer HOLD_COUNT =
        (CLK_FREQ_HZ / 1000) * HOLD_TIME_MS;

    reg [31:0] hold_counter;
    reg [1:0]  pending_risk;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            effective_risk <= 2'd0;
            pending_risk   <= 2'd0;
            hold_counter   <= 32'd0;
            latch_active   <= 1'b0;
        end
        else begin

            /*
             * 현재 유지 중인 위험도 이상이 들어오면
             * 즉시 반영하고 하락 유지 동작을 취소한다.
             */
            if (risk_valid &&
                (risk_level >= effective_risk)) begin

                effective_risk <= risk_level;
                pending_risk   <= risk_level;
                hold_counter   <= 32'd0;
                latch_active   <= 1'b0;
            end

            /*
             * 이미 하락 유지 동작이 진행 중인 경우
             */
            else if (latch_active) begin

                /*
                 * 유지 시간 중 더 낮거나 다른 낮은 값이
                 * 들어오면 최종 적용값만 갱신한다.
                 *
                 * 타이머는 다시 처음부터 시작하지 않는다.
                 */
                if (risk_valid &&
                    (risk_level < effective_risk)) begin
                    pending_risk <= risk_level;
                end

                if (hold_counter >= HOLD_COUNT - 1) begin
                    /*
                     * 타이머 만료와 새 패킷 수신이
                     * 같은 클럭에 발생할 수 있으므로
                     * 최신 risk_level을 우선 적용한다.
                     */
                    if (risk_valid &&
                        (risk_level < effective_risk))
                        effective_risk <= risk_level;
                    else
                        effective_risk <= pending_risk;

                    hold_counter <= 32'd0;
                    latch_active <= 1'b0;
                end
                else begin
                    hold_counter <= hold_counter + 1'b1;
                end
            end

            /*
             * 위험도가 처음 하락한 시점
             */
            else if (risk_valid &&
                     (risk_level < effective_risk)) begin

                pending_risk <= risk_level;
                hold_counter <= 32'd0;
                latch_active <= 1'b1;
            end
        end
    end

endmodule