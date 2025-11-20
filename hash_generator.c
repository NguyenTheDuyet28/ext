#include <stdio.h>
#include <string.h>
#include <sodium.h>

int main(void) {
    char hashed_password[crypto_pwhash_STRBYTES];
    const char *password = "Nhom01"; // Mật khẩu bạn muốn sử dụng

    if (sodium_init() == -1) {
        fprintf(stderr, "Loi khoi tao libsodium.\n");
        return 1;
    }

    if (crypto_pwhash_str(hashed_password, password, strlen(password),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        fprintf(stderr, "Loi tao HASH.\n");
        return 1;
    }

    printf("==================================================================\n");
    printf("1. MAT KHAU THO: %s\n", password);
    printf("2. GIA TRI HASH (DUNG DE PASTE VAO MY_FUSE.C):\n");
    printf("%s\n", hashed_password);
    printf("==================================================================\n");
    
    return 0;
}
