module estop_filter #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer RELEASE_DEBOUNCE_MS = 20
)(
    input  wire clk,
    input  wire rst_n,

    /*
     * Active-low 비상정지 입력
     */
    input  wire estop_n_async,

    output reg  estop_pressed
);

    localparam integer RELEASE_COUNT =
        (CLK_FREQ_HZ / 1000) * RELEASE_DEBOUNCE_MS;

    reg estop_sync1;
    reg estop_sync2;

    reg [31:0] release_counter;

    wire raw_pressed;

    assign raw_pressed = ~estop_sync2;

    /*
     * 비동기 입력 2단 동기화
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            estop_sync1 <= 1'b1;
            estop_sync2 <= 1'b1;
        end
        else begin
            estop_sync1 <= estop_n_async;
            estop_sync2 <= estop_sync1;
        end
    end

    /*
     * 누름은 즉시 인정하고,
     * 해제만 debounce 처리
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            estop_pressed  <= 1'b0;
            release_counter <= 32'd0;
        end
        else begin
            if (raw_pressed) begin
                estop_pressed  <= 1'b1;
                release_counter <= 32'd0;
            end
            else if (estop_pressed) begin
                if (release_counter >= RELEASE_COUNT - 1) begin
                    estop_pressed  <= 1'b0;
                    release_counter <= 32'd0;
                end
                else begin
                    release_counter <= release_counter + 1'b1;
                end
            end
            else begin
                release_counter <= 32'd0;
            end
        end
    end

endmodule