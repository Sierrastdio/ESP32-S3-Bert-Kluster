#pragma once
#include <stdbool.h>

#define CONFIG_PIO_USB_DP_PIN 8 // D- 는 자동으로 GPIO 9

void usb_kbd_start(void);  // core1에서 USB 호스트 시작
int usb_kbd_getchar(void); // 없으면 -1 (PICO_ERROR_TIMEOUT)
