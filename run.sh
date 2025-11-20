#!/bin/bash

# Thiết lập chế độ thoát ngay lập tức nếu có lỗi xảy ra (trừ khi được xử lý)
set -e

# --- CẤU HÌNH ---
# Tự động tìm đường dẫn gốc của USB này
USB_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Định nghĩa các đường dẫn cần thiết
PROGRAM="$USB_ROOT/my_fuse"
# Thư mục DATA là thư mục gốc của USB
DATA_DIR="$USB_ROOT" 
MOUNT_POINT="$HOME/usb_secure" # Điểm mount ảo trên máy tính của người dùng
LOGIN_FILE="$MOUNT_POINT/LOGIN_HERE"
USB_DEVICE="" # Tên thiết bị vật lý (ví dụ: sdb1)

echo "=========================================="
echo "        BỘ KHỞI ĐỘNG SECURE FS            "
echo "=========================================="

# --- 1. TÌM VÀ MOUNT THIẾT BỊ VẬT LÝ ---
if ! mount | grep -q "$DATA_DIR"; then
    echo ">>> Đang tìm kiếm thiết bị USB..."
    
    # Tìm tên thiết bị vật lý (ví dụ: sdb1)
    USB_DEVICE=$(lsblk -o NAME,MOUNTPOINT | grep "$DATA_DIR" | awk '{print $1}' | tail -1)
    
    if [ -z "$USB_DEVICE" ]; then
        echo "LỖI: Không thể tự động tìm thấy thiết bị vật lý đang mount. Vui lòng mount thủ công."
        exit 1
    fi

    # Cần quyền SUDO để MOUNT
    echo ">>> Đang mount thiết bị /dev/$USB_DEVICE. Vui lòng nhập mật khẩu Admin."
    sudo mount "/dev/$USB_DEVICE" "$DATA_DIR"
fi
if [ ! -d "$MOUNT_POINT" ]; then
    echo ">>> Tạo điểm mount ảo: $MOUNT_POINT"
    # Dùng -p để tạo thư mục cha nếu cần
    mkdir -p "$MOUNT_POINT" 
fi

# --- 2. KHỞI ĐỘNG FUSE ---
if mount | grep -q "$MOUNT_POINT"; then
    echo ">>> TRẠNG THÁI: FUSE ĐÃ CHẠY SẴN. (Tiếp tục bước Đăng nhập)"
else
    echo ">>> Đang khởi động Secure FS bằng quyền SUDO..."
    # Chạy my_fuse bằng SUDO để đảm bảo quyền truy cập file
    sudo "$PROGRAM" -d --root="$DATA_DIR" "$MOUNT_POINT" -o allow_other &
    sleep 2 # Tăng thời gian chờ khởi động FUSE
fi


# --- 3. GIAO DIỆN ĐĂNG NHẬP ---
# Kiểm tra xem file LOGIN_HERE (file ảo) có tồn tại không
if sudo test -f "$LOGIN_FILE"; then
    echo ""
    echo "==========================================="
    echo "   HỆ THỐNG ĐANG KHÓA - CẦN ĐĂNG NHẬP"
    echo "==========================================="
    
    # Yêu cầu mật khẩu (Ẩn ký tự)
    read -s -p "Mật khẩu: " PASSWORD
    echo ""

    # Ghi mật khẩu vào file LOGIN_HERE
    # Sử dụng sudo tee để ghi mật khẩu vào file FUSE (chạy bằng root)
    echo ">>> Đang xác minh mật khẩu..."
    printf "%s" "$PASSWORD" | sudo tee "$LOGIN_FILE" >/dev/null || true
    # 4. Kiểm tra kết quả
    # Logic kiểm tra: Nếu đăng nhập thành công, FUSE sẽ xóa file LOGIN_HERE
    if ! sudo test -f "$LOGIN_FILE"; then
        echo ">>> ĐĂNG NHẬP THÀNH CÔNG! <<<"
        echo ">>> Dữ liệu đã mở tại: $MOUNT_POINT"
        echo "-------------------------------------------"
        ls -lh "$MOUNT_POINT"
    else
        echo ">>> SAI MẬT KHẨU! (Vui lòng chạy lại script để thử lại.)"
    fi
else
    # TRẠNG THÁI: FUSE đã chạy và đã mở khóa sẵn
    echo ">>> TRẠNG THÁI: ĐÃ MỞ KHÓA SẴN."
    echo ">>> Dữ liệu đang ở: $MOUNT_POINT"
    ls -lh "$MOUNT_POINT"
fi

echo "-------------------------------------------"
echo "Để ngắt kết nối an toàn, chạy: sudo fusermount -u "$MOUNT_POINT""
