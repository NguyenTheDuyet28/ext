#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <sys/types.h>
#include <dirent.h> 
#include <time.h>
#include <sys/stat.h>
#include <zlib.h>
#include <sodium.h>
#include <limits.h>
#include <termios.h>
#include <keyutils.h>

// Mật khẩu mặc định: "password123" (Bạn có thể đổi hash nếu muốn)
const char PASSWORD_HASH[crypto_pwhash_STRBYTES] = 
    "$argon2id$v=19$m=65536,t=2,p=1$N9zNbtx9epcKdUWZKgQqDA$4wHGVCNT8Ql6USw+i5lxPY2MWYf7zfSx/XoKVHGDJYI"; 

static char *root_path;
static key_serial_t global_key_id = 0; 
static const unsigned char global_salt[crypto_pwhash_SALTBYTES] = {
    0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x09, 0x87, 0x65, 0x43, 0x21
};

struct file_header { unsigned long original_size; unsigned char nonce[crypto_secretbox_NONCEBYTES]; };

// --- CÁC HÀM HỖ TRỢ ---
static char* get_full_path(const char *path) {
    char *f = malloc(PATH_MAX); strcpy(f, root_path);
    strncat(f, path, PATH_MAX - strlen(root_path) - 1); return f;
}
static char* get_temp_path(const char *path) {
    char *f = get_full_path(path); char *t = malloc(strlen(f)+5);
    strcpy(t, f); strcat(t, ".tmp"); free(f); return t;
}
void get_password(char *password, size_t size) {
    struct termios oldt, newt; int i = 0; int c;
    tcgetattr(STDIN_FILENO, &oldt); newt = oldt; newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    while ((c = getchar()) != '\n' && c != EOF && i < size - 1) password[i++] = c;
    password[i] = '\0'; tcsetattr(STDIN_FILENO, TCSANOW, &oldt); printf("\n");
}
int fetch_key_from_kernel(unsigned char *buffer, size_t len) {
    long ret = keyctl_read(global_key_id, (char*)buffer, len); return (ret < 0) ? -1 : 0;
}

// --- FUSE CORE OPERATIONS ---
static int my_getattr(const char *path, struct stat *stbuf) {
    char* f = get_full_path(path); int res = lstat(f, stbuf);
    if (res == -1) { free(f); return -errno; }
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
    (void)o; (void)fi; filler(buf, ".", NULL, 0); filler(buf, "..", NULL, 0);
    char* f = get_full_path(path); DIR *dp = opendir(f);
    if (!dp) { free(f); return -errno; }
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strstr(de->d_name, ".tmp") || strcmp(de->d_name, ".")==0 || strcmp(de->d_name, "..")==0) continue;
        filler(buf, de->d_name, NULL, 0);
    }
    closedir(dp); free(f); return 0;
}
static int my_open(const char *p, struct fuse_file_info *fi) {
    char* f = get_full_path(p); int r = open(f, fi->flags); 
    free(f); if (r == -1) return -errno; fi->fh = r; return 0;
}
static int my_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct stat st; fstat(fi->fh, &st);
    if (st.st_size <= sizeof(struct file_header)) return 0;
    struct file_header h; pread(fi->fh, &h, sizeof(h), 0);
    size_t enc_len = st.st_size - sizeof(h);
    unsigned char *enc = malloc(enc_len); pread(fi->fh, enc, enc_len, sizeof(h));
    
    unsigned char temp_key[crypto_secretbox_KEYBYTES];
    if (fetch_key_from_kernel(temp_key, sizeof(temp_key)) < 0) { free(enc); return -EACCES; }

    size_t dec_len = enc_len - crypto_secretbox_MACBYTES;
    unsigned char *dec = malloc(dec_len);
    if (crypto_secretbox_open_easy(dec, enc, enc_len, h.nonce, temp_key) != 0) {
        sodium_memzero(temp_key, sizeof(temp_key)); free(enc); free(dec); return -EIO;
    }
    sodium_memzero(temp_key, sizeof(temp_key)); free(enc);

    Bytef *raw = malloc(h.original_size); unsigned long destLen = h.original_size;
    uncompress(raw, &destLen, dec, dec_len); free(dec);

    if (offset < h.original_size) {
        size_t copy_len = (offset + size > h.original_size) ? (h.original_size - offset) : size;
        memcpy(buf, raw + offset, copy_len);
    }
    free(raw); return (offset < h.original_size) ? ((offset + size > h.original_size) ? (h.original_size - offset) : size) : 0;
}
static int my_write(const char *path, const char *buf, size_t size, off_t o, struct fuse_file_info *fi) {
    char* t=get_temp_path(path); int d=open(t, O_WRONLY|O_CREAT, 0644); 
    if (d == -1) { free(t); return -errno; }
    int res = pwrite(d, buf, size, o); close(d); free(t); return (res == -1) ? -errno : size;
}
static int my_create(const char *p, mode_t m, struct fuse_file_info *fi) {
    char* f = get_full_path(p); int r = open(f, fi->flags, m); 
    free(f); if (r == -1) return -errno; fi->fh = r; return 0;
}
static int my_release(const char *path, struct fuse_file_info *fi) {
    close(fi->fh); char* tf = get_temp_path(path); int tfd = open(tf, O_RDONLY);
    if (tfd == -1) { free(tf); return 0; }
    struct stat st; fstat(tfd, &st); unsigned long orig_len = st.st_size;
    Bytef *raw = malloc(orig_len); pread(tfd, raw, orig_len, 0); close(tfd);
    unsigned long comp_len = compressBound(orig_len);
    Bytef *comp = malloc(comp_len); compress(comp, &comp_len, raw, orig_len); free(raw);
    
    struct file_header h; h.original_size = orig_len; randombytes_buf(h.nonce, sizeof(h.nonce));
    unsigned char temp_key[crypto_secretbox_KEYBYTES];
    if (fetch_key_from_kernel(temp_key, sizeof(temp_key)) < 0) { free(comp); return -EACCES; }

    size_t enc_len = comp_len + crypto_secretbox_MACBYTES;
    unsigned char *enc = malloc(enc_len);
    crypto_secretbox_easy(enc, comp, comp_len, h.nonce, temp_key);
    sodium_memzero(temp_key, sizeof(temp_key)); free(comp);

    char* ff = get_full_path(path); int rfd = open(ff, O_WRONLY | O_TRUNC);
    pwrite(rfd, &h, sizeof(h), 0); pwrite(rfd, enc, enc_len, sizeof(h));
    close(rfd); free(enc); free(ff); unlink(tf); free(tf); return 0;
}
static int my_truncate(const char *path, off_t size) {
    char* f = get_full_path(path); int res = truncate(f, size); free(f); return (res == -1) ? -errno : 0;
}
static int my_unlink(const char *p) { char* f = get_full_path(p); unlink(f); free(f); return 0; }

static const struct fuse_operations my_oper = {
    .getattr = my_getattr, .readdir = my_readdir, .open = my_open,
    .read = my_read, .write = my_write, .create = my_create,
    .unlink = my_unlink, .release = my_release, .truncate = my_truncate,
};

int main(int argc, char *argv[]) {
    if (sodium_init() < 0) return 1;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    char* custom_root = NULL; int root_idx = -1;
    for (int i = 0; i < args.argc; i++) {
        if (strncmp(args.argv[i], "--root=", 7) == 0) { custom_root = args.argv[i] + 7; root_idx = i; break; }
    }
    if (!custom_root || root_idx == -1) return 1;
    root_path = realpath(custom_root, NULL);
    for (int i = root_idx; i < args.argc-1; i++) {
       args.argv[i] = args.argv[i+1];
    }
args.argc--;
    printf("\n============================================\n");
    printf("     HE THONG USB AN TOAN (SAFE MODE)      \n");
    printf("============================================\n");
    
    char password[256]; 
    printf(">>> Nhap mat khau de mo khoa USB: "); 
    get_password(password, sizeof(password));

    if (crypto_pwhash_str_verify(PASSWORD_HASH, password, strlen(password)) != 0) { 
        printf("\n>>> SAI MAT KHAU! Tu choi truy cap.\n"); 
        return 1; 
    }

    unsigned char temp_key[crypto_secretbox_KEYBYTES];
    if (crypto_pwhash(temp_key, sizeof(temp_key), password, strlen(password), global_salt, crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_DEFAULT) != 0) return 1;
    
    global_key_id = add_key("user", "secure_fs_key", temp_key, sizeof(temp_key), KEY_SPEC_SESSION_KEYRING);
    sodium_memzero(temp_key, sizeof(temp_key)); sodium_memzero(password, sizeof(password));
    
    printf("\n>>> MAT KHAU DUNG!\n");
    printf(">>> File giai ma dang duoc mount ra thu muc 'plain'...\n");
    return fuse_main(args.argc, argv, &my_oper, NULL);
}
