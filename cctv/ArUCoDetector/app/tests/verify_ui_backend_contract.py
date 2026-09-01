"""Verify the benchmark request and displayed app version stay in sync."""

import json
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parents[1]


def main():
    html = (APP_ROOT / "html" / "index.html").read_text(encoding="utf-8")
    controller = (APP_ROOT / "src" / "DetectorManager" / "features" / "test" /
                  "test_run_controller.cc").read_text(encoding="utf-8")
    for field in ("worker_counts", "scales", "input_mode", "dictionary_name",
                  "warmup_samples", "measurement_samples"):
        assert f"{field}:" in html and f'document.HasMember("{field}")' in controller
    assert "scales: [scaleId]" in html
    assert "판정 불가" not in html
    assert "미채점 / ${aggregate.unscored || 0} (정답 ID 미입력)" in html
    assert "미채점 / ${summary.unscored} (정답 ID 미입력)" in html
    assert "function csvCell(value)" in html and "function csvRow(values)" in html
    assert "unscored_count" in html and "requested_count" not in html
    assert "if (!r.ok) throw new Error(`CSV 다운로드 실패 (${r.status})`);" in html
    assert 'document.AddMember("samples"' not in controller
    assert 'kind != "samples"' in controller

    version = json.loads((APP_ROOT.parent / "config" / "app_manifest.json").read_text())["AppVersion"]
    component = APP_ROOT / "src" / "DetectorManager" / "component" / "manifests" / "DetectorManager_manifest.json"
    assert json.loads(component.read_text())["Version"] == version and f">v{version}<" in html
    print("PASS")


if __name__ == "__main__":
    main()
