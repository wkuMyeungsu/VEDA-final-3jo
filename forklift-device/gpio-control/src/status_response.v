module status_response (
    input  wire       clk,
    input  wire       rst_n,

    input  wire       read_status_req,

    input  wire [1:0] effective_risk,
    input  wire       comm_error,
    input  wire       estop_active,
    input  wire       latch_active,

    input  wire       checksum_error_latched,
    input  wire       protocol_error_latched,
    input  wire       timeout_error_latched,

    /*
     * uart_tx 연결
     */
    input  wire       tx_busy,
    input  wire       tx_done,

    output reg  [7:0] tx_data,
    output reg        tx_start,

    output reg        response_busy
);

    localparam [7:0]
        RESPONSE_HEADER = 8'h55,
        RESP_STATUS     = 8'h82;

    localparam [1:0]
        STATE_IDLE      = 2'd0,
        STATE_SEND_BYTE = 2'd1,
        STATE_WAIT_DONE = 2'd2;

    reg [1:0] state;
    reg [1:0] byte_index;

    reg [7:0] status_snapshot;
    reg [7:0] checksum_snapshot;

    wire [7:0] current_status;

    assign current_status = {
        timeout_error_latched,
        protocol_error_latched,
        checksum_error_latched,
        latch_active,
        estop_active,
        comm_error,
        effective_risk
    };

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state             <= STATE_IDLE;
            byte_index        <= 2'd0;
            status_snapshot   <= 8'd0;
            checksum_snapshot <= 8'd0;

            tx_data           <= 8'd0;
            tx_start          <= 1'b0;
            response_busy     <= 1'b0;
        end
        else begin
            /*
             * 기본적으로 1클럭 펄스
             */
            tx_start <= 1'b0;

            case (state)

                STATE_IDLE: begin
                    response_busy <= 1'b0;
                    byte_index    <= 2'd0;

                    if (read_status_req) begin
                        /*
                         * 요청 순간의 상태를 저장한다.
                         * 송신 도중 상태가 변경돼도
                         * 하나의 응답 패킷 내부 값은 유지된다.
                         */
                        status_snapshot <= current_status;

                        checksum_snapshot <=
                            RESPONSE_HEADER ^
                            RESP_STATUS ^
                            current_status;

                        response_busy <= 1'b1;
                        state         <= STATE_SEND_BYTE;
                    end
                end

                STATE_SEND_BYTE: begin
                    response_busy <= 1'b1;

                    if (!tx_busy) begin
                        case (byte_index)
                            2'd0:
                                tx_data <= RESPONSE_HEADER;

                            2'd1:
                                tx_data <= RESP_STATUS;

                            2'd2:
                                tx_data <= status_snapshot;

                            2'd3:
                                tx_data <= checksum_snapshot;

                            default:
                                tx_data <= 8'h00;
                        endcase

                        tx_start <= 1'b1;
                        state    <= STATE_WAIT_DONE;
                    end
                end

                STATE_WAIT_DONE: begin
                    response_busy <= 1'b1;

                    if (tx_done) begin
                        if (byte_index == 2'd3) begin
                            response_busy <= 1'b0;
                            state         <= STATE_IDLE;
                        end
                        else begin
                            byte_index <= byte_index + 1'b1;
                            state      <= STATE_SEND_BYTE;
                        end
                    end
                end

                default: begin
                    state         <= STATE_IDLE;
                    response_busy <= 1'b0;
                end

            endcase
        end
    end

endmodule