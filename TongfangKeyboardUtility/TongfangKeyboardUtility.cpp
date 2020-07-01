#include "TongfangKeyboardUtility.hpp"

typedef IOService super;
OSDefineMetaClassAndStructors(TongfangKeyboardUtility, IOService)

IOService* TongfangKeyboardUtility::probe(IOService* provider, SInt32* score) {
    IOService* result = super::probe(provider, score);

    IOACPIPlatformDevice* device = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!device || device->validateObject(SAC1_GETTER_METHOD_NAME) != kIOReturnSuccess) {
        return nullptr;
    }

    return result;
}

bool TongfangKeyboardUtility::start(IOService* provider) {
    DEBUG_LOG("%s::start: called\n", getName());
    if (!super::start(provider)) {
        return false;
    }

    registerService();

    return true;
}

IOReturn TongfangKeyboardUtility::setProperties(OSObject* properties) {
    DEBUG_LOG("%s get property", getName());
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

IOReturn TongfangKeyboardUtility::message(UInt32 type, IOService* provider, void* argument) {
    if (type == kIOACPIMessageDeviceNotification) {
        DEBUG_LOG("%s::message(%x, %p, %x)\n", getName(), type, provider, *reinterpret_cast<unsigned*>(argument));
    }

    UInt32 eventType = *reinterpret_cast<UInt32*>(argument);
    if (eventType != WMBC_CALL_CODE) {
        return kIOReturnSuccess;
    }

    IOACPIPlatformDevice* wmiDevice = OSDynamicCast(IOACPIPlatformDevice, getProvider());
    if (!wmiDevice) {
        return kIOReturnSuccess;
    }

    UInt32 eventCode = 0;
    OSNumber* getterArgument = OSNumber::withNumber(SAC1_GETTER_ARG0, 32);
    OSObject* argList[] = { getterArgument };
    IOReturn ret = wmiDevice->evaluateInteger(SAC1_GETTER_METHOD_NAME, &eventCode, argList, 1);
    getterArgument->release();
    if (ret != kIOReturnSuccess) {
        DEBUG_LOG("%s:: failed to invoke GETC method", getName());
        return kIOReturnSuccess;
    }
    DEBUG_LOG("%s:: get event: 0x%X", getName(), eventCode);

    dispatchCommand(eventCode, 0);

    return kIOReturnSuccess;
}

void TongfangKeyboardUtility::stop(IOService* provider) {
    DEBUG_LOG("%s::stop: called\n", getName());
    super::stop(provider);
}

void TongfangKeyboardUtility::sendMessageToDaemon(int type, int arg1, int arg2) {
    TongfangKeyboardUtilityKernEventServer eventServer;
    eventServer.setVendorID(KERNEL_EVENT_VENDOR_ID);
    eventServer.setEventCode(KERNEL_EVENT_CODE);
    eventServer.sendMessage(type, arg1, arg2);
}

void TongfangKeyboardUtility::toggleTouchpad(bool enable) {
    const OSSymbol* key = OSSymbol::withCString("RM,deliverNotifications");
    OSDictionary* serviceMatch = propertyMatching(key, kOSBooleanTrue);
    serviceMatch = serviceMatching("VoodooI2CPrecisionTouchpadHIDEventDriver", serviceMatch);
    if (IOService* touchpadDevice = waitForMatchingService(serviceMatch, 1e9)) {
        DEBUG_LOG("%s::get VoodooI2C touchpad service\n", getName());
        touchpadDevice->message(kKeyboardSetTouchStatus, this, &enable);
        touchpadDevice->release();
    } else {
        DEBUG_LOG("%s no VoodooI2C touchpad service", getName());
    }
    key->release();
    serviceMatch->release();
}

void TongfangKeyboardUtility::adjustBrightness(bool increase) {
    const OSSymbol* key = OSSymbol::withCString("RM,deliverNotifications");
    OSDictionary* serviceMatch = propertyMatching(key, kOSBooleanTrue);
    serviceMatch = serviceMatching("ApplePS2Keyboard", serviceMatch);
    if (IOService* keyboardService = waitForMatchingService(serviceMatch, 1e9)) {
        DEBUG_LOG("%s::get VoodooPS2 keyboard service\n", getName());
        unsigned keyCode = increase ? 0x0406 : 0x0405;
        keyboardService->message(kIOACPIMessageDeviceNotification, this, &keyCode);
        keyboardService->release();
    } else {
        DEBUG_LOG("%s VoodooPS2 keyboard service", getName());
    }
    key->release();
    serviceMatch->release();
}

void TongfangKeyboardUtility::dispatchCommand(uint8_t id, uint8_t arg) {
    switch (id) {
        case kWMIEventWiFiOn:
        case kWMIEventWiFiOff:
            sendMessageToDaemon(kToggleWifi, 0, 0);
            break;
        case kWMIEventAdjustKeyboardBacklight:
            sendMessageToDaemon(kIncreaseKeyboardBacklight, 0, 0);
            break;
        case kWMIEventScreenBacklightDown:
            adjustBrightness(false);
            break;
        case kWMIEventScreenBacklightUp:
            adjustBrightness(true);
            break;
        default:
            break;
    }
}
