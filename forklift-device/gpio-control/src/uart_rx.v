module uart_rx #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer BAUD_RATE = 115200
)(
    input  wire       clk, //27MHZ
    input  wire       rst_n,
    input  wire       uart_rx_i,

    output reg  [7:0] rx_data,
    output reg        rx_valid,
    output reg        framing_error
);
    localparam integer CLKS_PER_BIT = CLK_FREQ_HZ / BAUD_RATE;

    
    localparam [2:0]
        STATE_IDLE = 3'd0,
        STATE_START = 3'd1,
        STATE_DATA = 3'd2,
        STATE_STOP = 3'd3;
    
    reg [2:0] state;
    
    reg uart_rx_meta;
    reg uart_rx_sync;
    
    reg[15:0] clk_count;
    reg[2:0] bit_index;
    reg [7:0] rx_shift;


    /*
     * UART 입력은 FPGA 클럭과 비동기이므로
     * 2단 플립플롭으로 동기화한다.
     */
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            uart_rx_meta <= 1'b1;
            uart_rx_sync <= 1'b1;
        end
        else begin
            uart_rx_meta <= uart_rx_i;
            uart_rx_sync <= uart_rx_meta;
        end
    end
    
    /*
     * UART RX 상태 머신
     */
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            state         <= STATE_IDLE;
            clk_count     <= 16'd0;
            bit_index     <= 3'd0;
            rx_shift      <= 8'd0;
            rx_data       <= 8'd0;
            rx_valid      <= 1'b0;
            framing_error <= 1'b0;
        end
        else begin
            // 두 출력은 기본적으로 1클럭 펄스
            rx_valid      <= 1'b0;
            framing_error <= 1'b0;
            
            case (state)

                STATE_IDLE: begin
                    clk_count <= 16'd0;
                    bit_index <= 3'd0;

                    // UART는 평상시 HIGH, Start bit는 LOW
                    if (uart_rx_sync == 1'b0)
                        state <= STATE_START;
                end

                STATE_START: begin
                    /*
                     * Start bit의 중앙에서 다시 LOW인지 확인한다.
                     * 순간적인 노이즈를 Start bit로 잘못 인식하는 것을 줄인다.
                     */
                    if (clk_count == (CLKS_PER_BIT / 2) - 1) begin
                        clk_count <= 16'd0;

                        if (uart_rx_sync == 1'b0)
                            state <= STATE_DATA;
                        else
                            state <= STATE_IDLE;
                    end
                    else begin
                        clk_count <= clk_count + 1'b1;
                    end
                end
                
                STATE_DATA: begin
                    if (clk_count == CLKS_PER_BIT - 1) begin
                        clk_count <= 16'd0;

                        // UART는 LSB부터 전송한다.
                        rx_shift[bit_index] <= uart_rx_sync;

                        if (bit_index == 3'd7) begin
                            bit_index <= 3'd0;
                            state     <= STATE_STOP;
                        end
                        else begin
                            bit_index <= bit_index + 1'b1;
                        end
                    end
                    else begin
                        clk_count <= clk_count + 1'b1;
                    end
                end

                STATE_STOP: begin
                    if (clk_count == CLKS_PER_BIT - 1) begin
                        clk_count <= 16'd0;

                        // 정상 Stop bit는 HIGH
                        if (uart_rx_sync == 1'b1) begin
                            rx_data  <= rx_shift;
                            rx_valid <= 1'b1;
                        end
                        else begin
                            framing_error <= 1'b1;
                        end

                        state <= STATE_IDLE;
                    end
                    else begin
                        clk_count <= clk_count + 1'b1;
                    end
                end

                default: begin
                    state <= STATE_IDLE;
                end

            endcase
        end
    end

endmodule

