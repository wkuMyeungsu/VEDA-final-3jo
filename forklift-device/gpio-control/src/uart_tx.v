module uart_tx #(
    parameter integer CLK_FREQ_HZ = 27_000_000,
    parameter integer BAUD_RATE   = 115200
)(
    input  wire       clk,
    input  wire       rst_n,

    input  wire [7:0] tx_data,
    input  wire       tx_start,

    output reg        uart_tx_o,
    output reg        tx_busy,
    output reg        tx_done
);

    localparam integer CLKS_PER_BIT =
        CLK_FREQ_HZ / BAUD_RATE;

    localparam [2:0]
        STATE_IDLE  = 3'd0,
        STATE_START = 3'd1,
        STATE_DATA  = 3'd2,
        STATE_STOP  = 3'd3;

    reg [2:0] state;

    reg [31:0] clk_counter;
    reg [2:0]  bit_index;
    reg [7:0]  tx_data_reg;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state       <= STATE_IDLE;
            uart_tx_o   <= 1'b1;
            tx_busy     <= 1'b0;
            tx_done     <= 1'b0;
            clk_counter <= 32'd0;
            bit_index   <= 3'd0;
            tx_data_reg <= 8'd0;
        end
        else begin
            tx_done <= 1'b0;

            case (state)

                STATE_IDLE: begin
                    uart_tx_o   <= 1'b1;
                    tx_busy     <= 1'b0;
                    clk_counter <= 32'd0;
                    bit_index   <= 3'd0;

                    if (tx_start) begin
                        tx_data_reg <= tx_data;
                        tx_busy     <= 1'b1;

                        /*
                         * Start bit
                         */
                        uart_tx_o <= 1'b0;
                        state     <= STATE_START;
                    end
                end

                STATE_START: begin
                    if (clk_counter >= CLKS_PER_BIT - 1) begin
                        clk_counter <= 32'd0;

                        /*
                         * UART는 LSB부터 송신
                         */
                        uart_tx_o <= tx_data_reg[0];
                        bit_index <= 3'd0;
                        state     <= STATE_DATA;
                    end
                    else begin
                        clk_counter <= clk_counter + 1'b1;
                    end
                end

                STATE_DATA: begin
                    if (clk_counter >= CLKS_PER_BIT - 1) begin
                        clk_counter <= 32'd0;

                        if (bit_index == 3'd7) begin
                            /*
                             * Stop bit
                             */
                            uart_tx_o <= 1'b1;
                            state     <= STATE_STOP;
                        end
                        else begin
                            bit_index <= bit_index + 1'b1;
                            uart_tx_o <= tx_data_reg[bit_index + 1'b1];
                        end
                    end
                    else begin
                        clk_counter <= clk_counter + 1'b1;
                    end
                end

                STATE_STOP: begin
                    if (clk_counter >= CLKS_PER_BIT - 1) begin
                        clk_counter <= 32'd0;
                        uart_tx_o   <= 1'b1;
                        tx_busy     <= 1'b0;
                        tx_done     <= 1'b1;
                        state       <= STATE_IDLE;
                    end
                    else begin
                        clk_counter <= clk_counter + 1'b1;
                    end
                end

                default: begin
                    state     <= STATE_IDLE;
                    uart_tx_o <= 1'b1;
                    tx_busy   <= 1'b0;
                end

            endcase
        end
    end

endmodule