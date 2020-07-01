#ifndef __TONGFANG_KEYBOARD_UTILITY__
#define __TONGFANG_KEYBOARD_UTILITY__

#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

#include "KernEventServer.hpp"

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

#define WMBC_CALL_CODE 0xD2
#define SAC1_GETTER_ARG0 1
#define SAC1_GETTER_METHOD_NAME "GETC"

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



class EXPORT TongfangKeyboardUtility : public IOService {
    OSDeclareDefaultStructors(TongfangKeyboardUtility)

 public:
    IOService* probe(IOService* provider, SInt32* score) override;
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    IOReturn setProperties(OSObject* properties) override;
    IOReturn message(UInt32 type, IOService* provider, void* argument) override;

 private:
    void sendMessageToDaemon(int type, int arg1, int arg2);
    void dispatchCommand(uint8_t id, uint8_t arg);

    void toggleTouchpad(bool enable);
    void adjustBrightness(bool increase);
};

#endif  // __TONGFANG_KEYBOARD_UTILITY__
