#ifndef __VOODOOWMI_HOTKEY_DRIVER__
#define __VOODOOWMI_HOTKEY_DRIVER__

#include <IOKit/IOService.h>
#include <IOKit/hidevent/IOHIDEventService.h>
#include <IOKit/IOUserClient.h>
#include "VoodooWMIController.hpp"
#include "KernelMessage.h"

class VoodooWMIHotkeyDriver : public IOService {
    OSDeclareDefaultStructors(VoodooWMIHotkeyDriver)

    using super = IOService;
    bool debug = false;

    VoodooWMIController* wmiController = nullptr;
    OSArray* eventArray = nullptr;

    friend class VoodooWMIHotkeyUserClient;

 public:
    IOService* probe(IOService* provider, SInt32* score) override;
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;

    void onWMIEvent(WMIBlock* block, OSObject* eventData);

 private:
    void sendMessageToDaemon(int type, int arg1, int arg2);
    int dispatchCommand(uint8_t id);

    int8_t toggleTouchpad();
    void adjustBrightness(bool increase);

    bool sendKernelMessage(const char *vendorCode, uint32_t eventCode, int arg1, int arg2, int arg3);
};


class VoodooWMIHotkeyUserClient : public IOUserClient {
    OSDeclareDefaultStructors(VoodooWMIHotkeyUserClient);

 public:
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments,
                            IOExternalMethodDispatch* dispatch = 0, OSObject* target = 0, void* reference = 0) override;

    IOReturn clientClose() override;
};

#endif  // __VOODOOWMI_HOTKEY_DRIVER__
