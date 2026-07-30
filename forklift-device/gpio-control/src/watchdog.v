module watchdog #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer TIMEOUT_MS  = 500
)(
    input  wire clk,
    input  wire rst_n,

    /*
     * 정상 패킷을 받았을 때 1클럭 동안 HIGH
     */
    input  wire packet_valid,

    /*
     * 통신 오류 상태
     */
    output reg  comm_error
);

    /*
     * 곱셈 오버플로를 피하기 위해
     * CLK_FREQ_HZ를 먼저 1000으로 나눈다.
     *
     * 27MHz, 500ms:
     * 27,000 × 500 = 13,500,000 clocks
     */
    localparam integer TIMEOUT_COUNT =
        (CLK_FREQ_HZ / 1000) * TIMEOUT_MS;

    reg [31:0] watchdog_count;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            watchdog_count <= 32'd0;

            /*
             * 전원 인가 직후 정상 패킷을 받기 전까지는
             * 통신 상태를 신뢰할 수 없으므로 오류 상태로 시작
             */
            comm_error <= 1'b1;
        end
        else begin
            /*
             * checksum까지 통과한 정상 패킷을 받으면
             * watchdog 초기화 및 통신 오류 해제
             */
            if (packet_valid) begin
                watchdog_count <= 32'd0;
                comm_error     <= 1'b0;
            end

            /*
             * 아직 timeout에 도달하지 않은 경우 카운트
             */
            else if (!comm_error) begin
                if (watchdog_count >= TIMEOUT_COUNT - 1) begin
                    watchdog_count <= watchdog_count;
                    comm_error     <= 1'b1;
                end
                else begin
                    watchdog_count <= watchdog_count + 1'b1;
                end
            end

            /*
             * 이미 통신 오류라면 다음 정상 패킷까지 유지
             */
            else begin
                watchdog_count <= 32'd0;
                comm_error     <= 1'b1;
            end
        end
    end

endmodule