# SJ-CanvasOS Android App

**Spatial Operating System for Android** — 1024x1024 캔버스 위에서 동작하는 공간형 OS를 안드로이드 앱으로.

## Build (GitHub Actions)

**Push만 하면 APK가 자동 빌드됩니다.**

1. 이 저장소를 GitHub에 push
2. Actions 탭 → `Build CanvasOS APK` 워크플로우 실행
3. Artifacts에서 APK 다운로드

## Build (GitHub Codespaces)

1. 이 저장소에서 Codespaces 시작 (`.devcontainer` 설정 포함)
2. 터미널에서:
```bash
./gradlew assembleDebug
```
3. `app/build/outputs/apk/debug/app-debug.apk` 생성

## Project Structure

```
SJ-CANVAOS-App/
├── app/                          ← Android 앱
│   ├── src/main/
│   │   ├── java/.../canvasos/
│   │   │   ├── MainActivity.kt   ← 홈 화면 (모드 선택)
│   │   │   ├── TerminalActivity.kt ← 터미널 UI
│   │   │   └── NativeBridge.kt   ← 네이티브 바이너리 관리
│   │   ├── res/                   ← 레이아웃, 아이콘, 테마
│   │   ├── assets/canvasos_launcher ← 네이티브 바이너리
│   │   └── jniLibs/arm64-v8a/    ← .so 라이브러리
│   └── build.gradle.kts
├── native/build/                  ← C 소스 (50+ 모듈)
├── .github/workflows/build-apk.yml ← CI/CD
└── .devcontainer/                 ← Codespaces 설정
```

## Native Tests

```bash
cd native/build
make test_all CC=clang   # 160/160 PASS
```

## License

CanvasOS — sjpupro-lab | Busan, South Korea
