#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <sys/types.h>
#include <dirent.h> 
#include <time.h>
#include <sys/stat.h>
#include <zlib.h>
#include <sodium.h>
#include <limits.h>

// === CẤU HÌNH BẢO MẬT HASH ===

const char PASSWORD_HASH[crypto_pwhash_STRBYTES] = 
    "$argon2id$v=19$m=65536,t=2,p=1$N9zNbtx9epcKdUWZKgQqDA$4wHGVCNT8Ql6USw+i5lxPY2MWYf7zfSx/XoKVHGDJYI"; 

// === BIẾN TOÀN CỤC ===
static char *root_path;
static unsigned char global_key[crypto_secretbox_KEYBYTES];
static int is_logged_in = 0; // 0: Chưa, 1: Rồi

// Muối cố định
static const unsigned char global_salt[crypto_pwhash_SALTBYTES] = {
    0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF,
    0xFE, 0xDC, 0xBA, 0x09, 0x87, 0x65, 0x43, 0x21
};

struct file_header {
    unsigned long original_size;
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
};

// === HÀM BẢO MẬT ===

// Hàm khởi tạo khóa mặc định (chỉ dùng để điền vào global_key khi khởi động)
int init_global_key() {
    printf(">>> Đang khởi tạo khóa mặc định...\n");
    // Khởi tạo khóa từ một chuỗi rỗng (khóa thực sẽ được tạo sau khi đăng nhập)
    if (crypto_pwhash(global_key, sizeof(global_key),
                      "", 0, // Chuỗi rỗng
                      global_salt,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_DEFAULT) != 0) {
        return -1;
    }
    return 0;
}

// Hàm xác minh mật khẩu bằng HASH
int verify_password(const char *input_password) {
    if (crypto_pwhash_str_verify(PASSWORD_HASH, input_password, strlen(input_password)) == 0) {
        return 0;
    }
    return -1;
}

// HÀM QUAN TRỌNG: TẠO KHÓA GIẢI MÃ THỰC TẾ TỪ MẬT KHẨU NHẬP VÀO
int create_runtime_key(const char *input_password) {
    if (crypto_pwhash(global_key, sizeof(global_key),
                      input_password, strlen(input_password),
                      global_salt,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_DEFAULT) != 0) {
        return -1;
    }
    return 0;
}

// === HÀM TRỢ GIÚP FILE ===
static char* get_full_path(const char *path) {
    char *f = malloc(PATH_MAX); strcpy(f, root_path);
    strncat(f, path, PATH_MAX - strlen(root_path) - 1); return f;
}
static char* get_temp_path(const char *path) {
    char *f = get_full_path(path); char *t = malloc(strlen(f)+5);
    strcpy(t, f); strcat(t, ".tmp"); free(f); return t;
}
void log_op(const char* op, const char* path) {
    // Sửa lỗi hardcode path, dùng root_path động
    char log_file_path[PATH_MAX];
    snprintf(log_file_path, sizeof(log_file_path), "%s/my_fuse.log", root_path);

    FILE *f = fopen(log_file_path, "a");
    if (f) {
        time_t n = time(NULL); char t[26];
        strftime(t, 26, "%Y-%m-%d %H:%M:%S", localtime(&n));
        fprintf(f, "[%s] %s %s\n", t, op, path); fclose(f);
    }
}

// === CÁC HÀM FUSE ===

static int my_getattr(const char *path, struct stat *stbuf) {
    if (strcmp(path, "/LOGIN_HERE") == 0) {
        if(is_logged_in){
 	    return -ENOENT;
        }
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFREG | 0666; 
        stbuf->st_nlink = 1;
        stbuf->st_size = 100;
        return 0;
    }
    if (!is_logged_in && strcmp(path, "/") != 0) return -ENOENT;

    char* f = get_full_path(path); int res = lstat(f, stbuf);
    if (res == -1) { free(f); return -errno; }
    
    // Chỉ cập nhật kích thước nếu file là file mã hóa
    if (S_ISREG(stbuf->st_mode) && stbuf->st_size > sizeof(struct file_header)) {
        struct file_header h; int fd = open(f, O_RDONLY);
        if (fd != -1) {
            if (pread(fd, &h, sizeof(h), 0) == sizeof(h)) stbuf->st_size = h.original_size;
            close(fd);
        }
    }
    free(f); return 0;
}

static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t o, struct fuse_file_info *fi) {
    (void)o; (void)fi; 
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    if (!is_logged_in) {
        filler(buf, "LOGIN_HERE", NULL, 0);
        return 0;
    }

    char* f = get_full_path(path); DIR *dp = opendir(f);
    if (!dp) { free(f); return -errno; }
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strstr(de->d_name, ".tmp") || strstr(de->d_name, ".salt") || strcmp(de->d_name, ".")==0 || strcmp(de->d_name, "..")==0) continue;
        filler(buf, de->d_name, NULL, 0);
    }
    closedir(dp); free(f); return 0;
}

// --- TRUNCATE ---
static int my_truncate(const char *path, off_t size) {
    if (strcmp(path, "/LOGIN_HERE") == 0) return 0; 
    if (!is_logged_in) return -EACCES;
    char* f = get_full_path(path);
    int res = truncate(f, size);
    free(f);
    return (res == -1) ? -errno : 0;
}

// --- WRITE (Xử lý Đăng nhập + Cập nhật Khóa) ---
static int my_write(const char *path, const char *buf, size_t size, off_t o, struct fuse_file_info *fi) {
    if (strcmp(path, "/LOGIN_HERE") == 0) {
        
        is_logged_in = 0; // Reset trạng thái trước khi kiểm tra

        char password[256];
        size_t len = (size < 255) ? size : 255;
        strncpy(password, buf, len);
        password[len] = '\0';
        
        printf(">>> Đang kiểm tra mật khẩu: '%s'\n", password);

        if (verify_password(password) == 0) {
            
            if (create_runtime_key(password) != 0) {
                printf(">>> LỖI: Không thể tạo khóa chạy (Runtime Key).\n");
                return -EACCES; 
            }
            
            is_logged_in = 1;
            printf(">>> ĐÚNG MẬT KHẨU! Đăng nhập thành công, khóa đã cập nhật.\n");
       return -ENOENT;
 	} else {
            // is_logged_in vẫn là 0
            printf(">>> SAI MẬT KHẨU! Hệ thống đã khóa.\n");
	return -EACCES;
        }
    }

    if (!is_logged_in) return -EACCES;

    char* t=get_temp_path(path); int d=open(t,O_WRONLY|O_CREAT,0644); 
    if (d == -1) { free(t); return -errno; }
    int res = pwrite(d, buf, size, o);
    close(d); free(t); 
    return (res == -1) ? -errno : size;
}

static int my_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (strcmp(path, "/LOGIN_HERE") == 0) {
        const char *msg = "Ghi mat khau (Nhom01) vao file nay de mo khoa.\n";
        size_t len = strlen(msg);
        if (offset >= len) return 0;
        if (offset + size > len) size = len - offset;
        memcpy(buf, msg + offset, size);
        return size;
    }
    if (!is_logged_in) return -EACCES;

    struct stat st; fstat(fi->fh, &st);
    if (st.st_size <= sizeof(struct file_header)) return 0;
    struct file_header h; pread(fi->fh, &h, sizeof(h), 0);
    size_t enc_len = st.st_size - sizeof(h);
    unsigned char *enc = malloc(enc_len);
    pread(fi->fh, enc, enc_len, sizeof(h));
    size_t dec_len = enc_len - crypto_secretbox_MACBYTES;
    unsigned char *dec = malloc(dec_len);
    
    // Dùng global_key đã được tạo bởi create_runtime_key() để giải mã
    if (crypto_secretbox_open_easy(dec, enc, enc_len, h.nonce, global_key) != 0) {
        free(enc); free(dec); return -EIO;
    }
    free(enc);
    Bytef *raw = malloc(h.original_size);
    unsigned long destLen = h.original_size;
    uncompress(raw, &destLen, dec, dec_len);
    free(dec);
    if (offset < h.original_size) {
        size_t copy_len = (offset + size > h.original_size) ? (h.original_size - offset) : size;
        memcpy(buf, raw + offset, copy_len);
        free(raw); return copy_len;
    }
    free(raw); return 0;
}

static int my_open(const char *p, struct fuse_file_info *fi) {
    if (strcmp(p, "/LOGIN_HERE") == 0) return 0;
    if (!is_logged_in) return -EACCES;
    
    char* f = get_full_path(p); 
    int r = open(f, fi->flags); 
    free(f); 
    
    if (r == -1) {
        return -errno;
    }
    
    fi->fh = r; 
    return 0;
}
static int my_create(const char *p, mode_t m, struct fuse_file_info *fi) {
    if (!is_logged_in) return -EACCES;
    
    log_op("create", p); 
    char* f = get_full_path(p); 
    int r = open(f, fi->flags, m); 
    free(f); 
    
    if (r == -1) {
        return -errno; 
    }
    
    fi->fh = r; 
    return 0;
}
static int my_unlink(const char *p) {
    if (!is_logged_in) return -EACCES;
    
    log_op("delete", p); 
    char* f = get_full_path(p); 
    char* t = get_temp_path(p);
    
    unlink(t); 
    int r = unlink(f); 
    
    free(f); 
    free(t); 
    
    if (r == -1) {
        return -errno;
    }
    return 0;
}
static int my_release(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, "/LOGIN_HERE") == 0) return 0; 
    if (!is_logged_in) return 0;

    log_op("save", path); close(fi->fh);
    char* tf = get_temp_path(path); int tfd = open(tf, O_RDONLY);
    if (tfd == -1) { free(tf); return 0; }

    struct stat st; fstat(tfd, &st);
    unsigned long orig_len = st.st_size;
    Bytef *raw = malloc(orig_len);
    pread(tfd, raw, orig_len, 0); close(tfd);

    unsigned long comp_len = compressBound(orig_len);
    Bytef *comp = malloc(comp_len);
    compress(comp, &comp_len, raw, orig_len); free(raw);

    struct file_header h; h.original_size = orig_len;
    randombytes_buf(h.nonce, sizeof(h.nonce));

    size_t enc_len = comp_len + crypto_secretbox_MACBYTES;
    unsigned char *enc = malloc(enc_len);
    
    crypto_secretbox_easy(enc, comp, comp_len, h.nonce, global_key);
    free(comp);

    char* ff = get_full_path(path);
    int rfd = open(ff, O_WRONLY | O_TRUNC);
    pwrite(rfd, &h, sizeof(h), 0);
    pwrite(rfd, enc, enc_len, sizeof(h));
    
    close(rfd); free(enc); free(ff); unlink(tf); free(tf);
    return 0;
}

static const struct fuse_operations my_oper = {
    .getattr  = my_getattr,
    .readdir  = my_readdir,
    .open     = my_open,
    .read     = my_read,
    .write    = my_write,
    .create   = my_create,
    .unlink   = my_unlink,
    .release  = my_release,
    .truncate = my_truncate,
};

int main(int argc, char *argv[]) {
    if (sodium_init() < 0) return 1;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    
    char* custom_root = NULL;
    int root_idx = -1;
    for (int i = 0; i < args.argc; i++) {
        if (strncmp(args.argv[i], "--root=", 7) == 0) {
            custom_root = args.argv[i] + 7;
            root_idx = i; break;
        }
    }
    if (!custom_root) return 1;
    root_path = realpath(custom_root, NULL);
    if (root_idx != -1) {
        for (int i = root_idx; i < args.argc-1; i++) args.argv[i] = args.argv[i+1];
        args.argc--;
    }

    if (init_global_key() != 0) return 1;

    printf("\n>>> SECURE FS V7.2 (Pass: Verifying via HASH) <<<\n");
    printf("Hay nhap mat khau vao file LOGIN_HERE!\n");
    
    return fuse_main(args.argc, argv, &my_oper, NULL);
}
