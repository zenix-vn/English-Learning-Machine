# Sophia cho Android

Ứng dụng Android (Kotlin, Jetpack Compose) có các tính năng như thiết bị ESP32, dùng chung máy chủ `backend/`.

| Ô | Việc | Cách hoạt động |
|---|---|---|
| Học từ mới, Ôn tập | thẻ từ vựng offline | tải gói từ `/sync/*`, thuật toán SM-2 giống thiết bị, gửi kết quả ôn lên máy chủ |
| Từ điển | tra từ trong gói offline | tìm kiếm, lọc theo chữ cái, nghe phát âm |
| Học Online | 24 bài theo chương trình, chỉ học tiếng Anh | WebSocket `/ws/`, mở đầu bằng "Chế độ học online." |
| Phát âm | luyện phát âm với Sophia | "Mình muốn luyện phát âm." |
| Phiên dịch | Việt ⇄ Anh bằng giọng nói | "Chế độ phiên dịch." |
| Trò chuyện | nói chuyện tự do (model riêng) | "Chế độ trò chuyện." |
| Thành tích | từ đã học, chuỗi ngày, tiến độ bộ từ | tính từ tiến độ trên điện thoại |
| Cài đặt | số từ mới mỗi ngày, đồng bộ, thông tin thiết bị | |

Điện thoại đăng ký với máy chủ như một thiết bị mới (`POST /ota/`, mã `android-<id>`), hiện mã kích hoạt 6 số;
quản trị viên nhập mã ở trang quản trị → Thiết bị. Gói cước và hạn mức áp dụng như với thiết bị.

## Build

```
set JAVA_HOME=C:\Program Files\Android\Android Studio\jbr
gradlew.bat :app:assembleDebug        # app\build\outputs\apk\debug\app-debug.apk
gradlew.bat :app:assembleRelease      # APK rút gọn (R8), ký bằng khóa debug: đổi khóa trước khi phát hành
gradlew.bat :app:testDebugUnitTest    # SM-2, hàng đợi thẻ, gói học, tiến độ, Opus, chụp màn hình
gradlew.bat :app:recordPaparazziDebug # vẽ lại ảnh các màn hình vào app\src\test\snapshots (không cần máy ảo)
```

`local.properties` cần `sdk.dir=...` (không đưa vào git). Máy chủ mặc định ở `app/build.gradle.kts` (`DEFAULT_SERVER`).

## Giao thức giọng nói (backend/app/session.py)

WebSocket `wss://…/ws/` với các header `Authorization: Bearer <token từ /ota/>`, `Device-Id`, `Client-Id`, `Protocol-Version: 1`.
Tin nhắn chữ là JSON (`hello`, `listen` start/stop/detect, `abort`; máy chủ gửi `stt`, `tts` start/sentence_start/stop);
âm thanh là các gói Opus 16 kHz, mono, khung 60 ms trong khung nhị phân, cả hai chiều. Micro và loa không bao giờ mở cùng lúc.
Opus chạy bằng Concentus (Java thuần), không cần thư viện native.
