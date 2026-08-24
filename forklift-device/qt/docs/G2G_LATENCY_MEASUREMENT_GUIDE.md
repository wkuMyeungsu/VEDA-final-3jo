# Glass-to-Glass(G2G) 지연 실측 및 네트워크 패킷 분석 가이드

본 문서는 Hanwha Vision 멀티채널 IP 카메라(192.168.0.3)와 단말 애플리케이션(`operator_terminal`) 간의 영상 지연을 하드웨어 레벨에서 분리 계측하고 튜닝하기 위한 실측 가이드입니다.

---

## 🎯 측정 목표 및 산출물

1. **지연 분리 계측**:
   $$\text{전체 지연 (G2G)} = \text{카메라 파이프라인 지연 (센서/인코더)} + \text{네트워크 전송 지연} + \text{단말 디코딩/렌더링 지연}$$
2. **지터버퍼(Jitterbuffer) 최적값 도출**:
   * 카메라 GOP(I-frame 주기) 및 네트워크 지터 특성에 따른 최적 지연(`rtsp_latency_ms`) 산출.
3. **포트폴리오/보고서용 정량적 비교표 완성**:
   * 기준선(ffplay low_delay) vs 단말 앱(Custom GStreamer)
   * 버퍼 크기별(100ms vs 50ms vs 0ms) 지연 및 프레임 드랍 비교.

---

## 📋 필수 사전 준비 (도구 확인)

### 1. FFmpeg 도구 세트
```powershell
# 설치 확인
ffprobe -version
ffplay -version
```
* 미설치 시: `winget install Gyan.FFmpeg` 실행 후 새 터미널 오픈.

### 2. Wireshark / TShark
```powershell
# CLI 패킷 캡처 도구 확인
tshark -v
```
* 미설치 시: `winget install WiresharkFoundation.Wireshark` 설치 시 **TShark CLI 컴포넌트** 및 **Npcap** 체크 필수.

---

## ⏱️ 실측 세션 진행 순서 (약 1 ~ 1.5시간)

```mermaid
graph TD
    A["[1] 카메라 프로파일/GOP 확인 (ffprobe)"] --> B["[2] 촬영 환경 세팅 (240fps 슬로모션)"]
    B --> C["[3] 기준선 촬영 (ffplay 10회)"]
    C --> D["[4] 단말 앱 촬영 (기본 10회)"]
    D --> E["[5] Wireshark 패킷 캡처 (유선/무선)"]
    E --> F["[6] 지터버퍼 A/B 비교 (100ms / 50ms / 0ms)"]
```

---

### Step 1. 카메라 스펙 및 프로파일 확인 (1회 실행)

카메라 채널 0(CAM_01_CH_01)의 profile1 / profile2 / profile3 스펙을 순서대로 확인하여 기록합니다.

```powershell
# Profile 1 확인
ffprobe -rtsp_transport tcp -i "rtsp://admin:5hanwha%21@192.168.0.3:554/0/onvif/profile1/media.smp"

# Profile 2 확인 (단말 기본값)
ffprobe -rtsp_transport tcp -i "rtsp://admin:5hanwha%21@192.168.0.3:554/0/onvif/profile2/media.smp"

# Profile 3 확인
ffprobe -rtsp_transport tcp -i "rtsp://admin:5hanwha%21@192.168.0.3:554/0/onvif/profile3/media.smp"
```

> **기록 항목**: 해상도(Resolution), FPS, 코덱(H.264 / H.265), 비트레이트.

---

### Step 2. GOP(I-frame 간격) 측정

I-frame 간격은 카메라의 하드웨어 지연 및 최악 복구 지연의 하한선을 결정합니다.

```powershell
# 첫 100개 프레임의 픽처 타입(I / P / B) 추출
ffprobe -rtsp_transport tcp -select_streams v -show_frames -show_entries frame=pict_type -of csv "rtsp://admin:5hanwha%21@192.168.0.3:554/0/onvif/profile2/media.smp" | Select-Object -First 100
```

* **GOP 계산법**: `I` 프레임과 다음 `I` 프레임 사이의 `P` 프레임 개수 + 1. (예: 30fps 환경에서 GOP 30이면 1.0초 주기).

---

### Step 3. 240fps 슬로모션 촬영 세팅

1. **스톱워치 배치**: 밀리초(ms) 단위까지 표시되는 고정밀 온라인 스톱워치(예: `https://stopwatch.online/` 또는 태블릿 앱)를 카메라가 정면으로 비추도록 배치.
2. **카메라/폰 고정**: 스마트폰(240fps 슬로모션 모드 지원 기종)을 거치대에 고정하여 **"스톱워치 원본 화면"**과 **"모니터(단말/ffplay) 표출 화면"**이 한 프레임 안에 동시에 나오도록 구도 조정.
3. **조명 확보**: 240fps 슬로모션 촬영은 셔터 스피드가 매우 빠르므로 조명을 충분히 밝게 설정.

---

### Step 4. 기준선(Baseline) 촬영: `ffplay` (10회)

> ⚠️ **주의**: ffplay와 단말 앱을 동시에 띄우지 마세요. 카메라 RTSP 동시 세션 경합을 방지하기 위해 반드시 하나씩 실행합니다.

```powershell
# 저지연 기준선 재생
ffplay -rtsp_transport tcp -fflags nobuffer -flags low_delay -probesize 32 -analyzeduration 0 "rtsp://admin:5hanwha%21@192.168.0.3:554/0/onvif/profile2/media.smp"
```
* **촬영**: 240fps 슬로모션으로 10회 촬영 (동영상 파일명: `ffplay_tcp_01.mp4` ~ `ffplay_tcp_10.mp4`).

---

### Step 5. 단말 앱 기준선 촬영: `operator_terminal` (10회)

`ffplay`를 종료한 후, 단말 애플리케이션을 구동합니다.

```powershell
cd c:\VEDA_Final_project\forklift-device\qt
.\build\windows-mingw\operator_terminal.exe
```
* **촬영**: 같은 환경에서 단말 화면을 240fps 슬로모션으로 10회 촬영 (동영상 파일명: `app_tcp_lat100_01.mp4` ~ `app_tcp_lat100_10.mp4`).

---

### Step 6. Wireshark / TShark 패킷 캡처 (유선 vs 무선)

RTP 패킷 지터 및 손실 분석을 위해 UDP 전송 모드로 패킷을 캡처합니다.

1. **[`config/cameras.json`](file:///c:/VEDA_Final_project/forklift-device/qt/config/cameras.json)에서 프로토콜을 UDP로 변경**:
   * `"rtsp_protocols": "udp"` 로 수정 (재빌드 없이 앱 재시작만으로 적용).
2. **TShark 인터페이스 확인 및 캡처 시작**:
```powershell
# 네트워크 인터페이스 번호 확인
tshark -D

# 유선 캡처 (카메라 IP 192.168.0.3 필터링)
tshark -i <인터페이스번호> -f "host 192.168.0.3" -w rtsp_wired.pcap

# 무선 Wi-Fi 전환 후 무선 캡처
tshark -i <무선인터페이스번호> -f "host 192.168.0.3" -w rtsp_wireless.pcap
```
3. **캡처 완료 후**: `cameras.json`의 `"rtsp_protocols"`를 다시 `"tcp"`로 복구.

---

### Step 7. 지터버퍼 A/B 테스트 (rtsp_latency_ms 튜닝)

[`config/cameras.json`](file:///c:/VEDA_Final_project/forklift-device/qt/config/cameras.json)의 `rtsp_latency_ms` 파라미터를 변경해가며 각 10회씩 촬영합니다:

| 조건 | `rtsp_latency_ms` 설정값 | 동영상 파일명 규칙 | 비고 |
| :--- | :--- | :--- | :--- |
| **조건 1** | `100` (기본값) | `app_tcp_lat100_01.mp4` ~ `10.mp4` | 지터 완충 기본 |
| **조건 2** | `50` | `app_tcp_lat050_01.mp4` ~ `10.mp4` | 저지연 튜닝 |
| **조건 3** | `0` | `app_tcp_lat000_01.mp4` ~ `10.mp4` | 버퍼 제로 극한 저지연 |

---

## 📊 결과 기록 및 분석 시트 템플릿

슬로모션 동영상을 정지화면으로 멈춘 후:
$$\text{지연 시간 (G2G)} = \text{원본 스톱워치 시각} - \text{모니터 표출 스톱워치 시각}$$

### 1. 실측 데이터 기록표 (ms)
| 시도 | ffplay (Baseline) | 앱 (lat=100ms) | 앱 (lat=50ms) | 앱 (lat=0ms) |
| :---: | :---: | :---: | :---: | :---: |
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |
| 5 | | | | |
| 6 | | | | |
| 7 | | | | |
| 8 | | | | |
| 9 | | | | |
| 10 | | | | |
| **중앙값(Median)** | | | | |
| **최악값(Max)** | | | | |
| **표준편차** | | | | |

---

## 💡 분석 및 포트폴리오 활용 팁

1. **지연 분리 근거**:
   * $\text{카메라 파이프라인 지연} \approx \text{ffplay 최솟값} - \text{네트워크 RTT}$
   * $\text{단말 순수 지연} = \text{앱 G2G 지연} - \text{ffplay G2G 지연}$
2. **안전성 기준**:
   * 지게차 안전 시스템의 핵심 지표는 "평균값"이 아닌 **"최악값(Max / Worst-case)"**입니다.
   * `lat=0ms`에서 패킷 유실 시 화면 찢어짐(Artifact)이나 끊김 발생 여부를 관찰하고, 안정적인 최적 지연 구간(예: 30~50ms)을 엔지니어링 근거로 제시합니다.
3. **주의사항**:
   * `.pcap` 패킷 파일에는 카메라의 RTSP 인증 정보(`admin:5hanwha!`)가 평문 Base64로 포함되어 있으므로 외부 공개 시 주의합니다.
