#include "rdp.hpp"
#include <freerdp/transport_io.h>
#include <unistd.h>
#include "bootloader_random.h"
#include "clipboard_text.hpp"
#include "crypto_gate.h"
#include "esp_heap_caps.h"
#include "esp_pthread.h"
#include "esp_timer.h"
#include "esp_vfs_eventfd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "network.hpp"
#include "profiles.hpp"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/x509.h"
#include "sdkconfig.h"
#include "ssh.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/codec/color.h>
#include <freerdp/error.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <freerdp/transport_io.h>
#include <freerdp/update.h>
#include <pthread.h>
#include <winpr/crt.h>
#include <winpr/wlog.h>
extern "C" BOOL cliprdr_VirtualChannelEntryEx(PCHANNEL_ENTRY_POINTS_EX, PVOID);
extern "C" BOOL drdynvc_VirtualChannelEntryEx(PCHANNEL_ENTRY_POINTS_EX, PVOID);
extern "C" UINT disp_DVCPluginEntry(IDRDYNVC_ENTRY_POINTS *);
namespace tabremoto {
RdpRequest::~RdpRequest() {
  mbedtls_platform_zeroize(password.data(), password.size());
}
namespace {
std::atomic<bool> vault_pause{false};
struct Session {
  struct Context {
    rdpContext base;
    Session *owner;
  };
  static Session &owner(rdpContext *ctx) {
    return *reinterpret_cast<Context *>(ctx)->owner;
  }
  RdpStatus snapshot() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto s = state;
    xSemaphoreGive(mutex);
    return s;
  }
  SemaphoreHandle_t mutex = nullptr;
  QueueHandle_t requests, events;
#if CONFIG_TABREMOTO_DIAGNOSTICS
  SemaphoreHandle_t probe_done;
  std::atomic<bool> probe_result{false};
  std::string os_detail;
  void probe_detail(const char *text) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    os_detail = text;
    xSemaphoreGive(mutex);
  }
#endif
  RdpStatus state;
  std::atomic<bool> busy{false}, cancel{false};
  std::string accepted;
  std::unique_ptr<RdpRequest> resume_request;
  std::array<std::shared_ptr<RdpFrame>, 2> frame_pool;
  std::atomic<size_t> presentation_capacity{0};
  int64_t resized_at=0;
  uint32_t resize_target=0;
  using Pixels=std::unique_ptr<uint8_t,void(*)(void*)>;
  Pixels primary{nullptr,heap_caps_free},prepared_primary{nullptr,heap_caps_free},retired_primary{nullptr,heap_caps_free};
  std::atomic<size_t> primary_capacity{0};
  size_t prepared_capacity=0;
  uint64_t sequence=0,copied_pixels=0;unsigned copy_max_us=0,paint_calls=0;
  int64_t presented_at=0;RdpDamage pending_damage,last_damage;
  bool primary_cached=false;
  unsigned primary_width=0,primary_height=0;
  std::array<std::shared_ptr<RdpFrame>,2> prepared_frames;
  static void borrowed_pixels(void*) {}
  void clear_prepared() {
    prepared_primary.reset();prepared_capacity=0;
    for(auto &frame:prepared_frames) frame.reset();
  }
  bool prepare_geometry(size_t capacity) {
    clear_prepared();
    if(capacity>primary_capacity.load()) {
      prepared_primary.reset(static_cast<uint8_t*>(heap_caps_calloc(1,capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
      if(!prepared_primary) return false;
      prepared_capacity=capacity;
    }
    for(unsigned i=0;i<frame_pool.size();++i) if(!frame_pool[i] || frame_pool[i]->capacity<capacity/2) {
      auto next=std::make_shared<RdpFrame>();
      auto *pixels=static_cast<uint8_t*>(heap_caps_malloc(capacity/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
      if(!pixels) {clear_prepared();return false;}
      next->pixels=std::shared_ptr<uint8_t>(pixels,heap_caps_free);next->capacity=capacity/2;prepared_frames[i]=next;
    }
    return true;
  }
  bool presentation_pending = false;
  struct Input {
    uint16_t code, x, y, flags;
    bool down, unicode, pointer, click;
  };
  CliprdrClientContext *clip = nullptr;
  DispClientContext *display = nullptr;
  RdpClipboardStatus clipboard;
  std::string offered_clipboard;
  std::atomic<bool> offer_pending{false}, fetch_pending{false};
  std::atomic<uint32_t> resize_pending{0};
  void clipboard_note(const std::string &detail) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    clipboard.detail = detail;
    ++clipboard.revision;
    ++state.revision;
    xSemaphoreGive(mutex);
  }
  static UINT clip_monitor(CliprdrClientContext *clip,
                           const CLIPRDR_MONITOR_READY *) {
    auto &self = *static_cast<Session *>(clip->custom);
    CLIPRDR_GENERAL_CAPABILITY_SET general{};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES;
    CLIPRDR_CAPABILITIES caps{};
    caps.common.msgType = CB_CLIP_CAPS;
    caps.cCapabilitiesSets = 1;
    caps.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET *>(&general);
    UINT rc = clip->ClientCapabilities(clip, &caps);
    if (rc != CHANNEL_RC_OK)
      return rc;
    CLIPRDR_FORMAT_LIST list{};
    list.common.msgType = CB_FORMAT_LIST;
    rc = clip->ClientFormatList(clip, &list);
    xSemaphoreTake(self.mutex, portMAX_DELAY);
    self.clipboard.ready = rc == CHANNEL_RC_OK;
    self.state.clipboard_ready = self.clipboard.ready;
    xSemaphoreGive(self.mutex);
    self.clipboard_note(rc == CHANNEL_RC_OK ? "Clipboard de texto disponivel"
                                            : "Servidor recusou o clipboard");
    return rc;
  }
  static UINT clip_formats(CliprdrClientContext *clip,
                           const CLIPRDR_FORMAT_LIST *list) {
    auto &self = *static_cast<Session *>(clip->custom);
    bool unicode = false;
    for (UINT32 i = 0; i < list->numFormats; ++i)
      if (list->formats[i].formatId == 13)
        unicode = true;
    xSemaphoreTake(self.mutex, portMAX_DELAY);
    self.clipboard.remote_available = unicode;
    ++self.clipboard.revision;
    xSemaphoreGive(self.mutex);
    CLIPRDR_FORMAT_LIST_RESPONSE response{};
    response.common.msgType = CB_FORMAT_LIST_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    return clip->ClientFormatListResponse(clip, &response);
  }
  static UINT
  clip_offer_response(CliprdrClientContext *clip,
                      const CLIPRDR_FORMAT_LIST_RESPONSE *response) {
    auto &self = *static_cast<Session *>(clip->custom);
    self.clipboard_note(response->common.msgFlags & CB_RESPONSE_OK
                            ? "Texto anunciado ao remoto"
                            : "O remoto recusou o texto");
    return CHANNEL_RC_OK;
  }
  static UINT clip_request(CliprdrClientContext *clip,
                           const CLIPRDR_FORMAT_DATA_REQUEST *request) {
    auto &self = *static_cast<Session *>(clip->custom);
    std::string text;
    xSemaphoreTake(self.mutex, portMAX_DELAY);
    text = self.offered_clipboard;
    xSemaphoreGive(self.mutex);
    std::vector<uint8_t> wire;
    bool ok = request->requestedFormatId == 13 && clipboard_utf16(text, wire);
    mbedtls_platform_zeroize(text.data(), text.size());
    CLIPRDR_FORMAT_DATA_RESPONSE response{};
    response.common.msgType = CB_FORMAT_DATA_RESPONSE;
    response.common.msgFlags = ok ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
    response.common.dataLen = ok ? wire.size() : 0;
    response.requestedFormatData = ok ? wire.data() : nullptr;
    UINT rc = clip->ClientFormatDataResponse(clip, &response);
    if (!wire.empty())
      mbedtls_platform_zeroize(wire.data(), wire.size());
    if (ok) {
      xSemaphoreTake(self.mutex, portMAX_DELAY);
      ++self.clipboard.sent;
      xSemaphoreGive(self.mutex);
      self.clipboard_note("Texto entregue ao clipboard remoto");
    }
    return rc;
  }
  static UINT clip_response(CliprdrClientContext *clip,
                            const CLIPRDR_FORMAT_DATA_RESPONSE *response) {
    auto &self = *static_cast<Session *>(clip->custom);
    std::string text;
    xSemaphoreTake(self.mutex, portMAX_DELAY);
    bool pending = self.clipboard.waiting;
    xSemaphoreGive(self.mutex);
    bool ok = pending && (response->common.msgFlags & CB_RESPONSE_OK) &&
              clipboard_utf8(response->requestedFormatData,
                             response->common.dataLen, text);
    xSemaphoreTake(self.mutex, portMAX_DELAY);
    self.clipboard.waiting = false;
    if (ok) {
      mbedtls_platform_zeroize(self.clipboard.text.data(),
                               self.clipboard.text.size());
      self.clipboard.text = std::move(text);
      ++self.clipboard.received;
    }
    xSemaphoreGive(self.mutex);
    mbedtls_platform_zeroize(text.data(), text.size());
    self.clipboard_note(
        ok ? "Texto recebido do remoto"
           : "Clipboard remoto invalido, ausente ou maior que 4096 bytes");
    return CHANNEL_RC_OK;
  }
  static UINT display_caps(DispClientContext *display, UINT32 monitors,
                           UINT32 factorA, UINT32 factorB) {
    auto &self = *static_cast<Session *>(display->custom);
    xSemaphoreTake(self.mutex, portMAX_DELAY);
    self.state.dynamic_resize = monitors > 0 && factorA > 0 && factorB > 0;
    ++self.state.revision;
    xSemaphoreGive(self.mutex);
    return CHANNEL_RC_OK;
  }
  static void channel_connected(void *context,
                                const ChannelConnectedEventArgs *e) {
    auto &self = owner(static_cast<rdpContext *>(context));
    if (!strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME)) {
      auto *clip = static_cast<CliprdrClientContext *>(e->pInterface);
      self.clip = clip;
      clip->custom = &self;
      clip->MonitorReady = clip_monitor;
      clip->ServerFormatList = clip_formats;
      clip->ServerFormatListResponse = clip_offer_response;
      clip->ServerFormatDataRequest = clip_request;
      clip->ServerFormatDataResponse = clip_response;
    } else if (!strcmp(e->name, DISP_DVC_CHANNEL_NAME)) {
      self.display = static_cast<DispClientContext *>(e->pInterface);
      self.display->custom = &self;
      self.display->DisplayControlCaps = display_caps;
    }
  }
  static void channel_disconnected(void *context,
                                   const ChannelDisconnectedEventArgs *e) {
    auto &self = owner(static_cast<rdpContext *>(context));
    if (!strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME)) {
      self.clip = nullptr;
      xSemaphoreTake(self.mutex, portMAX_DELAY);
      self.clipboard.ready = false;
      self.clipboard.waiting = false;
      self.state.clipboard_ready = false;
      ++self.state.revision;
      xSemaphoreGive(self.mutex);
    }
    if (!strcmp(e->name, DISP_DVC_CHANNEL_NAME)) {
      self.display = nullptr;
      xSemaphoreTake(self.mutex, portMAX_DELAY);
      self.state.dynamic_resize = false;
      ++self.state.revision;
      xSemaphoreGive(self.mutex);
    }
  }
  NetworkBinding connection;
  int prepared_socket=-1;
  std::string connection_error;
  std::string target_host; unsigned target_port=0;
  static int tcp_connect(rdpContext *context,rdpSettings*,const char *hostname,int port,DWORD) {
    auto &self=owner(context);
    if(!hostname || self.target_host!=hostname || int(self.target_port)!=port || self.prepared_socket<0 || !network_connection_valid(self.connection)) return -1;
    int fd=self.prepared_socket;self.prepared_socket=-1;return fd;
  }
  bool prepare_socket(const RdpRequest &request) {
    if(prepared_socket>=0) {close(prepared_socket);prepared_socket=-1;}
    target_host=request.host;target_port=request.port;
    connection_error.clear();
    int64_t deadline=esp_timer_get_time()+30000000;
    while(!cancel && esp_timer_get_time()<deadline) {
      auto net=network_status();
      bool ready=request.require_vpn?network_vpn_active(request.vpn_profile):
        request.transport=="wifi"?net.wifi:request.transport=="ethernet"?net.ethernet:net.wifi||net.ethernet;
      if(ready) break;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    prepared_socket=network_connect(request.host,request.port,request.transport,request.require_vpn,request.vpn_profile,connection,connection_error,&cancel);
    return prepared_socket>=0;
  }
  bool channel_events_registered=false;
  bool channels_init(rdpContext *context) {
    if(!channel_events_registered) {
      if(PubSub_SubscribeChannelConnected(context->pubSub,channel_connected)<0) return false;
      if(PubSub_SubscribeChannelDisconnected(context->pubSub,channel_disconnected)<0) {
        PubSub_UnsubscribeChannelConnected(context->pubSub,channel_connected);return false;
      }
      channel_events_registered=true;
    }
    auto *settings = context->settings;
    if(freerdp_dynamic_channel_collection_find(settings,"disp")) return true;
    const char *args[] = {"disp"};
    auto *addin = freerdp_addin_argv_new(1, args);
    if (!addin)
      return false;
    if (!freerdp_dynamic_channel_collection_add(settings, addin)) {
      freerdp_addin_argv_free(addin);
      return false;
    }
    return true;
  }
  static BOOL load_channels(freerdp *instance) {
    auto *context = instance->context;
    auto *settings = context->settings;
    return freerdp_channels_client_load_ex(context->channels, settings,
                                           cliprdr_VirtualChannelEntryEx,
                                           settings) == 0 &&
           freerdp_channels_client_load_ex(context->channels, settings,
                                           drdynvc_VirtualChannelEntryEx,
                                           settings) == 0;
  }

  bool resize_failure(const char *message) {
    auto current=snapshot();current.resizing=false;current.detail=message;
    ++current.resize_errors;publish(current);return false;
  }
  bool channels_tick() {
    if (offer_pending.exchange(false)) {
      if (clip) {
        CLIPRDR_FORMAT fmt{};
        fmt.formatId = 13;
        CLIPRDR_FORMAT_LIST list{};
        list.common.msgType = CB_FORMAT_LIST;
        list.numFormats = 1;
        list.formats = &fmt;
        if (clip->ClientFormatList(clip, &list) != CHANNEL_RC_OK)
          clipboard_note("Nao foi possivel anunciar o clipboard");
      } else
        clipboard_note("Clipboard nao negociado com este servidor");
    }
    if (fetch_pending.exchange(false)) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      bool ok = clip && clipboard.ready && clipboard.remote_available &&
                !clipboard.waiting;
      if (ok)
        clipboard.waiting = true;
      xSemaphoreGive(mutex);
      if (ok) {
        CLIPRDR_FORMAT_DATA_REQUEST request{};
        request.common.msgType = CB_FORMAT_DATA_REQUEST;
        request.requestedFormatId = 13;
        if (clip->ClientFormatDataRequest(clip, &request) != CHANNEL_RC_OK) {
          xSemaphoreTake(mutex, portMAX_DELAY);
          clipboard.waiting = false;
          xSemaphoreGive(mutex);
          clipboard_note("Falha ao solicitar clipboard remoto");
        }
      } else
        clipboard_note("O remoto nao anunciou texto disponivel");
    }
    auto size = resize_pending.load();
    auto current=snapshot();
    bool painted=current.frame && current.frame->width==current.width && current.frame->height==current.height;
    if (size && display && painted && esp_timer_get_time()-resized_at>1000000) {
      size=resize_pending.exchange(0);
      if((size>>16)==current.width && (size&65535)==current.height){auto next=snapshot();next.resizing=false;publish(next);return true;}
      if(!prepare_geometry(size_t(size>>16)*(size&65535)*4)) {
        resize_failure("Nao ha bloco livre para ampliar a resolucao. O desktop atual foi mantido.");return true;
      }
      resized_at=esp_timer_get_time();resize_target=size;
      DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
      monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
      monitor.Width = size >> 16;
      monitor.Height = size & 65535;
      monitor.PhysicalWidth = 280;
      monitor.PhysicalHeight = 160;
      monitor.DesktopScaleFactor = 100;
      monitor.DeviceScaleFactor = 100;
      if (display->SendMonitorLayout(display, 1, &monitor) != CHANNEL_RC_OK) {
        clear_prepared();resize_target=0;resize_failure("O servidor recusou esta resolucao. O desktop atual foi mantido.");
      }
    }
    if(resize_target && esp_timer_get_time()-resized_at>10000000){clear_prepared();resize_target=0;resize_failure("O servidor nao confirmou a resolucao em 10 segundos. O desktop atual foi mantido.");}
    return true;
  }
  void channels_clear(rdpContext *context) {
    PubSub_UnsubscribeChannelConnected(context->pubSub, channel_connected);
    PubSub_UnsubscribeChannelDisconnected(context->pubSub,
                                          channel_disconnected);
    channel_events_registered=false;
    clip = nullptr;
    display = nullptr;
    offer_pending = false;
    fetch_pending = false;
    resize_pending = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    mbedtls_platform_zeroize(offered_clipboard.data(),
                             offered_clipboard.size());
    offered_clipboard.clear();
    mbedtls_platform_zeroize(clipboard.text.data(), clipboard.text.size());
    clipboard = RdpClipboardStatus{};
    xSemaphoreGive(mutex);
  }
  void publish(RdpStatus s) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    s.revision = state.revision + 1;
    state = std::move(s);
    xSemaphoreGive(mutex);
  }
  template <class F> auto crypto(F f) -> decltype(f()) {
    while (!crypto_gate_enter_for(100))
      vTaskDelay(1);
    bootloader_random_enable();
    auto value = f();
    bootloader_random_disable();
    crypto_gate_leave();
    return value;
  }
  int verify_impl(freerdp *, const BYTE *der, size_t length, const char *,
                  UINT16, DWORD) {
    unsigned char hash[32];
    unsigned int used = 0;
    BIO *bio = BIO_new_mem_buf(der, length);
    if (!bio)
      return 0;
    X509 *certificate = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!certificate)
      return 0;
    int result = X509_digest(certificate, EVP_sha256(), hash, &used);
    X509_free(certificate);
    if (result != 1 || used != 32)
      return 0;
    char hex[65];
    for (unsigned i = 0; i < 32; i++)
      snprintf(hex + 2 * i, 3, "%02x", hash[i]);
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool changed = !accepted.empty() && accepted != hex;
    xSemaphoreGive(mutex);
    auto s = snapshot();
    if (changed) {
      s.phase = RdpStatus::Failed;
      s.fingerprint = hex;
      s.detail = "Certificado RDP alterado; conexao bloqueada antes da senha";
      publish(s);
      return 0;
    }
    xSemaphoreTake(mutex,portMAX_DELAY);bool known=accepted==hex;xSemaphoreGive(mutex);
    if(known){if(profiles_pin("rdp",target_host,target_port).empty())profiles_save_pin("rdp",target_host,target_port,hex);s.fingerprint=hex;s.detail="Certificado confiado confirmado";publish(s);return 1;}
    s.phase = RdpStatus::Verify;
    s.fingerprint = hex;
    s.detail = "Confira o SHA-256 do certificado antes de autenticar";
    publish(s);
    auto deadline = esp_timer_get_time() + 60000000;
    // No TLS operation runs while waiting for the operator. Let the durable
    // trust worker use crypto and SD; reacquire before returning into OpenSSL.
    crypto_gate_leave();
    while (!cancel && !vault_pause && esp_timer_get_time() < deadline) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      bool ok = accepted == hex;
      xSemaphoreGive(mutex);
      if (ok && profiles_pin("rdp",target_host,target_port)==hex) {
        while(!crypto_gate_enter_for(100))vTaskDelay(1);
        bootloader_random_enable();
        s.phase = RdpStatus::Connecting;
        s.detail = "Certificado confirmado; autenticando NLA";
        publish(s);
        return 1;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    while(!crypto_gate_enter_for(100))vTaskDelay(1);
    bootloader_random_enable();
    return 0;
  }
  static int verify(freerdp *instance, const BYTE *der, size_t length,
                    const char *host, UINT16 port, DWORD flags) {
    return owner(instance->context)
        .verify_impl(instance, der, length, host, port, flags);
  }
  static BOOL pre_connect(freerdp *instance) {
    instance->VerifyX509Certificate = verify;
    return owner(instance->context).channels_init(instance->context);
  }
  static BOOL begin_paint(rdpContext *context) {
    if (context->gdi && context->gdi->primary)
      context->gdi->primary->hdc->hwnd->invalid->null = TRUE;
    return TRUE;
  }
  BOOL end_paint_impl(rdpContext *context) {
    auto *gdi = context->gdi;
    if (!gdi || !gdi->primary_buffer)
      return FALSE;
    auto *invalid=gdi->primary->hdc->hwnd->invalid;
    if(!invalid->null)pending_damage.add(RdpDamage{invalid->x,invalid->y,invalid->w,invalid->h}.clipped(gdi->width,gdi->height));
    if(pending_damage.empty()){
      if(presentation_pending && primary_cached)pending_damage={0,0,gdi->width,gdi->height};
      else return TRUE;
    }
    if(esp_timer_get_time()-presented_at<33333){presentation_pending=true;return TRUE;}
    auto s = snapshot();
    std::shared_ptr<RdpFrame> frame;
    for (auto &candidate : frame_pool) {
      if (candidate && candidate.use_count() == 1) {
        frame = candidate;
        break;
      }
    }
    // UI may still hold the previous image. Drop this presentation, not the
    // decoder update; the next paint copies the complete primary surface.
    if (!frame) {
      presentation_pending = true;
      return TRUE;
    }
    presentation_pending = false;
    RdpDamage update=pending_damage;
    if(frame->sequence+1!=sequence || frame->width!=unsigned(gdi->width) || frame->height!=unsigned(gdi->height))update={0,0,gdi->width,gdi->height};
    else update.add(last_damage);
    update=update.clipped(gdi->width,gdi->height);
    frame->width=gdi->width;frame->height=gdi->height;frame->stride=gdi->width*2;
    frame->bytes=size_t(frame->stride)*frame->height;
    if(frame->bytes>frame->capacity)return FALSE;
    auto start=esp_timer_get_time();
    if(gdi->dstFormat==PIXEL_FORMAT_RGB16)rdp_copy_rgb565_surface(frame->pixels.get(),frame->stride,gdi->primary_buffer,gdi->stride,update);
    else rdp_copy_rgb565(frame->pixels.get(),frame->stride,gdi->primary_buffer,gdi->stride,update);
    copy_max_us=std::max(copy_max_us,unsigned(esp_timer_get_time()-start));copied_pixels+=uint64_t(update.w)*update.h;
    frame->damage=pending_damage;frame->sequence=++sequence;last_damage=pending_damage;pending_damage={};presented_at=esp_timer_get_time();
    primary_cached=true;primary_width=frame->width;primary_height=frame->height;
    s.copied_pixels=copied_pixels;s.copy_max_us=copy_max_us;s.paint_calls=paint_calls;
    s.width = frame->width;
    s.height = frame->height;
    s.frame = frame;
    ++s.frames;
    publish(s);
    return TRUE;
  }
  static BOOL end_paint(rdpContext *ctx) {
    auto &self=owner(ctx);++self.paint_calls;return self.end_paint_impl(ctx);
  }
  static BOOL desktop_resize(rdpContext *context) {
    auto &self = owner(context);
    auto *settings = context->settings;
    unsigned width =
                 freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
             height =
                 freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
    if (width < 640 || width > 1280 || height < 360 || height > 720)
      return FALSE;
    if(context->gdi && unsigned(context->gdi->width)==width && unsigned(context->gdi->height)==height) {
      self.clear_prepared();self.resize_target=0;auto next=self.snapshot();next.resizing=false;self.publish(next);return TRUE;
    }
    size_t capacity=size_t(width)*height*4;
    if(capacity>self.primary_capacity.load()) {
      if(!self.prepared_primary || self.prepared_capacity<capacity) return FALSE;
      self.retired_primary=std::move(self.primary);
      self.primary=std::move(self.prepared_primary);
      self.primary_capacity=self.prepared_capacity;
    }
    if(!self.primary || !gdi_resize_ex(context->gdi,width,height,width*2,PIXEL_FORMAT_RGB16,self.primary.get(),borrowed_pixels)) return FALSE;
    self.retired_primary.reset();
    memset(self.primary.get(),0,capacity);
    self.primary_cached=false;self.pending_damage=self.last_damage={};self.presentation_pending=false;self.presented_at=0;
    for(unsigned i=0;i<self.frame_pool.size();++i) {
      if(self.prepared_frames[i] && self.prepared_frames[i]->capacity>=capacity/2) self.frame_pool[i]=std::move(self.prepared_frames[i]);
      if(!self.frame_pool[i] || self.frame_pool[i]->capacity<capacity/2) return FALSE;
    }
    self.clear_prepared();
    self.presentation_capacity=std::max(self.presentation_capacity.load(),capacity/2);
    auto s = self.snapshot();
    s.width = width;
    s.height = height;
    s.resizing=false;self.resize_target=0;
    s.detail="Resolucao remota atualizada";
    self.publish(s);
    return TRUE;
  }
  static BOOL post_connect(freerdp *instance) {
    auto *settings=instance->context->settings;
    if(freerdp_settings_get_uint32(settings,FreeRDP_DesktopWidth)>1280 || freerdp_settings_get_uint32(settings,FreeRDP_DesktopHeight)>720) return FALSE;
    auto &self = owner(instance->context);
    size_t requested=size_t(freerdp_settings_get_uint32(settings,FreeRDP_DesktopWidth))*freerdp_settings_get_uint32(settings,FreeRDP_DesktopHeight)*4;
    if(self.primary_capacity.load()<requested || !self.primary) {
      self.primary.reset(static_cast<uint8_t*>(heap_caps_calloc(1,requested,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)));
      self.primary_capacity=self.primary?requested:0;
      self.primary_cached=false;
    }
    if(!self.primary || !gdi_init_ex(instance,PIXEL_FORMAT_RGB16,0,self.primary.get(),borrowed_pixels)) return FALSE;
    size_t needed =
        size_t(instance->context->gdi->width)*2 * instance->context->gdi->height;
    for (auto &frame : self.frame_pool)
      if (!frame || frame->capacity < needed) {
        frame.reset();
        auto next = std::make_shared<RdpFrame>();
        auto *pixels = static_cast<uint8_t *>(
            heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!pixels)
          return FALSE;
        next->pixels = std::shared_ptr<uint8_t>(pixels, heap_caps_free);
        next->capacity = needed;
        frame = next;
      }
    self.presentation_capacity=std::max(self.presentation_capacity.load(),needed);
    if(self.primary_cached && self.primary_width==unsigned(instance->context->gdi->width) && self.primary_height==unsigned(instance->context->gdi->height)) self.presentation_pending=true;
    instance->context->update->BeginPaint = begin_paint;
    instance->context->update->EndPaint = end_paint;
    instance->context->update->DesktopResize = desktop_resize;
    return TRUE;
  }
  static void post_disconnect(freerdp *instance) {
    gdi_free(instance);
    auto &self=owner(instance->context);self.retired_primary.reset();self.clear_prepared();
  }
  void run(RdpRequest &request) {
    presentation_pending = false;
    primary_cached=false;sequence=copied_pixels=0;copy_max_us=paint_calls=0;presented_at=0;pending_damage=last_damage={};
    auto s = RdpStatus{};
    s.resize_errors=snapshot().resize_errors;
    s.host = request.host;
    s.user = request.user;
    s.width = request.width;
    s.height = request.height;
    s.phase = RdpStatus::Connecting;
    s.detail = "Conectando RDP nativo com NLA/TLS";
    publish(s);
    const size_t capacity = size_t(request.width) * request.height * 2;
    presentation_capacity=capacity;
    resized_at=0;resize_target=0;
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) <
        capacity * 4 + 6 * 1024 * 1024) {
      s.phase = RdpStatus::Failed;
      s.detail =
          "Memoria insuficiente; reduza a resolucao ou feche outra sessao";
      publish(s);
      return;
    }
    for (auto &frame : frame_pool) {
      frame = std::make_shared<RdpFrame>();
      auto *pixels = static_cast<uint8_t *>(
          heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!pixels) {
        for (auto &item : frame_pool)
          item.reset();
        s.phase = RdpStatus::Failed;
        s.detail = "Memoria insuficiente para os buffers RDP";
        publish(s);
        return;
      }
      frame->pixels = std::shared_ptr<uint8_t>(pixels, heap_caps_free);
      frame->capacity = capacity;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    accepted = request.fingerprint.empty()?profiles_pin("rdp",request.host,request.port):request.fingerprint;
    xSemaphoreGive(mutex);
    freerdp *instance = freerdp_new();
    if (!instance) {
      for (auto &frame : frame_pool)
        frame.reset();
      s.phase = RdpStatus::Failed;
      s.detail = "Memoria insuficiente para FreeRDP";
      publish(s);
      return;
    }
    instance->ContextSize = sizeof(Context);
    bool context_ok = crypto([&] { return freerdp_context_new(instance); });
    if (context_ok) {
      reinterpret_cast<Context *>(instance->context)->owner = this;
      auto *settings = instance->context->settings;
      bool valid =
          freerdp_settings_set_string(settings, FreeRDP_ServerHostname,
                                      request.host.c_str()) &&
          freerdp_settings_set_uint32(settings, FreeRDP_ServerPort,
                                      request.port) &&
          freerdp_settings_set_string(settings, FreeRDP_Username,
                                      request.user.c_str()) &&
          freerdp_settings_set_string(settings, FreeRDP_Password,
                                      request.password.data()) &&
          freerdp_settings_set_string(settings, FreeRDP_Domain,
                                      request.domain.c_str()) &&
          freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth,
                                      request.width) &&
          freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight,
                                      request.height) &&
          freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 16) &&
          freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline,
                                    FALSE) &&
          freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, FALSE) &&
          freerdp_settings_set_bool(settings, FreeRDP_NSCodec, FALSE) &&
          freerdp_settings_set_uint32(settings, FreeRDP_ThreadingFlags,
                                      THREADING_FLAGS_DISABLE_THREADS) &&
          freerdp_settings_set_bool(settings,
                                    FreeRDP_SynchronousDynamicChannels, TRUE) &&
          freerdp_settings_set_bool(settings, FreeRDP_SupportDynamicChannels,
                                    TRUE) &&
          freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl,
                                    TRUE) &&
          freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate,
                                    TRUE) &&
          freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard,
                                    TRUE) &&
          freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, TRUE) &&
          freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE) &&
          freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE) &&
          freerdp_settings_set_bool(
              settings, FreeRDP_ExternalCertificateManagement, TRUE) &&
          freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate,
                                    FALSE) &&
          freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled,
                                    request.reconnect) &&
          freerdp_settings_set_string(settings, FreeRDP_ConfigPath,
                                      "/sdcard/TabRemoto/config");
      auto io=*freerdp_get_io_callbacks(instance->context);
      io.TCPConnect=tcp_connect;
      valid=valid && freerdp_set_io_callbacks(instance->context,&io);
      instance->PreConnect = pre_connect;
      instance->LoadChannels = load_channels;
      instance->PostConnect = post_connect;
      instance->PostDisconnect = post_disconnect;
      bool connected =
          valid && prepare_socket(request) && crypto([&] { return freerdp_connect(instance); });
      mbedtls_platform_zeroize(request.password.data(),
                               request.password.size());
      if (connected) {
        s = snapshot();
        s.selected_protocol =
            freerdp_settings_get_uint32(settings, FreeRDP_SelectedProtocol);
        s.phase = RdpStatus::Ready;
        s.transport=connection.transport;
        s.vpn_profile=connection.profile;
        s.detail = "RDP autenticado";
        publish(s);
        for (;;) {
          while (!cancel && !vault_pause && network_connection_valid(connection)) {
            HANDLE handles[64];
            auto n = freerdp_get_event_handles(instance->context, handles, 64);
            if (!n)
              break;
            DWORD rc = WaitForMultipleObjects(n, handles, FALSE, 20);
            if (rc == WAIT_FAILED)
              break;
            if (!crypto([&] {
                  return freerdp_check_event_handles(instance->context);
                }))
              break;
            if (presentation_pending && !end_paint_impl(instance->context))
              break;
            channels_tick();
            Input input;
            while (xQueueReceive(events, &input, 0) == pdTRUE) {
              bool sent = crypto([&] {
                if(input.click){
                  bool down=freerdp_input_send_mouse_event(instance->context->input,input.flags|0x8000,input.x,input.y);
                  bool up=freerdp_input_send_mouse_event(instance->context->input,input.flags,input.x,input.y);
                  return BOOL(down && up);
                }
                return input.pointer ? freerdp_input_send_mouse_event(
                                           instance->context->input,
                                           input.flags, input.x, input.y)
                       : input.unicode
                           ? freerdp_input_send_unicode_keyboard_event(
                                 instance->context->input,
                                 input.down ? 0 : KBD_FLAGS_RELEASE, input.code)
                           : freerdp_input_send_keyboard_event_ex(
                                 instance->context->input, input.down, FALSE,
                                 input.code);
              });
              if (sent) {
                auto current = snapshot();
                if (input.pointer){
                  current.pointer_events+=input.click?2:1;
                  if(input.click){if(input.flags==0x1000)++current.left_clicks;else ++current.right_clicks;}
                }
                else
                  ++current.key_events;
                publish(current);
              }
            }
          }
          if (cancel || vault_pause || !request.reconnect ||
              freerdp_error_info(instance) != ERRINFO_SUCCESS)
            break;
          bool restored = false;
          unsigned attempts=0;
          while (!cancel && attempts < 3) {
            ++attempts;
            s = snapshot();
            s.phase = RdpStatus::Connecting;
            ++s.reconnect_attempts;
            s.frame.reset();
            s.detail = "Reconectando RDP: tentativa " +
                       std::to_string(attempts) + "/3";
            publish(s);
            // Let the UI release the old presentation before allocating a new
            // GDI.
            for (unsigned i = 0; i < 100 && !cancel; ++i)
              vTaskDelay(pdMS_TO_TICKS(10));
            if (cancel)
              break;
            // FreeRDP keeps GDI and may skip PostConnect during an automatic
            // reconnect. Keep the presentation buffers too: clearing them
            // here would leave EndPaint without a writable surface forever.
            Input discard;
            while (xQueueReceive(events, &discard, 0) == pdTRUE) {
            }
            bool reconnected =
                prepare_socket(request) && crypto([&] { return freerdp_reconnect(instance); });
            if (reconnected) {
              if (primary_cached && instance->context->gdi)
                presentation_pending = true;
              s = snapshot();
              s.phase = RdpStatus::Ready;
              s.error = 0;
              ++s.reconnections;
              s.detail = "RDP reconectado";
              publish(s);
              restored = true;
              break;
            }
            const auto error = freerdp_get_last_error(instance->context);
            if (error == FREERDP_ERROR_CONNECT_LOGON_FAILURE ||
                error == FREERDP_ERROR_CONNECT_WRONG_PASSWORD ||
                error == FREERDP_ERROR_CONNECT_ACCESS_DENIED ||
                error == FREERDP_ERROR_CONNECT_ACCOUNT_LOCKED_OUT ||
                snapshot().detail.find("Certificado RDP alterado") == 0)
              break;
          }
          if (!restored)
            break;
        }
        auto disconnect_error=freerdp_get_last_error(instance->context);
        auto disconnect_reason=freerdp_error_info(instance);
        for (uint16_t key :
             {0x1d, 0x11d, 0x2a, 0x36, 0x38, 0x138, 0x15b, 0x15c})
          crypto([&] {
            return freerdp_input_send_keyboard_event_ex(
                instance->context->input, FALSE, FALSE, key);
          });
        bool suspended=vault_pause && !cancel && disconnect_reason==ERRINFO_SUCCESS;
        if(suspended){
          auto saved=std::make_unique<RdpRequest>(request);
          const char *password=freerdp_settings_get_string(settings,FreeRDP_Password);
          snprintf(saved->password.data(),saved->password.size(),"%s",password?password:"");
          saved->width=snapshot().width;saved->height=snapshot().height;
          xSemaphoreTake(mutex,portMAX_DELAY);saved->fingerprint=accepted;resume_request=std::move(saved);xSemaphoreGive(mutex);
        }
        crypto([&] { return freerdp_disconnect(instance); });
        s = snapshot();
        bool interrupted=!cancel && disconnect_reason==ERRINFO_SUCCESS;
        s.phase=suspended?RdpStatus::Suspended:interrupted?RdpStatus::Failed:RdpStatus::Closed;
        s.error=interrupted?disconnect_error:0;
        if(suspended)s.detail="Sessao pausada para liberar memoria do cofre";
        else if(s.detail.find("Certificado RDP alterado")!=0)
          s.detail=interrupted?(request.reconnect?"Nao foi possivel reconectar RDP":"Conexao RDP interrompida"):"RDP desconectado";
      } else {
        s = snapshot();
        s.error = freerdp_get_last_error(instance->context);
        s.phase = RdpStatus::Failed;
        if (s.detail.find("Certificado RDP alterado") != 0)
          s.detail = connection_error.empty()?"RDP falhou; codigo " + std::to_string(s.error):connection_error;
      }
      channels_clear(instance->context);
      crypto([&] {
        freerdp_context_free(instance);
        return 0;
      });
    } else {
      s.phase = RdpStatus::Failed;
      s.detail = "Nao foi possivel criar contexto RDP";
    }
    if(prepared_socket>=0) {close(prepared_socket);prepared_socket=-1;}
    freerdp_free(instance);
    primary.reset();primary_capacity=0;retired_primary.reset();clear_prepared();
    for (auto &frame : frame_pool)
      frame.reset();
    if (cancel) {
      s.phase = RdpStatus::Closed;
      s.detail = "RDP desconectado";
      s.error = 0;
    }
    s.frame.reset();
    publish(s);
  }
  void *worker(void *) {
    for (;;) {
      RdpRequest *r = nullptr;
      if (xQueueReceive(requests, &r, portMAX_DELAY) == pdTRUE) {
        if (!r) {
#if CONFIG_TABREMOTO_DIAGNOSTICS
          probe_result = os_selftest_impl();
          xSemaphoreGive(probe_done);
#endif
          busy = false;
          continue;
        }
        cancel = false;
        run(*r);
        delete r;
        Input discard;
        while (xQueueReceive(events, &discard, 0) == pdTRUE) {
        }
        busy = false;
      }
    }
  }
#if CONFIG_TABREMOTO_DIAGNOSTICS
  bool os_selftest_impl() {
    probe_detail("memoria alinhada: falha preserva bloco original");
    auto *block = static_cast<unsigned char *>(winpr_aligned_calloc(1, 64, 32));
    if (!block)
      return false;
    memset(block, 0xa5, 64);
    void *failed = winpr_aligned_recalloc(block, 1, 64 * 1024 * 1024, 32);
    if (failed) {
      winpr_aligned_free(failed);
      return false;
    }
    bool preserved = true;
    for (unsigned i = 0; i < 64; ++i)
      preserved = preserved && block[i] == 0xa5;
    winpr_aligned_free(block);
    if (!preserved)
      return false;
    probe_detail("evento: criar/sinalizar/limpar");
    HANDLE event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!event)
      return false;
    bool ok =
        WaitForSingleObject(event, 0) == WAIT_TIMEOUT && SetEvent(event) &&
        WaitForSingleObject(event, 0) == WAIT_OBJECT_0 && ResetEvent(event) &&
        WaitForSingleObject(event, 0) == WAIT_TIMEOUT;
    CloseHandle(event);
    if (!ok)
      return false;
    probe_detail("semaforo: contar/consumir/liberar");
    HANDLE semaphore = CreateSemaphoreA(nullptr, 2, 2, nullptr);
    if (!semaphore)
      return false;
    ok = ok && WaitForSingleObject(semaphore, 0) == WAIT_OBJECT_0 &&
         WaitForSingleObject(semaphore, 0) == WAIT_OBJECT_0 &&
         WaitForSingleObject(semaphore, 0) == WAIT_TIMEOUT;
    LONG previous = -1;
    ok = ok && ReleaseSemaphore(semaphore, 1, &previous) && previous == 0 &&
         WaitForSingleObject(semaphore, 0) == WAIT_OBJECT_0;
    CloseHandle(semaphore);
    if (!ok)
      return false;
    probe_detail("timer: disparar/repetir/cancelar/fechar");
    HANDLE timer = CreateWaitableTimerA(nullptr, FALSE, nullptr);
    if (!timer)
      return false;
    LARGE_INTEGER due;
    due.QuadPart = -200000;
    ok = ok && SetWaitableTimer(timer, &due, 10, nullptr, nullptr, FALSE) &&
         WaitForSingleObject(timer, 200) == WAIT_OBJECT_0 &&
         WaitForSingleObject(timer, 200) == WAIT_OBJECT_0;
    ok = ok && CancelWaitableTimer(timer) &&
         WaitForSingleObject(timer, 0) == WAIT_TIMEOUT;
    ok = CloseHandle(timer) && ok;
    if (ok)
      probe_detail("eventos/semaforos/timers aprovados");
    return ok;
  }
#endif
};
std::array<std::atomic<Session *>, rdp_session_limit> sessions{};
std::atomic<unsigned> active{0};
SemaphoreHandle_t registry;
Session *ensure(unsigned id) {
  if (id >= sessions.size())
    return nullptr;
  xSemaphoreTake(registry, portMAX_DELAY);
  auto *s = sessions[id].load();
  if (!s && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 80 * 1024) {
    s = new Session;
    s->mutex = xSemaphoreCreateMutex();
#if CONFIG_TABREMOTO_DIAGNOSTICS
    s->probe_done = xSemaphoreCreateBinary();
#endif
    s->requests = xQueueCreate(1, sizeof(RdpRequest *));
    s->events = xQueueCreate(32, sizeof(Session::Input));
    esp_pthread_cfg_t previous = esp_pthread_get_default_config();
    esp_pthread_get_cfg(&previous);
    auto cfg = esp_pthread_get_default_config();
    cfg.stack_size = 32768;
    cfg.prio = 1;
    cfg.pin_to_core = 1;
    cfg.thread_name = "rdp_session";
    cfg.inherit_cfg = true;
    esp_pthread_set_cfg(&cfg);
    pthread_t thread;
    int rc = s->mutex && s->requests && s->events
                 ? pthread_create(
                       &thread, nullptr,
                       [](void *p) -> void * {
                         return static_cast<Session *>(p)->worker(nullptr);
                       },
                       s)
                 : -1;
    esp_pthread_set_cfg(&previous);
    if (rc == 0) {
      pthread_detach(thread);
      sessions[id] = s;
    } else {
      if (s->mutex)
        vSemaphoreDelete(s->mutex);
      if (s->requests)
        vQueueDelete(s->requests);
      if (s->events)
        vQueueDelete(s->events);
#if CONFIG_TABREMOTO_DIAGNOSTICS
      if (s->probe_done)
        vSemaphoreDelete(s->probe_done);
#endif
      delete s;
      s = nullptr;
    }
  }
  xSemaphoreGive(registry);
  return s;
}
Session &selected() { return *sessions[active.load()].load(); }
} // namespace
#if CONFIG_TABREMOTO_DIAGNOSTICS
std::string rdp_os_detail() {
  auto &s = selected();
  xSemaphoreTake(s.mutex, portMAX_DELAY);
  auto detail = s.os_detail;
  xSemaphoreGive(s.mutex);
  return detail;
}
bool rdp_os_selftest() {
  auto &s = selected();
  if (s.busy.exchange(true))
    return false;
  RdpRequest *probe = nullptr;
  if (xQueueSend(s.requests, &probe, 0) != pdTRUE) {
    s.busy = false;
    return false;
  }
  return xSemaphoreTake(s.probe_done, pdMS_TO_TICKS(2000)) == pdTRUE &&
         s.probe_result;
}
#else
std::string rdp_os_detail() { return ""; }
bool rdp_os_selftest() { return false; }
#endif
void rdp_start() {
  registry = xSemaphoreCreateMutex();
  esp_vfs_eventfd_config_t config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
  config.max_fds = 32;
  esp_vfs_eventfd_register(&config);
  WLog_SetLogLevel(WLog_GetRoot(), WLOG_ERROR);
  freerdp_register_addin_provider(
      [](LPCSTR name, LPCSTR, LPCSTR, DWORD) -> PVIRTUALCHANNELENTRY {
        return name && !strcmp(name, "disp")
                   ? reinterpret_cast<PVIRTUALCHANNELENTRY>(disp_DVCPluginEntry)
                   : nullptr;
      },
      0);
  ensure(0);
}
unsigned rdp_selected() { return active; }
bool rdp_select(unsigned id) {
  if (id >= sessions.size() || !sessions[id].load())
    return false;
  active = id;
  return true;
}
bool rdp_new_session() {
  for (unsigned id = 0; id < sessions.size(); ++id) {
    auto *s = sessions[id].load();
    if (!s || !s->busy) {
      if (!ensure(id))
        return false;
      active = id;
      return true;
    }
  }
  return false;
}
std::vector<RdpTab> rdp_sessions() {
  std::vector<RdpTab> tabs;
  for (unsigned id = 0; id < sessions.size(); ++id)
    if (auto *s = sessions[id].load())
      tabs.push_back({id, s->snapshot()});
  return tabs;
}
bool rdp_connect(RdpRequest *r) {
  if(vault_pause)return false;
  if (!r || r->host.empty() || r->host.size() > 253 || r->user.empty() ||
      r->user.size() > 128 || !r->port || r->port > 65535 || r->width < 640 ||
      r->width > 1280 || r->height < 360 || r->height > 720 || r->width % 4)
    return false;
  if (selected().busy && !rdp_new_session())
    return false;
  auto &s = selected();
  if (s.busy.exchange(true))
    return false;
  if (xQueueSend(s.requests, &r, 0) != pdTRUE) {
    s.busy = false;
    return false;
  }
  return true;
}
void rdp_disconnect() {
  auto &ctx = selected();
  ctx.cancel = true;
  xSemaphoreTake(ctx.mutex, portMAX_DELAY);
  ctx.resume_request.reset();
  if(ctx.state.phase==RdpStatus::Suspended){ctx.state.phase=RdpStatus::Closed;ctx.state.detail="RDP desconectado";++ctx.state.revision;}
  if (ctx.busy) {
    ctx.state.phase = RdpStatus::Closing;
    ctx.state.detail = "Desconectando RDP...";
    ctx.state.frame.reset();
    ++ctx.state.revision;
  }
  xSemaphoreGive(ctx.mutex);
}
void rdp_disconnect_all() {
  for (auto &entry : sessions)
    if (auto *s = entry.load()) {
      s->cancel=true;xSemaphoreTake(s->mutex,portMAX_DELAY);s->resume_request.reset();
      if(s->state.phase==RdpStatus::Suspended){s->state.phase=RdpStatus::Closed;s->state.detail="RDP desconectado";++s->state.revision;}
      xSemaphoreGive(s->mutex);
    }
}
bool rdp_suspend_for_vault(unsigned timeout_ms){
 vault_pause=true;auto deadline=esp_timer_get_time()+int64_t(timeout_ms)*1000;
 while(esp_timer_get_time()<deadline){bool running=false;for(auto &entry:sessions)if(auto *ctx=entry.load())running=running||ctx->busy.load();if(!running)return true;vTaskDelay(pdMS_TO_TICKS(20));}
 rdp_resume_after_vault();return false;
}
void rdp_resume_after_vault(){
 vault_pause=false;
 for(auto &entry:sessions)if(auto *ctx=entry.load()){
  xSemaphoreTake(ctx->mutex,portMAX_DELAY);auto request=std::move(ctx->resume_request);bool cancelled=ctx->cancel.load();xSemaphoreGive(ctx->mutex);
  if(!request || cancelled)continue;
  auto *raw=request.release();
  if(ctx->busy.exchange(true)){delete raw;continue;}
  if(xQueueSend(ctx->requests,&raw,0)!=pdTRUE){ctx->busy=false;delete raw;}
 }
}
bool rdp_accept(const std::string &fingerprint) {
  auto &ctx = selected();
  auto s = ctx.snapshot();
  if (s.phase != RdpStatus::Verify || s.fingerprint != fingerprint)
    return false;
  if(!profiles_save_pin("rdp",s.host,ctx.target_port,fingerprint))return false;
  xSemaphoreTake(ctx.mutex, portMAX_DELAY);
  ctx.accepted = fingerprint;
  xSemaphoreGive(ctx.mutex);
  return true;
}
RdpStatus rdp_status() {
  auto *s = sessions[active.load()].load();
  return s ? s->snapshot() : RdpStatus{};
}
bool rdp_key(uint16_t code, bool down, bool unicode) {
  auto &s = selected();
  Session::Input e{};
  e.code = code;
  e.down = down;
  e.unicode = unicode;
  return s.snapshot().phase == RdpStatus::Ready &&
         xQueueSend(s.events, &e, 0) == pdTRUE;
}
bool rdp_pointer(uint16_t x, uint16_t y, uint16_t flags) {
  auto &s = selected();
  Session::Input e{};
  e.pointer = true;
  e.x = x;
  e.y = y;
  e.flags = flags;
  return s.snapshot().phase == RdpStatus::Ready &&
         xQueueSend(s.events, &e, 0) == pdTRUE;
}
bool rdp_click(uint16_t x,uint16_t y,bool right){
  auto &s=selected();Session::Input e{};e.pointer=true;e.click=true;e.x=x;e.y=y;e.flags=right?0x2000:0x1000;
  return s.snapshot().phase==RdpStatus::Ready && xQueueSend(s.events,&e,0)==pdTRUE;
}
RdpClipboardStatus rdp_clipboard_status() {
  auto &s = selected();
  xSemaphoreTake(s.mutex, portMAX_DELAY);
  auto value = s.clipboard;
  xSemaphoreGive(s.mutex);
  return value;
}
bool rdp_clipboard_offer(const std::string &text) {
  std::vector<uint8_t> checked;
  bool valid = clipboard_utf16(text, checked);
  if (!checked.empty())
    mbedtls_platform_zeroize(checked.data(), checked.size());
  if (!valid)
    return false;
  auto &s = selected();
  xSemaphoreTake(s.mutex, portMAX_DELAY);
  bool ready = s.state.phase == RdpStatus::Ready && s.clipboard.ready;
  if (ready) {
    mbedtls_platform_zeroize(s.offered_clipboard.data(),
                             s.offered_clipboard.size());
    s.offered_clipboard = text;
    s.offer_pending = true;
  }
  xSemaphoreGive(s.mutex);
  return ready;
}
bool rdp_clipboard_fetch() {
  auto &s = selected();
  xSemaphoreTake(s.mutex, portMAX_DELAY);
  bool ready = s.state.phase == RdpStatus::Ready && s.clipboard.ready &&
               s.clipboard.remote_available && !s.clipboard.waiting;
  if (ready)
    s.fetch_pending = true;
  xSemaphoreGive(s.mutex);
  return ready;
}
bool rdp_resize(unsigned width, unsigned height) {
  auto &s=selected();auto status=s.snapshot();
  if(status.phase!=RdpStatus::Ready)return s.resize_failure("Conecte o desktop antes de alterar a resolucao.");
  // Choosing the current geometry is a no-op, even on a server without disp.
  if(width==status.width && height==status.height)return true;
  if(!status.dynamic_resize)return s.resize_failure("Este servidor nao anunciou alteracao de resolucao durante a sessao.");
  if(width<640 || width>1280 || height<360 || height>720 || width%4)
    return s.resize_failure("Resolucao fora dos limites do Tab5.");
  if(status.resizing)return false;
  size_t capacity=size_t(width)*height*4;
  size_t pool=s.presentation_capacity.load(),primary=s.primary_capacity.load();
  size_t frame_capacity=capacity/2;
  size_t needed=(capacity>primary?capacity:0)+(frame_capacity>pool?2*frame_capacity:0);
  if(needed && (heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<needed+128*1024 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<capacity))
    return s.resize_failure("Nao ha bloco livre para ampliar a resolucao. O desktop atual foi mantido.");
  status.resizing=true;status.detail="Alterando resolucao remota...";s.publish(status);
  s.resize_pending=(width<<16)|height;return true;
}

} // namespace tabremoto
