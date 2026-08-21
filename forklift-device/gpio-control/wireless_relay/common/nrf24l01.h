#ifndef NRF24L01_H
#define NRF24L01_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/*
 * 레지스터 레벨 최소 NRF24L01(+) 드라이버.
 *
 * 이 링크 용도로 필요한 것만 구현: 고정 5바이트 페이로드, 파이프0 하나,
 * Enhanced ShockBurst 오토ACK/오토재전송. 벤더/Arduino 라이브러리를 쓰지
 * 않고 직접 작성한 이유는 FPGA_control 프로젝트 전체가 UART/워치독 등을
 * 전부 손으로 구현해온 것과 같은 기조 - 안전 관련 코드는 작고 감사
 * 가능해야 한다.
 *
 * 라디오 인스턴스는 보드당 하나뿐이라 멀티 인스턴스 API로 만들지 않고
 * 파일 스코프 static 상태를 쓴다.
 */

/* CSN/CE 핀을 지정해서 초기화. 이 링크에서는 TX/RX노드 둘 다
 * CSN=PB6, CE=PC7로 동일하게 배선하기로 했다 (wireless_relay 계획 참고). */
void NRF24_Init(SPI_HandleTypeDef *hspi,
                 GPIO_TypeDef *csn_port, uint16_t csn_pin,
                 GPIO_TypeDef *ce_port, uint16_t ce_pin,
                 const uint8_t addr[5], uint8_t channel);

/* PTX(송신) 모드로 전환. */
void NRF24_SetModeTX(void);

/* PRX(수신) 모드로 전환하고 CE를 계속 High로 유지 (상시 리스닝). */
void NRF24_SetModeRX(void);

/* len(<=5)바이트를 보내고 ACK를 기다린다 (최대 약 25ms 블로킹).
 * 반환 1 = 상대가 ACK함, 0 = MAX_RT(재전송 전부 실패) 또는 타임아웃.
 * 이 링크의 안전 판단은 RX노드의 자체 워치독이 담당하므로, 이 반환값은
 * TX노드 쪽 진단 용도일 뿐이다. */
uint8_t NRF24_Transmit(const uint8_t *payload, uint8_t len);

/* RX FIFO에 읽을 페이로드가 있는지 (PRX 모드에서 폴링용). */
uint8_t NRF24_Available(void);

/* RX FIFO에서 len(<=5)바이트를 꺼낸다. NRF24_Available()이 1일 때만 호출. */
void NRF24_Receive(uint8_t *payload, uint8_t len);

#endif /* NRF24L01_H */
