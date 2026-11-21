#include "putty_key.h"
#include "putty.h"
#include "ssh.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
strbuf *tabremoto_putty_openssh(ssh2_userkey *);
void random_read(void *data, size_t len) {
#ifdef ESP_PLATFORM
  esp_fill_random(data, len);
#else
  FILE *f = fopen("/dev/urandom", "rb");
  if (!f || fread(data, 1, len, f) != len)
    abort();
  fclose(f);
#endif
}
unsigned long getticks(void) {
#ifdef ESP_PLATFORM
  return esp_timer_get_time() / 1000;
#else
  return 0;
#endif
}
void tabremoto_putty_yield(void) {
#ifdef ESP_PLATFORM
  vTaskDelay(1);
#endif
}
void old_keyfile_warning(void) {}
void out_of_memory(void) { abort(); }
static int convert_terminated(const char *input, size_t size,
                              const char *password, char **output,
                              size_t *output_size, const char **error) {
  *output = NULL;
  *output_size = 0;
  *error = "Chave PPK invalida";
  if (size > 8192 || size < 25 || memchr(input, 0, size))
    return 0;
  if (strncmp(input, "PuTTY-User-Key-File-2:", 22) &&
      strncmp(input, "PuTTY-User-Key-File-3:", 22)) {
    *error = "PPK: somente versoes 2 e 3";
    return 0;
  }
  // Resource bounds precede the upstream parser/KDF, without changing crypto.
  const char *names[] = {
      "Argon2-Memory: ", "Argon2-Passes: ", "Argon2-Parallelism: "};
  unsigned caps[] = {16384, 100, 8};
  size_t memory = 0;
  for (unsigned i = 0; i < 3; i++) {
    const char *p = strstr(input, names[i]);
    while (p && p != input && p[-1] != '\n')
      p = strstr(p + 1, names[i]);
    if (p) {
      char *end;
      unsigned long n = strtoul(p + strlen(names[i]), &end, 10);
      if (!n || n > caps[i] || (*end != '\r' && *end != '\n')) {
        *error = "PPK excede limites de memoria/custo do Tab5";
        return 0;
      }
      if (i == 0)
        memory = n * 1024;
    }
  }
#ifdef ESP_PLATFORM
  if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) <
      memory + 2 * 1024 * 1024) {
    *error = "Memoria insuficiente para descriptografar esta PPK";
    return 0;
  }
#endif
  BinarySource source;
  BinarySource_BARE_INIT(&source, input, size);
  bool encrypted = ppk_encrypted_s(&source, NULL);
  BinarySource_BARE_INIT(&source, input, size);
  ssh2_userkey *key =
      ppk_load_s(&source, encrypted && password ? password : "", error);
  if (!key || key == SSH2_WRONG_PASSPHRASE) {
    if (key == SSH2_WRONG_PASSPHRASE)
      *error = "Passphrase PPK incorreta";
    return 0;
  }
  strbuf *pem = tabremoto_putty_openssh(key);
  ssh_key_free(key->key);
  sfree(key->comment);
  sfree(key);
  if (!pem) {
    *error = "Nao foi possivel converter a chave em memoria";
    return 0;
  }
  *output = malloc(pem->len + 1);
  if (!*output) {
    strbuf_free(pem);
    *error = "Sem memoria para a chave";
    return 0;
  }
  memcpy(*output, pem->s, pem->len);
  (*output)[pem->len] = 0;
  *output_size = pem->len;
  strbuf_free(pem);
  return 1;
}

int tabremoto_ppk_convert(const char *input, size_t size, const char *password,
                          char **output, size_t *output_size,
                          const char **error) {
  *output = NULL;
  *output_size = 0;
  *error = "Chave PPK invalida ou maior que 8 KiB";
  if (!input || size > 8192 || size < 25 || memchr(input, 0, size))
    return 0;
  char *copy = malloc(size + 1);
  if (!copy) {
    *error = "Sem memoria para importar PPK";
    return 0;
  }
  memcpy(copy, input, size);
  copy[size] = 0;
  int result =
      convert_terminated(copy, size, password, output, output_size, error);
  smemclr(copy, size);
  free(copy);
  return result;
}
