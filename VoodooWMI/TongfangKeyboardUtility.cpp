#include "TongfangKeyboardUtility.hpp"

typedef IOService super;
OSDefineMetaClassAndStructors(TongfangKeyboardUtility, IOService)

IOService* TongfangKeyboardUtility::probe(IOService* provider, SInt32* score) {
    IOService* result = super::probe(provider, score);

    if (!(wmiController = OSDynamicCast(VoodooWMIController, provider))) {
        return nullptr;
    }
    if (!wmiController->hasGuid(TONGFANG_WMI_EVENT_GUID)) {
        return nullptr;
    }
    DEBUG_LOG("%s::find target event guid\n", getName());

    return result;
}

bool TongfangKeyboardUtility::start(IOService* provider) {
    DEBUG_LOG("%s::start: called\n", getName());
    if (!super::start(provider)) {
        return false;
    }

    wmiController->registerWMIEvent(TONGFANG_WMI_EVENT_GUID, this, OSMemberFunctionCast(WMIEventAction, this, &TongfangKeyboardUtility::onWMIEvent));

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

void TongfangKeyboardUtility::onWMIEvent(WMIBlock* block, OSObject* eventData) {
    if (OSNumber* id = OSDynamicCast(OSNumber, eventData)) {
        DEBUG_LOG("%s::onWMIEvent %02X, %02X\n", getName(), block->notifyId, id->unsigned32BitValue());
        dispatchCommand(id->unsigned32BitValue(), 0);
    }
}

void TongfangKeyboardUtility::stop(IOService* provider) {
    DEBUG_LOG("%s::stop: called\n", getName());

    wmiController->unregisterWMIEvent(TONGFANG_WMI_EVENT_GUID);

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

void TongfangKeyboardUtility::adjustBrightness(bool increase) {
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
