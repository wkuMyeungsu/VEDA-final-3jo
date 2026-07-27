module packet_parser #(
    parameter integer CLK_FREQ_HZ         = 27_000_000,
    parameter integer INTERBYTE_TIMEOUT_MS = 10
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire [7:0] rx_data,
    input  wire       rx_valid,

    /*
     * 정상 패킷 공통 출력
     */
    output reg        packet_valid,

    /*
     * SET_RISK 출력
     */
    output reg  [1:0] risk_level,
    output reg        risk_valid,

    /*
     * CLEAR_ERROR 요청
     */
    output reg        clear_error_req,

    /*
    /*
     * SELF_TEST 요청
     */
    output reg  [1:0] self_test_mode,
    output reg        self_test_valid,

    /*
     * 1클럭 오류 펄스 (event_detector로 전달)
     */
    output reg        checksum_error,
    output reg        protocol_error,
    output reg        interbyte_timeout_error,

    /*
     * 오류 누적 플래그
     * CLEAR_ERROR 정상 패킷으로 초기화된다.
     * heartbeat_gen의 상태 바이트에 실려 주기적으로 Pi에 보고된다.
     */
    output reg        checksum_error_latched,
    output reg        protocol_error_latched,
    output reg        timeout_error_latched
);

    /*
     * Command 정의
     *
     * v2 프로토콜: FPGA는 "안전 최종 방어선" 역할에 필요한
     * 명령만 받는다. 상태/로그 조회는 FPGA가 주기적으로
     * push하므로(heartbeat_gen, event_uart_tx 참고) 더 이상
     * Pi가 요청할 필요가 없다.
     */
    localparam [7:0]
        CMD_SET_RISK     = 8'h01,
        CMD_CLEAR_ERROR  = 8'h02,
        CMD_SELF_TEST    = 8'h03;

    /*
     * Parser 상태
     */
    localparam [1:0]
        STATE_HEADER   = 2'd0,
        STATE_COMMAND  = 2'd1,
        STATE_DATA     = 2'd2,
        STATE_CHECKSUM = 2'd3;

    reg [1:0] state;

    reg [7:0] command_reg;
    reg [7:0] data_reg;

    /*
     * 바이트 간 timeout 카운터
     *
     * 27MHz, 10ms:
     * 27,000 × 10 = 270,000 clocks
     */
    localparam integer TIMEOUT_COUNT_RAW =
        (CLK_FREQ_HZ / 1000) * INTERBYTE_TIMEOUT_MS;

    localparam integer TIMEOUT_COUNT =
        (TIMEOUT_COUNT_RAW < 2) ? 2 : TIMEOUT_COUNT_RAW;

    reg [31:0] interbyte_counter;

    wire [7:0] expected_checksum;

    assign expected_checksum =
        8'hAA ^ command_reg ^ data_reg;

    /*
     * 현재 수신한 command가 지원되는 명령인지 확인
     */
    wire command_is_valid;

     assign command_is_valid =
        (rx_data == CMD_SET_RISK)    ||
        (rx_data == CMD_CLEAR_ERROR) ||
        (rx_data == CMD_SELF_TEST);

    /*
     * DATA 상태에서 command별 data 유효성 검사
     */
    reg data_is_valid;

    always @(*) begin
        case (command_reg)

            CMD_SET_RISK: begin
                data_is_valid = (rx_data <= 8'd3);
            end

            CMD_CLEAR_ERROR: begin
                data_is_valid = (rx_data == 8'h00);
            end

            CMD_SELF_TEST: begin
                data_is_valid = (rx_data <= 8'd3);
            end

            default: begin
                data_is_valid = 1'b0;
            end

        endcase
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state                     <= STATE_HEADER;
            command_reg               <= 8'd0;
            data_reg                  <= 8'd0;
            interbyte_counter         <= 32'd0;

            packet_valid              <= 1'b0;

            risk_level                <= 2'd0;
            risk_valid                <= 1'b0;

            clear_error_req           <= 1'b0;

            self_test_mode            <= 2'd0;
            self_test_valid           <= 1'b0;

            checksum_error            <= 1'b0;
            protocol_error            <= 1'b0;
            interbyte_timeout_error   <= 1'b0;

            checksum_error_latched    <= 1'b0;
            protocol_error_latched    <= 1'b0;
            timeout_error_latched     <= 1'b0;
        end
        else begin
            /*
             * 이벤트 출력은 모두 1클럭 펄스
             */
            packet_valid            <= 1'b0;
            risk_valid               <= 1'b0;
            clear_error_req          <= 1'b0;
            self_test_valid          <= 1'b0;

            checksum_error           <= 1'b0;
            protocol_error           <= 1'b0;
            interbyte_timeout_error  <= 1'b0;

            /*
             * 새 바이트 수신을 timeout보다 우선 처리한다.
             *
             * timeout 마지막 시점과 rx_valid가 동시에 발생하면
             * 해당 바이트를 정상적으로 처리한다.
             */
            if (rx_valid) begin
                interbyte_counter <= 32'd0;

                case (state)

                    /*
                     * 쓰레기 데이터는 모두 무시하고
                     * 0xAA가 올 때까지 기다린다.
                     */
                    STATE_HEADER: begin
                        if (rx_data == 8'hAA)
                            state <= STATE_COMMAND;
                    end

                    /*
                     * 지원 명령만 허용한다.
                     *
                     * command 위치에서 0xAA가 다시 들어오면
                     * 새로운 패킷 Header로 간주한다.
                     */
                    STATE_COMMAND: begin
                        if (rx_data == 8'hAA) begin
                            state <= STATE_COMMAND;
                        end
                        else if (command_is_valid) begin
                            command_reg <= rx_data;
                            state       <= STATE_DATA;
                        end
                        else begin
                            protocol_error         <= 1'b1;
                            protocol_error_latched <= 1'b1;
                            state                  <= STATE_HEADER;
                        end
                    end

                    /*
                     * Command별 DATA 범위를 검사한다.
                     *
                     * DATA 위치에 0xAA가 들어오면
                     * 현재 패킷을 폐기하고 새로운 Header로 인정한다.
                     */
                    STATE_DATA: begin
                        if (rx_data == 8'hAA) begin
                            protocol_error         <= 1'b1;
                            protocol_error_latched <= 1'b1;

                            state <= STATE_COMMAND;
                        end
                        else if (data_is_valid) begin
                            data_reg <= rx_data;
                            state    <= STATE_CHECKSUM;
                        end
                        else begin
                            protocol_error         <= 1'b1;
                            protocol_error_latched <= 1'b1;

                            state <= STATE_HEADER;
                        end
                    end

                    /*
                     * Checksum 검사 후 Command별 이벤트 발생
                     */
                    STATE_CHECKSUM: begin
                        if (rx_data == expected_checksum) begin
                            packet_valid <= 1'b1;

                            case (command_reg)

                                CMD_SET_RISK: begin
                                    risk_level <= data_reg[1:0];
                                    risk_valid <= 1'b1;
                                end

                                CMD_CLEAR_ERROR: begin
                                    clear_error_req <= 1'b1;

                                    /*
                                     * Parser 내부 누적 오류 초기화
                                     */
                                    checksum_error_latched <= 1'b0;
                                    protocol_error_latched <= 1'b0;
                                    timeout_error_latched  <= 1'b0;
                                end

                                CMD_SELF_TEST: begin
                                    self_test_mode  <= data_reg[1:0];
                                    self_test_valid <= 1'b1;
                                end

                                default: begin
                                    /*
                                     * Command 단계에서 이미 검사하므로
                                     * 실제로 진입할 가능성은 없다.
                                     */
                                end

                            endcase

                            state <= STATE_HEADER;
                        end
                        else begin
                            checksum_error         <= 1'b1;
                            checksum_error_latched <= 1'b1;

                            /*
                             * 잘못된 checksum 바이트가 0xAA라면
                             * 다음 패킷의 Header일 가능성이 있으므로
                             * 바로 Command 대기 상태로 이동한다.
                             *
                             * 정상 checksum이 0xAA인 경우에는
                             * 위의 checksum 일치 조건에서 먼저 처리된다.
                             */
                            if (rx_data == 8'hAA)
                                state <= STATE_COMMAND;
                            else
                                state <= STATE_HEADER;
                        end
                    end

                    default: begin
                        state <= STATE_HEADER;
                    end

                endcase
            end

            /*
             * 패킷 조립 중 바이트가 들어오지 않으면
             * inter-byte timeout을 검사한다.
             */
            else if (state != STATE_HEADER) begin
                if (interbyte_counter >= TIMEOUT_COUNT - 1) begin
                    interbyte_counter       <= 32'd0;
                    state                   <= STATE_HEADER;

                    interbyte_timeout_error <= 1'b1;
                    timeout_error_latched   <= 1'b1;
                end
                else begin
                    interbyte_counter <= interbyte_counter + 1'b1;
                end
            end

            /*
             * Header 대기 상태에서는 timeout을 사용하지 않는다.
             */
            else begin
                interbyte_counter <= 32'd0;
            end
        end
    end

endmodule
