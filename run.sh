#!/bin/bash
# Lấy đường dẫn gốc của USB
USB_DIR=$(cd "$(dirname "$0")"; pwd)

# === CẤU HÌNH ĐƯỜNG DẪN ===
# 1. File thực thi ẩn tại gốc
BINARY="$USB_DIR/.secure_usb"

# 2. Thư mục dữ liệu ẩn tại gốc
CIPHER_DIR="$USB_DIR/.cipher"

# 3. Thư mục hiển thị 
PLAIN_POINT="$USB_DIR/plain"

# 4. Đường dẫn Backup trên máy tính
BACKUP_PATH="$HOME/.usb_secure_backup"

# Thư viện cần thiết cho Ubuntu
REQUIRED_PACKAGES="build-essential libfuse-dev libsodium-dev libkeyutils-dev zlib1g-dev pkg-config"

echo "=== USB BAO MAT (STEALTH & BACKUP MODE) ==="

# --- PHẦN 1: TỰ ĐỘNG SAO LƯU & PHỤC HỒI ---
sync_data() {
    # TH1: USB có dữ liệu -> Backup vào máy tính
    if [ -d "$CIPHER_DIR" ] && [ "$(ls -A $CIPHER_DIR)" ]; then
        echo ">>> Phat hien du lieu. Dang dong bo vao may tinh..."
        mkdir -p "$BACKUP_PATH"
        cp -ru "$CIPHER_DIR/." "$BACKUP_PATH/"
        echo ">>> Da Backup an toan."
    
    # TH2: USB mất dữ liệu -> Phục hồi từ máy tính
    elif [ -d "$BACKUP_PATH" ] && [ "$(ls -A $BACKUP_PATH)" ]; then
        echo "!!! CANH BAO: KHO DU LIEU TREN USB DA BI XOA!"
        echo ">>> Dang khoi phuc tu may tinh..."
        mkdir -p "$CIPHER_DIR"
        cp -r "$BACKUP_PATH/." "$CIPHER_DIR/"
        echo ">>> KHOI PHUC THANH CONG!"
        
    else
        # TH3: Lần đầu dùng
        echo ">>> Khoi tao kho du lieu moi..."
        mkdir -p "$CIPHER_DIR"
    fi
}

# --- PHẦN 2: XỬ LÝ CẤU TRÚC & RÁC ---
# Xóa thư mục rác của Windows cho sạch mắt (nếu có)
if [ -d "$USB_DIR/System Volume Information" ]; then
    sudo rm -rf "$USB_DIR/System Volume Information"
fi

# Tạo lại điểm mount nếu bị xóa
if [ ! -d "$PLAIN_POINT" ]; then
    mkdir -p "$PLAIN_POINT"
fi

# Chạy đồng bộ dữ liệu
sync_data

# --- PHẦN 3: KIỂM TRA THƯ VIỆN ---
install_libs() {
    MISSING_PKGS=""
    for pkg in $REQUIRED_PACKAGES; do
        if ! dpkg -s "$pkg" >/dev/null 2>&1; then
            MISSING_PKGS="$MISSING_PKGS $pkg"
        fi
    done
    if [ -n "$MISSING_PKGS" ]; then
        echo ">>> May thieu thu vien. Dang cai dat tu dong..."
        sudo apt-get update && sudo apt-get install -y $MISSING_PKGS
    fi
}
install_libs

# --- PHẦN 4: CHẠY CHƯƠNG TRÌNH ---
# Kiểm tra file thực thi ẩn có tồn tại không
if [ ! -f "$BINARY" ]; then
    echo "LOI NGHIEM TRONG: Khong tim thay file '.secure_usb'!"
    echo "Ban hay chac chan da doi ten file 'secure_usb' thanh '.secure_usb' (co dau cham)."
    exit 1
fi

# Dọn dẹp kết nối cũ
if mount | grep -q "$PLAIN_POINT"; then
    sudo fusermount -uz "$PLAIN_POINT"
fi

echo ">>> Nhap mat khau:"
sudo "$BINARY" -f -s "$PLAIN_POINT" --root="$CIPHER_DIR" -o allow_other
