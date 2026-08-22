#ifndef FPGA_FRAME_H
#define FPGA_FRAME_H

#include <stdint.h>

/*
 * FPGA -> Pi push 프레임(FPGA_control/src/PROTOCOL.md 2.1절)과 완전히 동일한
 * 5바이트 레이아웃. 새 포맷을 만들지 않고 이걸 그대로 무선으로 중계한다.
 *
 * [0] 0x55 헤더
 * [1] event_code
 * [2] event_detail
 * [3] {overflow(1bit), movement_cutoff_active(1bit), 미사용(3bit), seq(3bit)}
 * [4] checksum = XOR([0]..[3])
 */
#define FPGA_FRAME_LEN     5
#define FPGA_FRAME_HEADER  0x55

/* byte[3]의 movement_cutoff_active 비트 (bit6) */
#define FPGA_FRAME_CUTOFF_BIT 0x40

/* 헤더 + XOR 체크섬이 둘 다 맞는지 검증. TX/RX 양쪽에서 각자 재검증한다
 * (TX노드: FPGA UART 탭이 실제로 깨끗한지, RX노드: 라디오 페이로드가
 * 손상 없이 왔는지 - 이중 방어). */
uint8_t FpgaFrame_IsValid(const uint8_t frame[FPGA_FRAME_LEN]);

/* frame[3]의 movement_cutoff_active 비트만 추출 */
uint8_t FpgaFrame_CutoffActive(const uint8_t frame[FPGA_FRAME_LEN]);

#endif /* FPGA_FRAME_H */
