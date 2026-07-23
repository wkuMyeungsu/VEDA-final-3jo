module top #(
    parameter integer CLK_FREQ_HZ  = 27_000_000,
    parameter integer BAUD_RATE    = 115200,
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

    wire [7:0] rx_data;
    wire       rx_valid;
    wire       framing_error;

    wire       packet_valid;
    wire [1:0] risk_level;
    wire       risk_valid;

    wire       read_status_req;
    wire       clear_error_req;

    wire [1:0] self_test_mode;
    wire       self_test_valid;

    wire       read_log_info_req;
    wire       read_log_req;
    wire [3:0] read_log_index;
    wire       clear_log_req;

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

    wire       safety_override;
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
    wire       uart_response_busy;

    wire        tick_1ms;
    wire [31:0] timestamp_ms;

    wire       event_valid;
    wire [7:0] event_code;
    wire [7:0] event_detail;

    wire [63:0] event_log_read_data;
    wire        event_log_read_valid;
    wire [4:0]  event_log_count;
    wire [3:0]  event_log_write_pointer;
    wire        event_log_full;

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

        .read_status_req         (read_status_req),
        .clear_error_req         (clear_error_req),
        .self_test_mode          (self_test_mode),
        .self_test_valid         (self_test_valid),

        .checksum_error          (checksum_error),
        .protocol_error          (protocol_error),
        .interbyte_timeout_error (interbyte_timeout_error),

        .checksum_error_latched  (checksum_error_latched),
        .protocol_error_latched  (protocol_error_latched),
        .timeout_error_latched   (timeout_error_latched),

        .read_log_info_req       (read_log_info_req),
        .read_log_req            (read_log_req),
        .read_log_index          (read_log_index),
        .clear_log_req           (clear_log_req)
    );

    /*
     * SET_RISK만 heartbeat로 인정한다.
     * READ_STATUS/READ_LOG만 반복해 watchdog이 해제되는 것을 방지한다.
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
        .comm_error         (comm_error),
        .estop_active       (estop_active),
        .self_test_active   (self_test_active),
        .test_led_out       (self_test_led_out),
        .test_buzzer_enable (self_test_buzzer_enable),
        .self_test_done     (self_test_done),
        .self_test_rejected (self_test_rejected)
    );

    assign safety_override = estop_active || comm_error;

    assign selected_led_pattern =
        (self_test_active && !safety_override)
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
        .test_override (self_test_active && !safety_override),
        .test_enable   (self_test_buzzer_enable),
        .warning_state (warning_state),
        .buzzer_out    (buzzer_out),
        .buzzer_enable (final_buzzer_active)
    );

    uart_response_controller u_uart_response_controller (
    .clk                    (clk),
    .rst_n                  (rst_n),

    .read_status_req        (read_status_req),
    .read_log_info_req      (read_log_info_req),
    .read_log_req           (read_log_req),

    .effective_risk         (effective_risk),
    .comm_error             (comm_error),
    .estop_active           (estop_active),
    .latch_active           (latch_active),

    .checksum_error_latched (checksum_error_latched),
    .protocol_error_latched (protocol_error_latched),
    .timeout_error_latched  (timeout_error_latched),

    .log_count              (event_log_count),
    .log_full               (event_log_full),

    .log_read_index         (read_log_index),
    .log_read_data          (event_log_read_data),
    .log_read_valid         (event_log_read_valid),

    .tx_busy                (uart_tx_busy),
    .tx_done                (uart_tx_done),

    .tx_data                (uart_tx_data),
    .tx_start               (uart_tx_start),

    .response_busy          (uart_response_busy)
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

    timestamp_counter #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ)
    ) u_timestamp_counter (
        .clk         (clk),
        .rst_n       (rst_n),
        .tick_1ms    (tick_1ms),
        .timestamp_ms(timestamp_ms)
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
        .risk_level              (risk_level),
        .self_test_active        (self_test_active),
        .self_test_mode          (self_test_mode),
        .self_test_rejected      (self_test_rejected),
        .clear_error_req         (clear_error_req),
        .event_valid             (event_valid),
        .event_code              (event_code),
        .event_detail            (event_detail)
    );

    event_logger u_event_logger (
        .clk                    (clk),
        .rst_n                  (rst_n),
        .event_valid            (event_valid),
        .event_code             (event_code),
        .event_detail           (event_detail),
        .timestamp_ms           (timestamp_ms),
        .effective_risk         (effective_risk),
        .warning_state          (warning_state),
        .estop_active           (estop_active),
        .comm_error             (comm_error),
        .latch_active           (latch_active),
        .checksum_error_latched (checksum_error_latched),
        .protocol_error_latched (protocol_error_latched),
        .timeout_error_latched  (timeout_error_latched),
        .self_test_active       (self_test_active),
        .led_active_pattern     (selected_led_pattern),
        .buzzer_active          (final_buzzer_active),
        .clear_log_req          (clear_log_req),
        .read_index             (read_log_index),
        .read_data              (event_log_read_data),
        .read_valid             (event_log_read_valid),
        .log_count              (event_log_count),
        .write_pointer          (event_log_write_pointer),
        .log_full               (event_log_full)
    );

    /*
     * read_log_info_req/read_log_req는 다음 단계의 log_response에서 사용한다.
     * 현재 단계에서는 parser와 event_logger의 read port까지만 연결된 상태다.
     */

endmodule
