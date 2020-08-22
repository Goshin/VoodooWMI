#include "VoodooWMIHotkeyDriver.hpp"
#include <IOKit/pwr_mgt/RootDomain.h>

extern "C" {
#include <sys/kern_event.h>
#include "KernelMessage.h"
}

#define DEBUG_LOG(args...) do { if (this->debug) IOLog(args); } while (0)

OSDefineMetaClassAndStructors(VoodooWMIHotkeyDriver, IOService)
OSDefineMetaClassAndStructors(VoodooWMIHotkeyUserClient, IOUserClient)

enum {
    kKeyboardSetTouchStatus = iokit_vendor_specific_msg(100),  // set disable/enable touchpad (data is bool*)
    kKeyboardGetTouchStatus = iokit_vendor_specific_msg(101),  // get disable/enable touchpad (data is bool*)
    kKeyboardKeyPressTime = iokit_vendor_specific_msg(110),    // notify of timestamp a non-modifier key was pressed (data is uint64_t*)
};

IOService* VoodooWMIHotkeyDriver::probe(IOService* provider, SInt32* score) {
    IOService* result = super::probe(provider, score);

    // Type casting must succeed, guaranteed by IOKit, IOProviderClass.
    wmiController = OSDynamicCast(VoodooWMIController, provider);

    // Omit info.plist integrity check for the module itself.
    OSDictionary* platforms = OSDynamicCast(OSDictionary, getProperty("Platforms"));
    OSCollectionIterator* iterator = OSCollectionIterator::withCollection(platforms);
    while (OSSymbol* key = OSDynamicCast(OSSymbol, iterator->getNextObject())) {
        OSDictionary* platform = OSDynamicCast(OSDictionary, platforms->getObject(key));
        if (wmiController->hasGuid(OSDynamicCast(OSString, platform->getObject("GUIDMatch"))->getCStringNoCopy())) {
            IOLog("%s::find matched hotkey scheme: %s\n", getName(), key->getCStringNoCopy());
            eventArray = OSDynamicCast(OSArray, platform->getObject("WMIEvents"));
            setProperty(key, platform);
            removeProperty("Platforms");
            return result;
        }
    }

    return nullptr;
}

bool VoodooWMIHotkeyDriver::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }

    debug = OSDynamicCast(OSBoolean, getProperty("DebugMode"))->getValue();

    // Validate event table
    for (int i = 0; i < eventArray->getCount(); i++) {
        OSDictionary* dict = OSDynamicCast(OSDictionary, eventArray->getObject(i));
        if (!dict) {
            IOLog("%s::failed to parse hotkey event %d", getName(), i);
            continue;
        }
        OSString* guid = OSDynamicCast(OSString, dict->getObject("GUID"));
        OSNumber* notifyId = OSDynamicCast(OSNumber, dict->getObject("NotifyID"));
        OSNumber* eventId = OSDynamicCast(OSNumber, dict->getObject("EventData"));
        OSNumber* actionId = OSDynamicCast(OSNumber, dict->getObject("ActionID"));
        if (!guid || !notifyId || !eventId || !actionId) {
            IOLog("%s::failed to parse hotkey event %d", getName(), i);
            continue;
        }
        wmiController->registerWMIEvent(guid->getCStringNoCopy(), this, OSMemberFunctionCast(WMIEventAction, this, &VoodooWMIHotkeyDriver::onWMIEvent));
    }

    registerService();

    return true;
}

void VoodooWMIHotkeyDriver::onWMIEvent(WMIBlock* block, OSObject* eventData) {
    int obtainedEventData = 0;
    if (OSNumber* numberObj = OSDynamicCast(OSNumber, eventData)) {
        obtainedEventData = numberObj->unsigned32BitValue();
    }
    DEBUG_LOG("%s::onWMIEvent (0X%02X, 0X%02X)\n", getName(), block->notifyId, obtainedEventData);

    for (int i = 0; i < eventArray->getCount(); i++) {
        OSDictionary* dict = OSDynamicCast(OSDictionary, eventArray->getObject(i));
        UInt8 notifyId = OSDynamicCast(OSNumber, dict->getObject("NotifyID"))->unsigned8BitValue();
        int eventData = OSDynamicCast(OSNumber, dict->getObject("EventData"))->unsigned32BitValue();
        UInt8 actionId = OSDynamicCast(OSNumber, dict->getObject("ActionID"))->unsigned8BitValue();
        if (block->notifyId == notifyId && obtainedEventData == eventData) {
            dispatchCommand(actionId);
        }
    }
}

void VoodooWMIHotkeyDriver::stop(IOService* provider) {
    for (int i = 0; i < eventArray->getCount(); i++) {
        OSDictionary* dict = OSDynamicCast(OSDictionary, eventArray->getObject(i));
        OSString* guid = OSDynamicCast(OSString, dict->getObject("GUID"));
        wmiController->unregisterWMIEvent(guid->getCStringNoCopy());
    }

    super::stop(provider);
}

void VoodooWMIHotkeyDriver::sendMessageToDaemon(int type, int arg1 = 0, int arg2 = 0) {
    struct kev_msg kernelEventMsg = {0};

    uint32_t vendorID = 0;
    if (KERN_SUCCESS != kev_vendor_code_find(KERNEL_EVENT_VENDOR_ID, &vendorID)) {
        return;
    }
    kernelEventMsg.vendor_code = vendorID;
    kernelEventMsg.event_code = KERNEL_EVENT_CODE;
    kernelEventMsg.kev_class = KEV_ANY_CLASS;
    kernelEventMsg.kev_subclass = KEV_ANY_SUBCLASS;

    kernelEventMsg.dv[0].data_length = sizeof(int);
    kernelEventMsg.dv[0].data_ptr = &type;
    kernelEventMsg.dv[1].data_length = sizeof(int);
    kernelEventMsg.dv[1].data_ptr = &arg1;
    kernelEventMsg.dv[2].data_length = sizeof(int);
    kernelEventMsg.dv[2].data_ptr = &arg2;

    kev_msg_post(&kernelEventMsg);
}

int8_t VoodooWMIHotkeyDriver::toggleTouchpad() {
    int8_t isEnabled = -1;
    const OSSymbol* key = OSSymbol::withCString("RM,deliverNotifications");
    OSDictionary* serviceMatch = propertyMatching(key, kOSBooleanTrue);
    if (OSIterator* iterator = getMatchingServices(serviceMatch)) {
        while (IOService* candidateService = OSDynamicCast(IOService, iterator->getNextObject())) {
            candidateService->message(kKeyboardGetTouchStatus, this, &isEnabled);
            if (isEnabled != -1) {
                DEBUG_LOG("%s::get touchpad service: %s\n", getName(), candidateService->getMetaClass()->getClassName());
                isEnabled = !isEnabled;
                candidateService->message(kKeyboardSetTouchStatus, this, &isEnabled);
                break;
            }
        }
        iterator->release();
    } else {
        DEBUG_LOG("%s failed to get touchpad service", getName());
    }
    key->release();
    serviceMatch->release();
    return isEnabled;
}

void VoodooWMIHotkeyDriver::adjustBrightness(bool increase) {
    OSDictionary* serviceMatch = serviceMatching("IOHIDEventService");
    if (IOService* hidEventService = waitForMatchingService(serviceMatch, 1e9)) {
        DEBUG_LOG("%s::get HID event service\n", getName());

        const unsigned int DISPATCH_KEY_EVENT_METHOD_INDEX = 281;
        void** vtable = *(void***)hidEventService;
        typedef void(*dispatchKeyboardEventMethod)(void* self, AbsoluteTime timeStamp, UInt32 usagePage, UInt32 usage, UInt32 value, IOOptionBits options);
        dispatchKeyboardEventMethod method = (dispatchKeyboardEventMethod)vtable[DISPATCH_KEY_EVENT_METHOD_INDEX];

        AbsoluteTime timestamp;
        clock_get_uptime(&timestamp);
        UInt32 keyCode = increase ? kHIDUsage_KeyboardF15 : kHIDUsage_KeyboardF14;
        method(hidEventService, timestamp, kHIDPage_KeyboardOrKeypad, keyCode, true, 0);
        method(hidEventService, timestamp, kHIDPage_KeyboardOrKeypad, keyCode, false, 0);

        hidEventService->release();
    } else {
        DEBUG_LOG("%s failed to get HID event service", getName());
    }
    serviceMatch->release();
}

void VoodooWMIHotkeyDriver::sleep() {
    if (IOPMrootDomain* rootDomain = getPMRootDomain()) {
        rootDomain->receivePowerNotification(kIOPMSleepNow);
    }
}

int VoodooWMIHotkeyDriver::dispatchCommand(uint8_t id) {
    switch (id) {
        case kActionLockScreen:
        case kActionSwitchScreen:
        case kActionToggleAirplaneMode:
        case kActionKeyboardBacklightUp:
        case kActionKeyboardBacklightDown:
            sendMessageToDaemon(id);
            break;
        case kActionSleep:
            sleep();
            break;
        case kActionToggleTouchpad:
            return toggleTouchpad();
            break;
        case kActionScreenBrightnessDown:
            adjustBrightness(false);
            break;
        case kActionScreenBrightnessUp:
            adjustBrightness(true);
            break;
        default:
            return -1;
    }
    return 0;
}

IOReturn VoodooWMIHotkeyUserClient::externalMethod(uint32_t selector,
                                                   IOExternalMethodArguments* arguments,
                                                   IOExternalMethodDispatch* dispatch,
                                                   OSObject* target,
                                                   void* reference) {
    VoodooWMIHotkeyDriver* driver = OSDynamicCast(VoodooWMIHotkeyDriver, getProvider());
    if (!driver) {
        return kIOReturnError;
    }

    if (selector == kClientSelectorDispatchCommand) {
        const VoodooWMIHotkeyMessage* input = static_cast<const VoodooWMIHotkeyMessage*>(arguments->structureInput);
        *static_cast<int*>(arguments->structureOutput) = driver->dispatchCommand(input->type);
        return kIOReturnSuccess;
    }
    return kIOReturnNotFound;
}

IOReturn VoodooWMIHotkeyUserClient::clientClose() {
    if (!isInactive()) {
        terminate();
    }
    return kIOReturnSuccess;
}
