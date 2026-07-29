module top #(
    parameter integer CLK_FREQ_HZ    = 27_000_000,
    parameter integer BAUD_RATE      = 115200,
    parameter integer LED_ACTIVE_LOW = 0
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire       uart_rx_i,
    output wire       uart_tx_o,

    input  wire       estop_n,
    input  wire       manual_reset_n,

    output wire [1:0] led,
    output wire       buzzer_out
);

    /*
     * ------------------------------------------------------------
     * v2 아키텍처 요약
     *
     * FPGA는 "Pi가 죽거나 통신이 끊겨도 하드웨어로 보장되어야 하는
     * 안전 최종 방어선" 역할만 맡는다.
     *
     *   Pi -> FPGA : SET_RISK / CLEAR_ERROR / SELF_TEST 3개 명령만
     *   FPGA -> Pi : 상태 조회 응답 없음. 상태 변화 이벤트와
     *                200ms 주기 heartbeat(현재 상태 요약 포함)를
     *                FPGA가 스스로 push한다.
     *
     * 이력 로그의 저장/조회는 Pi가 수신한 이벤트를 자체 DB에
     * 기록하는 방식으로 이관했다. FPGA에는 더 이상 이력을
     * 저장하지 않고, UART가 잠깐 바쁠 때 이벤트 순서를 보장하기
     * 위한 8-depth 전송 FIFO만 남겼다(event_tx_fifo).
     * ------------------------------------------------------------
     */

    localparam [7:0] EVENT_HEARTBEAT = 8'h0E;

    wire [7:0] rx_data;
    wire       rx_valid;
    wire       framing_error;

    wire       packet_valid;
    wire [1:0] risk_level;
    wire       risk_valid;

    wire       clear_error_req;

    wire [1:0] self_test_mode;
    wire       self_test_valid;

    wire       checksum_error;
    wire       protocol_error;
    wire       interbyte_timeout_error;

    wire       checksum_error_latched;
    wire       protocol_error_latched;
    wire       timeout_error_latched;

    wire       comm_error;

    wire [1:0] effective_risk;
    wire       latch_active;

    wire       manual_reset_pressed;
    wire       manual_reset_pulse;
    wire       manual_reset_release_pulse;

    wire       estop_pressed;
    wire       estop_active;

    wire [2:0] warning_state;
    wire [1:0] led_pattern_out;

    wire       risk_is_safe;
    wire       safety_override;
    wire       self_test_output_enable;
    wire [1:0] selected_led_pattern;

    wire       self_test_active;
    wire [1:0] self_test_led_out;
    wire       self_test_buzzer_enable;
    wire       self_test_done;
    wire       self_test_rejected;

    wire       final_buzzer_active;

    wire [7:0] uart_tx_data;
    wire       uart_tx_start;
    wire       uart_tx_busy;
    wire       uart_tx_done;

    /*
     * 이벤트 소스 (상태 변화 이벤트 + 주기적 heartbeat)
     */
    wire        event_valid;
    wire [7:0]  event_code;
    wire [7:0]  event_detail;

    wire        heartbeat_valid;
    wire [7:0]  heartbeat_detail;

    wire        push_valid  = event_valid | heartbeat_valid;
    wire [7:0]  push_code   = event_valid ? event_code   : EVENT_HEARTBEAT;
    wire [7:0]  push_detail = event_valid ? event_detail : heartbeat_detail;

    /*
     * 전송 FIFO <-> event_uart_tx
     */
    wire        fifo_empty;
    wire [7:0]  fifo_code;
    wire [7:0]  fifo_detail;
    wire [2:0]  fifo_seq;
    wire        fifo_overflow;
    wire        fifo_pop;

    /*
     * self_test_controller가 실제로 실행 중인 모드.
     */
    wire [1:0] self_test_active_mode;


    uart_rx #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .BAUD_RATE  (BAUD_RATE)
    ) u_uart_rx (
        .clk          (clk),
        .rst_n        (rst_n),
        .uart_rx_i    (uart_rx_i),
        .rx_data      (rx_data),
        .rx_valid     (rx_valid),
        .framing_error(framing_error)
    );

    packet_parser #(
        .CLK_FREQ_HZ         (CLK_FREQ_HZ),
        .INTERBYTE_TIMEOUT_MS(10)
    ) u_packet_parser (
        .clk                     (clk),
        .rst_n                   (rst_n),
        .rx_data                 (rx_data),
        .rx_valid                (rx_valid),

        .packet_valid            (packet_valid),
        .risk_level              (risk_level),
        .risk_valid              (risk_valid),

        .clear_error_req         (clear_error_req),
        .self_test_mode          (self_test_mode),
        .self_test_valid         (self_test_valid),

        .checksum_error          (checksum_error),
        .protocol_error          (protocol_error),
        .interbyte_timeout_error (interbyte_timeout_error),

        .checksum_error_latched  (checksum_error_latched),
        .protocol_error_latched  (protocol_error_latched),
        .timeout_error_latched   (timeout_error_latched)
    );

    /*
     * SET_RISK만 heartbeat(watchdog 리셋)로 인정한다.
     * Pi -> FPGA 생존 감시.
     */
    watchdog #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .TIMEOUT_MS (500)
    ) u_watchdog (
        .clk         (clk),
        .rst_n       (rst_n),
        .packet_valid(risk_valid),
        .comm_error  (comm_error)
    );

    warning_latch #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ),
        .HOLD_TIME_MS(2000)
    ) u_warning_latch (
        .clk           (clk),
        .rst_n         (rst_n),
        .risk_level    (risk_level),
        .risk_valid    (risk_valid),
        .effective_risk(effective_risk),
        .latch_active  (latch_active)
    );

    button_debounce #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .DEBOUNCE_MS(20),
        .ACTIVE_LOW (1)
    ) u_manual_reset_button (
        .clk           (clk),
        .rst_n         (rst_n),
        .button_async  (manual_reset_n),
        .button_pressed(manual_reset_pressed),
        .press_pulse   (manual_reset_pulse),
        .release_pulse (manual_reset_release_pulse)
    );

    estop_filter #(
        .CLK_FREQ_HZ        (CLK_FREQ_HZ),
        .RELEASE_DEBOUNCE_MS(20)
    ) u_estop_filter (
        .clk          (clk),
        .rst_n        (rst_n),
        .estop_n_async(estop_n),
        .estop_pressed(estop_pressed)
    );

    estop_latch u_estop_latch (
        .clk               (clk),
        .rst_n             (rst_n),
        .estop_pressed     (estop_pressed),
        .manual_reset_pulse(manual_reset_pulse),
        .estop_active      (estop_active)
    );

    warning_fsm u_warning_fsm (
        .effective_risk(effective_risk),
        .comm_error    (comm_error),
        .estop_active  (estop_active),
        .warning_state (warning_state)
    );

    led_pattern #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ)
    ) u_led_pattern (
        .clk            (clk),
        .rst_n          (rst_n),
        .warning_state  (warning_state),
        .led_pattern_out(led_pattern_out)
    );

    self_test_controller #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ),
        .AUTO_STOP_MS(5000)
    ) u_self_test_controller (
        .clk                (clk),
        .rst_n              (rst_n),
        .self_test_mode     (self_test_mode),
        .self_test_valid    (self_test_valid),
        .effective_risk     (effective_risk),
        .comm_error         (comm_error),
        .estop_active       (estop_active),
        .self_test_active   (self_test_active),
        .active_mode        (self_test_active_mode),
        .test_led_out       (self_test_led_out),
        .test_buzzer_enable (self_test_buzzer_enable),
        .self_test_done     (self_test_done),
        .self_test_rejected (self_test_rejected)
    );

    assign risk_is_safe =
        (effective_risk == 2'd0);

    assign safety_override =
        estop_active ||
        comm_error ||
        !risk_is_safe;

    assign self_test_output_enable =
        self_test_active &&
        !safety_override;

    assign selected_led_pattern =
        self_test_output_enable
        ? self_test_led_out
        : led_pattern_out;

    assign led =
        LED_ACTIVE_LOW ? ~selected_led_pattern : selected_led_pattern;

    buzzer_ctrl #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ),
        .TONE_FREQ_HZ(2000)
    ) u_buzzer_ctrl (
        .clk           (clk),
        .rst_n         (rst_n),
        .test_override (self_test_output_enable),
        .test_enable   (self_test_buzzer_enable),
        .warning_state (warning_state),
        .buzzer_out    (buzzer_out),
        .buzzer_enable (final_buzzer_active)
    );

    event_detector u_event_detector (
    .clk                     (clk),
    .rst_n                   (rst_n),
    .checksum_error          (checksum_error),
    .protocol_error          (protocol_error),
    .interbyte_timeout_error (interbyte_timeout_error),
    .framing_error           (framing_error),
    .comm_error              (comm_error),
    .estop_active            (estop_active),
    .effective_risk          (effective_risk),
    .self_test_active        (self_test_active),
    .self_test_active_mode   (self_test_active_mode),
    .self_test_requested_mode(self_test_mode),
    .self_test_rejected      (self_test_rejected),
    .clear_error_req         (clear_error_req),
    .event_valid             (event_valid),
    .event_code              (event_code),
    .event_detail            (event_detail)
    );

    heartbeat_gen #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .PERIOD_MS  (200)
    ) u_heartbeat_gen (
        .clk                    (clk),
        .rst_n                  (rst_n),
        .warning_state          (warning_state),
        .self_test_active       (self_test_active),
        .latch_active           (latch_active),
        .checksum_error_latched (checksum_error_latched),
        .protocol_error_latched (protocol_error_latched),
        .timeout_error_latched  (timeout_error_latched),
        .heartbeat_valid        (heartbeat_valid),
        .heartbeat_detail       (heartbeat_detail)
    );

    event_tx_fifo u_event_tx_fifo (
        .clk          (clk),
        .rst_n        (rst_n),

        .push_valid   (push_valid),
        .push_code    (push_code),
        .push_detail  (push_detail),

        .pop          (fifo_pop),
        .pop_code     (fifo_code),
        .pop_detail   (fifo_detail),
        .fifo_empty   (fifo_empty),
        .pop_seq      (fifo_seq),
        .pop_overflow (fifo_overflow)
    );

    event_uart_tx u_event_uart_tx (
        .clk           (clk),
        .rst_n         (rst_n),

        .fifo_empty    (fifo_empty),
        .fifo_code     (fifo_code),
        .fifo_detail   (fifo_detail),
        .fifo_seq      (fifo_seq),
        .fifo_overflow (fifo_overflow),
        .fifo_pop      (fifo_pop),

        .tx_busy       (uart_tx_busy),
        .tx_done       (uart_tx_done),
        .tx_data       (uart_tx_data),
        .tx_start      (uart_tx_start)
    );

    uart_tx #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ),
        .BAUD_RATE  (BAUD_RATE)
    ) u_uart_tx (
        .clk      (clk),
        .rst_n    (rst_n),
        .tx_data  (uart_tx_data),
        .tx_start (uart_tx_start),
        .uart_tx_o(uart_tx_o),
        .tx_busy  (uart_tx_busy),
        .tx_done  (uart_tx_done)
    );

endmodule
