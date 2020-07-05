#ifndef KernelMessage_h
#define KernelMessage_h

#define KERNEL_EVENT_CODE 0x8102
#define KERNEL_EVENT_VENDOR_ID "VoodooWMI"

enum {
    kToggleWifi = 1,
    kSwitchDisplay = 2,
    kEnableTouchpad = 3,
    kDisableTouchpad = 4,
    kDecreaseKeyboardBacklight = 5,
    kIncreaseKeyboardBacklight = 6,
};

struct VoodooWMIHotkeyMessage {
    int type;
    int arg1;
    int arg2;
};

#endif /* KernelMessage_h */
