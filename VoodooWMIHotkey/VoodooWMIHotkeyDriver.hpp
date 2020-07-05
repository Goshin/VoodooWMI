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

class EXPORT VoodooWMIHotkeyDriver : public IOService {
    OSDeclareDefaultStructors(VoodooWMIHotkeyDriver)

    VoodooWMIController* wmiController = nullptr;
    OSArray* eventArray = nullptr;

 public:
    IOService* probe(IOService* provider, SInt32* score) override;
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    IOReturn setProperties(OSObject* properties) override;

    void onWMIEvent(WMIBlock* block, OSObject* eventData);

 private:
    void sendMessageToDaemon(int type, int arg1, int arg2);
    void dispatchCommand(uint8_t id);

    void toggleTouchpad(bool enable);
    void adjustBrightness(bool increase);

    bool sendKernelMessage(const char *vendorCode, uint32_t eventCode, int arg1, int arg2, int arg3);
};

#endif  // __VOODOOWMI_HOTKEY_DRIVER__
