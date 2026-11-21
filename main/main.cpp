#include "sdkconfig.h"
#ifndef CONFIG_TABREMOTO_DIAGNOSTICS
#define CONFIG_TABREMOTO_DIAGNOSTICS 0
#endif
#include "board.hpp"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "files_diag.hpp"
#include "mbedtls/base64.h"
#include "network.hpp"
#include "profiles.hpp"
#include "openssl/crypto.h"
#include "provision.hpp"
#include "rdp.hpp"
#include "rdp_port.h"
#include "rdp_ui.hpp"
#include "remote_ui.hpp"
#include "security.hpp"
#include "security_ui.hpp"
#include "ssh.hpp"
#include "storage.hpp"
#include "ui.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace tabremoto;
static BoardStatus board;
static bool ready = false;
static void *ssl_alloc(size_t n, const char *, int) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void *ssl_realloc(void *p, size_t n, const char *, int) {
  return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void ssl_free(void *p, const char *, int) { heap_caps_free(p); }

static int command(int argc, char **argv) {
  const char *op = argc > 1 ? argv[1] : "status";
  auto *reply = cJSON_CreateObject();
  bool ok = true;
  if (!strcmp(op, "status")) {
    auto storage = storage_snapshot();
    cJSON_AddBoolToObject(reply, "sd_busy", storage.busy);
    cJSON_AddNumberToObject(reply, "sd_generation", storage.generation);
    cJSON_AddBoolToObject(reply, "external_font", ui_external_font());
    cJSON_AddNumberToObject(reply, "input_selftest", ui_test_result());
    cJSON_AddStringToObject(reply, "input_selftest_detail",
                            ui_test_detail().c_str());
    cJSON_AddBoolToObject(reply, "ready", ready);
    cJSON_AddBoolToObject(reply, "diagnostics", CONFIG_TABREMOTO_DIAGNOSTICS);
    if(CONFIG_TABREMOTO_DIAGNOSTICS)cJSON_AddBoolToObject(reply,"ui_pointer_busy",tabremoto_ui_pointer_busy());
    cJSON_AddNumberToObject(reply, "ui_max_tick_us", ui_max_tick_us());
    cJSON_AddNumberToObject(reply, "ui_slow_ticks", ui_slow_ticks());
    cJSON_AddStringToObject(reply, "app",
                            esp_app_get_description()->project_name);
    cJSON_AddStringToObject(reply, "version",
                            esp_app_get_description()->version);
    cJSON_AddBoolToObject(reply, "display", board.display);
    cJSON_AddBoolToObject(reply, "touch_driver", board.touch);
    cJSON_AddBoolToObject(reply, "keyboard_detected", board.keyboard);
    cJSON_AddNumberToObject(reply, "keyboard_version", board.keyboard_version);
    cJSON_AddBoolToObject(reply, "sd_mounted", storage.mounted);
    cJSON_AddBoolToObject(reply, "sd_writable", storage.writable);
    cJSON_AddBoolToObject(reply, "resources", storage.resources);
    cJSON_AddStringToObject(reply, "storage_error", storage.error.c_str());
    cJSON_AddNumberToObject(reply, "flash_bytes", board.flash_bytes);
    cJSON_AddNumberToObject(reply, "chip_revision", board.chip_revision);
    cJSON_AddNumberToObject(
        reply, "internal_free",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(
        reply, "internal_min",
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(reply, "internal_largest",
                            heap_caps_get_largest_free_block(
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(reply,"psram_largest",heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  cJSON_AddNumberToObject(reply, "psram_free",
                            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(reply, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(reply, "touch_clicks", ui_clicks());
    cJSON_AddNumberToObject(reply, "ui_flushes", ui_flushes());
    auto keys = keyboard_stats();
    cJSON_AddNumberToObject(reply, "key_presses", keys.pressed);
    cJSON_AddNumberToObject(reply, "key_releases", keys.released);
    cJSON_AddNumberToObject(reply, "key_errors", keys.errors);
    auto port = rdp_port_probe_status();
    auto *probe = cJSON_AddObjectToObject(reply, "rdp_crypto_probe");
    cJSON_AddBoolToObject(probe, "running", port.running);
    cJSON_AddBoolToObject(probe, "sha256", port.sha256_ok);
    cJSON_AddBoolToObject(probe, "random", port.random_ok);
    cJSON_AddBoolToObject(probe, "tls", port.tls_ok);
    cJSON_AddBoolToObject(probe, "hostname_rejected", port.hostname_rejected);
    cJSON_AddBoolToObject(probe, "untrusted_rejected", port.untrusted_rejected);
    cJSON_AddNumberToObject(probe, "error", port.error);
    cJSON_AddNumberToObject(probe, "elapsed_ms", port.elapsed_ms);
    cJSON_AddNumberToObject(probe, "internal_before", port.internal_before);
    cJSON_AddNumberToObject(probe, "internal_after", port.internal_after);
    cJSON_AddStringToObject(probe, "version", port.version);
    cJSON_AddStringToObject(probe, "protocol", port.protocol);
    cJSON_AddStringToObject(probe, "cipher", port.cipher);
    cJSON_AddStringToObject(probe, "detail", port.detail);
    auto sec = security_status();
    auto *security = cJSON_AddObjectToObject(reply, "security");
    cJSON_AddBoolToObject(security, "ready", sec.ready);
    cJSON_AddBoolToObject(security, "busy", security_busy());
    cJSON_AddBoolToObject(security, "exists", sec.exists);
    cJSON_AddBoolToObject(security, "unlocked", sec.unlocked);
    cJSON_AddNumberToObject(security, "generation", sec.generation);
    cJSON_AddNumberToObject(security, "connections", sec.connections);
    cJSON_AddNumberToObject(security, "identities", sec.identities);
    cJSON_AddNumberToObject(security, "secrets", sec.secrets);
    cJSON_AddNumberToObject(security, "trust", sec.trust);
    cJSON_AddNumberToObject(security, "selftest", sec.selftest);
    cJSON_AddNumberToObject(security, "operation_ms", sec.operation_ms);
    cJSON_AddStringToObject(security, "message", sec.message.c_str());
    auto ns = network_status();
    auto *network = cJSON_AddObjectToObject(reply, "network");
    cJSON_AddBoolToObject(network, "wifi", ns.wifi);
    cJSON_AddBoolToObject(network,"wifi_automatic",ns.wifi_automatic);
    cJSON_AddBoolToObject(network,"profiles_ready",profiles_ready());
    cJSON_AddNumberToObject(network,"saved_wifi_count",profiles_wifi().size());
    cJSON_AddStringToObject(network,"profiles_message",profiles_message().c_str());
    cJSON_AddBoolToObject(network, "ethernet", ns.ethernet);
    cJSON_AddBoolToObject(network, "usb_present", ns.usb_present);
    cJSON_AddNumberToObject(network, "usb_vid", ns.vid);
    cJSON_AddNumberToObject(network, "usb_pid", ns.pid);
    cJSON_AddBoolToObject(network, "vpn", ns.vpn);
    auto *tunnels = cJSON_AddArrayToObject(network, "tunnels");
    for (const auto &tunnel : ns.tunnels) {
      auto *entry = cJSON_CreateObject();
      cJSON_AddStringToObject(entry, "id", tunnel.id.c_str());
      cJSON_AddStringToObject(entry, "transport", tunnel.transport.c_str());
      cJSON_AddBoolToObject(entry, "up", tunnel.up);
      cJSON_AddBoolToObject(entry, "armed", tunnel.armed);
      cJSON_AddNumberToObject(entry, "rx", tunnel.rx);
      cJSON_AddNumberToObject(entry, "tx", tunnel.tx);
      cJSON_AddItemToArray(tunnels, entry);
    }

    cJSON_AddBoolToObject(network, "vpn_armed", ns.vpn_armed);
    cJSON_AddNumberToObject(network, "vpn_rx_udp", ns.rx);
    cJSON_AddNumberToObject(network, "vpn_tx_udp", ns.tx);
    cJSON_AddNumberToObject(network, "vpn_handshake_age_ms",
                            ns.handshake_age_ms);
    cJSON_AddNumberToObject(network, "epoch", network_epoch());
    cJSON_AddNumberToObject(network, "wifi_rssi_dbm", ns.rssi);
    cJSON_AddNumberToObject(network, "scan_count", ns.scan_count);
    cJSON_AddBoolToObject(network, "scan_match", ns.scan_match);
    cJSON_AddNumberToObject(network, "scan_channel", ns.scan_channel);
    cJSON_AddStringToObject(network, "wifi_ip", ns.wifi_ip.c_str());
    cJSON_AddStringToObject(network, "detail", ns.detail.c_str());
    cJSON_AddStringToObject(network, "vpn_detail", ns.vpn_detail.c_str());
    auto *sessions = cJSON_AddArrayToObject(reply, "sessions");
    for (const auto &tab : ssh_sessions()) {
      auto *entry = cJSON_CreateObject();
      cJSON_AddStringToObject(entry, "protocol", "ssh");
      cJSON_AddNumberToObject(entry, "id", tab.id);
      cJSON_AddBoolToObject(entry, "selected", tab.id == ssh_selected());
      cJSON_AddNumberToObject(entry, "phase", tab.status.phase);
      cJSON_AddStringToObject(entry, "host", tab.status.host.c_str());
      cJSON_AddStringToObject(entry,"transport",tab.status.transport.c_str());
      cJSON_AddStringToObject(entry,"vpn_profile",tab.status.vpn_profile.c_str());
      cJSON_AddNumberToObject(entry, "rx", tab.status.rx);
      cJSON_AddNumberToObject(entry, "tx", tab.status.tx);
      cJSON_AddItemToArray(sessions, entry);
    }
    for (const auto &tab : rdp_sessions()) {
      auto *entry = cJSON_CreateObject();
      cJSON_AddStringToObject(entry, "protocol", "rdp");
      cJSON_AddNumberToObject(entry, "id", tab.id);
      cJSON_AddBoolToObject(entry, "selected", tab.id == rdp_selected());
      cJSON_AddNumberToObject(entry, "phase", tab.status.phase);
      cJSON_AddStringToObject(entry, "host", tab.status.host.c_str());
      cJSON_AddStringToObject(entry,"transport",tab.status.transport.c_str());
      cJSON_AddStringToObject(entry,"vpn_profile",tab.status.vpn_profile.c_str());
      cJSON_AddNumberToObject(entry, "frames", tab.status.frames);
      cJSON_AddItemToArray(sessions, entry);
    }
    auto ss = ssh_status();
    auto *ssh = cJSON_AddObjectToObject(reply, "ssh");
    cJSON_AddNumberToObject(ssh, "phase", ss.phase);
    cJSON_AddStringToObject(ssh, "detail", ss.detail.c_str());
    cJSON_AddStringToObject(ssh, "fingerprint", ss.fingerprint.c_str());
    cJSON_AddStringToObject(ssh, "key_type", ss.key_type.c_str());
    cJSON_AddNumberToObject(ssh, "revision", ss.revision);
    cJSON_AddNumberToObject(ssh, "rx", ss.rx);
    cJSON_AddNumberToObject(ssh, "tx", ss.tx);
    cJSON_AddBoolToObject(ssh, "transfer", ss.transfer);
    cJSON_AddBoolToObject(ssh, "transfer_ok", ss.transfer_ok);
    cJSON_AddNumberToObject(ssh, "transfer_bytes", ss.transfer_bytes);
    cJSON_AddNumberToObject(ssh, "transfer_total", ss.transfer_total);
    cJSON_AddNumberToObject(ssh, "queued", ss.queued);
    cJSON_AddNumberToObject(ssh, "terminal_history",
                            ss.screen ? ss.screen->history : 0);
    auto rs = rdp_status();
    cJSON_AddNumberToObject(reply,"rdp_copied_pixels",rs.copied_pixels);
    cJSON_AddNumberToObject(reply,"rdp_copy_max_us",rs.copy_max_us);
    cJSON_AddNumberToObject(reply,"rdp_paint_calls",rs.paint_calls);
    cJSON_AddBoolToObject(reply, "rdp_clipboard", rs.clipboard_ready);
    cJSON_AddBoolToObject(reply, "rdp_dynamic_resize", rs.dynamic_resize);
    cJSON_AddBoolToObject(reply,"rdp_resizing",rs.resizing);
    cJSON_AddNumberToObject(reply, "rdp_width", rs.width);
    cJSON_AddNumberToObject(reply, "rdp_height", rs.height);
    cJSON_AddNumberToObject(reply, "rdp_reconnect_attempts",
                            rs.reconnect_attempts);
    cJSON_AddNumberToObject(reply, "rdp_reconnections", rs.reconnections);
    auto *rdp = cJSON_AddObjectToObject(reply, "rdp");
    cJSON_AddNumberToObject(rdp, "phase", rs.phase);
    cJSON_AddNumberToObject(rdp,"left_clicks",rs.left_clicks);cJSON_AddNumberToObject(rdp,"right_clicks",rs.right_clicks);
    auto ts=rdp_ui_touch_stats();auto *touch=cJSON_AddObjectToObject(reply,"rdp_touch");
    cJSON_AddNumberToObject(touch,"presses",ts.presses);cJSON_AddNumberToObject(touch,"moves",ts.moves);
    cJSON_AddNumberToObject(touch,"releases",ts.releases);cJSON_AddNumberToObject(touch,"cancels",ts.cancels);
    cJSON_AddNumberToObject(touch,"taps",ts.taps);cJSON_AddNumberToObject(touch,"queued",ts.queued);
    cJSON_AddNumberToObject(touch,"dropped",ts.dropped);cJSON_AddNumberToObject(touch,"last_ms",ts.last_ms);
    cJSON_AddNumberToObject(touch,"last_motion",ts.last_motion);
    cJSON_AddNumberToObject(rdp, "frames", rs.frames);
    cJSON_AddNumberToObject(rdp,"resize_errors",rs.resize_errors);
    cJSON_AddNumberToObject(rdp, "key_events", rs.key_events);
    cJSON_AddNumberToObject(rdp, "pointer_events", rs.pointer_events);
    cJSON_AddNumberToObject(rdp, "error", rs.error);
    cJSON_AddNumberToObject(rdp, "selected_protocol", rs.selected_protocol);
    cJSON_AddStringToObject(rdp, "detail", rs.detail.c_str());
    cJSON_AddStringToObject(rdp, "fingerprint", rs.fingerprint.c_str());
    cJSON_AddStringToObject(reply, "protocols", "SSH; RDP NLA/TLS");
  } else if (!strcmp(op,"provision-status")) {
    provision_status(reply);
  } else if (!strcmp(op, "provision-open")) {
    ok = provision_open(reply);
  } else if (!strcmp(op, "provision") && argc == 3) {
    ok = provision_apply(argv[2]);
  } else if (!strcmp(op, "network-open")) {
    remote_ui_open(true);
  } else if (!strcmp(op, "screen-read") && argc == 3) {
    long offset = strtol(argv[2], nullptr, 10);
    ok = offset >= 0 && offset < 4000000 && storage_capture_begin();
    if (ok) {
      FILE *f = fopen("/sdcard/TabRemoto/cache/screen.ppm", "rb");
      ok = f && fseek(f, offset, SEEK_SET) == 0;
      std::vector<unsigned char> bytes(768), encoded(1025);
      size_t n = 0, used = 0;
      if (ok) {
        n = fread(bytes.data(), 1, bytes.size(), f);
        ok = !ferror(f) && mbedtls_base64_encode(encoded.data(), encoded.size(),
                                                 &used, bytes.data(), n) == 0;
      }
      if (f)
        fclose(f);
      if (ok) {
        cJSON_AddStringToObject(reply, "data", (char *)encoded.data());
        cJSON_AddNumberToObject(reply, "bytes", n);
      }
      storage_capture_end();
    }
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "ssh-ui-trust")) &&
             argc == 3) {
    ok = remote_ui_test_accept(argv[2]);
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "ssh-ui-probe"))) {
    ui_terminal_probe();
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "ssh-ui-status"))) {
    ok = lvgl_port_lock(2000);
    if (ok) {
      cJSON_AddBoolToObject(reply, "focused", remote_ui_terminal_focused());
      cJSON_AddBoolToObject(reply, "fullscreen",
                            remote_ui_session_fullscreen());
      cJSON_AddBoolToObject(reply, "trust_dialog", remote_ui_trust_visible());
      cJSON_AddBoolToObject(reply, "rendered", remote_ui_rendered_marker());
      lvgl_port_unlock();
    }
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "rdp-os-test")) {
    ok = rdp_os_selftest();
    cJSON_AddStringToObject(reply, "detail", rdp_os_detail().c_str());
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS &&
             !strcmp(op, "rdp-clipboard-fetch")) {
    ok = rdp_clipboard_fetch();
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS &&
             !strcmp(op, "rdp-clipboard-read")) {
    auto clip = rdp_clipboard_status();
    cJSON_AddStringToObject(reply, "text", clip.text.c_str());
    cJSON_AddNumberToObject(reply, "sent", clip.sent);
    cJSON_AddNumberToObject(reply, "received", clip.received);
    cJSON_AddStringToObject(reply, "detail", clip.detail.c_str());
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "rdp-resize") &&
             argc == 4) {
    ok = rdp_resize(atoi(argv[2]), atoi(argv[3]));
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "ui-action-status")) {
    cJSON_AddNumberToObject(reply, "result", ui_action_result());
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op,"ui-layout-status")) {
    ok=lvgl_port_lock(2000);
    if(ok){cJSON_AddNumberToObject(reply,"errors",ui_button_layout_errors());lvgl_port_unlock();}
  } else if (!strcmp(op, "rdp-open")) {
    rdp_ui_open();
  } else if (!strcmp(op, "rdp-disconnect-all")) {
    rdp_disconnect_all();
  } else if (!strcmp(op, "ssh-disconnect-all")) {
    ssh_disconnect_all();
  } else if (!strcmp(op, "rdp-disconnect")) {
    rdp_disconnect();
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "rdp-trust") &&
             argc == 3) {
    ok = lvgl_port_lock(2000);
    if (ok) {
      ok = rdp_ui_test_accept(argv[2]);
      lvgl_port_unlock();
    }
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "rdp-ui-status")) {
    ok = lvgl_port_lock(2000);
    if (ok) {
      cJSON_AddBoolToObject(reply, "focused", rdp_ui_focused());
      cJSON_AddBoolToObject(reply, "rendered", rdp_ui_rendered());
      cJSON_AddBoolToObject(reply, "trust_visible", rdp_ui_trust_visible());
      cJSON_AddBoolToObject(reply, "sidebar_clear", rdp_ui_sidebar_clear());
      cJSON_AddBoolToObject(reply,"icons_only",rdp_ui_icons_only());
      cJSON_AddBoolToObject(reply,"toast_visible",rdp_ui_toast_visible());
      cJSON_AddNumberToObject(reply,"pointer_x",rdp_ui_pointer_x());
      cJSON_AddNumberToObject(reply,"pointer_y",rdp_ui_pointer_y());
      lvgl_port_unlock();
    }
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op,"rdp-touch") && argc==6) {
    ok=lvgl_port_lock(2000);
    if(ok){ok=rdp_ui_touch_test(atoi(argv[2]),atoi(argv[3]),atoi(argv[4]),strtoul(argv[5],nullptr,10));lvgl_port_unlock();}
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "rdp-key") &&
             argc == 4) {
    ok = rdp_key(strtoul(argv[2], nullptr, 0), atoi(argv[3]) != 0);
  } else if (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "rdp-pointer") &&
             argc == 5) {
    ok =
        rdp_pointer(atoi(argv[2]), atoi(argv[3]), strtoul(argv[4], nullptr, 0));
  } else if (!strcmp(op, "ssh-open")) {
    remote_ui_open();
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "vpn-probe")) &&
             argc == 4) {
    ok = network_probe(argv[2], atoi(argv[3]), true);
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS &&
              !strcmp(op, "wifi-reconnect-test"))) {
    ok = network_wifi_cycle();
  } else if (!strcmp(op, "wifi-scan-result")) {
    cJSON_AddStringToObject(reply, "networks",
                            network_status().wifi_scan_json.c_str());
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS &&
              !strcmp(op, "network-selftest"))) {
    ok = network_policy_selftest();
  } else if (!strcmp(op, "ethernet-start")) {
    ok = network_ethernet();
  } else if (!strcmp(op, "vpn-stop")) {
    network_vpn_stop(false, argc == 3 ? argv[2] : "");
  } else if (!strcmp(op, "ssh-disconnect")) {
    ssh_disconnect();
  } else if (!strcmp(op, "ssh-output")) {
    auto s = ssh_status();
    cJSON_AddStringToObject(reply, "output", s.output.c_str());
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "file-fixture"))) {
    ok = files_fixture(reply);
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "file-hash")) &&
             argc == 3) {
    ok = files_hash(argv[2], reply);
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "file-remove")) &&
             argc == 3) {
    ok = files_remove(argv[2]);
  } else if (!strcmp(op, "ssh-list-result")) {
    cJSON_AddStringToObject(reply, "listing", ssh_status().listing.c_str());
  } else if (!strcmp(op, "ssh-list") && argc == 3) {
    FileRequest r;
    r.remote = argv[2];
    ok = ssh_files(r);
  } else if (!strcmp(op, "ssh-file") && argc == 5) {
    FileRequest r;
    r.remote = argv[3];
    r.local = argv[4];
    r.operation = !strcmp(argv[2], "get")       ? FileRequest::Download
                  : !strcmp(argv[2], "put")     ? FileRequest::Upload
                  : !strcmp(argv[2], "scp-get") ? FileRequest::ScpDownload
                  : !strcmp(argv[2], "scp-put") ? FileRequest::ScpUpload
                                                : FileRequest::Remove;
    ok = (!strcmp(argv[2], "get") || !strcmp(argv[2], "put") ||
          !strcmp(argv[2], "scp-get") || !strcmp(argv[2], "scp-put") ||
          !strcmp(argv[2], "rm")) &&
         ssh_files(r);
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "storage-test"))) {
    std::string error;
    ok = storage_selftest(error);
    cJSON_AddStringToObject(reply, "error", error.c_str());
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "rdp-probe"))) {
    ok = !security_busy() && rdp_port_probe_start() == 0;
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS &&
              !strcmp(op, "security-boot-prepare")) ||
             (CONFIG_TABREMOTO_DIAGNOSTICS &&
              !strcmp(op, "security-boot-check"))) {
    auto *r = new SecurityRequest;
    r->action =
        (CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "security-boot-prepare"))
            ? SecurityAction::PrepareBoot
            : SecurityAction::CheckBoot;
    ok = security_submit(r);
    if (!ok)
      delete r;
  } else if (!strcmp(op, "security-open")) {
    security_ui_open();
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS &&
              !strcmp(op, "security-selftest"))) {
    auto *r = new SecurityRequest;
    r->action = SecurityAction::Selftest;
    ok = security_submit(r);
    if (!ok)
      delete r;
  } else if ((CONFIG_TABREMOTO_DIAGNOSTICS && !strcmp(op, "input-selftest"))) {
    ui_test_request();
  } else if (!strcmp(op, "storage-retry")) {
    storage_retry();
  } else if (!strcmp(op, "snapshot"))
    ok = ready && storage_snapshot().writable && ui_snapshot();
  else if (!strcmp(op, "reboot")) {
    board_prepare_reboot();
    puts("TABREMOTO {\"ok\":true}");
    fflush(stdout);
    esp_restart();
  } else {
    ok = false;
    cJSON_AddStringToObject(reply, "error", "unknown command");
  }
  cJSON_AddBoolToObject(reply, "ok", ok);
  char *text = cJSON_PrintUnformatted(reply);
  // USB Serial/JTAG has a bounded TX buffer. Pace large JSON replies so
  // terminal history is never silently truncated by the VFS write timeout.
  fputs("TABREMOTO ", stdout);
  for (size_t offset = 0, length = strlen(text); offset < length;) {
    size_t count = std::min(size_t(128), length - offset);
    fwrite(text + offset, 1, count, stdout);
    fflush(stdout);
    offset += count;
    vTaskDelay(1);
  }
  putchar('\n');
  fflush(stdout);
  cJSON_free(text);
  cJSON_Delete(reply);
  return ok ? 0 : 1;
}
extern "C" void app_main() {
  // Start the console after initialization so diagnostics never race mutable
  // boot state.
  CRYPTO_set_mem_functions(ssl_alloc, ssl_realloc, ssl_free);
  // RTOS applications reboot instead of calling process exit. Initialize this
  // option before any provider or TLS consumer can register an exit handler.
  ESP_ERROR_CHECK(OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr) == 1
                      ? ESP_OK
                      : ESP_FAIL);
  board = board_start();
  auto storage = storage_start();
  profiles_start();
  network_start();
  ssh_start();
  rdp_start();
  security_start();
  ready = ui_start(board, storage);
  storage.assets.reset();
  esp_console_repl_t *repl = nullptr;
  esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  cfg.prompt = "TabRemoto>";
  cfg.task_stack_size = 12288;
  cfg.max_cmdline_length = 4096;
  esp_console_dev_usb_serial_jtag_config_t usb =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &cfg, &repl));
  esp_console_cmd_t cmd{};
  cmd.command = "tabremoto";
  cmd.help = "status | snapshot | provision-open | provision-status | reboot";
  cmd.func = command;
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
