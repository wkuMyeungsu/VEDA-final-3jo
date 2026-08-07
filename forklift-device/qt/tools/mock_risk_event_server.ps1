<#
  danger_judgment_engine이 9000 포트로 보내는 것과 같은 형식의 NDJSON을
  흉내내는 mock 서버. 명수님 ArUco/거리값 파이프라인이 준비되기 전에도
  RiskEventSource의 파싱, stale 필터, 워치독, 화면 반영을 검증하기 위한
  임시 도구.

  실제 서버 포맷(server/src/logic/judgment/danger_judgment_engine.cpp:226
  toJson() 기준):
    {"utc_time":"...","camera_id":"...","zone":"...","terminal_id":"...",
     "exception_state":"NONE","distance_m":1.23,"risk_level":0}

  사용법:
    PS> .\mock_risk_event_server.ps1                 # 기본: 9000 포트에서 시나리오 순환 전송
    PS> .\mock_risk_event_server.ps1 -Port 9000 -Scenario silence  # 워치독 테스트용: 접속만 받고 무전송
    PS> .\mock_risk_event_server.ps1 -Port 9000 -Scenario manual   # Enter 누를 때마다 다음 시나리오 하나씩 전송
#>

param(
    [int]$Port = 9000,
    [ValidateSet('cycle', 'silence', 'stale', 'manual')]
    [string]$Scenario = 'cycle'
)

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
    $distancePart = if ($null -eq $DistanceM) { "null" } else { [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:F2}", $DistanceM) }
    return "{`"utc_time`":`"$UtcTime`",`"camera_id`":`"$CameraId`",`"zone`":`"$Zone`",`"terminal_id`":`"$TerminalId`",`"exception_state`":`"$ExceptionState`",`"distance_m`":$distancePart,`"risk_level`":$RiskLevel}"
}

function Get-UtcNowIso {
    param([int]$OffsetSeconds = 0)
    $t = (Get-Date).ToUniversalTime().AddSeconds($OffsetSeconds)
    return $t.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
}

# cycle/manual 공용 시나리오 목록. Name은 manual 모드에서 사람이 알아볼 프롬프트용.
$scenarios = @(
    @{ Name = "SAFE";                  Risk = 0; Exception = "NONE";                  Distance = 3.5 },
    @{ Name = "CAUTION";                Risk = 1; Exception = "NONE";                  Distance = 1.8 },
    @{ Name = "DANGER";                 Risk = 2; Exception = "NONE";                  Distance = 0.9 },
    @{ Name = "EMERGENCY";              Risk = 3; Exception = "NONE";                  Distance = 0.3 },
    @{ Name = "UNCONFIRMED_PROXIMITY";  Risk = 1; Exception = "UNCONFIRMED_PROXIMITY"; Distance = $null },
    @{ Name = "SAFE (복귀)";            Risk = 0; Exception = "NONE";                  Distance = 4.0 }
)

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
$listener.Start()
Write-Host "[mock-risk-event-server] listening on 0.0.0.0:$Port (scenario=$Scenario) - Ctrl+C to stop"

try {
    while ($true) {
        Write-Host "[mock-risk-event-server] waiting for RiskEventSource connection..."
        $client = $listener.AcceptTcpClient()
        Write-Host "[mock-risk-event-server] client connected: $($client.Client.RemoteEndPoint)"
        $stream = $client.GetStream()
        $writer = New-Object System.IO.StreamWriter($stream)
        $writer.NewLine = "`n"
        $writer.AutoFlush = $true

        try {
            switch ($Scenario) {
                'silence' {
                    # 접속만 받고 아무것도 안 보냄 - 워치독(무수신 감지) 확인용.
                    # RiskEventSource의 kWatchdogThresholdMs(현재 600ms)가 지나면
                    # 화면에 NETWORK_DISCONNECTED가 떠야 정상.
                    Write-Host "[mock-risk-event-server] silence mode: sending nothing, watch the UI for NETWORK_DISCONNECTED"
                    Start-Sleep -Seconds 3600
                }
                'stale' {
                    # utc_time을 10초 전으로 찍어서 stale 필터(1000ms 초과 시 폐기)가
                    # 실제로 화면 반영을 막는지 확인용.
                    $line = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId "TERM_01" `
                        -ExceptionState "NONE" -DistanceM 0.8 -RiskLevel 2 -UtcTime (Get-UtcNowIso -OffsetSeconds -10)
                    Write-Host "[mock-risk-event-server] sending stale (10s old): $line"
                    $writer.WriteLine($line)
                    Start-Sleep -Seconds 5
                }
                'manual' {
                    # 한 번에 하나씩: 이 콘솔에서 Enter를 누를 때마다 다음 시나리오로 전환.
                    # 실제 서버(result_dispatcher.hpp)처럼 상태가 안 바뀐 동안에도 200ms마다
                    # 같은 값을 재전송해야 클라이언트 워치독(600ms)이 안 걸림 - 이걸 안 하면
                    # Enter 누르고 몇 초만 가만히 있어도 화면이 NETWORK_DISCONNECTED로 되돌아감.
                    $i = 0
                    $current = $scenarios[0]
                    Write-Host "[mock-risk-event-server] Enter=다음 시나리오로 전환 (지금: $($current.Name))"
                    $line = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId "TERM_01" `
                        -ExceptionState $current.Exception -DistanceM $current.Distance -RiskLevel $current.Risk -UtcTime (Get-UtcNowIso)
                    $writer.WriteLine($line)
                    Write-Host "[mock-risk-event-server] sent (change): $line"
                    $nextHeartbeat = (Get-Date).AddMilliseconds(200)

                    while ($client.Connected) {
                        if ([Console]::KeyAvailable) {
                            $key = [Console]::ReadKey($true)
                            if ($key.Key -eq [ConsoleKey]::Enter) {
                                $i++
                                $current = $scenarios[$i % $scenarios.Count]
                                $line = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId "TERM_01" `
                                    -ExceptionState $current.Exception -DistanceM $current.Distance -RiskLevel $current.Risk -UtcTime (Get-UtcNowIso)
                                $writer.WriteLine($line)
                                Write-Host "[mock-risk-event-server] sent (change): $line"
                                Write-Host "[mock-risk-event-server] Enter=다음 시나리오로 전환 (지금: $($current.Name))"
                                $nextHeartbeat = (Get-Date).AddMilliseconds(200)
                            }
                        }
                        if ((Get-Date) -ge $nextHeartbeat) {
                            $line = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId "TERM_01" `
                                -ExceptionState $current.Exception -DistanceM $current.Distance -RiskLevel $current.Risk -UtcTime (Get-UtcNowIso)
                            $writer.WriteLine($line)
                            $nextHeartbeat = (Get-Date).AddMilliseconds(200)
                        }
                        Start-Sleep -Milliseconds 50
                    }
                }
                default {
                    # 기본: Safe -> Caution -> Danger -> Emergency -> Safe 순환, 2초마다 전환.
                    # 실제 서버처럼 전환 사이에도 200ms마다 현재 값을 재전송(하트비트)해서
                    # 클라이언트 워치독(600ms)이 걸리지 않게 함.
                    while ($client.Connected) {
                        foreach ($s in $scenarios) {
                            if (-not $client.Connected) { break }
                            $line = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId "TERM_01" `
                                -ExceptionState $s.Exception -DistanceM $s.Distance -RiskLevel $s.Risk -UtcTime (Get-UtcNowIso)
                            Write-Host "[mock-risk-event-server] sending: $line"
                            $writer.WriteLine($line)

                            $switchAt = (Get-Date).AddSeconds(2)
                            while ((Get-Date) -lt $switchAt -and $client.Connected) {
                                Start-Sleep -Milliseconds 200
                                $hbLine = New-RiskEventJson -CameraId "CAM_01" -Zone "ZONE_A" -TerminalId "TERM_01" `
                                    -ExceptionState $s.Exception -DistanceM $s.Distance -RiskLevel $s.Risk -UtcTime (Get-UtcNowIso)
                                $writer.WriteLine($hbLine)
                            }
                        }
                    }
                }
            }
        } catch {
            Write-Host "[mock-risk-event-server] client loop ended: $($_.Exception.Message)"
        } finally {
            $writer.Dispose()
            $client.Close()
        }
    }
} finally {
    $listener.Stop()
}
