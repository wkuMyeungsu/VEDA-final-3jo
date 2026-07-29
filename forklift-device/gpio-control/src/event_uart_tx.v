module event_uart_tx (
    input  wire       clk,
    input  wire       rst_n,

    /*
     * event_tx_fifo 인터페이스
     */
    input  wire        fifo_empty,
    input  wire [7:0]  fifo_code,
    input  wire [7:0]  fifo_detail,
    input  wire [2:0]  fifo_seq,
    input  wire        fifo_overflow,
    output reg          fifo_pop,

    /*
     * uart_tx 연결
     */
    input  wire       tx_busy,
    input  wire       tx_done,

    output reg  [7:0] tx_data,
    output reg        tx_start
);

    /*
     * v2 프로토콜: 요청-응답이 아니라 push 전용.
     * FPGA는 FIFO에 뭔가 쌓이면(이벤트 또는 heartbeat)
     * 그때그때 5바이트 고정 프레임으로 흘려보낸다.
     *
     * [0] 0x55            헤더
     * [1] event_code       (heartbeat는 EVENT_HEARTBEAT=0x0E 고정)
     * [2] event_detail
     * [3] {overflow(1b), 4'b0000, seq[2:0]}
     * [4] checksum = XOR([0]..[3])
     */
    localparam [7:0] MSG_HEADER = 8'h55;

    localparam [1:0]
        STATE_IDLE      = 2'd0,
        STATE_SEND_BYTE = 2'd1,
        STATE_WAIT_DONE = 2'd2;

    reg [1:0] state;
    reg [2:0] byte_index; // 0~4

    reg [7:0] code_snap;
    reg [7:0] detail_snap;
    reg [7:0] seq_byte_snap;
    reg [7:0] checksum_snap;

    wire [7:0] seq_byte =
        {fifo_overflow, 4'b0000, fifo_seq};

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state         <= STATE_IDLE;
            byte_index    <= 3'd0;
            fifo_pop      <= 1'b0;

            code_snap     <= 8'd0;
            detail_snap   <= 8'd0;
            seq_byte_snap <= 8'd0;
            checksum_snap <= 8'd0;

            tx_data  <= 8'd0;
            tx_start <= 1'b0;
        end
        else begin
            tx_start <= 1'b0;
            fifo_pop <= 1'b0;

            case (state)

                STATE_IDLE: begin
                    byte_index <= 3'd0;

                    if (!fifo_empty) begin
                        /*
                         * 이번 클럭에 FIFO head를 스냅샷으로
                         * 고정하고 동시에 pop한다.
                         * 전송 도중 새 이벤트가 들어와도
                         * 이번 프레임 내용은 바뀌지 않는다.
                         */
                        code_snap     <= fifo_code;
                        detail_snap   <= fifo_detail;
                        seq_byte_snap <= seq_byte;
                        checksum_snap <=
                            MSG_HEADER ^ fifo_code ^
                            fifo_detail ^ seq_byte;

                        fifo_pop <= 1'b1;
                        state    <= STATE_SEND_BYTE;
                    end
                end

                STATE_SEND_BYTE: begin
                    if (!tx_busy) begin
                        case (byte_index)
                            3'd0: tx_data <= MSG_HEADER;
                            3'd1: tx_data <= code_snap;
                            3'd2: tx_data <= detail_snap;
                            3'd3: tx_data <= seq_byte_snap;
                            3'd4: tx_data <= checksum_snap;
                            default: tx_data <= 8'h00;
                        endcase

                        tx_start <= 1'b1;
                        state    <= STATE_WAIT_DONE;
                    end
                end

                STATE_WAIT_DONE: begin
                    if (tx_done) begin
                        if (byte_index == 3'd4) begin
                            state <= STATE_IDLE;
                        end
                        else begin
                            byte_index <= byte_index + 1'b1;
                            state      <= STATE_SEND_BYTE;
                        end
                    end
                end

                default: begin
                    state <= STATE_IDLE;
                end

            endcase
        end
    end

endmodule
