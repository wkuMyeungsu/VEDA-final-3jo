// rtp_metadata_receiver.hpp
//
// 입력값: GStreamer appsink에서 받은 raw RTP 패킷 (RTP 헤더 + payload)
// 처리과정: RTP 헤더를 파싱해 payload만 추출 -> 시퀀스 번호 연속성 확인 -> 계속 이어붙임
//           -> marker bit(M=1)이 뜨면 "이 XML 문서가 끝났다"는 뜻이므로 완성된 문자열로 반환
//           중간에 시퀀스 번호가 끊기면(패킷 유실) 지금까지 모은 조각은 깨진 것으로 보고 버림
// 출력값: 완성된 ONVIF 메타데이터 XML 문자열 (그대로 parseOnvifMetadata()에 넘기면 됨)
#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

// RTP 헤더 하나를 파싱한 결과.
// RFC 3550 기준: 표준 헤더는 12바이트, CSRC 리스트가 있으면 그만큼 더 붙음.
struct RtpHeaderInfo {
    bool marker = false;      // true면 "이 패킷이 현재 XML 문서의 마지막 조각"이라는 뜻
    uint8_t payloadType = 0;
    uint16_t sequenceNumber = 0;
    size_t headerLength = 0;  // 이 값만큼 건너뛰면 payload 시작 지점
};

// buffer/size: appsink에서 받은 RTP 패킷 원본 바이트
// 반환값: 파싱 성공 여부. 실패(너무 짧은 패킷 등)면 false.
bool parseRtpHeader(const uint8_t* buffer, size_t size, RtpHeaderInfo& outInfo);

// 여러 RTP 패킷에 걸쳐 조각난 XML을 이어붙이는 누적기.
// 카메라 -> RTP 패킷 여러 개 -> feed()를 패킷마다 호출 -> marker bit 뜨는 패킷에서
// 완성된 XML 문자열이 반환됨 (그 전까지는 std::nullopt).
class OnvifMetadataReassembler {
public:
    // payload: RTP 헤더를 뺀 순수 데이터 조각
    // sequenceNumber: 이 패킷의 RTP 시퀀스 번호 (유실 감지용)
    // marker: 이 패킷의 RTP marker bit
    // 반환값: marker=true인 패킷 처리 후 완성된 XML 문자열. 아직 안 끝났으면 nullopt.
    //         시퀀스 갭이 감지되면 그 시점까지 모은 조각은 버리고 새로 시작함(경고 로그 출력).
    std::optional<std::string> feed(const uint8_t* payload, size_t payloadSize,
                                     uint16_t sequenceNumber, bool marker);

    // 스트림이 끊기거나 재연결될 때 시퀀스 번호 및 버퍼를 초기화한다.
    void reset();

private:
    std::string buffer_;       // 지금까지 이어붙인 조각들
    uint16_t lastSeq_ = 0;     // 마지막으로 본 시퀀스 번호
    bool hasLastSeq_ = false;  // lastSeq_가 유효한 값인지 (첫 패킷 이전엔 false)
    std::size_t sequence_gap_count_ = 0;
    std::size_t last_logged_sequence_gap_ = 0;
    static constexpr std::size_t kSequenceGapLogInterval = 100;
};
