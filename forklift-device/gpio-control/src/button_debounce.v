module button_debounce #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer DEBOUNCE_MS = 20,
    parameter integer ACTIVE_LOW  = 1
)(
    input  wire clk,
    input  wire rst_n,

    input  wire button_async,

    output reg  button_pressed,
    output reg  press_pulse,
    output reg  release_pulse
);

    localparam integer DEBOUNCE_COUNT =
        (CLK_FREQ_HZ / 1000) * DEBOUNCE_MS;

    reg button_sync1;
    reg button_sync2;

    reg [31:0] debounce_counter;

    wire raw_pressed;

    assign raw_pressed =
        ACTIVE_LOW ? ~button_sync2 : button_sync2;

    /*
     * 비동기 버튼 입력 2단 동기화
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            if (ACTIVE_LOW) begin
                button_sync1 <= 1'b1;
                button_sync2 <= 1'b1;
            end
            else begin
                button_sync1 <= 1'b0;
                button_sync2 <= 1'b0;
            end
        end
        else begin
            button_sync1 <= button_async;
            button_sync2 <= button_sync1;
        end
    end

    /*
     * Debounce 및 edge pulse 생성
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            button_pressed  <= 1'b0;
            press_pulse     <= 1'b0;
            release_pulse   <= 1'b0;
            debounce_counter <= 32'd0;
        end
        else begin
            press_pulse   <= 1'b0;
            release_pulse <= 1'b0;

            /*
             * 현재 안정 상태와 입력이 같으면
             * 카운터 초기화
             */
            if (raw_pressed == button_pressed) begin
                debounce_counter <= 32'd0;
            end
            else begin
                /*
                 * 다른 상태가 DEBOUNCE_MS 동안 유지되면
                 * 새로운 버튼 상태로 인정
                 */
                if (debounce_counter >= DEBOUNCE_COUNT - 1) begin
                    debounce_counter <= 32'd0;
                    button_pressed   <= raw_pressed;

                    if (raw_pressed)
                        press_pulse <= 1'b1;
                    else
                        release_pulse <= 1'b1;
                end
                else begin
                    debounce_counter <= debounce_counter + 1'b1;
                end
            end
        end
    end

endmodule