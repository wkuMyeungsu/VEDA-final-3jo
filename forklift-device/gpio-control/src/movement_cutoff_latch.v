module movement_cutoff_latch (
    input  wire clk,
    input  wire rst_n,

    /*
     * estop_active || (effective_risk == CRITICAL) 를 top.v에서
     * 미리 조합해 넣는다. COMM_ERROR는 의도적으로 제외했다
     * (팀 결정: CRITICAL/ESTOP만큼 확실한 위험은 아니라고 판단).
     */
    input  wire cutoff_trigger,

    /*
     * ESTOP 래치와 동일한 물리 리셋 버튼을 그대로 재사용한다.
     * 즉 ESTOP과 전진 차단은 항상 한 번의 물리 조작으로
     * 같이 풀린다 (해제 지점을 하나로 유지해 조작 혼란을 줄인다).
     */
    input  wire manual_reset_pulse,

    output reg  movement_cutoff_active
);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            /*
             * 전원 인가 직후에는 CRITICAL/ESTOP이 실제로 발생하기
             * 전이므로 cutoff_trigger가 자연히 0이라, 별도의
             * "일단 차단" 초기값을 줄 필요가 없다. 릴레이 자체의
             * 무전원 기본 상태(NC=통과)가 실제 안전판단을 담당한다.
             */
         movement_cutoff_active <= 1'b0;
        end
        else begin
            /*
             * CRITICAL/ESTOP이 걸려있는 한 항상 우선
             */
            if (cutoff_trigger) begin
                movement_cutoff_active <= 1'b1;
            end

            /*
             * 위험 상태가 해제된 뒤에만 수동 Reset으로 래치 해제
             * (estop_latch와 동일한 패턴)
             */
            else if (manual_reset_pulse) begin
                movement_cutoff_active <= 1'b0;
            end
        end
    end

endmodule
