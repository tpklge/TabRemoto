/* Derived from PuTTY 0.83 import.c openssh_new_write (MIT licence).
 * Only the file sink was replaced with a secure in-memory strbuf sink.
 * See research/putty/LICENCE and patches/putty-0.83-esp-idf.patch.
 */
#include "putty.h"
#include "ssh.h"
strbuf *tabremoto_putty_openssh(ssh2_userkey *ukey) {
  strbuf *pubblob, *privblob, *cblob;
  int padvalue;
  unsigned checkint;
  const char *passphrase = NULL;
  strbuf *output = NULL;
  unsigned char bcrypt_salt[16];
  const int bcrypt_rounds = 16;

  /* OpenSSH's private key files never contain a certificate, so
   * revert to the underlying base key if necessary */
  ssh_key *key = ssh_key_base_key(ukey->key);

  /*
   * Fetch the key blobs and find out the lengths of things.
   */
  pubblob = strbuf_new();
  ssh_key_public_blob(key, BinarySink_UPCAST(pubblob));
  privblob = strbuf_new_nm();
  ssh_key_openssh_blob(key, BinarySink_UPCAST(privblob));

  /*
   * Construct the cleartext version of the blob.
   */
  cblob = strbuf_new_nm();

  /* Magic number. */
  put_asciz(cblob, "openssh-key-v1");

  /* Cipher and kdf names, and kdf options. */
  if (!passphrase) {
    memset(bcrypt_salt, 0, sizeof(bcrypt_salt)); /* prevent warnings */
    put_stringz(cblob, "none");
    put_stringz(cblob, "none");
    put_stringz(cblob, "");
  } else {
    strbuf *substr;

    random_read(bcrypt_salt, sizeof(bcrypt_salt));
    put_stringz(cblob, "aes256-ctr");
    put_stringz(cblob, "bcrypt");
    substr = strbuf_new_nm();
    put_string(substr, bcrypt_salt, sizeof(bcrypt_salt));
    put_uint32(substr, bcrypt_rounds);
    put_stringsb(cblob, substr);
  }

  /* Number of keys. */
  put_uint32(cblob, 1);

  /* Public blob. */
  put_string(cblob, pubblob->s, pubblob->len);

  /* Private section. */
  {
    strbuf *cpblob = strbuf_new_nm();

    /* checkint. */
    uint8_t checkint_buf[4];
    random_read(checkint_buf, 4);
    checkint = GET_32BIT_MSB_FIRST(checkint_buf);
    put_uint32(cpblob, checkint);
    put_uint32(cpblob, checkint);

    /* Private key. The main private blob goes inline, with no string
     * wrapper. */
    put_stringz(cpblob, ssh_key_ssh_id(key));
    put_data(cpblob, privblob->s, privblob->len);

    /* Comment. */
    put_stringz(cpblob, ukey->comment);

    /* Pad out the encrypted section. */
    padvalue = 1;
    do {
      put_byte(cpblob, padvalue++);
    } while (cpblob->len & 15);

    if (passphrase) {
      /*
       * Encrypt the private section. We need 48 bytes of key
       * material: 32 bytes AES key + 16 bytes iv.
       */
      unsigned char keybuf[48];
      ssh_cipher *cipher;

      openssh_bcrypt(ptrlen_from_asciz(passphrase),
                     make_ptrlen(bcrypt_salt, sizeof(bcrypt_salt)),
                     bcrypt_rounds, keybuf, sizeof(keybuf));

      cipher = ssh_cipher_new(&ssh_aes256_sdctr);
      ssh_cipher_setkey(cipher, keybuf);
      ssh_cipher_setiv(cipher, keybuf + 32);
      ssh_cipher_encrypt(cipher, cpblob->u, cpblob->len);
      ssh_cipher_free(cipher);

      smemclr(keybuf, sizeof(keybuf));
    }

    put_stringsb(cblob, cpblob);
  }

  /*
   * And save it. We'll use Unix line endings just in case it's
   * subsequently transferred in binary mode.
   */
  output = strbuf_new_nm();
  put_data(output, "-----BEGIN OPENSSH PRIVATE KEY-----\n", 36);
  base64_encode_bs(BinarySink_UPCAST(output), ptrlen_from_strbuf(cblob), 64);
  put_data(output, "-----END OPENSSH PRIVATE KEY-----\n", 34);

  if (cblob)
    strbuf_free(cblob);
  if (privblob)
    strbuf_free(privblob);
  if (pubblob)
    strbuf_free(pubblob);
  return output;
}
