# 지게차 사각지대 충돌 방지 시스템

한화비전 PNM-C16083RVQ 4채널 카메라와 ArUco 마커 기반 좌표 정합으로
지게차 사각지대의 위험 구역을 감시하고 충돌을 사전 경고하는 시스템.

## 디렉터리 구조

```
root
├── README.md     
├── .gitignore        
├── visioncore/     
├── embedded/           
├── netdev/           
└── integrate/           
```

각 폴더의 세부 구조와 빌드 방법은 해당 폴더의 README를 참고.

## 협업 규칙

브랜치 전략, 커밋 메시지, PR 규칙은 별도 문서를 따른다.

- 브랜치: `main`(발표용) / `develop`(통합) / `feature/<트랙>/<작업>`
- 커밋: Conventional Commits (`feat`, `fix`, `docs`, `chore` ...)
- 병합: PR + 리뷰어 1명 + Squash and merge
- 자세한 내용은 `docs/git협업규칙.md` 참고