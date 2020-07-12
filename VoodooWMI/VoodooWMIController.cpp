#include "VoodooWMIController.hpp"

#define DEBUG_LOG(args...) do { if (this->debug) IOLog(args); } while (0)

typedef IOService super;
OSDefineMetaClassAndStructors(VoodooWMIController, IOService)

/*
 * Convert a raw GUID to the ACII string representation
 */
static int wmi_gtoa(const char* in, char* out) {
    int i;

    for (i = 3; i >= 0; i--)
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);

    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[5] & 0xFF);
    out += snprintf(out, 3, "%02X", in[4] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[7] & 0xFF);
    out += snprintf(out, 3, "%02X", in[6] & 0xFF);
    out += snprintf(out, 2, "-");
    out += snprintf(out, 3, "%02X", in[8] & 0xFF);
    out += snprintf(out, 3, "%02X", in[9] & 0xFF);
    out += snprintf(out, 2, "-");

    for (i = 10; i <= 15; i++)
        out += snprintf(out, 3, "%02X", in[i] & 0xFF);

    return 0;
}

IOService* VoodooWMIController::probe(IOService* provider, SInt32* score) {
    IOService* result = super::probe(provider, score);

    device = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!device || device->validateObject("_WDG") != kIOReturnSuccess) {
        return nullptr;
    }

    return result;
}

bool VoodooWMIController::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }

    debug = OSDynamicCast(OSBoolean, getProperty("DebugMode"))->getValue();

    if (!loadBlocks()) {
        return false;
    }

    registerService();

    return true;
}

void VoodooWMIController::stop(IOService* provider) {
    IOFree(blockList, blockCount * sizeof(WMIBlock));
    IOFree(handlerList, blockCount * sizeof(WMIEventHandler));

    super::stop(provider);
}

IOReturn VoodooWMIController::message(UInt32 type, IOService* provider, void* argument) {
    if (type != kIOACPIMessageDeviceNotification || !argument) {
        return kIOReturnSuccess;
    }
    DEBUG_LOG("%s::message(%s, 0x%x)\n", getName(), provider->getName(), *reinterpret_cast<unsigned*>(argument));

    UInt8 notifyId = *reinterpret_cast<unsigned*>(argument);
    OSObject* eventData = nullptr;
    if (getEventData(notifyId, &eventData) != kIOReturnSuccess) {
        DEBUG_LOG("%s failed to get event data", getName());
    } else if (OSNumber* eventID = OSDynamicCast(OSNumber, eventData)) {
        DEBUG_LOG("%s event: (NotifyID 0x%02x, EventData 0x%x)", getName(), notifyId, eventID->unsigned32BitValue());
    }

    WMIBlock* targetBlock = nullptr;
    for (int i = 0; i < blockCount; i++) {
        WMIBlock* block = &blockList[i];
        if (block->notifyId == notifyId) {
            targetBlock = block;
        }
    }
    if (targetBlock == nullptr) {
        DEBUG_LOG("%s::unknown event, no matched block found", getName());
        return kIOReturnSuccess;
    }

    WMIEventHandler* handler = &handlerList[targetBlock - blockList];
    if (handler->action == nullptr) {
        DEBUG_LOG("%s::unknown event, not registered", getName());
        return kIOReturnSuccess;
    }
    handler->action(handler->target, targetBlock, eventData);

    return kIOReturnSuccess;
}

bool VoodooWMIController::loadBlocks() {
    OSObject* result = nullptr;
    if (device->evaluateObject("_WDG", &result) != kIOReturnSuccess) {
        return false;
    }

    OSData* blocksData = OSDynamicCast(OSData, result);
    if (blocksData == nullptr) {
        return false;
    }
    int dataLength = blocksData->getLength();
    DEBUG_LOG("%s::block size %d", getName(), dataLength);
    if (dataLength % sizeof(WMIBlock) != 0) {
        return false;
    }
    blockCount = dataLength / sizeof(WMIBlock);
    if (!blockCount) {
        return false;
    }

    if (!(blockList = (WMIBlock*) IOMalloc(blockCount * sizeof(WMIBlock))) ||
        !(handlerList = (WMIEventHandler*) IOMallocZero(blockCount * sizeof(WMIEventHandler)))) {
        return false;
    }
    memcpy(blockList, blocksData->getBytesNoCopy(), dataLength);

    if (debug) {
        // log blocks in property
        setProperty("Raw WDG", result);
        OSArray* array = OSArray::withCapacity(blockCount);
        for (int i = 0; i < blockCount; i++) {
            WMIBlock* block = &blockList[i];
            OSDictionary* dict = OSDictionary::withCapacity(6);

            char guid[37];
            wmi_gtoa(block->guid, guid);
            dict->setObject("GUID", OSString::withCString(guid));

            char objID[3] = {0};
            memcpy(objID, block->objectId, 2);
            dict->setObject("ObjectID", OSString::withCString(objID));

            dict->setObject("NotifyID", OSNumber::withNumber(block->notifyId, 8));
            dict->setObject("Reserved", OSNumber::withNumber(block->reserved, 8));
            dict->setObject("Instance", OSNumber::withNumber(block->instanceCount, 8));
            dict->setObject("Flags", OSNumber::withNumber(block->flags, 8));

            array->setObject(dict);
            dict->release();
        }
        setProperty("WMI-Blocks", array);
        array->release();

        // enable all WMI events for debugging
        for (int i = 0; i < blockCount; i++) {
            WMIBlock* block = &blockList[i];
            if (block->flags & ACPI_WMI_EVENT) {
                char guid[37];
                wmi_gtoa(block->guid, guid);
                setEventEnable(guid, true);
                DEBUG_LOG("%s::debug: enable event %s", getName(), guid);
            }
        }
    }

    return true;
}

WMIBlock* VoodooWMIController::findBlock(const char* guid) {
    for (int i = 0; i < blockCount; i++) {
        WMIBlock* block = &blockList[i];
        char matchingGuid[37];
        wmi_gtoa(block->guid, matchingGuid);
        if (strcmp(guid, matchingGuid) == 0) {
            return block;
        }
    }
    DEBUG_LOG("%s::block not found %s", getName(), guid);
    return nullptr;
}

IOReturn VoodooWMIController::setEventEnable(const char* guid, bool enabled) {
    WMIBlock* block = nullptr;
    if (!(block = findBlock(guid))) {
        return kIOReturnNotFound;
    }
    if (!(block->flags & ACPI_WMI_EVENT)) {
        return kIOReturnInvalid;
    }

    char methodName[5] = {0};
    snprintf(methodName, 5, "WE%02X", block->notifyId);

    OSNumber* argument = OSNumber::withNumber(enabled ? 1 : 0, 8);
    OSObject* argumentList[] = { argument };
    return device->evaluateObject(methodName, nullptr, argumentList, 1);
}

IOReturn VoodooWMIController::setBlockEnable(const char* guid, bool enabled) {
    WMIBlock* block = nullptr;
    if (!(block = findBlock(guid))) {
        return kIOReturnNotFound;
    }
    if (block->flags & (ACPI_WMI_EVENT | ACPI_WMI_METHOD)) {
        return kIOReturnInvalid;
    }

    char methodName[5] = {0};
    strncpy(methodName, "WC", 2);
    strncat(methodName, block->objectId, 2);

    OSNumber* argument = OSNumber::withNumber(enabled ? 1 : 0, 8);
    OSObject* argumentList[] = { argument };
    return device->evaluateObject(methodName, nullptr, argumentList, 1);
}

bool VoodooWMIController::hasGuid(const char* guid) {
    return (findBlock(guid) != nullptr);
}

IOReturn VoodooWMIController::getEventData(UInt8 notifyId, OSObject** result) {
    char methodName[5] = "_WED";

    OSNumber* argument = OSNumber::withNumber(notifyId, 8);
    OSObject* argumentList[] = { argument };
    return device->evaluateObject(methodName, result, argumentList, 1);
}

IOReturn VoodooWMIController::registerWMIEvent(const char* guid, OSObject* target, WMIEventAction handler) {
    WMIBlock* block = nullptr;
    if (!(block = findBlock(guid))) {
        return kIOReturnNotFound;
    }
    if (!(block->flags & ACPI_WMI_EVENT)) {
        return kIOReturnInvalid;
    }

    handlerList[block - blockList].target = target;
    handlerList[block - blockList].action = handler;

    return setEventEnable(guid, true);
}

IOReturn VoodooWMIController::unregisterWMIEvent(const char* guid) {
    WMIBlock* block = nullptr;
    if (!(block = findBlock(guid))) {
        return kIOReturnNotFound;
    }
    if (!(block->flags & ACPI_WMI_EVENT)) {
        return kIOReturnInvalid;
    }

    handlerList[block - blockList].target = nullptr;
    handlerList[block - blockList].action = nullptr;

    return setEventEnable(guid, false);
}

IOReturn VoodooWMIController::setBlock(const char* guid, UInt8 instanceIndex, OSObject* inputData) {
    WMIBlock* block = nullptr;
    if (!(block = findBlock(guid))) {
        return kIOReturnNotFound;
    }
    if (!(block->flags & (ACPI_WMI_STRING | ACPI_WMI_EXPENSIVE))) {
        return kIOReturnInvalid;
    }

    char methodName[5] = {0};
    strncpy(methodName, "WS", 2);
    strncat(methodName, block->objectId, 2);

    OSObject* argumentList[] = {
        OSNumber::withNumber(instanceIndex, 8),
        inputData
    };
    return device->evaluateObject(methodName, nullptr, argumentList, 2);
}

IOReturn VoodooWMIController::queryBlock(const char* guid, UInt8 instanceIndex, OSObject** result) {
    WMIBlock* block = nullptr;
    if (!(block = findBlock(guid))) {
        return kIOReturnNotFound;
    }
    if (!(block->flags & (ACPI_WMI_STRING | ACPI_WMI_EXPENSIVE))) {
        return kIOReturnInvalid;
    }

    char methodName[5] = {0};
    strncpy(methodName, "WQ", 2);
    strncat(methodName, block->objectId, 2);

    OSObject* argumentList[] = { OSNumber::withNumber(instanceIndex, 8) };

    if (block->flags & ACPI_WMI_EXPENSIVE) {
        setBlockEnable(guid, true);
    }
    IOReturn ret = device->evaluateObject(methodName, result, argumentList, 1);
    if (block->flags & ACPI_WMI_EXPENSIVE) {
        setBlockEnable(guid, false);
    }

    return ret;
}

IOReturn VoodooWMIController::evaluateMethod(const char* guid, UInt8 instanceIndex, UInt32 methodId, OSObject* inputData, OSObject** result) {
    WMIBlock* block = nullptr;
    if (!(block = findBlock(guid))) {
        return kIOReturnNotFound;
    }
    if (!(block->flags & ACPI_WMI_METHOD)) {
        return kIOReturnInvalid;
    }

    char methodName[5] = {0};
    strncpy(methodName, "WM", 2);
    strncat(methodName, block->objectId, 2);

    OSObject* argumentList[] = {
        OSNumber::withNumber(instanceIndex, 8),
        OSNumber::withNumber(methodId, 32),
        inputData
    };
    return device->evaluateObject(methodName, result, argumentList, 3);
}
