#ifndef KernelMessage_h
#define KernelMessage_h

#define KERNEL_EVENT_CODE 0x8102
#define KERNEL_EVENT_VENDOR_ID "VoodooWMI"

enum WMIHotkeyAction {
    kActionSleep,
    kActionLockScreen,
    kActionSwitchScreen,
    kActionToggleAirplaneMode,
    kActionToggleTouchpad,
    kActionKeyboardBacklightDown,
    kActionKeyboardBacklightUp,
    kActionScreenBrightnessDown,
    kActionScreenBrightnessUp,
};

struct VoodooWMIHotkeyMessage {
    int type;
    int arg1;
    int arg2;
};

#endif /* KernelMessage_h */
