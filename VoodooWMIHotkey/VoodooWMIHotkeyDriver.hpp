#ifndef __VOODOOWMI_HOTKEY_DRIVER__
#define __VOODOOWMI_HOTKEY_DRIVER__

#include <IOKit/IOService.h>
#include <IOKit/hidevent/IOHIDEventService.h>
#include "VoodooWMIController.hpp"
#include "KernelMessage.h"

class VoodooWMIHotkeyDriver : public IOService {
    OSDeclareDefaultStructors(VoodooWMIHotkeyDriver)

    bool debug = false;

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
