/* Link against OpenSSL built with TABREMOTO_SCRYPT_PAGED. The allocator
 * simulates a fragmented heap: no single allocation above 128 KiB. */
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t largest, page_attempt, fail_page, live_pages;
static void *pages[256];
static void *allocate(size_t n, const char *file, int line)
{
    (void)file; (void)line;
    if (n > largest) largest = n;
    if (n > 131072) return NULL;
    if (n == 65536 && ++page_attempt == fail_page) return NULL;
    void *p = malloc(n);
    if (p && n == 65536) {
        for (size_t i = 0; i < 256; ++i) if (!pages[i]) {
            pages[i] = p; ++live_pages; break;
        }
    }
    return p;
}
static void release(void *p, const char *file, int line)
{
    (void)file; (void)line;
    for (size_t i = 0; i < 256; ++i) if (p && pages[i] == p) {
        pages[i] = NULL; --live_pages; break;
    }
    free(p);
}
static void *resize(void *p, size_t n, const char *file, int line)
{
    (void)file; (void)line;
    if (n > largest) largest = n;
    return n > 131072 ? NULL : realloc(p, n);
}
int main(void)
{
    assert(CRYPTO_set_mem_functions(allocate, resize, release));
    OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, NULL);
    unsigned char output[32];
    const size_t failures[] = {1, 2, 127, 256};
    for (size_t i = 0; i < 4; ++i) {
        fail_page = failures[i]; page_attempt = 0;
        assert(EVP_PBE_scrypt("Tab58221", 8, (unsigned char *)"salt", 4,
                             16384, 8, 1, 24*1024*1024, output, 32) == 0);
        assert(live_pages == 0);
        ERR_clear_error();
    }
    fail_page = 0;
    for (unsigned test = 0; test < 12; ++test) {
        char pass[32], salt[32];
        snprintf(pass, sizeof(pass), "Tab%05u", test);
        snprintf(salt, sizeof(salt), "salt-%u", test);
        assert(EVP_PBE_scrypt(pass, strlen(pass), (unsigned char *)salt,
                             strlen(salt), test < 4 ? 16 : 16384,
                             test < 4 ? 1 : 8, 1, 24*1024*1024, output, 32));
        assert(live_pages == 0);
        printf("%u ", test);
        for (unsigned j = 0; j < 32; ++j) printf("%02x", output[j]);
        puts("");
    }
    const unsigned cases[][3] = {{16,1,2},{32,3,2},{1024,8,16},{16384,8,1}};
    for (unsigned i=0; i<4; ++i) {
        assert(EVP_PBE_scrypt("", 0, (unsigned char *)"", 0,
                             cases[i][0], cases[i][1], cases[i][2],
                             24*1024*1024, output, 32));
        assert(live_pages == 0);
        printf("params:%u,%u,%u ", cases[i][0], cases[i][1], cases[i][2]);
        for(unsigned j=0;j<32;++j)printf("%02x",output[j]);
        puts("");
    }
    fprintf(stderr, "allocation_failure_cleanup=passed largest_request=%zu\n", largest);
    assert(largest <= 65536);
    OPENSSL_cleanup();
    return 0;
}
