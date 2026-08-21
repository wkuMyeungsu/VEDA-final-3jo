/*
 * RX노드 (릴레이 쪽) - NUCLEO-F401RE
 *
 * NRF24L01로 TX노드가 중계한 FPGA 5바이트 프레임을 받아서, 그 안의
 * movement_cutoff_active 비트(byte[3] bit6, PROTOCOL.md 2.1절)로 릴레이
 * 인에이블 GPIO(PB10, Arduino D6)를 그대로 미러링한다. 이 핀은 기존
 * FPGA_control/src/PROTOCOL.md 3.1절의 트랜지스터 베이스 저항으로 연결 -
 * 회로 자체는 그대로, 구동원만 FPGA GPIO에서 이 보드로 바뀐다.
 *
 * 안전 정책(사용자 확정, wireless_relay 계획 참고): 마지막 유효 프레임
 * 이후 WLINK_WATCHDOG_MS(600ms) 이상 아무것도 못 받으면, 수신 비트값과
 * 무관하게 강제로 컷오프를 건다. 부팅 직후 첫 프레임을 받기 전까지도
 * 동일하게 컷오프 상태로 시작한다 - "링크가 있다고 확인되기 전까지는
 * 안전하지 않다고 간주"하는 정책의 자연스러운 연장.
 *
 * 순수 폴링 루프로 구현 (인터럽트 없음) - 200ms 주기 링크에 폴링이면
 * 충분하고, 안전 관련 코드는 상태가 적을수록 감사하기 쉽다.
 *
 * 프로젝트 생성 방법은 tx_node/Core/Src/main.c 상단 주석과
 * wireless_relay/PROTOCOL_WIRELESS.md 참고 (이 보드는 USART 불필요,
 * SPI1만 CubeMX Pinout에서 Mode 활성화하면 됨).
 */

#include "stm32f4xx_hal.h"
#include "nrf24l01.h"
#include "fpga_frame.h"
#include "wireless_link_config.h"

#define RELAY_GPIO_PORT GPIOB
#define RELAY_GPIO_PIN  GPIO_PIN_10

static SPI_HandleTypeDef hspi1;

/* 벤치 테스트용 진단 카운터 - Keil Watch 창에서 지켜보는 용도. */
volatile uint32_t dbg_rx_packet_count;    /* NRF24L01이 뭐라도 받은 횟수 */
volatile uint32_t dbg_valid_frame_count;  /* 그 중 체크섬 통과한 횟수 */
volatile uint32_t dbg_checksum_fail_count;
volatile uint8_t  dbg_last_frame[FPGA_FRAME_LEN];
volatile uint8_t  dbg_link_ok;      /* 워치독 판단 결과 (1=정상) */
volatile uint8_t  dbg_relay_cutoff; /* 현재 릴레이 출력 상태 (1=차단) */
volatile uint32_t dbg_ms_since_last_frame; /* 마지막 유효 프레임 이후 경과(ms) -
                                             * 이 값이 WLINK_WATCHDOG_MS(600)을
                                             * 넘는 순간 dbg_relay_cutoff=1로
                                             * 바뀌는지 Watch 창에서 그대로 확인 가능 */
volatile uint32_t dbg_cutoff_trigger_elapsed_ms; /* 워치독이 실제로 타임아웃을
                                             * 판정한 그 순간의 elapsed 값을
                                             * 한 번만 캡처 - Watch 창 갱신
                                             * 주기(~0.5s)에 쫓기지 않고
                                             * 나중에 그냥 읽으면 됨 */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void Error_Handler(void);

static void SetRelayCutoff(uint8_t cutoff)
{
    /* movement_cutoff_active=1 -> 코일 여자 -> NC 열림 -> 전진 차단.
     * PROTOCOL.md 3.1절 표와 동일한 극성. */
    HAL_GPIO_WritePin(RELAY_GPIO_PORT, RELAY_GPIO_PIN,
                       cutoff ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

int main(void)
{
    const uint8_t addr[5] = WLINK_ADDR;
    uint8_t payload[FPGA_FRAME_LEN];
    uint8_t got_first_frame = 0;
    uint32_t last_valid_tick = 0;
    uint8_t  cutoff_bit_cached = 1; /* 첫 프레임 전까지는 안전 쪽(차단)으로 */
    uint8_t  prev_link_ok = 0; /* link_ok의 실제 초기값(0, got_first_frame=0)과
                                 * 맞춰야 부팅 직후 첫 판정을 가짜 전환으로
                                 * 오인식하지 않는다 */

    HAL_Init();
    SystemClock_Config();

    /* 라디오/그 무엇보다 먼저 릴레이를 컷오프 상태로 못박는다 - 계획에
     * 명시된 "부팅 시 기본값" 정책. */
    MX_GPIO_Init();
    SetRelayCutoff(1);

    MX_SPI1_Init();

    NRF24_Init(&hspi1, GPIOB, GPIO_PIN_6, GPIOC, GPIO_PIN_7, addr, WLINK_CHANNEL);
    NRF24_SetModeRX();

    while (1) {
        while (NRF24_Available()) {
            NRF24_Receive(payload, FPGA_FRAME_LEN);
            dbg_rx_packet_count++;
            if (FpgaFrame_IsValid(payload)) {
                last_valid_tick = HAL_GetTick();
                cutoff_bit_cached = FpgaFrame_CutoffActive(payload);
                got_first_frame = 1;
                dbg_valid_frame_count++;
                for (uint8_t i = 0; i < FPGA_FRAME_LEN; i++) {
                    dbg_last_frame[i] = payload[i];
                }
            } else {
                dbg_checksum_fail_count++;
            }
            /* 체크섬 불일치면 조용히 버린다 - last_valid_tick을 안 건드리므로
             * 손상된 프레임이 계속 와도 워치독은 정상적으로 결국 컷오프로
             * 넘어간다 (안전 쪽으로 수렴). */
        }

        {
            uint32_t elapsed = HAL_GetTick() - last_valid_tick;
            uint8_t link_ok = got_first_frame && (elapsed < WLINK_WATCHDOG_MS);
            dbg_ms_since_last_frame = elapsed;
            dbg_link_ok = link_ok;
            if (prev_link_ok && !link_ok) {
                /* 링크가 있다가 워치독이 막 타임아웃 판정한 그 순간 - 1회 캡처 */
                dbg_cutoff_trigger_elapsed_ms = elapsed;
            }
            prev_link_ok = link_ok;
            dbg_relay_cutoff = !link_ok || cutoff_bit_cached;
            SetRelayCutoff(dbg_relay_cutoff);
        }
    }
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

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

    /* 릴레이 인에이블 출력 (PB10, D6) - 라디오 초기화 전에 먼저 SET */
    HAL_GPIO_WritePin(RELAY_GPIO_PORT, RELAY_GPIO_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = RELAY_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RELAY_GPIO_PORT, &GPIO_InitStruct);

    /* NRF24L01 CSN (PB6) - idle High */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* NRF24L01 CE (PC7) - idle Low (SetModeRX가 High로 올림) */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static void MX_SPI1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

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
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        Error_Handler();
    }
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
