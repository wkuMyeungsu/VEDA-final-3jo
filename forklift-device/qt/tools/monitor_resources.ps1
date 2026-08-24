<#
.SYNOPSIS
  운전자 단말(operator_terminal)의 CPU 및 메모리(RSS) 사용량을 1초 주기로 CSV에 기록하는 샘플러 스크립트.

.DESCRIPTION
  - 대상 프로세스가 실행 중이지 않으면 대기하다가 시작되면 자동으로 부착(attach)됩니다.
  - 24시간 장시간 안정성(Soak) 테스트 중 단일 CSV 파일에 지속 기록합니다.
  - 프로세스가 종료되면 마지막 종료 시각을 기록하고 정상 종료됩니다.

.PARAMETER ProcessName
  모니터링할 프로세스 이름 (확장자 제외, 기본값: 'operator_terminal')

.PARAMETER OutputFile
  CSV 출력 파일 경로 (기본값: 'resource_usage_operator_terminal.csv')

.PARAMETER IntervalSec
  샘플링 주기 (초 단위, 기본값: 1)

.EXAMPLE
  PS> .\monitor_resources.ps1
  PS> .\monitor_resources.ps1 -OutputFile "soak_test_24h.csv" -IntervalSec 1
#>

param(
    [string]$ProcessName = "operator_terminal",
    [string]$OutputFile = "resource_usage_operator_terminal.csv",
    [double]$IntervalSec = 1.0
)

# 1. CSV 헤더 초기화 (기존 파일이 없으면 새로 생성)
if (-not (Test-Path $OutputFile)) {
    "timestamp,elapsed_sec,cpu_percent,rss_kb" | Out-File -FilePath $OutputFile -Encoding utf8
    Write-Host "[monitor_resources] Created new CSV log file: $OutputFile"
} else {
    Write-Host "[monitor_resources] Appending to existing CSV log file: $OutputFile"
}

# 2. 프로세스 탐색 루프 (기동 대기)
Write-Host "[monitor_resources] Waiting for process '$ProcessName' to start..."
$proc = $null
while ($null -eq $proc -or $proc.HasExited) {
    $candidates = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
    if ($candidates) {
        $proc = $candidates[0]
        break
    }
    Start-Sleep -Milliseconds 1000
}

$pidNum = $proc.Id
Write-Host "[monitor_resources] Attached to process '$ProcessName' (PID: $pidNum). Monitoring started (interval: ${IntervalSec}s)..."

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$prevTime = [DateTime]::UtcNow
$prevCpuTime = $proc.TotalProcessorTime.TotalSeconds
$numCores = [Environment]::ProcessorCount

try {
    while (-not $proc.HasExited) {
        Start-Sleep -Milliseconds ([int]($IntervalSec * 1000))
        
        $proc.Refresh()
        if ($proc.HasExited) {
            break
        }

        $currTime = [DateTime]::UtcNow
        $currCpuTime = $proc.TotalProcessorTime.TotalSeconds
        $timeDelta = ($currTime - $prevTime).TotalSeconds

        if ($timeDelta -gt 0) {
            # 전체 시스템 기준 CPU % (코어 수로 정규화)
            $cpuPercent = [math]::Round((($currCpuTime - $prevCpuTime) / $timeDelta / $numCores) * 100, 2)
            if ($cpuPercent -lt 0) { $cpuPercent = 0.0 }
        } else {
            $cpuPercent = 0.0
        }

        # 메모리: Working Set (RSS) in KB
        $rssKb = [math]::Round($proc.WorkingSet64 / 1KB)
        $elapsedSec = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        $isoTimestamp = $currTime.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")

        $line = "$isoTimestamp,$elapsedSec,$cpuPercent,$rssKb"
        $line | Out-File -FilePath $OutputFile -Append -Encoding utf8

        $prevTime = $currTime
        $prevCpuTime = $currCpuTime
    }
}
catch {
    Write-Warning "[monitor_resources] Monitoring loop interrupted: $_"
}
finally {
    $exitTime = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
    $finalElapsed = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    $finalMsg = "$exitTime,$finalElapsed,PROCESS_TERMINATED,0"
    $finalMsg | Out-File -FilePath $OutputFile -Append -Encoding utf8
    Write-Host "[monitor_resources] Process '$ProcessName' (PID: $pidNum) terminated at $exitTime. Total monitored: ${finalElapsed}s."
}
