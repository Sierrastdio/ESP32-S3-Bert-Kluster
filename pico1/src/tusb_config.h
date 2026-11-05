#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE)   // 네이티브 USB: stdio CDC
#define CFG_TUH_ENABLED         1
#define CFG_TUH_RPI_PIO_USB     1                   // rhport 1 = PIO USB 호스트
#define BOARD_TUH_RHPORT        1
#define CFG_TUH_HID             4
#define CFG_TUH_DEVICE_MAX      (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_ENUMERATION_BUFSIZE 256
