#include "VoodooWMIHotkeyDriver.hpp"

extern "C" {
#include <sys/kern_event.h>
#include "KernelMessage.h"
}

#define DEBUG_LOG(args...) do { if (this->debug) IOLog(args); } while (0)

typedef IOService super;
OSDefineMetaClassAndStructors(VoodooWMIHotkeyDriver, IOService)

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
            return false;
        }
        OSString* guid = OSDynamicCast(OSString, dict->getObject("GUID"));
        OSNumber* notifyId = OSDynamicCast(OSNumber, dict->getObject("NotifyID"));
        OSNumber* eventId = OSDynamicCast(OSNumber, dict->getObject("EventData"));
        OSNumber* actionId = OSDynamicCast(OSNumber, dict->getObject("ActionID"));
        if (!guid || !notifyId || !eventId || !actionId) {
            IOLog("%s::failed to parse hotkey event %d", getName(), i);
            return false;
        }
        wmiController->registerWMIEvent(guid->getCStringNoCopy(), this, OSMemberFunctionCast(WMIEventAction, this, &VoodooWMIHotkeyDriver::onWMIEvent));
    }

    registerService();

    return true;
}

IOReturn VoodooWMIHotkeyDriver::setProperties(OSObject* properties) {
    if (OSDictionary* dict = OSDynamicCast(OSDictionary, properties)) {
        if (OSCollectionIterator* i = OSCollectionIterator::withCollection(dict)) {
            while (OSSymbol* key = OSDynamicCast(OSSymbol, i->getNextObject())) {
                if (key->isEqualTo("Touchpad")) {
                    if (OSBoolean* value = OSDynamicCast(OSBoolean, dict->getObject(key))) {
                        DEBUG_LOG("%s::setProperties %s = %x\n", getName(), key->getCStringNoCopy(), value->getValue());
                        toggleTouchpad(value->getValue());
                    }
                }
            }
            i->release();
        }
    }
    return kIOReturnSuccess;
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

void VoodooWMIHotkeyDriver::sendMessageToDaemon(int type, int arg1, int arg2) {
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

void VoodooWMIHotkeyDriver::toggleTouchpad(bool enable) {
    const OSSymbol* key = OSSymbol::withCString("RM,deliverNotifications");
    OSDictionary* serviceMatch = propertyMatching(key, kOSBooleanTrue);
    if (IOService* touchpadDevice = waitForMatchingService(serviceMatch, 1e9)) {
        DEBUG_LOG("%s::get touchpad service\n", getName());
        touchpadDevice->message(kKeyboardSetTouchStatus, this, &enable);
        touchpadDevice->release();
    } else {
        DEBUG_LOG("%s failed to get touchpad service", getName());
    }
    key->release();
    serviceMatch->release();
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

void VoodooWMIHotkeyDriver::dispatchCommand(uint8_t id) {
    switch (id) {
        case kActionToggleAirplaneMode:
            sendMessageToDaemon(kActionToggleAirplaneMode, 0, 0);
            break;
        case kActionKeyboardBacklightDown:
            sendMessageToDaemon(kActionKeyboardBacklightUp, 0, 0);
            break;
        case kActionKeyboardBacklightUp:
            sendMessageToDaemon(kActionKeyboardBacklightUp, 0, 0);
            break;
        case kActionScreenBrightnessDown:
            adjustBrightness(false);
            break;
        case kActionScreenBrightnessUp:
            adjustBrightness(true);
            break;
        default:
            break;
    }
}
