#include "storage.hpp"
#include "bsp/m5stack_tab5.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"
#include <atomic>
namespace tabremoto {
static SemaphoreHandle_t io_lock=nullptr,state_lock=nullptr;
static StorageStatus current;
static std::atomic<bool> retry_requested{false};
static void publish(StorageStatus s){xSemaphoreTake(state_lock,portMAX_DELAY);s.generation=current.generation+1;current=std::move(s);xSemaphoreGive(state_lock);}
StorageStatus storage_snapshot(){xSemaphoreTake(state_lock,portMAX_DELAY);auto s=current;xSemaphoreGive(state_lock);return s;}
void storage_retry(){retry_requested=true;}
bool storage_capture_begin(){return io_lock&&xSemaphoreTake(io_lock,pdMS_TO_TICKS(2000))==pdTRUE;}
void storage_capture_end(){xSemaphoreGive(io_lock);}

static bool validate_manifest(cJSON* j) {
    auto* schema=cJSON_GetObjectItemCaseSensitive(j,"schema");
    auto* app=cJSON_GetObjectItemCaseSensitive(j,"app");
    auto* version=cJSON_GetObjectItemCaseSensitive(j,"resource_version");
    auto* firmware=cJSON_GetObjectItemCaseSensitive(j,"firmware");
    auto* files=cJSON_GetObjectItemCaseSensitive(j,"files");
    if(!cJSON_IsNumber(schema)||schema->valuedouble!=1||!cJSON_IsString(app)||strcmp(app->valuestring,"TabRemoto")||
       !cJSON_IsString(version)||strcmp(version->valuestring,"0.1.0")||!cJSON_IsString(firmware)||strcmp(firmware->valuestring,"0.1.0")||
       !cJSON_IsObject(files)||cJSON_GetArraySize(files)!=7)return false;
    // Fixed allowlist: untrusted manifest paths can never escape the app root.
    for(auto name:{"assets/themes/dark.json","locales/pt-BR.json","config/app.json","assets/fonts/latin20.bin","assets/fonts/OFL.txt","assets/fonts/terminal20.bin","assets/fonts/TERMINAL-OFL.txt"}) {
        auto* expected=cJSON_GetObjectItemCaseSensitive(files,name);
        if(!cJSON_IsString(expected)||strlen(expected->valuestring)!=64)return false;
        FILE* f=fopen((std::string(sd_root)+"/"+name).c_str(),"rb");if(!f)return false;
        mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);bool ok=mbedtls_sha256_starts(&ctx,0)==0;
        uint8_t buffer[512],digest[32];size_t used=0,n;
        while(ok&&(n=fread(buffer,1,sizeof(buffer),f))) {used+=n;if(used>262144){ok=false;break;}ok=mbedtls_sha256_update(&ctx,buffer,n)==0;}
        if(ferror(f))ok=false;
        if(mbedtls_sha256_finish(&ctx,digest)!=0)ok=false;
        mbedtls_sha256_free(&ctx);fclose(f);
        char hex[65]{};for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
        if(!ok||strcmp(hex,expected->valuestring))return false;
    }
    return true;
}
static bool dir(const std::string& path) {
    if(mkdir(path.c_str(),0755)==0) return true;
    struct stat st{}; return errno==EEXIST && stat(path.c_str(),&st)==0 && S_ISDIR(st.st_mode);
}
static bool probe(std::string& error) {
    // Exclusive temporary file: never truncate an existing user file.
    char path[]="/sdcard/TabRemoto/cache/probe-XXXXXX";
    int fd=mkstemp(path); if(fd<0){error=strerror(errno);return false;}
    const char payload[]="TabRemoto storage probe v1\n"; char readback[sizeof(payload)]{};
    bool ok=write(fd,payload,sizeof(payload))==sizeof(payload) && fsync(fd)==0 &&
        lseek(fd,0,SEEK_SET)==0 && read(fd,readback,sizeof(readback))==sizeof(readback) &&
        memcmp(payload,readback,sizeof(payload))==0;
    if(close(fd)!=0)ok=false;
    if(unlink(path)!=0)ok=false;
    if(!ok)error="write/fsync/read/remove probe failed";
    return ok;
}
static StorageStatus mount_storage() {
    StorageStatus state;
    if(bsp_sdcard_get_handle())bsp_sdcard_unmount();
    auto err=bsp_sdcard_mount(); state.mounted=err==ESP_OK;
    if(!state.mounted){state.error=esp_err_to_name(err);return state;}
    if(!dir(sd_root)){state.error="Cannot create /TabRemoto";return state;}
    for(auto name:{"config","vault","vault/keys","vault/vpn","trust","trust/ca","assets","assets/fonts","assets/icons","assets/themes","assets/backgrounds","keymaps","locales","history","transfers","transfers/downloads","transfers/uploads","transfers/partial","logs","cache","backups","updates"}) {
        if(!dir(std::string(sd_root)+"/"+name)){state.error="Cannot create application directory";return state;}
    }
    state.writable=probe(state.error);
    char manifest[2049]{}; FILE* f=fopen("/sdcard/TabRemoto/manifest.json","rb");
    if(f){size_t n=fread(manifest,1,2048,f);bool bounded=n<2048&&!ferror(f);fclose(f);
        auto* j=bounded&&!memchr(manifest,0,n)?cJSON_ParseWithOpts(manifest,nullptr,true):nullptr;
        state.resources=validate_manifest(j);
        cJSON_Delete(j);
    }
    if(!state.resources && state.error.empty())state.error="Missing or invalid resource manifest";
    return state;
}
static cJSON* read_json(const char* name){
 char buffer[4097]{};auto path=std::string(sd_root)+"/"+name;FILE* f=fopen(path.c_str(),"rb");if(!f)return nullptr;
 size_t n=fread(buffer,1,4096,f);bool ok=n<4096&&!ferror(f)&&!memchr(buffer,0,n);fclose(f);
 return ok?cJSON_ParseWithOpts(buffer,nullptr,true):nullptr;
}
static bool color(cJSON* j,const char* name,uint32_t& out){auto* v=cJSON_GetObjectItemCaseSensitive(j,name);if(!cJSON_IsString(v)||strlen(v->valuestring)!=7||v->valuestring[0]!='#')return false;char* end=nullptr;auto n=strtoul(v->valuestring+1,&end,16);if(!end||*end)return false;out=n;return true;}
static void load_assets(StorageStatus& s){
 if(!s.resources)return;
 auto r=std::make_shared<Resources>();auto* theme=read_json("assets/themes/dark.json");auto* locale=read_json("locales/pt-BR.json");
 bool ok=color(theme,"background",r->background)&&color(theme,"foreground",r->foreground)&&color(theme,"accent",r->accent);
 const char* keys[]={"diagnostics","hint","retry"};
 Resources& resource=*r;std::string* destinations[]={&resource.title,&resource.hint,&resource.retry};
 for(unsigned i=0;i<3;i++){auto* v=cJSON_GetObjectItemCaseSensitive(locale,keys[i]);if(!cJSON_IsString(v)||strlen(v->valuestring)>160)ok=false;else *destinations[i]=v->valuestring;}

 cJSON_Delete(theme);cJSON_Delete(locale);
 const std::pair<const char*,std::vector<uint8_t>*> fonts[]={{"latin20.bin",&r->font},{"terminal20.bin",&r->terminal_font}};
 for(const auto& item:fonts) {
  FILE* f=fopen((std::string(sd_root)+"/assets/fonts/"+item.first).c_str(),"rb");if(!f){ok=false;continue;}
  fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<=0||n>262144)ok=false;else{item.second->resize(n);if(fread(item.second->data(),1,n,f)!=(size_t)n)ok=false;}fclose(f);
 }
 if(ok)s.assets=r;else{s.resources=false;s.error="Invalid theme, locale or font";}
}
bool storage_selftest(std::string& error){if(!storage_capture_begin()){error="SD busy";return false;}auto s=storage_snapshot();bool ok=s.mounted&&probe(error);storage_capture_end();return ok;}
static void storage_worker(void*){
 for(;;){
  bool retry=retry_requested.exchange(false);
  if(retry){auto s=storage_snapshot();s.busy=true;publish(s);xSemaphoreTake(io_lock,portMAX_DELAY);auto fresh=mount_storage();load_assets(fresh);publish(fresh);xSemaphoreGive(io_lock);}
  else if(storage_snapshot().mounted){xSemaphoreTake(io_lock,portMAX_DELAY);auto* card=bsp_sdcard_get_handle();if(!card||sdmmc_get_status(card)!=ESP_OK){auto s=storage_snapshot();s.mounted=s.writable=s.resources=false;s.assets.reset();s.error="SD removed or I/O failure; insert card and retry";publish(s);}xSemaphoreGive(io_lock);}
  vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
StorageStatus storage_start(){io_lock=xSemaphoreCreateMutex();state_lock=xSemaphoreCreateMutex();auto s=mount_storage();load_assets(s);publish(s);if(xTaskCreate(storage_worker,"storage",8192,nullptr,1,nullptr)!=pdPASS){s.error="Cannot start SD worker";publish(s);}return storage_snapshot();}

}
