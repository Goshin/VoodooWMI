#include "VoodooWMIHotkeyDriver.hpp"

extern "C" {
#include <sys/kern_event.h>
#include "KernelMessage.h"
}

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

    OSString* deviceUid = OSDynamicCast(OSString, provider->getProperty("WMI-UID"));

    // Omit info.plist integrity check for the module itself.
    OSDictionary* schemes = OSDynamicCast(OSDictionary, getProperty("Schemes"));
    OSCollectionIterator* iterator = OSCollectionIterator::withCollection(schemes);
    while (OSSymbol* key = OSDynamicCast(OSSymbol, iterator->getNextObject())) {
        OSDictionary* scheme = OSDynamicCast(OSDictionary, schemes->getObject(key));
        if (deviceUid && deviceUid->isEqualTo(OSDynamicCast(OSString, scheme->getObject("WMI-UID"))) &&
            wmiController->hasGuid(OSDynamicCast(OSString, scheme->getObject("GUIDMatch"))->getCStringNoCopy())) {
            IOLog("%s::find matched hotkey scheme: %s\n", getName(), key->getCStringNoCopy());
            eventArray = OSDynamicCast(OSArray, scheme->getObject("WMIEvents"));
            return result;
        }
    }

    return nullptr;
}

bool VoodooWMIHotkeyDriver::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }

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
    DEBUG_LOG("%s::onWMIEvent (%02X, %02X)\n", getName(), block->notifyId, obtainedEventData);

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
    if (IOService* keyboardDevice = OSDynamicCast(IOService, IORegistryEntry::fromPath("IOService:/AppleACPIPlatformExpert/PS2K"))) {
        if (IOService* keyboardDriver = keyboardDevice->getClient()) {
            DEBUG_LOG("%s::get keyboard device\n", getName());
            unsigned keyCode = increase ? 0x0406 : 0x0405;
            keyboardDriver->message(kIOACPIMessageDeviceNotification, this, &keyCode);
        }
        keyboardDevice->release();
    } else {
        DEBUG_LOG("%s failed to get keyboard device", getName());
    }
}

void VoodooWMIHotkeyDriver::dispatchCommand(uint8_t id) {
    switch (id) {
        case kActionToggleAirplaneMode:
            sendMessageToDaemon(kToggleWifi, 0, 0);
            break;
        case kActionKeyboardBacklightDown:
        case kActionKeyboardBacklightUp:
            sendMessageToDaemon(kIncreaseKeyboardBacklight, 0, 0);
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
