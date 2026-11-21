#include "profiles.hpp"
#include "storage.hpp"
#include "crypto_gate.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bootloader_random.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
namespace tabremoto {
namespace {
SemaphoreHandle_t mutex;QueueHandle_t queue;bool ready=false;
cJSON *config=nullptr;std::array<unsigned char,32> key{};uint32_t generation=0;
std::string message="Carregando redes e confianca...";
struct Job {std::string type,host,value,password;unsigned port=0;bool secured=false;~Job(){mbedtls_platform_zeroize(password.data(),password.size());}};
std::string canonical(std::string s){while(!s.empty()&&s.back()=='.')s.pop_back();for(auto &c:s)c=std::tolower(static_cast<unsigned char>(c));return s;}
const char *str(cJSON *o,const char *k){auto *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:"";}
void note(const std::string &s){xSemaphoreTake(mutex,portMAX_DELAY);message=s;xSemaphoreGive(mutex);}
bool device_key(){
 if(nvs_flash_init()!=ESP_OK)return false;
 nvs_handle_t h;if(nvs_open("tabremoto_cfg",NVS_READWRITE,&h)!=ESP_OK)return false;
 size_t n=key.size();esp_err_t e=nvs_get_blob(h,"profile_key",key.data(),&n);
 if(e==ESP_ERR_NVS_NOT_FOUND){bootloader_random_enable();esp_fill_random(key.data(),key.size());bootloader_random_disable();e=nvs_set_blob(h,"profile_key",key.data(),key.size());if(e==ESP_OK)e=nvs_commit(h);}
 nvs_close(h);return e==ESP_OK&&n==key.size();
}
struct Header{char magic[8];uint32_t generation,length;unsigned char nonce[12],tag[16];};
static_assert(sizeof(Header)==44);
cJSON *read(const std::string &path,uint32_t &g){
 FILE *f=fopen(path.c_str(),"rb");if(!f)return nullptr;
 Header h{};bool ok=fread(&h,1,sizeof(h),f)==sizeof(h)&&!memcmp(h.magic,"TRCFG001",8)&&h.length&&h.length<=32768;
 std::vector<unsigned char> cipher(ok?h.length:0),plain(ok?h.length+1:0);
 ok=ok&&fread(cipher.data(),1,cipher.size(),f)==cipher.size()&&fgetc(f)==EOF;fclose(f);
 mbedtls_gcm_context c;mbedtls_gcm_init(&c);
 ok=ok&&mbedtls_gcm_setkey(&c,MBEDTLS_CIPHER_ID_AES,key.data(),256)==0&&mbedtls_gcm_auth_decrypt(&c,h.length,h.nonce,12,reinterpret_cast<unsigned char*>(&h),16,h.tag,16,cipher.data(),plain.data())==0;
 mbedtls_gcm_free(&c);cJSON *j=ok?cJSON_ParseWithLength(reinterpret_cast<char*>(plain.data()),h.length):nullptr;
 if(!plain.empty())mbedtls_platform_zeroize(plain.data(),plain.size());
 if(j&&(!cJSON_IsArray(cJSON_GetObjectItem(j,"wifi"))||!cJSON_IsArray(cJSON_GetObjectItem(j,"pins")))){cJSON_Delete(j);j=nullptr;}
 if(j) { g=h.generation; }
 return j;
}
bool save(cJSON *next){
 char *text=cJSON_PrintUnformatted(next);if(!text)return false;size_t n=strlen(text);Header h{};memcpy(h.magic,"TRCFG001",8);h.generation=generation+1;h.length=n;
 bootloader_random_enable();esp_fill_random(h.nonce,12);bootloader_random_disable();
 std::vector<unsigned char> cipher(n);mbedtls_gcm_context c;mbedtls_gcm_init(&c);
 bool ok=n&&n<=32768&&mbedtls_gcm_setkey(&c,MBEDTLS_CIPHER_ID_AES,key.data(),256)==0&&mbedtls_gcm_crypt_and_tag(&c,MBEDTLS_GCM_ENCRYPT,n,h.nonce,12,reinterpret_cast<unsigned char*>(&h),16,reinterpret_cast<unsigned char*>(text),cipher.data(),16,h.tag)==0;
 mbedtls_gcm_free(&c);mbedtls_platform_zeroize(text,n);cJSON_free(text);
 std::string dest=std::string(sd_root)+"/config/known-"+(h.generation%2?"a":"b")+".enc",tmp=dest+".tmp";
 FILE *f=ok?fopen(tmp.c_str(),"wb"):nullptr;ok=ok&&f;
 if(f){ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&fwrite(cipher.data(),1,n,f)==n&&fflush(f)==0&&fsync(fileno(f))==0;ok=fclose(f)==0&&ok;}
 if(ok){struct stat st{};if(stat(dest.c_str(),&st)==0)ok=unlink(dest.c_str())==0;}
 if(ok)ok=rename(tmp.c_str(),dest.c_str())==0;
 if(!ok){unlink(tmp.c_str());return false;}
 uint32_t verified=0;cJSON *check=read(dest,verified);ok=check&&verified==h.generation;cJSON_Delete(check);
 if(ok){xSemaphoreTake(mutex,portMAX_DELAY);cJSON_Delete(config);config=next;generation=h.generation;xSemaphoreGive(mutex);}return ok;
}
void worker(void*){
 while(!storage_snapshot().mounted)vTaskDelay(pdMS_TO_TICKS(200));
 while(!crypto_gate_enter_for(100))vTaskDelay(1);
 bool ok=device_key();
 if(ok){uint32_t a=0,b=0;auto *one=read(std::string(sd_root)+"/config/known-a.enc",a),*two=read(std::string(sd_root)+"/config/known-b.enc",b);
  struct stat st{};bool corrupt=(!one&&stat((std::string(sd_root)+"/config/known-a.enc").c_str(),&st)==0)||(!two&&stat((std::string(sd_root)+"/config/known-b.enc").c_str(),&st)==0);
  if(corrupt){cJSON_Delete(one);cJSON_Delete(two);one=two=nullptr;ok=false;}
  config=(two&&(!one||b>a))?two:one;generation=(config==two)?b:a;cJSON_Delete(config==one?two:one);
  if(ok&&!config){config=cJSON_CreateObject();cJSON_AddArrayToObject(config,"wifi");cJSON_AddArrayToObject(config,"pins");}
 }
 crypto_gate_leave();xSemaphoreTake(mutex,portMAX_DELAY);ready=ok;message=ok?"Redes conhecidas e certificados carregados":"Falha ao carregar configuracao protegida";xSemaphoreGive(mutex);
 for(;;){Job *job=nullptr;if(xQueueReceive(queue,&job,portMAX_DELAY)!=pdTRUE)continue;
  if(!ready){delete job;continue;}
  while(!crypto_gate_enter_for(100))vTaskDelay(1);
  xSemaphoreTake(mutex,portMAX_DELAY);auto *next=cJSON_Duplicate(config,true);xSemaphoreGive(mutex);
  if(!next){crypto_gate_leave();note("Sem memoria para salvar configuracao");delete job;continue;}
  auto *array=cJSON_GetObjectItem(next,job->type=="wifi"?"wifi":"pins");
  auto *first=cJSON_GetArrayItem(array,0);
  if(job->type=="wifi" && first && job->host==str(first,"ssid") && job->password==str(first,"password") && job->secured==cJSON_IsTrue(cJSON_GetObjectItem(first,"protected"))){cJSON_Delete(next);crypto_gate_leave();delete job;continue;}
  for(int i=cJSON_GetArraySize(array)-1;i>=0;--i){auto *item=cJSON_GetArrayItem(array,i);
   if(job->type=="wifi"?job->host==str(item,"ssid"):(job->host==str(item,"host")&&job->value==str(item,"protocol")&&cJSON_IsNumber(cJSON_GetObjectItem(item,"port"))&&cJSON_GetObjectItem(item,"port")->valueint==int(job->port)))cJSON_DeleteItemFromArray(array,i);
  }
  auto *item=cJSON_CreateObject();
  if(job->type=="wifi"){cJSON_AddStringToObject(item,"ssid",job->host.c_str());cJSON_AddStringToObject(item,"password",job->password.c_str());cJSON_AddBoolToObject(item,"protected",job->secured);cJSON_InsertItemInArray(array,0,item);while(cJSON_GetArraySize(array)>16)cJSON_DeleteItemFromArray(array,16);}
  else {cJSON_AddStringToObject(item,"protocol",job->value.c_str());cJSON_AddStringToObject(item,"host",job->host.c_str());cJSON_AddNumberToObject(item,"port",job->port);cJSON_AddStringToObject(item,"fingerprint",job->password.c_str());cJSON_AddItemToArray(array,item);while(cJSON_GetArraySize(array)>64)cJSON_DeleteItemFromArray(array,0);}
  bool held=storage_capture_begin();bool saved=held&&next&&save(next);if(held)storage_capture_end();if(!saved)cJSON_Delete(next);
  crypto_gate_leave();note(saved?"Configuracao salva no Tab5":"Falha ao salvar; configuracao anterior preservada");delete job;
 }
}
}
void profiles_start(){mutex=xSemaphoreCreateMutex();queue=xQueueCreate(8,sizeof(Job*));xTaskCreate(worker,"saved_profiles",6144,nullptr,1,nullptr);}
bool profiles_ready(){if(!mutex)return false;xSemaphoreTake(mutex,portMAX_DELAY);bool r=ready;xSemaphoreGive(mutex);return r;}
std::string profiles_message(){if(!mutex)return "Carregando...";xSemaphoreTake(mutex,portMAX_DELAY);auto r=message;xSemaphoreGive(mutex);return r;}
std::vector<SavedWifi> profiles_wifi(){std::vector<SavedWifi> out;if(!mutex)return out;xSemaphoreTake(mutex,portMAX_DELAY);if(ready && config)for(auto *p=cJSON_GetObjectItem(config,"wifi")->child;p;p=p->next)out.push_back({str(p,"ssid"),str(p,"password"),cJSON_IsTrue(cJSON_GetObjectItem(p,"protected")) != 0});xSemaphoreGive(mutex);return out;}
bool profiles_save_wifi(const std::string &ssid,const std::string &password,bool secured){if(!profiles_ready()||ssid.empty()||ssid.size()>32||password.size()>64)return false;auto *j=new Job;j->type="wifi";j->host=ssid;j->password=password;j->secured=secured&&!password.empty();if(xQueueSend(queue,&j,0)==pdTRUE)return true;delete j;return false;}
std::string profiles_pin(const std::string &protocol,const std::string &host,unsigned port){std::string fp;if(!mutex)return fp;xSemaphoreTake(mutex,portMAX_DELAY);if(ready && config)for(auto *p=cJSON_GetObjectItem(config,"pins")->child;p;p=p->next){auto *n=cJSON_GetObjectItem(p,"port");if(protocol==str(p,"protocol")&&canonical(host)==str(p,"host")&&n&&n->valueint==int(port)){fp=str(p,"fingerprint");break;}}xSemaphoreGive(mutex);return fp;}
bool profiles_save_pin(const std::string &protocol,const std::string &host,unsigned port,const std::string &fp){if(!profiles_ready()||host.empty()||host.size()>253||!port||port>65535||protocol!="rdp"||fp.size()!=64||!std::all_of(fp.begin(),fp.end(),[](unsigned char c){return std::isxdigit(c);}))return false;auto *j=new Job;j->type="pin";j->value=protocol;j->host=canonical(host);j->port=port;j->password=fp;if(xQueueSend(queue,&j,0)==pdTRUE)return true;delete j;return false;}
}
