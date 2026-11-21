/* Real OpenSSL tests on P4. This is not an RDP session or NLA implementation. */
#include "rdp_port.h"
#include "crypto_gate.h"
#include <stdio.h>
#include <string.h>
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/evp.h"
#include "openssl/rand.h"
#include "openssl/x509v3.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "bootloader_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static rdp_port_result_t result;
rdp_port_result_t rdp_port_probe_status(void){taskENTER_CRITICAL(&lock);rdp_port_result_t r=result;taskEXIT_CRITICAL(&lock);return r;}
static void publish(rdp_port_result_t r){taskENTER_CRITICAL(&lock);result=r;taskEXIT_CRITICAL(&lock);}
static int handshake(SSL_CTX* server_ctx,SSL_CTX* client_ctx,const char* host,long* verification,rdp_port_result_t* r){
 SSL *server=SSL_new(server_ctx),*client=SSL_new(client_ctx);BIO *a=NULL,*b=NULL;int ok=0;
 if(!server||!client||!BIO_new_bio_pair(&a,16384,&b,16384))goto done;
 SSL_set_bio(server,a,a);a=NULL;SSL_set_bio(client,b,b);b=NULL;
 SSL_set_accept_state(server);SSL_set_connect_state(client);
 if(SSL_set1_host(client,host)!=1)goto done;
 int64_t end=esp_timer_get_time()+5000000;
 while(esp_timer_get_time()<end){
  int c=SSL_do_handshake(client);int ce=c==1?0:SSL_get_error(client,c);
  if(c!=1&&ce!=SSL_ERROR_WANT_READ&&ce!=SSL_ERROR_WANT_WRITE)break;
  int s=SSL_do_handshake(server);int se=s==1?0:SSL_get_error(server,s);
  if(s!=1&&se!=SSL_ERROR_WANT_READ&&se!=SSL_ERROR_WANT_WRITE)break;
  if(SSL_is_init_finished(server)&&SSL_is_init_finished(client)){
   const char payload[]="TabRemoto real TLS probe";char received[sizeof(payload)]={0};
   if(SSL_write(client,payload,sizeof(payload))!=(int)sizeof(payload))break;
   if(SSL_read(server,received,sizeof(received))!=(int)sizeof(payload)||memcmp(payload,received,sizeof(payload)))break;
   if(r){snprintf(r->protocol,sizeof(r->protocol),"%s",SSL_get_version(client));snprintf(r->cipher,sizeof(r->cipher),"%s",SSL_get_cipher_name(client));}
   ok=1;break;
  }
  vTaskDelay(1);
 }
 *verification=SSL_get_verify_result(client);
 done:BIO_free(a);BIO_free(b);SSL_free(client);SSL_free(server);return ok;
}
static void probe(void* unused){
 (void)unused;rdp_port_result_t r={0};r.running=true;r.internal_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 int64_t started=esp_timer_get_time();SSL_CTX *server=NULL,*client=NULL,*untrusted=NULL;EVP_PKEY* key=NULL;X509* certificate=NULL;X509_EXTENSION* san=NULL;
 unsigned char digest[32],random_a[32],random_b[32];unsigned int size=0;long verification=-1;
 static const unsigned char expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
 // On P4 the C6 radio does not seed the P4 RNG. Keep the SDK's SAR entropy
 // source enabled throughout this single-worker probe; no other ADC user here.
 bootloader_random_enable();
 snprintf(r.version,sizeof(r.version),"%s",OpenSSL_version(OPENSSL_VERSION));
 if(!OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG,NULL))goto failed;
 r.sha256_ok=EVP_Digest("abc",3,digest,&size,EVP_sha256(),NULL)==1&&size==32&&!memcmp(digest,expected,32);
 r.random_ok=RAND_status()==1&&RAND_bytes(random_a,32)==1&&RAND_bytes(random_b,32)==1&&memcmp(random_a,random_b,32)!=0;
 if(!r.sha256_ok||!r.random_ok)goto failed;
 key=EVP_EC_gen("prime256v1");certificate=X509_new();if(!key||!certificate)goto failed;
 if(X509_set_version(certificate,2)!=1||ASN1_INTEGER_set(X509_get_serialNumber(certificate),1)!=1||!X509_gmtime_adj(X509_getm_notBefore(certificate),-60)||!X509_gmtime_adj(X509_getm_notAfter(certificate),3600)||X509_set_pubkey(certificate,key)!=1)goto failed;
 X509_NAME* name=X509_get_subject_name(certificate);
 if(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,(unsigned char*)"tabremoto-lab.invalid",-1,-1,0)!=1||X509_set_issuer_name(certificate,name)!=1)goto failed;
 san=X509V3_EXT_conf_nid(NULL,NULL,NID_subject_alt_name,"DNS:tabremoto-lab.invalid");
 if(!san||X509_add_ext(certificate,san,-1)!=1||!X509_sign(certificate,key,EVP_sha256()))goto failed;
 server=SSL_CTX_new(TLS_server_method());client=SSL_CTX_new(TLS_client_method());untrusted=SSL_CTX_new(TLS_client_method());
 if(!server||!client||!untrusted)goto failed;
 SSL_CTX_set_min_proto_version(server,TLS1_2_VERSION);SSL_CTX_set_max_proto_version(server,TLS1_2_VERSION);
 SSL_CTX_set_min_proto_version(client,TLS1_2_VERSION);SSL_CTX_set_min_proto_version(untrusted,TLS1_2_VERSION);
 SSL_CTX_set_verify(client,SSL_VERIFY_PEER,NULL);SSL_CTX_set_verify(untrusted,SSL_VERIFY_PEER,NULL);
 if(SSL_CTX_use_certificate(server,certificate)!=1||SSL_CTX_use_PrivateKey(server,key)!=1||X509_STORE_add_cert(SSL_CTX_get_cert_store(client),certificate)!=1)goto failed;
 r.tls_ok=handshake(server,client,"tabremoto-lab.invalid",&verification,&r)&&verification==X509_V_OK;
 if(!r.tls_ok)goto failed;
 ERR_clear_error();verification=-1;
 r.hostname_rejected=!handshake(server,client,"wrong-host.invalid",&verification,NULL)&&verification==X509_V_ERR_HOSTNAME_MISMATCH;
 ERR_clear_error();verification=-1;
 r.untrusted_rejected=!handshake(server,untrusted,"tabremoto-lab.invalid",&verification,NULL)&&verification!=X509_V_OK;
 if(!r.hostname_rejected||!r.untrusted_rejected)goto failed;
 snprintf(r.detail,sizeof(r.detail),"TLS pair and certificate checks passed; RDP/NLA not implemented");goto done;
 failed:r.error=1;ERR_error_string_n(ERR_peek_last_error(),r.detail,sizeof(r.detail));
 done:SSL_CTX_free(server);SSL_CTX_free(client);SSL_CTX_free(untrusted);X509_EXTENSION_free(san);X509_free(certificate);EVP_PKEY_free(key);
 OPENSSL_cleanse(random_a,sizeof(random_a));OPENSSL_cleanse(random_b,sizeof(random_b));bootloader_random_disable();
 r.running=false;r.elapsed_ms=(esp_timer_get_time()-started)/1000;r.internal_after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);publish(r);crypto_gate_leave();vTaskDelete(NULL);
}
int rdp_port_probe_start(void){if(!crypto_gate_enter())return -1;taskENTER_CRITICAL(&lock);if(result.running){taskEXIT_CRITICAL(&lock);crypto_gate_leave();return -1;}result=(rdp_port_result_t){.running=true};taskEXIT_CRITICAL(&lock);
 if(xTaskCreatePinnedToCore(probe,"rdp_tls_probe",16384,NULL,1,NULL,1)!=pdPASS){rdp_port_result_t r={.error=2};snprintf(r.detail,sizeof(r.detail),"No memory for TLS probe");publish(r);crypto_gate_leave();return -1;}return 0;}
