# analyze_detections.py
#
# 입력값: detections_*.csv 파일 (하나 또는 여러 개, 같은 폴더에 있으면 전부 합쳐서 분석)
# 처리과정:
#   1) CSV들을 하나로 합침
#   2) 프레임당 감지 수, 클래스별 분포, 신뢰도 통계, 객체별 지속 프레임 수를 계산
# 출력값: 콘솔에 요약 통계 출력
#
# 사용법: python analyze_detections.py detections_20260713_153916.csv
#        python analyze_detections.py detections_*.csv   (여러 파일 한 번에)

import sys
import glob
import pandas as pd


def load_detections(patterns):
    files = []
    for p in patterns:
        matched = glob.glob(p)
        files.extend(matched if matched else [p])
    if not files:
        print("[오류] 해당하는 CSV 파일을 찾지 못했습니다.")
        sys.exit(1)

    frames = []
    for f in files:
        df = pd.read_csv(f)
        df["source_file"] = f
        frames.append(df)
    combined = pd.concat(frames, ignore_index=True)
    print(f"파일 {len(files)}개 로드: {files}")
    return combined


def analyze(df: pd.DataFrame):
    print("\n" + "=" * 50)
    print("1) 기본 정보")
    print("=" * 50)
    total_rows = len(df)
    unique_frames = df["frame_utc_time"].nunique()
    first_time = df["frame_utc_time"].min()
    last_time = df["frame_utc_time"].max()
    print(f"총 감지 레코드 수      : {total_rows}")
    print(f"고유 프레임(시각) 수    : {unique_frames}")
    print(f"기록 구간              : {first_time} ~ {last_time}")

    print("\n" + "=" * 50)
    print("2) 프레임당 감지 개수")
    print("=" * 50)
    per_frame = df.groupby("frame_utc_time").size()
    print(f"평균 감지 수 / 프레임  : {per_frame.mean():.2f}")
    print(f"최소 / 최대            : {per_frame.min()} / {per_frame.max()}")

    print("\n" + "=" * 50)
    print("3) 클래스별 분포")
    print("=" * 50)
    class_counts = df["class"].value_counts()
    for cls, cnt in class_counts.items():
        pct = cnt / total_rows * 100
        print(f"  {cls:<15} {cnt:>6}건  ({pct:5.1f}%)")

    print("\n" + "=" * 50)
    print("4) 클래스별 신뢰도(likelihood) 통계")
    print("=" * 50)
    likelihood_stats = df.groupby("class")["likelihood"].agg(["mean", "min", "max", "std"])
    print(likelihood_stats.round(3).to_string())

    print("\n" + "=" * 50)
    print("5) 상위/하위 객체 (몇 프레임 동안 잡혔는지 = 추적 지속성)")
    print("=" * 50)
    persistence = df.groupby("object_id").size().sort_values(ascending=False)
    print("가장 오래 잡힌 객체 Top 5:")
    for obj_id, cnt in persistence.head(5).items():
        cls = df[df["object_id"] == obj_id]["class"].iloc[0]
        print(f"  ObjectId={obj_id:<6} ({cls:<10}) {cnt}프레임 동안 감지됨")

    single_frame_objects = (persistence == 1).sum()
    print(f"\n1프레임만 잠깐 잡힌 객체 수: {single_frame_objects}개"
          f" (오검출/찰나의 감지 후보일 수 있음 -> 확실하지 않음, 눈으로 재확인 권장)")

    print("\n" + "=" * 50)
    print("6) 부모-자식 관계 (Human 박스 안에 Face/Head가 딸린 구조)")
    print("=" * 50)
    has_parent = (df["parent_id"] != -1).sum()
    no_parent = (df["parent_id"] == -1).sum()
    print(f"Parent 있음 (자식 객체): {has_parent}건")
    print(f"Parent 없음 (최상위)   : {no_parent}건")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("사용법: python analyze_detections.py <csv파일 또는 패턴>")
        sys.exit(1)

    df = load_detections(sys.argv[1:])
    analyze(df)