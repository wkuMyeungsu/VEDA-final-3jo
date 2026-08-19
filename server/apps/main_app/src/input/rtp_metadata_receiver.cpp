// rtp_metadata_receiver.cpp
#include "input/rtp_metadata_receiver.hpp"
#include <iostream>

bool parseRtpHeader(const uint8_t* buffer, size_t size, RtpHeaderInfo& outInfo) {
    // 최소 12바이트(고정 헤더 크기)는 있어야 파싱 가능
    if (size < 12) return false;

    uint8_t byte0 = buffer[0];
    uint8_t byte1 = buffer[1];

    uint8_t version = (byte0 >> 6) & 0x03;
    if (version != 2) return false;  // RTP는 항상 버전 2

    bool hasExtension = (byte0 & 0x10) != 0;
    uint8_t csrcCount  = byte0 & 0x0F;

    outInfo.marker         = (byte1 & 0x80) != 0;
    outInfo.payloadType    = byte1 & 0x7F;
    outInfo.sequenceNumber = (static_cast<uint16_t>(buffer[2]) << 8) | buffer[3];

    size_t headerLen = 12 + static_cast<size_t>(csrcCount) * 4;

    // Extension 헤더가 있으면 (X비트=1) 추가로 건너뛰어야 함:
    // extension은 4바이트 헤더(2바이트 프로필ID + 2바이트 길이) + length*4바이트 데이터
    if (hasExtension) {
        if (size < headerLen + 4) return false;
        uint16_t extLenWords = (static_cast<uint16_t>(buffer[headerLen + 2]) << 8)
                              | buffer[headerLen + 3];
        headerLen += 4 + static_cast<size_t>(extLenWords) * 4;
    }

    if (size < headerLen) return false;  // 헤더보다 패킷이 짧으면 손상된 것으로 간주

    outInfo.headerLength = headerLen;
    return true;
}

std::optional<std::string> OnvifMetadataReassembler::feed(
    const uint8_t* payload, size_t payloadSize, uint16_t sequenceNumber, bool marker) {

    // 시퀀스 번호 연속성 확인.
    // RTP 시퀀스는 16비트라 65535 다음엔 0으로 감싸 도는데, uint16_t 덧셈이 자연스럽게
    // 그 wraparound를 처리해주므로 별도 분기 없이 expected 계산이 그대로 맞음.
    if (hasLastSeq_) {
        uint16_t expected = static_cast<uint16_t>(lastSeq_ + 1);
        if (sequenceNumber != expected) {
            // 패킷이 하나 이상 빠졌다는 뜻 -> 지금까지 모은 조각은 이미 깨진 상태이므로
            // 그대로 이어붙이면 잘못된 XML을 파서에 넘기게 됨. 버리고 이 패킷부터 새로 시작.
            std::cerr << "[경고] RTP 시퀀스 갭 감지 (expected=" << expected
                      << ", got=" << sequenceNumber << "). 지금까지 모은 프레임 폐기 후 재시작.\n";
            buffer_.clear();
        }
    }
    lastSeq_ = sequenceNumber;
    hasLastSeq_ = true;

    // 조각을 버퍼에 이어붙임
    buffer_.append(reinterpret_cast<const char*>(payload), payloadSize);

    if (!marker) {
        // 아직 문서가 안 끝남 -> 다음 패킷을 기다림
        return std::nullopt;
    }

    // marker=true 패킷까지 왔다 -> 지금까지 모은 게 완성된 XML 문서 하나
    std::string completed = std::move(buffer_);
    buffer_.clear();  // 다음 문서를 위해 초기화
    return completed;
}

void OnvifMetadataReassembler::reset() {
    buffer_.clear();
    lastSeq_ = 0;
    hasLastSeq_ = false;
}
