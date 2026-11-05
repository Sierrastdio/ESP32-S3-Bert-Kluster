#pragma once
#define CFG_TUSB_RHPORT0_MODE       0
#define CFG_TUSB_RHPORT1_MODE       (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)
#define CFG_TUH_ENABLED             1
#define CFG_TUH_RPI_PIO_USB         1
#define BOARD_TUH_RHPORT            1
#define CFG_TUH_HID                 4
#define CFG_TUH_DEVICE_MAX          1
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUSB_OS                 OPT_OS_PICO