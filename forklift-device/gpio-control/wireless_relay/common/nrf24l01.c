#include "nrf24l01.h"

/* ---- 커맨드 ---- */
#define CMD_R_REGISTER    0x00 /* | reg(5bit) */
#define CMD_W_REGISTER    0x20 /* | reg(5bit) */
#define CMD_R_RX_PAYLOAD  0x61
#define CMD_W_TX_PAYLOAD  0xA0
#define CMD_FLUSH_TX      0xE1
#define CMD_FLUSH_RX      0xE2
#define CMD_NOP           0xFF

/* ---- 레지스터 ---- */
#define REG_CONFIG      0x00
#define REG_EN_AA       0x01
#define REG_EN_RXADDR   0x02
#define REG_SETUP_AW    0x03
#define REG_SETUP_RETR  0x04
#define REG_RF_CH       0x05
#define REG_RF_SETUP    0x06
#define REG_STATUS      0x07
#define REG_RX_ADDR_P0  0x0A
#define REG_TX_ADDR     0x10
#define REG_RX_PW_P0    0x11
#define REG_FIFO_STATUS 0x17
#define REG_DYNPD       0x1C
#define REG_FEATURE     0x1D

/* ---- STATUS 비트 ---- */
#define STATUS_RX_DR  0x40
#define STATUS_TX_DS  0x20
#define STATUS_MAX_RT 0x10

/* ---- FIFO_STATUS 비트 ---- */
#define FIFO_STATUS_RX_EMPTY 0x01

/* 오토ACK 대기 타임아웃: ARD=750us x ARC=15회 재전송 최악의 경우
 * (~16.5ms) 대비 여유를 둔 값. 이 값 이상 걸리면 상대가 없다고 보고
 * 포기한다 - 200ms 주기로 다시 시도하므로 여기서 오래 블로킹할 필요 없다. */
#define TX_WAIT_TIMEOUT_MS 25

/* 이 링크는 fpga_frame.h의 FPGA_FRAME_LEN(5바이트) 고정 페이로드 하나만
 * 다룬다. nrf24l01.c는 fpga_frame.h에 의존하지 않도록 값만 로컬 상수로
 * 복제해둔다 (fpga_frame.h가 바뀌면 여기도 같이 확인할 것). */
#define NRF24_STATIC_PAYLOAD_LEN 5

static SPI_HandleTypeDef *s_hspi;
static GPIO_TypeDef *s_csn_port;
static uint16_t       s_csn_pin;
static GPIO_TypeDef *s_ce_port;
static uint16_t       s_ce_pin;

static void CsnLow(void)  { HAL_GPIO_WritePin(s_csn_port, s_csn_pin, GPIO_PIN_RESET); }
static void CsnHigh(void) { HAL_GPIO_WritePin(s_csn_port, s_csn_pin, GPIO_PIN_SET); }
static void CeLow(void)   { HAL_GPIO_WritePin(s_ce_port, s_ce_pin, GPIO_PIN_RESET); }
static void CeHigh(void)  { HAL_GPIO_WritePin(s_ce_port, s_ce_pin, GPIO_PIN_SET); }

static uint8_t SpiXfer(uint8_t data)
{
    uint8_t rx = 0;
    HAL_SPI_TransmitReceive(s_hspi, &data, &rx, 1, HAL_MAX_DELAY);
    return rx;
}

static uint8_t ReadReg(uint8_t reg)
{
    uint8_t val;
    CsnLow();
    SpiXfer(CMD_R_REGISTER | (reg & 0x1F));
    val = SpiXfer(CMD_NOP);
    CsnHigh();
    return val;
}

static void WriteReg(uint8_t reg, uint8_t val)
{
    CsnLow();
    SpiXfer(CMD_W_REGISTER | (reg & 0x1F));
    SpiXfer(val);
    CsnHigh();
}

static void WriteRegMulti(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    CsnLow();
    SpiXfer(CMD_W_REGISTER | (reg & 0x1F));
    for (i = 0; i < len; i++) {
        SpiXfer(buf[i]);
    }
    CsnHigh();
}

static uint8_t ReadStatus(void)
{
    uint8_t status;
    CsnLow();
    status = SpiXfer(CMD_NOP);
    CsnHigh();
    return status;
}

static void FlushTx(void) { CsnLow(); SpiXfer(CMD_FLUSH_TX); CsnHigh(); }
static void FlushRx(void) { CsnLow(); SpiXfer(CMD_FLUSH_RX); CsnHigh(); }

void NRF24_Init(SPI_HandleTypeDef *hspi,
                 GPIO_TypeDef *csn_port, uint16_t csn_pin,
                 GPIO_TypeDef *ce_port, uint16_t ce_pin,
                 const uint8_t addr[5], uint8_t channel)
{
    s_hspi = hspi;
    s_csn_port = csn_port;
    s_csn_pin  = csn_pin;
    s_ce_port  = ce_port;
    s_ce_pin   = ce_pin;

    CeLow();
    CsnHigh();

    /* 데이터시트: VDD 인가 후 첫 SPI 명령까지 100ms 이상 대기 권장. */
    HAL_Delay(100);

    WriteReg(REG_CONFIG, 0x00);       /* 파워다운, 이후 모드 진입 시 재설정 */
    WriteReg(REG_EN_AA, 0x01);        /* 파이프0 오토ACK 활성 */
    WriteReg(REG_EN_RXADDR, 0x01);    /* 파이프0만 사용 */
    WriteReg(REG_SETUP_AW, 0x03);     /* 5바이트 주소 */
    WriteReg(REG_SETUP_RETR, 0x2F);   /* ARD=750us, ARC=15회 */
    WriteReg(REG_RF_CH, channel);
    WriteReg(REG_RF_SETUP, 0x26);     /* 250kbps, 0dBm (사거리/안정성 우선) */
    WriteRegMulti(REG_RX_ADDR_P0, addr, 5);
    WriteRegMulti(REG_TX_ADDR, addr, 5);
    WriteReg(REG_RX_PW_P0, NRF24_STATIC_PAYLOAD_LEN);
    WriteReg(REG_DYNPD, 0x00);        /* 다이나믹 페이로드 미사용 */
    WriteReg(REG_FEATURE, 0x00);
    WriteReg(REG_STATUS, STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT);

    FlushTx();
    FlushRx();
}

void NRF24_SetModeTX(void)
{
    CeLow();
    /* EN_CRC + CRCO(16bit) + PWR_UP, PRIM_RX=0 (PTX) */
    WriteReg(REG_CONFIG, 0x0E);
    HAL_Delay(2); /* PWR_UP 후 스타트업 지연(>=1.5ms) */
}

void NRF24_SetModeRX(void)
{
    /* EN_CRC + CRCO(16bit) + PWR_UP + PRIM_RX(PRX) */
    WriteReg(REG_CONFIG, 0x0F);
    HAL_Delay(2);
    CeHigh(); /* 상시 리스닝 */
}

uint8_t NRF24_Transmit(const uint8_t *payload, uint8_t len)
{
    uint8_t i;
    uint32_t start;
    uint8_t status;

    CeLow();
    FlushTx();
    WriteReg(REG_STATUS, STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT);

    CsnLow();
    SpiXfer(CMD_W_TX_PAYLOAD);
    for (i = 0; i < len; i++) {
        SpiXfer(payload[i]);
    }
    CsnHigh();

    CeHigh();
    HAL_Delay(1); /* CE High >=10us 요구조건을 넉넉히 충족 */
    CeLow();

    start = HAL_GetTick();
    do {
        status = ReadStatus();
        if (status & STATUS_TX_DS) {
            WriteReg(REG_STATUS, STATUS_TX_DS);
            return 1;
        }
        if (status & STATUS_MAX_RT) {
            WriteReg(REG_STATUS, STATUS_MAX_RT);
            FlushTx();
            return 0;
        }
    } while ((HAL_GetTick() - start) < TX_WAIT_TIMEOUT_MS);

    return 0;
}

uint8_t NRF24_Available(void)
{
    return (ReadReg(REG_FIFO_STATUS) & FIFO_STATUS_RX_EMPTY) ? 0 : 1;
}

void NRF24_Receive(uint8_t *payload, uint8_t len)
{
    uint8_t i;
    CsnLow();
    SpiXfer(CMD_R_RX_PAYLOAD);
    for (i = 0; i < len; i++) {
        payload[i] = SpiXfer(CMD_NOP);
    }
    CsnHigh();
    WriteReg(REG_STATUS, STATUS_RX_DR);
}
