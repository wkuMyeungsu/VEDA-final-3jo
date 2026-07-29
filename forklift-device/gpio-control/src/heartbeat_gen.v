module heartbeat_gen #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer PERIOD_MS   = 200
)(
    input  wire       clk,
    input  wire       rst_n,

    /*
     * 매 주기마다 함께 실어 보낼 현재 상태
     */
    input  wire [2:0] warning_state,
    input  wire       self_test_active,
    input  wire       latch_active,
    input  wire       checksum_error_latched,
    input  wire       protocol_error_latched,
    input  wire       timeout_error_latched,

    /*
     * event_tx_fifo push 포트로 연결
     * event_code는 top.v에서 EVENT_HEARTBEAT 상수로 고정 부여한다.
     */
    output reg        heartbeat_valid,
    output reg  [7:0] heartbeat_detail
);

    /*
     * 27MHz, 200ms 기준:
     * 27,000 × 200 = 5,400,000 clocks
     *
     * Pi는 이 주기의 2~3배(예: 600ms) 동안 heartbeat가 끊기면
     * FPGA 자체 이상(hang/reset)으로 판단할 수 있다.
     * FPGA -> Pi 방향의 생존 감시는 watchdog.v(Pi -> FPGA)와
     * 대칭을 이루는 상호 감시 구조가 된다.
     */
    localparam integer PERIOD_COUNT_RAW =
        (CLK_FREQ_HZ / 1000) * PERIOD_MS;

    localparam integer PERIOD_COUNT =
        (PERIOD_COUNT_RAW < 2) ? 2 : PERIOD_COUNT_RAW;

    reg [31:0] period_counter;

    /*
     * 상태 바이트 구성 (8비트):
     * [7]   timeout_error_latched
     * [6]   protocol_error_latched
     * [5]   checksum_error_latched
     * [4]   latch_active      (위험도 하락 유지중)
     * [3]   self_test_active
     * [2:0] warning_state     (SAFE/CAUTION/DANGER/CRITICAL/COMM_ERROR/ESTOP)
     *
     * estop_active/comm_error는 warning_state 값 자체로
     * 이미 구분되므로 별도 비트를 쓰지 않는다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            period_counter   <= 32'd0;
            heartbeat_valid  <= 1'b0;
            heartbeat_detail <= 8'd0;
        end
        else begin
            heartbeat_valid <= 1'b0;

            if (period_counter >= PERIOD_COUNT - 1) begin
                period_counter <= 32'd0;

                heartbeat_valid  <= 1'b1;
                heartbeat_detail <= {
                    timeout_error_latched,
                    protocol_error_latched,
                    checksum_error_latched,
                    latch_active,
                    self_test_active,
                    warning_state
                };
            end
            else begin
                period_counter <= period_counter + 1'b1;
            end
        end
    end

endmodule
