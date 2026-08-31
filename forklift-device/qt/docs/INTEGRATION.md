# 실장비 연동 가이드 (forklift-device/qt)

모든 통신(RTSP, MQTT, FPGA UART) 모듈이 C++로 구현 완료되어 있으므로, **재빌드 없이 `config/*.json` 설정 수정만으로 실제 장비에 즉시 연결**됩니다.

---

## 1. 한화비전 4채널 멀티센서 카메라 연동 (`config/cameras.json`)
`source_type`을 `"rtsp"`로 변경하고 카메라 RTSP 주소를 입력합니다:
```json
{
  "stream_id": "CAM_01_CH_01",
  "camera_id": "CAM_01",
  "channel": 1,
  "name": "CH 1 (창고 입구)",
  "zone": "ZONE_A",
  "source_type": "rtsp",
  "rtsp_url": "rtsp://USERNAME:PASSWORD@CAMERA_IP:554/0/onvif/profile2/media.smp"
}
```

---

## 2. 중앙 안전 서버 MQTT 연동 (`config/terminal.json`)
`metadata_source_type`을 `"mqtt"`로 변경하고 라즈베리파이 서버 IP(`192.168.0.13`)를 지정합니다:
```json
{
  "mqtt_broker_host": "192.168.0.13",
  "mqtt_broker_port": 1883,
  "terminal_id": "TERM_01",
  "metadata_source_type": "mqtt"
}
```

---

## 3. Gowin FPGA 시리얼 연동 (`config/terminal.json`)
`warning_device_type`을 `"serial"`로 변경하고 라즈베리파이 GPIO 직결 UART 포트(`/dev/serial0`)를 지정합니다:
```json
{
  "fpga_serial_port": "/dev/serial0",
  "fpga_baud_rate": 115200,
  "warning_device_type": "serial"
}
```
