module estop_latch (
    input  wire clk,
    input  wire rst_n,

    input  wire estop_pressed,
    input  wire manual_reset_pulse,

    output reg  estop_active
);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            estop_active <= 1'b0;
        end
        else begin
            /*
             * 비상정지가 눌려 있으면 항상 우선
             */
            if (estop_pressed) begin
                estop_active <= 1'b1;
            end

            /*
             * 비상정지 버튼이 해제된 상태에서만
             * 수동 Reset으로 래치 해제
             */
            else if (manual_reset_pulse) begin
                estop_active <= 1'b0;
            end
        end
    end

endmodule