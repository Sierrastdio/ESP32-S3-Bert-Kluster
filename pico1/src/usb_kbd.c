#include "usb_kbd.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pio_usb.h"
#include "tusb.h"
#include <string.h>

// 1: 진단용 LED 사용 (연결되면 켜짐, 키보드 HID 인식 시 한 번 깜빡임), 0: 끔
#define USB_KBD_DEBUG_LED 1

#if USB_KBD_DEBUG_LED
  #include "hardware/gpio.h"
  #define DBG_LED PICO_DEFAULT_LED_PIN
  #define DBG(v) gpio_put(DBG_LED, (v))
#else
  #define DBG(v) ((void)0)
#endif

static queue_t kbd_q;                 // core1 -> core0
static uint8_t prev_keys[6];

static char hid_to_ascii(uint8_t k, bool shift) {
  if (k >= 0x04 && k <= 0x1d) { char c = 'a' + (k - 4); return shift ? c - 32 : c; }
  if (k >= 0x1e && k <= 0x27) {
    static const char n[] = "1234567890", s[] = "!@#$%^&*()";
    return shift ? s[k - 0x1e] : n[k - 0x1e];
  }
  switch (k) {
    case 0x28: case 0x58: return '\n';
    case 0x2a: return 8;
    case 0x2c: return ' ';
    case 0x2d: return shift ? '_' : '-';
    case 0x2e: return shift ? '+' : '=';
    case 0x2f: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x31: return shift ? '|' : '\\';
    case 0x33: return shift ? ':' : ';';
    case 0x34: return shift ? '"' : '\'';
    case 0x35: return shift ? '~' : '`';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
  }
  return 0;
}

// 기기가 연결되어 열거가 끝나면 호출됨 (HID 여부와 무관)
void tuh_mount_cb(uint8_t dev_addr)  { (void)dev_addr; DBG(1); }
void tuh_umount_cb(uint8_t dev_addr) { (void)dev_addr; DBG(0); memset(prev_keys, 0, sizeof(prev_keys)); }

void tuh_hid_mount_cb(uint8_t dev, uint8_t inst, const uint8_t *desc, uint16_t len) {
  (void)desc; (void)len;
  if (tuh_hid_interface_protocol(dev, inst) == HID_ITF_PROTOCOL_KEYBOARD) {
    tuh_hid_set_protocol(dev, inst, HID_PROTOCOL_BOOT);   // 8바이트 고정 리포트
    memset(prev_keys, 0, sizeof(prev_keys));
    tuh_hid_receive_report(dev, inst);
    DBG(0); sleep_ms(150); DBG(1);                        // 키보드 HID 인식 표시
  }
}

void tuh_hid_report_received_cb(uint8_t dev, uint8_t inst, const uint8_t *report, uint16_t len) {
  if (tuh_hid_interface_protocol(dev, inst) == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
    const hid_keyboard_report_t *r = (const hid_keyboard_report_t *)report;
    bool shift = r->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
    for (int i = 0; i < 6; i++) {
      uint8_t k = r->keycode[i];
      if (k < 4) continue;                                // 0=없음, 1=rollover 오류
      bool was = false;
      for (int j = 0; j < 6; j++) if (prev_keys[j] == k) { was = true; break; }
      if (was) continue;                                  // 눌린 채 유지 중인 키는 무시
      char c = hid_to_ascii(k, shift);
      if (c) queue_try_add(&kbd_q, &c);
    }
    memcpy(prev_keys, r->keycode, 6);
  }
  tuh_hid_receive_report(dev, inst);
}

static void core1_main(void) {
  pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
  cfg.pin_dp = CONFIG_PIO_USB_DP_PIN;                     // D-는 자동으로 GP9
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
  tuh_init(1);                                            // rhport 1 = PIO USB
  while (true) tuh_task();
}

void usb_kbd_start(void) {
  queue_init(&kbd_q, sizeof(char), 64);
  multicore_launch_core1(core1_main);
}

int usb_kbd_getchar(void) {
  char c;
  return queue_try_remove(&kbd_q, &c) ? (unsigned char)c : PICO_ERROR_TIMEOUT;
}