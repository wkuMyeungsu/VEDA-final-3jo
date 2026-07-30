module event_detector (
    input  wire       clk,
    input  wire       rst_n,

    input  wire       checksum_error,
    input  wire       protocol_error,
    input  wire       interbyte_timeout_error,
    input  wire       framing_error,

    input  wire       comm_error,
    input  wire       estop_active,

    input  wire [1:0] risk_level,

    input  wire       self_test_active,
    input  wire [1:0] self_test_mode,
    input  wire       self_test_rejected,

    input  wire       clear_error_req,

    output reg        event_valid,
    output reg  [7:0] event_code,
    output reg  [7:0] event_detail
);

    localparam [7:0]
        EVENT_NONE               = 8'h00,
        EVENT_CHECKSUM_ERROR     = 8'h01,
        EVENT_PROTOCOL_ERROR     = 8'h02,
        EVENT_INTERBYTE_TIMEOUT  = 8'h03,
        EVENT_FRAMING_ERROR      = 8'h04,
        EVENT_WATCHDOG_TIMEOUT   = 8'h05,
        EVENT_COMM_RECOVERED     = 8'h06,
        EVENT_ESTOP_ACTIVE       = 8'h07,
        EVENT_ESTOP_CLEARED      = 8'h08,
        EVENT_RISK_CHANGED       = 8'h09,
        EVENT_SELF_TEST_START    = 8'h0A,
        EVENT_SELF_TEST_DONE     = 8'h0B,
        EVENT_ERROR_CLEARED      = 8'h0C,
        EVENT_SELF_TEST_REJECTED = 8'h0D;

    reg       previous_comm_error;
    reg       previous_estop_active;
    reg       previous_self_test_active;
    reg [1:0] previous_risk_level;

    wire comm_error_changed;
    wire estop_changed;
    wire self_test_changed;
    wire risk_changed;

    assign comm_error_changed =
        (comm_error != previous_comm_error);

    assign estop_changed =
        (estop_active != previous_estop_active);

    assign self_test_changed =
        (self_test_active != previous_self_test_active);

    /* risk_level은 정상 SET_RISK 처리에서만 변경된다. */
    assign risk_changed =
        (risk_level != previous_risk_level);

    /*
     * 한 클럭에 여러 상태가 바뀌더라도, 처리하지 못한 상태의
     * previous 값은 갱신하지 않는다. 따라서 다음 클럭에도 차이가
     * 유지되어 이벤트가 누락되지 않는다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            event_valid              <= 1'b0;
            event_code               <= EVENT_NONE;
            event_detail             <= 8'h00;

            /* watchdog은 reset 직후 comm_error=1 */
            previous_comm_error      <= 1'b1;
            previous_estop_active    <= 1'b0;
            previous_self_test_active <= 1'b0;
            previous_risk_level      <= 2'd0;
        end
        else begin
            event_valid  <= 1'b0;
            event_code   <= EVENT_NONE;
            event_detail <= 8'h00;

            if (checksum_error) begin
                event_valid <= 1'b1;
                event_code  <= EVENT_CHECKSUM_ERROR;
            end
            else if (protocol_error) begin
                event_valid <= 1'b1;
                event_code  <= EVENT_PROTOCOL_ERROR;
            end
            else if (interbyte_timeout_error) begin
                event_valid <= 1'b1;
                event_code  <= EVENT_INTERBYTE_TIMEOUT;
            end
            else if (framing_error) begin
                event_valid <= 1'b1;
                event_code  <= EVENT_FRAMING_ERROR;
            end
            else if (self_test_rejected) begin
                event_valid  <= 1'b1;
                event_code   <= EVENT_SELF_TEST_REJECTED;
                event_detail <= {6'd0, self_test_mode};
            end
            else if (comm_error_changed) begin
                event_valid         <= 1'b1;
                previous_comm_error <= comm_error;

                if (comm_error) begin
                    event_code   <= EVENT_WATCHDOG_TIMEOUT;
                    event_detail <= 8'h01;
                end
                else begin
                    event_code   <= EVENT_COMM_RECOVERED;
                    event_detail <= 8'h00;
                end
            end
            else if (estop_changed) begin
                event_valid           <= 1'b1;
                previous_estop_active <= estop_active;

                if (estop_active) begin
                    event_code   <= EVENT_ESTOP_ACTIVE;
                    event_detail <= 8'h01;
                end
                else begin
                    /* 래치가 manual reset으로 해제된 시점 */
                    event_code   <= EVENT_ESTOP_CLEARED;
                    event_detail <= 8'h00;
                end
            end
            else if (risk_changed) begin
                event_valid         <= 1'b1;
                event_code          <= EVENT_RISK_CHANGED;
                event_detail        <= {
                    4'b0000,
                    previous_risk_level,
                    risk_level
                };
                previous_risk_level <= risk_level;
            end
            else if (self_test_changed) begin
                event_valid               <= 1'b1;
                previous_self_test_active <= self_test_active;

                if (self_test_active) begin
                    event_code   <= EVENT_SELF_TEST_START;
                    event_detail <= {6'd0, self_test_mode};
                end
                else begin
                    event_code   <= EVENT_SELF_TEST_DONE;
                    event_detail <= 8'h00;
                end
            end
            else if (clear_error_req) begin
                event_valid <= 1'b1;
                event_code  <= EVENT_ERROR_CLEARED;
            end
        end
    end

endmodule
