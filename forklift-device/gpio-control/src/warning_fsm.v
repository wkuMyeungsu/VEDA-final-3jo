module warning_fsm (
    input  wire [1:0] effective_risk,
    input  wire       comm_error,
    input  wire       estop_active,

    output reg  [2:0] warning_state
);

    localparam [2:0]
        STATE_SAFE       = 3'd0,
        STATE_CAUTION    = 3'd1,
        STATE_DANGER     = 3'd2,
        STATE_CRITICAL   = 3'd3,
        STATE_COMM_ERROR = 3'd4,
        STATE_ESTOP      = 3'd5;

    always @(*) begin
        /*
         * 우선순위:
         * ESTOP > COMM_ERROR > risk
         */
        if (estop_active) begin
            warning_state = STATE_ESTOP;
        end
        else if (comm_error) begin
            warning_state = STATE_COMM_ERROR;
        end
        else begin
            case (effective_risk)
                2'd0:
                    warning_state = STATE_SAFE;

                2'd1:
                    warning_state = STATE_CAUTION;

                2'd2:
                    warning_state = STATE_DANGER;

                2'd3:
                    warning_state = STATE_CRITICAL;

                default:
                    warning_state = STATE_SAFE;
            endcase
        end
    end

endmodule
