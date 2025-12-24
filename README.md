# Secure FUSE USB Filesystem 

Một hệ thống tập tin mã hóa an toàn trên USB, sử dụng FUSE (Filesystem in Userspace) và thư viện Sodium. Dự án này biến một chiếc USB bình thường thành một thiết bị lưu trữ bảo mật với khả năng ẩn giấu dữ liệu, nén và tự động sao lưu.

## Tính năng 

* **Mã hóa :** Sử dụng thuật toán **ChaCha20-Poly1305** để mã hóa nội dung file.
* **Bảo vệ mật khẩu:** Sử dụng **Argon2id** để băm và xác thực mật khẩu.
* **Tiết kiệm dung lượng:** Tự động nén dữ liệu bằng **zlib** trước khi mã hóa.
* **Tự động sao lưu dữ liệu vào máy tính và phục hồi lại USB nếu dữ liệu trên USB bị xóa nhầm.
* **Plug & Play:** Script tự động phát hiện và cài đặt các thư viện thiếu trên Ubuntu.

## Yêu cầu hệ thống

* **Hệ điều hành:** Linux (Khuyên dùng Ubuntu 20.04 trở lên).
* **USB:** Định dạng exFAT hoặc Ext4.
* **Thư viện:** `libfuse`, `libsodium`, `zlib`, `libkeyutils` (Sẽ được cài tự động bởi script).

## Hướng dẫn Cài đặt (Setup)

Vì lý do bảo mật, repo này chỉ chứa mã nguồn. Bạn cần biên dịch nó lần đầu tiên để tạo ra file chạy.

### Bước 1: Chuẩn bị
1.  Clone repo này về máy tính.
2.  Cắm USB của bạn vào máy.
3.  Copy 2 file `my_fuse.c` và `run.sh` vào thư mục gốc của USB.

### Bước 2: Biên dịch lần đầu
Mở Terminal tại thư mục USB và chạy lệnh sau để biên dịch mã nguồn thành file thực thi ẩn:

```bash
# Cài đặt thư viện biên dịch (nếu chưa có)
sudo apt-get update
sudo apt-get install build-essential libfuse-dev libsodium-dev libkeyutils-dev zlib1g-dev pkg-config -y

# Biên dịch mã nguồn và tạo file ẩn .secure_usb
gcc -Wall my_fuse.c `pkg-config fuse --cflags --libs` -lsodium -lz -lkeyutils -o .secure_usb
sudo bash run.sh (mật khẩu là Nhom01)

## Hướng dẫn Thao tác File 
Sau khi chạy lệnh `sudo bash run.sh` và nhập mật khẩu, hệ thống sẽ mở ra một thư mục tên là **`plain`**.

**BẠN CHỈ LÀM VIỆC TRONG THƯ MỤC `plain`!**

### 1. Tạo và Copy File (Mã hóa)
* **Thao tác:** Hãy copy, tạo mới, hoặc lưu file (ảnh, tài liệu, video...) vào bên trong thư mục **`plain`**.
* **Khi copy vào `plain`, hệ thống sẽ tự động nén và mã hóa file đó, sau đó cất giữ an toàn vào thư mục ẩn `.cipher`.
* **Lưu ý:** file hiện ra bình thường trong `plain`, nhưng thực chất file gốc trên USB đã bị biến đổi thành dữ liệu rác không thể đọc được.

### 2. Mở và Xem File (Giải mã)
* **Thao tác:** Mở trực tiếp các file đang nằm trong thư mục **`plain`**.
* **Hệ thống sẽ giải mã để xem được nội dung gốc. Khi đóng file lại, nó vẫn nằm ở trạng thái bảo mật.

### 3. Xóa File
* **Thao tác:** Chỉ cần xóa file trong thư mục **`plain`**.
* **File mã hóa tương ứng trong kho lưu trữ `.cipher` cũng sẽ bị xóa vĩnh viễn.

---

###LƯU Ý:
1.  **KHÔNG** xoá thư mục **`.cipher`** (nếu bạn vô tình tìm thấy nó).
2.  **KHÔNG** đổi tên, xóa, hay chép file trực tiếp vào **`.cipher`**. Việc này sẽ làm hỏng cấu trúc mã hóa và sẽ **mất dữ liệu vĩnh viễn**.
3.  Luôn tắt chương trình bằng **`Ctrl + C`** trước khi rút USB để đảm bảo dữ liệu được lưu trọn vẹn.
