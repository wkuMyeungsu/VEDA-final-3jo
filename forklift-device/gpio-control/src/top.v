module top #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer BAUD_RATE   = 115200
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire       uart_rx_i,
    
    input wire      estop_n,
    input wire      manual_reset_n,

    output wire [1:0] led,

    output wire buzzer_out
);

    wire [7:0] rx_data;
    wire       rx_valid;
    wire       framing_error;

    wire [1:0] risk_level;
    wire       risk_valid;
    wire       checksum_error;

    wire       comm_error;
    
    wire [1:0] effective_risk;
    wire       latch_active;
    
    wire manual_reset_pressed;
    wire manual_reset_pulse;
    wire manual_reset_release_pulse;

    wire estop_pressed;
    wire estop_active;

    wire [2:0] warning_state;
    wire [1:0] led_pattern_out;
    
    wire buzzer_enable;

   
    
    /*
     * UART 수신
     */
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

    /*
     * 패킷 및 checksum 검사
     */
    packet_parser u_packet_parser (
        .clk           (clk),
        .rst_n         (rst_n),
        .rx_data       (rx_data),
        .rx_valid      (rx_valid),
        .risk_level    (risk_level),
        .risk_valid    (risk_valid),
        .checksum_error(checksum_error)
    );

    /*
     * 정상 패킷 수신 감시
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

    /*
     * 경고 상태 결정
     */

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

    /*
     * 수동 Reset 버튼
     */
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

    /*
     * 비상정지 입력
     */
    estop_filter #(
        .CLK_FREQ_HZ       (CLK_FREQ_HZ),
        .RELEASE_DEBOUNCE_MS(20)
    ) u_estop_filter (
        .clk          (clk),
        .rst_n        (rst_n),
        .estop_n_async(estop_n),
        .estop_pressed(estop_pressed)
    );

    /*
     * 비상정지 래치
     */
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

 

    /*
     * LED 패턴 생성
     */
    led_pattern #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ)
    ) u_led_pattern (
        .clk            (clk),
        .rst_n          (rst_n),
        .warning_state  (warning_state),
        .led_pattern_out(led_pattern_out)
    );

    /*
     * LED가 active-high일 때
     */
    assign led = led_pattern_out;

    buzzer_ctrl #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ),
        .TONE_FREQ_HZ(2000)
    ) u_buzzer_ctrl (
        .clk          (clk),
        .rst_n        (rst_n),
        .warning_state(warning_state),
        .buzzer_out   (buzzer_out),
        .buzzer_enable(buzzer_enable)
);

    

endmodule