<#
.SYNOPSIS
  단말 결함 주입 및 동작 검증을 위한 Mock 위험 이벤트 MQTT 서버 스크립트.

.DESCRIPTION
  - 토픽: forklift/risk/{terminal_id}
  - 페이로드: 실제 서버(danger_judgment_engine)와 100% 동일한 JSON 형식
    {"utc_time":"...","camera_id":"...","zone":"...","terminal_id":"...",
     "exception_state":"NONE","distance_mm":1230,"distance_m":1.23,"risk_level":0}
  - 외부 의존성 없이 순수 .NET TCP/MQTT 3.1.1 프로토콜로 브로커에 직접 연결/발행.

.PARAMETER BrokerHost
  MQTT 브로커 호스트 주소 (기본값: 'localhost')

.PARAMETER BrokerPort
  MQTT 브로커 포트 (기본값: 1883)

.PARAMETER TerminalId
  대상 단말기 식별자 (기본값: 'TERM_01')

.PARAMETER Scenario
  시나리오 모드:
    - cycle   : 200ms 주기로 Safe -> Caution -> Danger -> Emergency -> Safe 순환 (정상 동작 기준선)
    - silence : 브로커 접속만 유지하고 메시지 무발행 (워치독 1000ms 만료 검증용)
    - stale   : 10초 전 과거 시각 찍힌 retained 메시지 1건 발행 후 종료 (접속 시 폐기 검증용)
    - manual  : 콘솔에서 Enter 누를 때마다 다음 위험도로 수동 전환 (200ms 하트비트 유지)

.SAFETY
  - stale(retained) 모드는 localhost/127.0.0.1이 아닌 브로커(공용 브로커 등)에 대해 원천 차단됩니다.
#>

param(
    [string]$BrokerHost = "localhost",
    [int]$BrokerPort = 1883,
    [string]$TerminalId = "TERM_01",
    [ValidateSet('cycle', 'silence', 'stale', 'manual')]
    [string]$Scenario = 'cycle'
)

# -----------------------------------------------------------------------------
# 안전장치 검사: non-localhost에 대한 retained 발행 방지
# -----------------------------------------------------------------------------
$isLocalhost = ($BrokerHost -eq "localhost" -or $BrokerHost -eq "127.0.0.1" -or $BrokerHost -eq "::1")
if ($Scenario -eq 'stale' -and -not $isLocalhost) {
    Write-Error "[mock-risk-event-server] SAFETY ERROR: Stale scenario publishes a RETAINED message. To prevent polluting the shared team broker, retained publish is only allowed on localhost/127.0.0.1 (Current host: $BrokerHost)."
    exit 1
}

# -----------------------------------------------------------------------------
# MQTT 3.1.1 경량 프로토콜 인코더 / 전송 헬퍼
# -----------------------------------------------------------------------------
function Encode-RemainingLength([int]$length) {
    $bytes = [System.Collections.Generic.List[byte]]::new()
    do {
        $digit = $length % 128
        $length = [math]::Floor($length / 128)
        if ($length -gt 0) {
            $digit = $digit -bor 0x80
        }
        $bytes.Add([byte]$digit)
    } while ($length -gt 0)
    return $bytes.ToArray()
}

function Send-MqttConnect($stream, [string]$clientId) {
    $clientBytes = [System.Text.Encoding]::UTF8.GetBytes($clientId)
    
    # Variable Header: Protocol Name(4: MQTT), Level(4), Flags(CleanSession=0x02), KeepAlive(60s=0x003C)
    $varHeader = @(0x00, 0x04, 0x4D, 0x51, 0x54, 0x54, 0x04, 0x02, 0x00, 0x3C)
    
    # Payload: ClientId Len + ClientId
    $payloadLenBytes = @([byte]($clientBytes.Length -shr 8), [byte]($clientBytes.Length -band 0xFF))
    
    $remainingLength = $varHeader.Length + $payloadLenBytes.Length + $clientBytes.Length
    $remLenBytes = Encode-RemainingLength $remainingLength

    $stream.WriteByte(0x10) # CONNECT packet
    $stream.Write($remLenBytes, 0, $remLenBytes.Length)
    $stream.Write($varHeader, 0, $varHeader.Length)
    $stream.Write($payloadLenBytes, 0, $payloadLenBytes.Length)
    $stream.Write($clientBytes, 0, $clientBytes.Length)
    $stream.Flush()

    # Read CONNACK (4 bytes: 0x20, 0x02, sessionPresent, returnCode)
    $buf = New-Object byte[] 4
    $read = $stream.Read($buf, 0, 4)
    if ($read -lt 4 -or $buf[0] -ne 0x20 -or $buf[3] -ne 0x00) {
        throw "MQTT CONNECT failed. Response: $([BitConverter]::ToString($buf))"
    }
}

function Send-MqttPublish($stream, [string]$topic, [string]$payloadStr, [bool]$retain = $false) {
    $topicBytes = [System.Text.Encoding]::UTF8.GetBytes($topic)
    $payloadBytes = [System.Text.Encoding]::UTF8.GetBytes($payloadStr)

    $topicLenBytes = @([byte]($topicBytes.Length -shr 8), [byte]($topicBytes.Length -band 0xFF))
    $remainingLength = $topicLenBytes.Length + $topicBytes.Length + $payloadBytes.Length
    $remLenBytes = Encode-RemainingLength $remainingLength

    $fixedHeader = if ($retain) { 0x31 } else { 0x30 } # QoS 0, retain flag
    $stream.WriteByte($fixedHeader)
    $stream.Write($remLenBytes, 0, $remLenBytes.Length)
    $stream.Write($topicLenBytes, 0, $topicLenBytes.Length)
    $stream.Write($topicBytes, 0, $topicBytes.Length)
    $stream.Write($payloadBytes, 0, $payloadBytes.Length)
    $stream.Flush()
}

function Send-MqttPing($stream) {
    $stream.Write(@(0xC0, 0x00), 0, 2)
    $stream.Flush()
}

function Send-MqttDisconnect($stream) {
    try {
        $stream.Write(@(0xE0, 0x00), 0, 2)
        $stream.Flush()
    } catch {}
}

# -----------------------------------------------------------------------------
# JSON 페이로드 생성기
# -----------------------------------------------------------------------------
function New-RiskEventJson {
    param(
        [string]$CameraId,
        [string]$Zone,
        [string]$TerminalId,
        [string]$ExceptionState,
        $DistanceM,
        [int]$RiskLevel,
        [string]$UtcTime
    )
    $distMStr = if ($null -eq $DistanceM) { "null" } else { [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:F2}", $DistanceM) }
    $distMmStr = if ($null -eq $DistanceM) { "null" } else { [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:F0}", $DistanceM * 1000.0) }
    return "{`"utc_time`":`"$UtcTime`",`"camera_id`":`"$CameraId`",`"zone`":`"$Zone`",`"terminal_id`":`"$TerminalId`",`"exception_state`":`"$ExceptionState`",`"distance_mm`":$distMmStr,`"distance_m`":$distMStr,`"risk_level`":$RiskLevel}"
}

function Get-UtcNowIso {
    param([int]$OffsetSeconds = 0)
    $t = (Get-Date).ToUniversalTime().AddSeconds($OffsetSeconds)
    return $t.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
}

# -----------------------------------------------------------------------------
# 시나리오 정의
# -----------------------------------------------------------------------------
$scenarios = @(
    @{ Name = "SAFE";                  Risk = 0; Exception = "NONE";                  Distance = 3.5 },
    @{ Name = "CAUTION";                Risk = 1; Exception = "NONE";                  Distance = 1.8 },
    @{ Name = "DANGER";                 Risk = 2; Exception = "NONE";                  Distance = 0.9 },
    @{ Name = "EMERGENCY";              Risk = 3; Exception = "NONE";                  Distance = 0.3 },
    @{ Name = "UNCONFIRMED_PROXIMITY";  Risk = 1; Exception = "UNCONFIRMED_PROXIMITY"; Distance = $null },
    @{ Name = "SAFE (복귀)";            Risk = 0; Exception = "NONE";                  Distance = 4.0 }
)

$topic = "forklift/risk/$TerminalId"
$clientId = "mock-risk-pub-$([Guid]::NewGuid().ToString('N').Substring(0,8))"

Write-Host "====================================================================="
Write-Host " [Mock Risk Event MQTT Server]"
Write-Host "  Broker    : $BrokerHost`:$BrokerPort"
Write-Host "  Topic     : $topic"
Write-Host "  Scenario  : $Scenario"
Write-Host "====================================================================="

$tcpClient = New-Object System.Net.Sockets.TcpClient
try {
    $tcpClient.Connect($BrokerHost, $BrokerPort)
    $stream = $tcpClient.GetStream()
    Send-MqttConnect $stream $clientId
    Write-Host "[mock-risk-event-server] Connected to MQTT broker successfully as '$clientId'"

    switch ($Scenario) {
        'silence' {
            Write-Host "[mock-risk-event-server] SILENCE MODE: Connection open, but no risk events published."
            Write-Host "  -> Watchdog threshold is 1000ms (5 * 200ms). UI should transition to NETWORK_DISCONNECTED after ~1s."
            Write-Host "  -> Keeping broker connection alive via MQTT PINGREQ every 10s... (Ctrl+C to stop)"
            while ($tcpClient.Connected) {
                Start-Sleep -Seconds 10
                Send-MqttPing $stream
            }
        }

        'stale' {
            # 과거 시각(10초 전) 메시지를 retained=true로 발행
            $stalePayload = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId $TerminalId `
                -ExceptionState "NONE" -DistanceM 0.8 -RiskLevel 2 -UtcTime (Get-UtcNowIso -OffsetSeconds -10)
            
            Write-Host "[mock-risk-event-server] STALE MODE: Publishing retained message with 10s past timestamp..."
            Write-Host "  Payload: $stalePayload"
            Send-MqttPublish $stream $topic $stalePayload $true
            Write-Host "[mock-risk-event-server] Published retained stale message. Closing connection to test client connect filtering."
            Start-Sleep -Milliseconds 500
        }

        'manual' {
            $i = 0
            $current = $scenarios[0]
            Write-Host "[mock-risk-event-server] MANUAL MODE: Press ENTER to cycle through risk scenarios."
            Write-Host "  [Enter] Next scenario (Current: $($current.Name))"

            # 최초 1회 발행
            $payload = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId $TerminalId `
                -ExceptionState $current.Exception -DistanceM $current.Distance -RiskLevel $current.Risk -UtcTime (Get-UtcNowIso)
            Send-MqttPublish $stream $topic $payload $false
            Write-Host "[mock-risk-event-server] Sent: $($current.Name) (Risk=$($current.Risk))"

            $nextHeartbeat = (Get-Date).AddMilliseconds(200)

            while ($tcpClient.Connected) {
                if ([Console]::KeyAvailable) {
                    $key = [Console]::ReadKey($true)
                    if ($key.Key -eq [ConsoleKey]::Enter) {
                        $i++
                        $current = $scenarios[$i % $scenarios.Count]
                        $payload = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId $TerminalId `
                            -ExceptionState $current.Exception -DistanceM $current.Distance -RiskLevel $current.Risk -UtcTime (Get-UtcNowIso)
                        Send-MqttPublish $stream $topic $payload $false
                        Write-Host "`n[mock-risk-event-server] >>> Switched to: $($current.Name) (Risk=$($current.Risk))"
                        Write-Host "  [Enter] Next scenario (Next: $($scenarios[($i+1) % $scenarios.Count].Name))"
                        $nextHeartbeat = (Get-Date).AddMilliseconds(200)
                    }
                }

                # 200ms 하트비트 재전송 (단말 워치독 1000ms 방지)
                if ((Get-Date) -ge $nextHeartbeat) {
                    $hbPayload = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId $TerminalId `
                        -ExceptionState $current.Exception -DistanceM $current.Distance -RiskLevel $current.Risk -UtcTime (Get-UtcNowIso)
                    Send-MqttPublish $stream $topic $hbPayload $false
                    $nextHeartbeat = (Get-Date).AddMilliseconds(200)
                }
                Start-Sleep -Milliseconds 20
            }
        }

        'cycle' {
            Write-Host "[mock-risk-event-server] CYCLE MODE: 200ms heartbeat, switching risk state every 2 seconds."
            Write-Host "  Press Ctrl+C to terminate."
            while ($tcpClient.Connected) {
                foreach ($s in $scenarios) {
                    $payload = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId $TerminalId `
                        -ExceptionState $s.Exception -DistanceM $s.Distance -RiskLevel $s.Risk -UtcTime (Get-UtcNowIso)
                    Write-Host "[mock-risk-event-server] State changed -> $($s.Name) (Risk=$($s.Risk))"
                    Send-MqttPublish $stream $topic $payload $false

                    $switchAt = (Get-Date).AddSeconds(2)
                    while ((Get-Date) -lt $switchAt -and $tcpClient.Connected) {
                        Start-Sleep -Milliseconds 200
                        $hbPayload = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId $TerminalId `
                            -ExceptionState $s.Exception -DistanceM $s.Distance -RiskLevel $s.Risk -UtcTime (Get-UtcNowIso)
                        Send-MqttPublish $stream $topic $hbPayload $false
                    }
                }
            }
        }
    }
}
catch {
    Write-Host "[mock-risk-event-server] Communication error / ended: $($_.Exception.Message)"
}
finally {
    if ($stream) {
        Send-MqttDisconnect $stream
        $stream.Dispose()
    }
    if ($tcpClient) {
        $tcpClient.Close()
    }
    Write-Host "[mock-risk-event-server] Stopped."
}
