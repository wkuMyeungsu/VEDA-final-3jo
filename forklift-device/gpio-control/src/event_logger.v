module event_logger (
    input  wire        clk,
    input  wire        rst_n,

    /*
     * event_detector 출력
     */
    input  wire        event_valid,
    input  wire [7:0]  event_code,
    input  wire [7:0]  event_detail,

    /*
     * 이벤트 발생 시점
     */
    input  wire [31:0] timestamp_ms,

    /*
     * 현재 상태 snapshot
     */
    input  wire [1:0]  effective_risk,
    input  wire [2:0]  warning_state,

    input  wire        estop_active,
    input  wire        comm_error,
    input  wire        latch_active,

    input  wire        checksum_error_latched,
    input  wire        protocol_error_latched,
    input  wire        timeout_error_latched,

    input  wire        self_test_active,

    /*
     * 논리적인 실제 출력 상태
     * active-low 반전 전의 값 사용
     */
    input  wire [1:0]  led_active_pattern,
    input  wire        buzzer_active,

    /*
     * 이후 CLEAR_LOG 명령에 연결
     */
    input  wire        clear_log_req,

    /*
     * 조회 포트
     *
     * read_index 0은 항상 가장 오래된 이벤트
     */
    input  wire [3:0]  read_index,

    output reg  [63:0] read_data,
    output reg         read_valid,

    /*
     * 현재 저장된 이벤트 개수
     */
    output reg  [4:0]  log_count,

    /*
     * 다음 이벤트가 기록될 물리 주소
     */
    output reg  [3:0]  write_pointer,

    output wire        log_full
);

    /*
     * 16-entry × 64-bit
     */
    reg [63:0] event_memory [0:15];

    wire [63:0] current_event_record;

    /*
     * 64비트 이벤트 레코드 생성
     */
    assign current_event_record = {
        1'b0,                       // [63] reserved
        buzzer_active,              // [62]
        led_active_pattern,         // [61:60]
        self_test_active,           // [59]
        timeout_error_latched,      // [58]
        protocol_error_latched,     // [57]
        checksum_error_latched,     // [56]
        latch_active,               // [55]
        comm_error,                 // [54]
        estop_active,               // [53]
        warning_state,              // [52:50]
        effective_risk,             // [49:48]
        event_detail,               // [47:40]
        event_code,                 // [39:32]
        timestamp_ms                // [31:0]
    };

    assign log_full =
        (log_count == 5'd16);

    /*
     * 이벤트 기록
     *
     * write_pointer는 다음 기록 위치를 가리킨다.
     * 로그가 가득 차면 가장 오래된 항목부터 덮어쓴다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            write_pointer <= 4'd0;
            log_count     <= 5'd0;
        end
        else if (clear_log_req) begin
            /*
             * 메모리 비트를 실제로 0으로 지우지 않고
             * 포인터와 개수만 초기화한다.
             *
             * count가 0이므로 기존 데이터는 유효하지 않다.
             */
            write_pointer <= 4'd0;
            log_count     <= 5'd0;
        end
        else if (event_valid) begin
            event_memory[write_pointer]
                <= current_event_record;

            /*
             * 4비트이므로 15 다음에는 자동으로 0
             */
            write_pointer <= write_pointer + 1'b1;

            if (log_count < 5'd16)
                log_count <= log_count + 1'b1;
            else
                log_count <= 5'd16;
        end
    end

    /*
     * read_index는 물리 주소가 아니라
     * 시간순 논리 index이다.
     *
     * read_index 0:
     * 가장 오래된 이벤트
     *
     * read_index log_count-1:
     * 가장 최근 이벤트
     */
    reg [3:0] physical_read_index;

    always @(*) begin
        read_data           = 64'd0;
        read_valid          = 1'b0;
        physical_read_index = 4'd0;

        if (read_index < log_count) begin
            read_valid = 1'b1;

            /*
             * 아직 한 번도 가득 차지 않은 경우
             * 메모리 0번부터 시간순으로 저장되어 있다.
             */
            if (log_count < 5'd16) begin
                physical_read_index = read_index;
            end

            /*
             * 로그가 가득 찬 경우 write_pointer는
             * 다음에 덮어쓸 가장 오래된 항목을 가리킨다.
             *
             * 4비트 덧셈이므로 15를 넘으면 자동 wrap.
             */
            else begin
                physical_read_index =
                    write_pointer + read_index;
            end

            read_data =
                event_memory[physical_read_index];
        end
    end

endmodule