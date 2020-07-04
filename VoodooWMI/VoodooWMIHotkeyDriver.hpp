#ifndef __VOODOOWMI_HOTKEY_DRIVER__
#define __VOODOOWMI_HOTKEY_DRIVER__

#include <IOKit/IOService.h>
#include "VoodooWMIController.hpp"

#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif

#define DEBUG_MSG
#ifdef DEBUG_MSG
#define DEBUG_LOG(args...)  IOLog(args)
#else
#define DEBUG_LOG(args...)
#endif

#define KERNEL_EVENT_CODE 0x8102
#define KERNEL_EVENT_VENDOR_ID "tongfang"

#define TONGFANG_WMI_EVENT_GUID "ABBC0F72-8EA1-11D1-00A0-C90629100000"

enum WMIEvent {
    kWMIEventWiFiOn = 0x1A,
    kWMIEventWiFiOff = 0x1B,
    kWMIEventAdjustKeyboardBacklight = 0xF0,
    kWMIEventVolumeMute = 0x35,
    kWMIEventVolumeDown = 0x36,
    kWMIEventVolumeUp = 0x37,
    kWMIEventScreenBacklightDown = 0x15,
    kWMIEventScreenBacklightUp = 0x14,
};

enum {
    kToggleWifi = 1,
    kSwitchDisplay = 2,
    kEnableTouchpad = 3,
    kDisableTouchpad = 4,
    kDecreaseKeyboardBacklight = 5,
    kIncreaseKeyboardBacklight = 6,
};

enum {
    kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100),  // set disable/enable touchpad (data is bool*)
    kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101),  // get disable/enable touchpad (data is bool*)
    kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),    // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
};



class EXPORT VoodooWMIHotkeyDriver : public IOService {
    OSDeclareDefaultStructors(VoodooWMIHotkeyDriver)

    VoodooWMIController* wmiController = nullptr;

 public:
    IOService* probe(IOService* provider, SInt32* score) override;
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    IOReturn setProperties(OSObject* properties) override;

    void onWMIEvent(WMIBlock* block, OSObject* eventData);

 private:
    void sendMessageToDaemon(int type, int arg1, int arg2);
    void dispatchCommand(uint8_t id, uint8_t arg);

    void toggleTouchpad(bool enable);
    void adjustBrightness(bool increase);

    bool sendKernelMessage(const char *vendorCode, uint32_t eventCode, int arg1, int arg2, int arg3);
};

#endif  // __VOODOOWMI_HOTKEY_DRIVER__
