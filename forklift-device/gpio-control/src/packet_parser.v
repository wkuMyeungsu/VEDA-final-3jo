module packet_parser (
    input  wire       clk,
    input  wire       rst_n,

    input  wire [7:0] rx_data,
    input  wire       rx_valid,

    output reg  [1:0] risk_level,
    output reg        risk_valid,
    output reg        checksum_error
);

    localparam [2:0]
        STATE_HEADER   = 3'd0,
        STATE_COMMAND  = 3'd1,
        STATE_DATA     = 3'd2,
        STATE_CHECKSUM = 3'd3;

    reg [2:0] state;

    reg [7:0] command_reg;
    reg [7:0] data_reg;

    wire [7:0] expected_checksum;

    /*
     * 현재 패킷 형식:
     * AA ^ command ^ data
     */
    assign expected_checksum =
        8'hAA ^ command_reg ^ data_reg;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state          <= STATE_HEADER;
            command_reg    <= 8'd0;
            data_reg       <= 8'd0;
            risk_level     <= 2'd0;
            risk_valid     <= 1'b0;
            checksum_error <= 1'b0;
        end
        else begin
            /*
             * 두 출력은 바이트 처리 시점에
             * 한 클럭 동안만 발생하는 펄스이다.
             */
            risk_valid     <= 1'b0;
            checksum_error <= 1'b0;

            if (rx_valid) begin
                case (state)

                    /*
                     * Header 0xAA가 올 때까지
                     * 모든 바이트를 무시한다.
                     */
                    STATE_HEADER: begin
                        if (rx_data == 8'hAA)
                            state <= STATE_COMMAND;
                    end

                    /*
                     * 현재는 command 0x01만 허용한다.
                     *
                     * command 위치에서 다시 0xAA가 들어오면
                     * 새로운 패킷의 header로 간주한다.
                     */
                    STATE_COMMAND: begin
                        if (rx_data == 8'hAA) begin
                            state <= STATE_COMMAND;
                        end
                        else if (rx_data == 8'h01) begin
                            command_reg <= rx_data;
                            state       <= STATE_DATA;
                        end
                        else begin
                            state <= STATE_HEADER;
                        end
                    end

                    /*
                     * risk_level은 0~3만 허용한다.
                     */
                    STATE_DATA: begin
                        if (rx_data == 8'hAA) begin
                            state <= STATE_COMMAND;
                        end
                        else if (rx_data <= 8'd3) begin
                            data_reg <= rx_data;
                            state    <= STATE_CHECKSUM;
                        end
                        else begin
                            state <= STATE_HEADER;
                        end
                    end

                    /*
                     * 수신 checksum과 계산 checksum 비교
                     */
                    STATE_CHECKSUM: begin
                        if (rx_data == expected_checksum) begin
                            risk_level <= data_reg[1:0];
                            risk_valid <= 1'b1;
                        end
                        else begin
                            checksum_error <= 1'b1;
                        end

                        /*
                         * checksum 처리 후 다음 패킷 대기
                         */
                        state <= STATE_HEADER;
                    end
                    

                    default: begin
                        state <= STATE_HEADER;
                    end
                    
                    STATE_CHECKSUM: begin
                        if (rx_data == expected_checksum) begin
                            risk_level <= data_reg[1:0];
                            risk_valid <= 1'b1;

                            state <= STATE_HEADER;
                    end
                    else begin
                        checksum_error <= 1'b1;

                        /*
                        * 잘못된 checksum 자리에 0xAA가 들어왔다면
                        * 다음 패킷의 header일 가능성이 있으므로
                        * command를 기다리는 상태로 이동한다.
                        *
                        * 정상 checksum이 0xAA인 risk 1 패킷은
                        * 위의 첫 번째 조건에서 이미 처리된다.
                        */
                        if (rx_data == 8'hAA)
                            state <= STATE_COMMAND;
                        else
                            state <= STATE_HEADER;
                    end
                end 

                endcase
            end
        end
    end

endmodule