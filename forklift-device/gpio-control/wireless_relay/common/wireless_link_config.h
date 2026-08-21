#ifndef WIRELESS_LINK_CONFIG_H
#define WIRELESS_LINK_CONFIG_H

/*
 * TX노드/RX노드 양쪽이 반드시 동일해야 하는 무선 링크 상수.
 * 한쪽만 고치고 다른 쪽을 안 고치면 링크가 아예 안 붙으므로
 * 이 파일 하나로 양쪽 프로젝트에 동일하게 복사해서 쓴다.
 */

/* 5바이트 라디오 주소 ("RLY01") - TX_ADDR == RX_ADDR_P0 (오토ACK 필수 조건) */
#define WLINK_ADDR { 0x52, 0x4C, 0x59, 0x30, 0x31 }

/* 2.4GHz 채널. 76 = 2.476GHz, 일반 Wi-Fi 1~11채널(~2.412~2.462GHz) 위쪽이라
 * 혼잡이 상대적으로 덜하다. 현장에서 링크가 자꾸 끊기면 이 값부터 바꿔볼 것. */
#define WLINK_CHANNEL 76

/* FPGA heartbeat 주기(200ms, PROTOCOL.md 2.1절)의 3배 마진.
 * 이 시간 동안 유효 프레임을 하나도 못 받으면 RX노드가 강제 컷오프. */
#define WLINK_WATCHDOG_MS 600

#endif /* WIRELESS_LINK_CONFIG_H */
