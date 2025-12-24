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
