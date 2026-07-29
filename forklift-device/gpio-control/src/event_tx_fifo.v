module event_tx_fifo (
    input  wire       clk,
    input  wire       rst_n,

    /*
     * push 측: event_detector / heartbeat_gen을 top.v에서
     * 하나의 포트로 묶어 연결한다 (동시 발생 시 event 우선).
     */
    input  wire       push_valid,
    input  wire [7:0] push_code,
    input  wire [7:0] push_detail,

    /*
     * pop 측: event_uart_tx가 전송 시작 시 1클럭 pop을 건다.
     */
    input  wire       pop,

    output wire [7:0] pop_code,
    output wire [7:0] pop_detail,
    output wire        fifo_empty,

    /*
     * 이번에 꺼내는 메시지에 실을 시퀀스/유실 플래그.
     * Pi는 seq가 1씩 증가하는지 확인해 유실을 감지하고,
     * overflow 비트로 "FIFO가 가득 차서 일부 이벤트를
     * 버렸다"는 사실 자체를 명시적으로 통보받는다.
     *
     * 과거 설계의 16-entry 이력 로그(휘발성, 전원 꺼지면 소실)와
     * 달리, 이 FIFO는 "역사 보관"이 목적이 아니라 "UART가 잠깐
     * 바쁠 때 이벤트를 놓치지 않고 순서대로 내보내는 것"이
     * 목적이다. 이력 보관/조회는 Pi가 수신 즉시 자체 DB에
     * 기록하는 방식으로 이관한다.
     */
    output wire [2:0] pop_seq,
    output wire       pop_overflow
);

    localparam integer DEPTH = 8; // 2^3, 포인터 3비트로 고정

    reg [7:0] mem_code   [0:DEPTH-1];
    reg [7:0] mem_detail [0:DEPTH-1];

    reg [2:0] wr_ptr;
    reg [2:0] rd_ptr;
    reg [3:0] count; // 0~8

    reg [2:0] seq_reg;
    reg       overflow_sticky;

    wire fifo_full = (count == DEPTH[3:0]);

    wire do_push = push_valid && !fifo_full;
    wire do_pop  = pop        && !fifo_empty;

    assign fifo_empty  = (count == 4'd0);
    assign pop_code    = mem_code[rd_ptr];
    assign pop_detail  = mem_detail[rd_ptr];
    assign pop_seq      = seq_reg;
    assign pop_overflow = overflow_sticky;

    /*
     * 쓰기 포인터 및 메모리
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr <= 3'd0;
        end
        else if (do_push) begin
            mem_code[wr_ptr]   <= push_code;
            mem_detail[wr_ptr] <= push_detail;
            wr_ptr             <= wr_ptr + 1'b1;
        end
    end

    /*
     * 읽기 포인터
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rd_ptr <= 3'd0;
        end
        else if (do_pop) begin
            rd_ptr <= rd_ptr + 1'b1;
        end
    end

    /*
     * 저장 개수
     * push/pop이 같은 클럭에 동시에 일어나면 개수는 그대로다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            count <= 4'd0;
        end
        else begin
            case ({do_push, do_pop})
                2'b10:   count <= count + 1'b1;
                2'b01:   count <= count - 1'b1;
                default: count <= count;
            endcase
        end
    end

    /*
     * 전송용 시퀀스 번호
     * 실제로 pop되어 나갈 때만 증가한다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            seq_reg <= 3'd0;
        end
        else if (do_pop) begin
            seq_reg <= seq_reg + 1'b1;
        end
    end

    /*
     * overflow_sticky:
     * FIFO가 가득 찬 상태에서 push를 시도하면 set.
     * 다음 pop 시점에 그 값이 pop_overflow로 그대로 실려나가고,
     * 그 클럭에 clear된다 (같은 클럭에 새 overflow가 생기면 유지).
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            overflow_sticky <= 1'b0;
        end
        else if (push_valid && fifo_full) begin
            overflow_sticky <= 1'b1;
        end
        else if (do_pop) begin
            overflow_sticky <= 1'b0;
        end
    end

endmodule
