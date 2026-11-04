# RedMagicEMU

RedMagicEMU là dự án độc lập, không chính thức, dựa trên [AEmulator](https://github.com/uxazu/AEmulator), vốn được phát triển từ [trình giả lập SENSES](https://github.com/Istratii-tech/SENSES) ban đầu. Đây không phải sản phẩm chính thức của REDMAGIC và dự án không liên kết với REDMAGIC.

Dự án nhắm tới firmware gốc REDMAGIC NX809J Android 16 / API 36 / ARM64 làm hệ thống khách. Giới hạn này áp dụng cho firmware khách, không áp dụng cho mẫu thiết bị chủ. Khả năng hỗ trợ và vận hành firmware này chưa được kiểm chứng trên thiết bị; xem [tiến độ](../PROGRESS.md) và [thông tin cơ sở](../BASELINE.md).

## Xây dựng

Dự án Android cần Android SDK 36 và NDK r28. Bản dựng cơ sở sử dụng JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

Môi trường phát triển cục bộ dùng ID ứng dụng `app.redmagicemu`. Android cài ứng dụng này riêng với ứng dụng gốc: dữ liệu riêng tư không tự động được chuyển. Hãy giữ ứng dụng cũ và dữ liệu cho đến khi có phương pháp chuyển dữ liệu đã được kiểm chứng.

## Ghi nhận và giấy phép

RedMagicEMU dựa trên AEmulator của [uxazu](https://github.com/uxazu), phát triển từ các trình giả lập HTC Desire HD và HTC One M7 của [tác giả ban đầu](https://t.me/istratii_tech1). Công sức của họ cùng các thông báo bản quyền và giấy phép hiện có được ghi nhận.

Dự án được phân phối theo [GPL-3.0](../../LICENSE). Android, REDMAGIC, các nhãn hiệu khác và firmware thuộc về những chủ sở hữu tương ứng.
