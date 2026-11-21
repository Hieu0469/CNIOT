// Chứng chỉ CA của test.mosquitto.org (dùng khi USE_TLS = 1, cổng 8883).
// Tải file "mosquitto.org.crt" (định dạng PEM) tại https://test.mosquitto.org/ rồi DÁN NGUYÊN VĂN
// toàn bộ nội dung (kể cả hai dòng BEGIN/END) vào giữa R"EOF( và )EOF" bên dưới.
// Kiểm tra: mở file .crt bằng trình soạn thảo, phải thấy -----BEGIN CERTIFICATE----- ... -----END CERTIFICATE-----
// KHÔNG dùng netTls.setInsecure() trong bài nộp (bỏ qua kiểm tra chứng chỉ = mất tác dụng chống giả mạo broker).
#pragma once
static const char CA_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
DAN_NOI_DUNG_CHUNG_CHI_O_DAY
-----END CERTIFICATE-----
)EOF";
