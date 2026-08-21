/*
 * TX노드 (FPGA 쪽) - NUCLEO-F401RE
 *
 * FPGA의 uart_tx_o(핀41, 115200 8N1)를 USART1_RX(PA10, Arduino D2)로
 * 수신 전용으로 탭한다. 기존 Pi도 같은 라인을 계속 받고 있으므로 이
 * 보드는 그저 두 번째 리시버일 뿐 - FPGA/Pi 쪽 배선은 손대지 않는다.
 *
 * 유효 프레임(0x55 헤더 + XOR 체크섬 통과)만 골라서 NRF24L01로 그대로
 * 무선 전송한다. 새 프로토콜을 만들지 않고 FPGA_control/src/PROTOCOL.md
 * 2.1절의 5바이트 push 프레임을 그대로 중계한다.
 *
 * 프로젝트 생성 방법 (wireless_relay/PROTOCOL_WIRELESS.md 참고):
 * STM32CubeIDE에서 NUCLEO-F401RE로 새 프로젝트 생성 -> Pinout 화면에서
 * SPI1(Full-Duplex Master)과 USART1(Asynchronous)만 Mode 활성화 -> Generate
 * Code -> 생성된 main.c를 이 파일 내용으로 교체. (Mode만 켜두는 이유는
 * stm32f4xx_hal_conf.h의 HAL_SPI/HAL_UART 모듈 매크로와 stm32f4xx_it.c의
 * USART1_IRQHandler 스텁이 자동으로 생기게 하기 위함 - 실제 GPIO/클럭
 * 초기화는 아래 MX_*_Init()이 전부 직접 한다.)
 *
 * common/ 아래 nrf24l01.c/h, fpga_frame.c/h, wireless_link_config.h를
 * 이 프로젝트의 Core/Src, Core/Inc에 복사해 넣을 것.
 */

#include "stm32f4xx_hal.h"
#include "nrf24l01.h"
#include "fpga_frame.h"
#include "wireless_link_config.h"

static SPI_HandleTypeDef  hspi1;
static UART_HandleTypeDef huart1;

/* USART1 RX 프레임 조립 상태머신 (인터럽트 콜백에서 갱신) */
static uint8_t  s_rx_byte;
static uint8_t  s_frame_buf[FPGA_FRAME_LEN];
static uint8_t  s_frame_idx;
static volatile uint8_t s_frame_ready;
static uint8_t  s_frame_ready_copy[FPGA_FRAME_LEN];

/* 벤치 테스트용 진단 카운터 - Keil Watch 창에서 지켜보는 용도.
 * 정상이면 valid_frame_count가 대략 초당 5씩(200ms heartbeat) 늘어난다. */
volatile uint32_t dbg_total_byte_count;
volatile uint32_t dbg_valid_frame_count;
volatile uint32_t dbg_checksum_fail_count;
volatile uint8_t  dbg_last_frame[FPGA_FRAME_LEN];

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void Error_Handler(void);

int main(void)
{
    const uint8_t addr[5] = WLINK_ADDR;

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_USART1_UART_Init();

    NRF24_Init(&hspi1, GPIOB, GPIO_PIN_6, GPIOC, GPIO_PIN_7, addr, WLINK_CHANNEL);
    NRF24_SetModeTX();

    /* 첫 바이트 수신 인터럽트를 걸어둔다. 이후는 콜백에서 계속 재무장. */
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);

    while (1) {
        if (s_frame_ready) {
            s_frame_ready = 0;
            /* 안전 판단은 RX노드의 워치독이 전담하므로 ACK 실패는 그냥
             * 무시하고 넘어간다 - 200ms 이내에 다음 heartbeat 프레임이
             * 또 온다. */
            (void)NRF24_Transmit(s_frame_ready_copy, FPGA_FRAME_LEN);
        }
    }
}

/* stm32f4xx_it.c에는 이 핸들러가 없다 (CubeMX NVIC 탭에서 USART1 global
 * interrupt를 안 켰기 때문 - Mode: Asynchronous만으로는 생성 안 됨).
 * huart1이 이 파일에 static으로 선언돼 있어서 여기 직접 두는 게 맞다 -
 * 약한 기본 벡터(startup 파일)를 이 강한 정의가 링크 시점에 덮어쓴다. */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    dbg_total_byte_count++;

    if (s_frame_idx == 0) {
        if (s_rx_byte == FPGA_FRAME_HEADER) {
            s_frame_buf[0] = s_rx_byte;
            s_frame_idx = 1;
        }
        /* 헤더가 아니면 그냥 버리고 계속 헤더를 찾는다 (동기화) */
    } else {
        s_frame_buf[s_frame_idx] = s_rx_byte;
        s_frame_idx++;
        if (s_frame_idx == FPGA_FRAME_LEN) {
            if (FpgaFrame_IsValid(s_frame_buf)) {
                for (uint8_t i = 0; i < FPGA_FRAME_LEN; i++) {
                    dbg_last_frame[i] = s_frame_buf[i];
                }
                dbg_valid_frame_count++;
                if (!s_frame_ready) {
                    for (uint8_t i = 0; i < FPGA_FRAME_LEN; i++) {
                        s_frame_ready_copy[i] = s_frame_buf[i];
                    }
                    s_frame_ready = 1;
                }
            } else {
                dbg_checksum_fail_count++;
            }
            s_frame_idx = 0;
        }
    }

    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* 리셋 기본값인 HSI 16MHz 그대로 사용 (PLL 불필요 - SPI/UART 부하가
     * 가벼워서 84MHz까지 올릴 이유가 없고, 클럭 트리 실수 리스크만
     * 줄어든다). */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* NRF24L01 CSN (PB6) - idle High */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* NRF24L01 CE (PC7) - idle Low */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static void MX_SPI1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* SCK=PA5(D13), MISO=PA6(D12), MOSI=PA7(D11).
     * PA5는 온보드 LED(LD2)와 공유 - SPI 통신 중 깜빡이는 건 정상. */
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    /* PCLK2=16MHz(HSI 그대로) / 4 = 4MHz - NRF24L01 SPI 한도(10MHz) 대비
     * 여유를 둬서 브레드보드 배선에서도 안정적으로 동작하게 함. */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_USART1_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    /* USART1_RX = PA10 (Arduino D2). FPGA uart_tx_o(핀41, 3.3V)를 여기에
     * 연결 + GND 공통. 수신 전용이라 PA9(TX)는 설정하지 않는다. */
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }

    /* HAL_UART_Receive_IT()는 주변장치 레벨 RXNEIE 비트만 켠다 - NVIC에서
     * 이 인터럽트를 CPU로 전달하게 별도로 활성화해야 콜백이 실제로
     * 불린다. 이게 빠지면 콜백이 영영 호출되지 않고 while(1) 루프만
     * 계속 돈다. */
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
