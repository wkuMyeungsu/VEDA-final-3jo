module uart_response_controller (
    input  wire        clk,
    input  wire        rst_n,

    /*
     * packet_parser 요청
     */
    input  wire        read_status_req,
    input  wire        read_log_info_req,
    input  wire        read_log_req,

    /*
     * 상태 조회 데이터
     */
    input  wire [1:0]  effective_risk,
    input  wire        comm_error,
    input  wire        estop_active,
    input  wire        latch_active,

    input  wire        checksum_error_latched,
    input  wire        protocol_error_latched,
    input  wire        timeout_error_latched,

    /*
     * 이벤트 로그 데이터
     */
    input  wire [4:0]  log_count,
    input  wire        log_full,

    input  wire [3:0]  log_read_index,
    input  wire [63:0] log_read_data,
    input  wire        log_read_valid,

    /*
     * uart_tx 연결
     */
    input  wire        tx_busy,
    input  wire        tx_done,

    output reg  [7:0]  tx_data,
    output reg         tx_start,

    output reg         response_busy
);

    localparam [7:0]
        RESPONSE_HEADER = 8'h55,
        RESP_STATUS     = 8'h82,
        RESP_LOG_INFO   = 8'h85,
        RESP_LOG_ENTRY  = 8'h86;

    localparam [1:0]
        RESPONSE_NONE     = 2'd0,
        RESPONSE_STATUS   = 2'd1,
        RESPONSE_LOG_INFO = 2'd2,
        RESPONSE_LOG      = 2'd3;

    localparam [1:0]
        STATE_IDLE      = 2'd0,
        STATE_SEND_BYTE = 2'd1,
        STATE_WAIT_DONE = 2'd2;

    reg [1:0] state;
    reg [1:0] response_type;

    reg [3:0] byte_index;
    reg [3:0] packet_length;

    reg [7:0] status_snapshot;
    reg [7:0] log_info_snapshot;
    reg [7:0] log_index_snapshot;

    reg [63:0] log_data_snapshot;

    reg [7:0] checksum_snapshot;

    wire [7:0] current_status;
    wire [7:0] current_log_info;
    wire [7:0] current_log_index_status;

    wire [7:0] status_checksum;
    wire [7:0] log_info_checksum;
    wire [7:0] log_entry_checksum;

    /*
     * 기존 STATUS 비트 구조 유지
     */
    assign current_status = {
        timeout_error_latched,
        protocol_error_latched,
        checksum_error_latched,
        latch_active,
        estop_active,
        comm_error,
        effective_risk
    };

    /*
     * [5]   log_full
     * [4:0] log_count
     */
    assign current_log_info = {
        2'b00,
        log_full,
        log_count
    };

    /*
     * [7]   read_valid
     * [3:0] requested index
     */
    assign current_log_index_status = {
        log_read_valid,
        3'b000,
        log_read_index
    };

    assign status_checksum =
        RESPONSE_HEADER ^
        RESP_STATUS ^
        current_status;

    assign log_info_checksum =
        RESPONSE_HEADER ^
        RESP_LOG_INFO ^
        current_log_info;

    /*
     * 12바이트 로그 응답에서 checksum을 제외한
     * 앞의 11바이트를 모두 XOR한다.
     */
    assign log_entry_checksum =
        RESPONSE_HEADER ^
        RESP_LOG_ENTRY ^
        current_log_index_status ^
        log_read_data[7:0] ^
        log_read_data[15:8] ^
        log_read_data[23:16] ^
        log_read_data[31:24] ^
        log_read_data[39:32] ^
        log_read_data[47:40] ^
        log_read_data[55:48] ^
        log_read_data[63:56];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state               <= STATE_IDLE;
            response_type       <= RESPONSE_NONE;

            byte_index          <= 4'd0;
            packet_length       <= 4'd0;

            status_snapshot     <= 8'd0;
            log_info_snapshot   <= 8'd0;
            log_index_snapshot  <= 8'd0;
            log_data_snapshot   <= 64'd0;
            checksum_snapshot   <= 8'd0;

            tx_data             <= 8'd0;
            tx_start            <= 1'b0;
            response_busy       <= 1'b0;
        end
        else begin
            /*
             * tx_start는 한 클럭 펄스
             */
            tx_start <= 1'b0;

            case (state)

                STATE_IDLE: begin
                    response_busy <= 1'b0;
                    response_type <= RESPONSE_NONE;
                    byte_index    <= 4'd0;

                    /*
                     * 한 번에 한 명령만 파서에서 발생하므로
                     * 실제로 동시에 들어오지는 않는다.
                     */
                    if (read_status_req) begin
                        status_snapshot   <= current_status;
                        checksum_snapshot <= status_checksum;

                        response_type <= RESPONSE_STATUS;
                        packet_length <= 4'd4;
                        response_busy <= 1'b1;

                        state <= STATE_SEND_BYTE;
                    end
                    else if (read_log_info_req) begin
                        log_info_snapshot <= current_log_info;
                        checksum_snapshot <= log_info_checksum;

                        response_type <= RESPONSE_LOG_INFO;
                        packet_length <= 4'd4;
                        response_busy <= 1'b1;

                        state <= STATE_SEND_BYTE;
                    end
                    else if (read_log_req) begin
                        /*
                         * 요청 순간의 로그를 스냅샷으로 고정한다.
                         * 송신 중 새 이벤트가 기록돼도 응답은 변하지 않는다.
                         */
                        log_index_snapshot <=
                            current_log_index_status;

                        log_data_snapshot <=
                            log_read_valid
                            ? log_read_data
                            : 64'd0;

                        checksum_snapshot <=
                            log_read_valid
                            ? log_entry_checksum
                            : (
                                RESPONSE_HEADER ^
                                RESP_LOG_ENTRY ^
                                current_log_index_status
                              );

                        response_type <= RESPONSE_LOG;
                        packet_length <= 4'd12;
                        response_busy <= 1'b1;

                        state <= STATE_SEND_BYTE;
                    end
                end

                STATE_SEND_BYTE: begin
                    response_busy <= 1'b1;

                    if (!tx_busy) begin
                        case (response_type)

                            RESPONSE_STATUS: begin
                                case (byte_index)
                                    4'd0:
                                        tx_data <= RESPONSE_HEADER;

                                    4'd1:
                                        tx_data <= RESP_STATUS;

                                    4'd2:
                                        tx_data <= status_snapshot;

                                    4'd3:
                                        tx_data <= checksum_snapshot;

                                    default:
                                        tx_data <= 8'h00;
                                endcase
                            end

                            RESPONSE_LOG_INFO: begin
                                case (byte_index)
                                    4'd0:
                                        tx_data <= RESPONSE_HEADER;

                                    4'd1:
                                        tx_data <= RESP_LOG_INFO;

                                    4'd2:
                                        tx_data <= log_info_snapshot;

                                    4'd3:
                                        tx_data <= checksum_snapshot;

                                    default:
                                        tx_data <= 8'h00;
                                endcase
                            end

                            RESPONSE_LOG: begin
                                case (byte_index)
                                    4'd0:
                                        tx_data <= RESPONSE_HEADER;

                                    4'd1:
                                        tx_data <= RESP_LOG_ENTRY;

                                    4'd2:
                                        tx_data <= log_index_snapshot;

                                    /*
                                     * 64비트 로그를 LSB부터 송신
                                     */
                                    4'd3:
                                        tx_data <= log_data_snapshot[7:0];

                                    4'd4:
                                        tx_data <= log_data_snapshot[15:8];

                                    4'd5:
                                        tx_data <= log_data_snapshot[23:16];

                                    4'd6:
                                        tx_data <= log_data_snapshot[31:24];

                                    4'd7:
                                        tx_data <= log_data_snapshot[39:32];

                                    4'd8:
                                        tx_data <= log_data_snapshot[47:40];

                                    4'd9:
                                        tx_data <= log_data_snapshot[55:48];

                                    4'd10:
                                        tx_data <= log_data_snapshot[63:56];

                                    4'd11:
                                        tx_data <= checksum_snapshot;

                                    default:
                                        tx_data <= 8'h00;
                                endcase
                            end

                            default: begin
                                tx_data <= 8'h00;
                            end

                        endcase

                        tx_start <= 1'b1;
                        state    <= STATE_WAIT_DONE;
                    end
                end

                STATE_WAIT_DONE: begin
                    response_busy <= 1'b1;

                    if (tx_done) begin
                        if (byte_index == packet_length - 1'b1) begin
                            response_busy <= 1'b0;
                            response_type <= RESPONSE_NONE;
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
                    response_type <= RESPONSE_NONE;
                    response_busy <= 1'b0;
                end

            endcase
        end
    end

endmodule