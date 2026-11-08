# Học offline trên thiết bị

Thiết bị học được **không cần mạng**: thẻ ghi nhớ, nghe phát âm từng từ, tự xếp lịch ôn. Kết quả được đồng bộ về server mỗi khi có Wi-Fi.

## 1. Về thẻ SD: board này không gắn được

| | |
|---|---|
| GPIO board dùng | 31 (màn hình RGB 16 bit + 3 chân khởi tạo, I2S, I2C, đèn nền, nút, loa) |
| GPIO bị module chiếm | 12 (GPIO26–37: flash và PSRAM octal của ESP32-S3-WROOM-1-N16R8) |
| GPIO còn trống | **43, 44** (là UART0 của chip CH343P, dùng để nạp firmware và xem log) |
| Thẻ SD cần | 3 chân (SDMMC 1 bit) hoặc 4 chân (SPI) |

Thông số board cũng không có khe thẻ nhớ. Muốn có thẻ SD phải đổi phần cứng (board khác, hoặc bỏ màn hình), nên tôi **không** làm driver thẻ SD cho board này.

**Giải pháp:** dùng chính 16 MB flash trên module. Phân vùng `assets` được cắt từ 8 MB xuống 4 MB, 4 MB còn lại là phân vùng FAT tên `store` (có wear levelling), gắn tại `/store`. Toàn bộ code học offline chỉ dùng đường dẫn dưới `/store`, nên nếu sau này bạn dùng phần cứng có thẻ SD, chỉ cần thay hàm `MountStore()` trong `main/offline/storage.cc` để gắn thẻ vào cùng đường dẫn.

> Phải nạp lại toàn bộ flash bằng USB một lần (bảng phân vùng thay đổi). OTA không đổi được bảng phân vùng. Khi build, nếu ảnh `assets` (font, emoji, model wake word) lớn hơn 4 MB thì bước nạp sẽ báo lỗi: lúc đó giảm bộ emoji hoặc font.

## 2. Gói học (server dựng, thiết bị tải)

Trang quản trị → **Học offline** → chọn bộ từ, số từ, chất lượng giọng → *Dựng gói*. Server chọn N từ đầu của bộ (bỏ từ chức năng như *the, and, of*), đọc từng từ bằng Edge TTS, cắt khoảng lặng, mã hóa Opus (OGG, 16 kHz, khung 60 ms) rồi đóng thành 4 file:

| File | Nội dung |
|---|---|
| `cards.bin` | mỗi thẻ: `từ\0phiên âm\0nghĩa\0ví dụ\0` (UTF-8) |
| `cards.idx` | uint32 vị trí từng thẻ, thêm một phần tử cuối là kích thước `cards.bin` |
| `audio.bin` | đoạn OGG/Opus của từng từ, nối liền nhau |
| `audio.idx` | mỗi thẻ: uint32 vị trí, uint32 độ dài (0 = không có âm thanh) |

Kích thước đo thực tế: khoảng **1,2–1,4 KB mỗi từ** ở 16 kbps (cả chữ lẫn âm thanh ~1,5 KB). Phân vùng 4 MB chứa được khoảng **2.500 từ**; 3.000 từ nếu chọn 12 kbps. Trang quản trị cảnh báo khi số từ vượt dung lượng thiết bị. Dựng 2.000 từ mất khoảng 20 phút (chạy nền, có thanh tiến độ).

Thiết bị phát âm bằng `AudioService::PlaySound()` có sẵn trong firmware (nhận thẳng dữ liệu OGG/Opus), nên không cần thêm bộ giải mã.

## 3. Đồng bộ qua Wi-Fi (`backend/app/sync.py`, `firmware/main/offline/sync.cc`)

Dùng token của thiết bị (cùng token websocket), chỉ cho thiết bị đã kích hoạt và gói cước chưa hết hạn.

| Yêu cầu | Việc |
|---|---|
| `GET /sync/manifest` | gói của bộ từ mà thiết bị đang học: id, số thẻ, danh sách file kèm kích thước và sha256 |
| `GET /sync/file/{pack}/{file}` | tải file (hỗ trợ Range) |
| `GET /sync/state?pack=` | tiến độ đã học online của các từ trong gói, để thiết bị không dạy lại từ đã biết |
| `POST /sync/progress` | các lượt ôn offline: `[số thẻ, điểm, thời điểm]`, server phát lại theo thứ tự thời gian bằng đúng thuật toán SM-2 và ghi vào sổ từ vựng |

Thiết bị kiểm tra dung lượng trước, tải vào thư mục tạm, kiểm tra sha256 từng file, rồi mới thay gói cũ. Mất kết nối giữa chừng thì gói cũ vẫn nguyên.

## 4. Học trên máy

Chỉ có **một nút Boot** dùng được (nút Power và Reset không đọc được bằng phần mềm), nên điều khiển bằng số lần bấm:

| Đang thấy | 1 bấm | 2 bấm | Giữ |
|---|---|---|---|
| Từ (phát âm tự động) | xem nghĩa | nghe lại | mở menu |
| Nghĩa + ví dụ | **Nhớ** | **Quên** (ôn lại sau 10 phút) | **Dễ** |
| Menu | mục kế tiếp | quay lại | chọn: Tiếp tục / Đồng bộ ngay / Thoát |

**Vào chế độ học offline:** (a) bấm đúp nút Boot khi máy đang khởi động hoặc chờ cài Wi-Fi (lúc không có mạng); (b) nói "Sophia, học offline đi" khi đang online, server gửi lệnh `offline` cho thiết bị. Lần đầu chưa có gói thì máy tự thử tải.

**Giao diện** (xem bản dựng đúng từng pixel ở [offline-ui-preview.html](offline-ui-preview.html), mở bằng trình duyệt):

* **Đầu màn hình:** nhãn loại thẻ (MỚI, HỌC LẠI, ÔN TẬP, ÔN THÊM, mỗi loại một màu) và ba bộ đếm *Học lại / Ôn / Mới*, kèm thanh tiến độ của lần học.
* **Mặt trước:** từ cỡ lớn ở giữa và phiên âm, tự phát âm khi thẻ hiện ra. Từ dài tự dùng cỡ chữ nhỏ hơn.
* **Mặt sau:** chia hai cột, từ và phiên âm bên trái, nghĩa (rút gọn tối đa 3 dòng) và ví dụ Anh-Việt bên phải.
* **Chú giải nút bấm** ở đáy, vẽ bằng hình (một chấm = 1 bấm, hai chấm = 2 bấm, thanh dài = giữ), tô **xanh/đỏ/vàng** cho Nhớ/Quên/Dễ nên không cần đọc chữ.
* **Phản hồi sau mỗi lần chấm:** "Nhớ · gặp lại sau 3 ngày" hiện ngắn rồi tự ẩn, để người học hiểu vì sao thẻ quay lại lúc đó.
* **Hoàn thành mục tiêu** có màn riêng ("Hôm nay bạn đã học xong!"), bấm một lần để ôn thêm.

**Chọn thẻ:** thẻ đang học lại → thẻ đến hạn ôn → thẻ mới (mặc định 10 thẻ/ngày, `CONFIG_EL_OFFLINE_NEW_PER_DAY`) → ôn thêm. Màn hình không bao giờ trống.

## 5. Thuật toán và lưu tiến độ

* **SM-2** giống hệt server (`apply_grade`): nhớ lần 1 → 1 ngày, lần 2 → 3 ngày, sau đó nhân hệ số dễ; quên → 10 phút và về lại bước đầu. Kiểm thử đối chiếu 300 chuỗi ngẫu nhiên với chính code server: không lệch.
* **Tiến độ** = ảnh chụp `progress.bin` (12 byte mỗi thẻ) + nhật ký `events.log` (8 byte mỗi lượt ôn, ghi ngay). Mất điện chỉ mất tối đa lượt đang làm. Nhật ký cũng là hàng đợi gửi lên server; khi đã gửi hết và dài hơn 1.000 lượt thì được dọn gọn.
* **Đồng hồ:** thiết bị không có pin RTC nên mất giờ khi tắt máy. Khi có mạng, giờ lấy từ server. Offline, giờ được ước lượng từ lần học cuối cộng thời gian máy đã chạy, nên thời gian lúc tắt máy không được tính: thẻ có thể đến hạn muộn hơn thực tế. Chế độ "ôn thêm" bảo đảm vẫn luôn có thẻ để học.

## 6. Font

Font chữ có sẵn của firmware chỉ có ASCII, nên nghĩa tiếng Việt và phiên âm IPA không hiển thị được. Màn hình học dùng font riêng sinh từ Noto Sans (SIL OFL 1.1):

* `font_en_64`: chữ Latin cỡ lớn cho từ đang học. Đã có sẵn trong repo.
* `font_en_44`, `font_vi_28`, `font_vi_22`: chữ vừa cho từ dài, tiếng Việt có dấu + IPA + dấu câu (cỡ 28 và 22). **Chạy `firmware/scripts/gen_fonts.sh` một lần** (cần Node.js và Internet). Chưa có thì firmware vẫn build được, nhưng chữ có dấu và IPA sẽ không hiện, và chữ nhỏ dùng font mặc định.

## 7. Cái đã kiểm chứng và cái chưa

| | |
|---|---|
| Đã chạy thật | Dựng gói với giọng đọc thật; tải gói qua HTTPS có xác minh sha256 và Range; bộ đọc độc lập đọc đúng gói; đồng bộ tiến độ hai chiều; SM-2 khớp server; lệnh "học offline" bằng giọng nói đến được thiết bị giả lập |
| Kiểm thử máy chủ (1.255 kiểm tra) | `sm2`, `pack`, `progress` (mất điện, bản ghi dở dang, nhật ký dọn gọn, gói lớn thêm), `card_queue` và `text_util` (rút gọn nghĩa an toàn UTF-8, đổi khoảng thời gian ra chữ, chọn cỡ chữ): `firmware/tests/offline/run.sh` |
| **Chưa biên dịch / chưa chạy trên thiết bị** | `storage.cc`, `sync.cc`, `study_view.cc` (giao diện LVGL), `study.cc` (điều khiển, nút bấm, âm thanh, mount FAT). Giao diện mới chỉ xem được bằng bản HTML, chưa thấy trên màn hình thật nên màu, khoảng cách và tốc độ vẽ cần chỉnh sau khi chạy thử, các sửa đổi ở `english_learner.cc`, `application.cc`, CMake, Kconfig, bảng phân vùng |

Tắt toàn bộ tính năng nếu có lỗi biên dịch: `idf.py menuconfig` → English Learner → bỏ *Offline study mode*.
