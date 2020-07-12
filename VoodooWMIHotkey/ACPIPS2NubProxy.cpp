#include "ACPIPS2NubProxy.hpp"

typedef IOACPIPlatformDevice super;
OSDefineMetaClassAndStructors(ACPIPS2NubProxy, IOACPIPlatformDevice)

#define DefineReservedUnused(className, index) \
void className ::_RESERVED ## className ## index() {}

DefineReservedUnused(IOPlatformDevice, 0);
DefineReservedUnused(IOPlatformDevice, 1);
DefineReservedUnused(IOPlatformDevice, 2);
DefineReservedUnused(IOPlatformDevice, 3);

#define DEBUG_TITLE "VoodooWMIHotkey::ACPIPS2NubProxy"
#define DEBUG_LOG(args...) do { if (this->debug) IOLog(args); } while (0)

IOService* ACPIPS2NubProxy::probe(IOService* provider, SInt32* score) {
    static bool probed = false;
    if (probed) {
        return nullptr;
    }
    IOService* result = super::probe(provider, score);

    DEBUG_LOG("%s::probe: called\n", DEBUG_TITLE);
    if (!OSDynamicCast(OSBoolean, getProperty("LoadCustomKeymap"))->getValue()) {
        return nullptr;
    }
    if (result) {
        probed = true;
        *score = 99;
    }

    return result;
}

bool ACPIPS2NubProxy::start(IOService* provider) {
    DEBUG_LOG("%s::start: called\n", DEBUG_TITLE);
    if (!super::start(provider)) {
        return false;
    }

    debug = OSDynamicCast(OSBoolean, getProperty("DebugMode"))->getValue();
    setProperty("Note", "This nub is for ACPI object injection");

    OSArray* controllers = OSDynamicCast(OSArray, provider->getProperty(gIOInterruptControllersKey));
    OSArray* specifiers = OSDynamicCast(OSArray, provider->getProperty(gIOInterruptSpecifiersKey));
    if (controllers == NULL || specifiers == NULL || controllers->getCount() == 0 || specifiers->getCount() == 0) {
        return false;
    }
    setProperty(gIOInterruptControllersKey, controllers);
    setProperty(gIOInterruptSpecifiersKey, specifiers);

    if (OSData* compatible = OSDynamicCast(OSData, provider->getProperty("compatible"))) {
        setName((const char*)compatible->getBytesNoCopy());
    } else {
        return false;
    }

    registerService();

    return true;
}

bool ACPIPS2NubProxy::compareName(OSString* name, OSString** matched) const {
    return this->IORegistryEntry::compareName(name, matched);
}

IOService* ACPIPS2NubProxy::matchLocation(IOService* client) {
    return getProvider()->matchLocation(client);
}

IOReturn ACPIPS2NubProxy::getResources(void) {
    IOACPIPlatformDevice* provider = OSDynamicCast(IOACPIPlatformDevice, getProvider());
    return provider->getResources();
}

IOReturn ACPIPS2NubProxy::message(UInt32 type, IOService* provider, void* argument) {
    if (OSIterator* iterator = getClientIterator()) {
        while (IOService* service = OSDynamicCast(IOService, iterator->getNextObject())) {
            service->message(type, provider, argument);
        }
        iterator->release();
    }
    return kIOReturnSuccess;
}

IOReturn ACPIPS2NubProxy::validateObject(const OSSymbol* objectName) {
    DEBUG_LOG("%s::validateObject: called %s\n", DEBUG_TITLE, objectName->getCStringNoCopy());
    IOACPIPlatformDevice* provider = OSDynamicCast(IOACPIPlatformDevice, getProvider());
    return provider->validateObject(objectName);
}

IOReturn ACPIPS2NubProxy::validateObject(const char* objectName) {
    DEBUG_LOG("%s::validateObject: called %s\n", DEBUG_TITLE, objectName);
    IOACPIPlatformDevice* provider = OSDynamicCast(IOACPIPlatformDevice, getProvider());
    return provider->validateObject(objectName);
}

IOReturn ACPIPS2NubProxy::evaluateObject(const OSSymbol* objectName, OSObject** result, OSObject* params[], IOItemCount paramCount, IOOptionBits options) {
    DEBUG_LOG("%s::evaluateObject: called %s\n", DEBUG_TITLE, objectName->getCStringNoCopy());
    IOACPIPlatformDevice* provider = OSDynamicCast(IOACPIPlatformDevice, getProvider());
    return provider->evaluateObject(objectName, result, params, paramCount);
}

IOReturn ACPIPS2NubProxy::evaluateObject(const char* objectName, OSObject** result, OSObject* params[], IOItemCount paramCount, IOOptionBits options) {
    DEBUG_LOG("%s::evaluateObject: called %s\n", DEBUG_TITLE, objectName);
    IOACPIPlatformDevice* provider = OSDynamicCast(IOACPIPlatformDevice, getProvider());
    if (strcmp(objectName, "RMCF") != 0) {
        return provider->evaluateObject(objectName, result, params, paramCount);
    }
    *result = injectKeymap();
    return kIOReturnSuccess;
}

OSObject* ACPIPS2NubProxy::injectKeymap() {
    OSObject* result = nullptr;

    // todo: fix leaks
    IOACPIPlatformDevice* p = OSDynamicCast(IOACPIPlatformDevice, getProvider());
    OSDictionary* dict = nullptr;
    if (p->evaluateObject("RMCF", &result) == kIOReturnSuccess) {
        if (OSArray* array = OSDynamicCast(OSArray, result)) {
            if ((dict = OSDynamicCast(OSDictionary, translateArray(array)))) {
                DEBUG_LOG("%s::evaluateObject: get original RMCF\n", DEBUG_TITLE);
            }
        }
    }
    if (dict == nullptr) {
        DEBUG_LOG("%s::evaluateObject: create dict\n", DEBUG_TITLE);
        dict = OSDictionary::withCapacity(1);
    }

    /*  dict = { "Keyboard": { "Custom PS2 Map": [ ... ] } }  */
    OSDictionary* keyboardDict;
    if (!(keyboardDict = OSDynamicCast(OSDictionary, dict->getObject("Keyboard")))) {
        DEBUG_LOG("%s::evaluateObject: create keyboard dict\n", DEBUG_TITLE);
        keyboardDict = OSDictionary::withCapacity(1);
        dict->setObject("Keyboard", keyboardDict);
    }

    OSArray* keyMapArray = nullptr;
    if (!(keyMapArray = OSDynamicCast(OSArray, keyboardDict->getObject("Custom PS2 Map")))) {
        DEBUG_LOG("%s::evaluateObject: create key map array\n", DEBUG_TITLE);
        keyMapArray = OSArray::withCapacity(1);
        keyboardDict->setObject("Custom PS2 Map", keyMapArray);
    }
    keyMapArray->merge(OSDynamicCast(OSArray, getProperty("Custom PS2 Map")));

    result = encodeObjToArray(dict);

    DEBUG_LOG("%s::evaluateObject: inject RMCF\n", DEBUG_TITLE);

    if (debug) {
        setProperty("Dict", dict);
        setProperty("Encoded-Dict", encodeObjToArray(dict));
    }

    return result;
}

OSObject* ACPIPS2NubProxy::encodeObjToArray(OSObject* obj) {
    // todo: fix leaks
    OSObject* result = obj;

    if (OSDictionary* dict = OSDynamicCast(OSDictionary, obj)) {
        DEBUG_LOG("%s:: encode dict\n", DEBUG_TITLE);
        OSArray* resultArray = OSArray::withCapacity(2);

        if (OSCollectionIterator* iterator = OSCollectionIterator::withCollection(dict)) {
            while (OSSymbol* key = OSDynamicCast(OSSymbol, iterator->getNextObject())) {
                DEBUG_LOG("%s:: encode item in dict %s\n", DEBUG_TITLE, key->getCStringNoCopy());
                resultArray->setObject(OSString::withCString(key->getCStringNoCopy()));
                resultArray->setObject(encodeObjToArray(dict->getObject(key)));
            }
            iterator->release();
        }

        result = resultArray;
    } else if (OSArray* array = OSDynamicCast(OSArray, obj)) {
        DEBUG_LOG("%s:: encode array\n", DEBUG_TITLE);
        OSArray* resultArray = OSArray::withCapacity(1);
        resultArray->setObject(OSArray::withCapacity(1));

        for (int i = 0; i < array->getCount(); i++) {
            DEBUG_LOG("%s:: encode item in array\n", DEBUG_TITLE);
            resultArray->setObject(encodeObjToArray(array->getObject(i)));
        }

        result = resultArray;
    }

    return result;
}

OSObject* ACPIPS2NubProxy::translateEntry(OSObject* obj) {
    // Note: non-NULL result is retained...

    // if object is another array, translate it
    if (OSArray* array = OSDynamicCast(OSArray, obj))
        return translateArray(array);

    // if object is a string, may be translated to boolean
    if (OSString* strObj = OSDynamicCast(OSString, obj)) {
        // object is string, translate special boolean values
        const char* cstring = strObj->getCStringNoCopy();
        if (cstring[0] == '>') {
            // boolean types true/false
            if (cstring[1] == 'y' && !cstring[2])
                return kOSBooleanTrue;
            else if (cstring[1] == 'n' && !cstring[2])
                return kOSBooleanFalse;
            // escape case ('>>n' '>>y'), replace with just string '>n' '>y'
            else if (cstring[1] == '>' && (cstring[2] == 'y' || cstring[2] == 'n') && !cstring[3])
                return OSString::withCString(&cstring[1]);
        }
    }
    return NULL;  // no translation
}

OSObject* ACPIPS2NubProxy::translateArray(OSArray* array) {
    // may return either OSArray* or OSDictionary*

    int count = array->getCount();
    if (!count)
        return NULL;

    OSObject* result = array;

    // if first entry is an empty array, process as array, else dictionary
    OSArray* test = OSDynamicCast(OSArray, array->getObject(0));
    if (test && test->getCount() == 0) {
        // using same array, but translating it...
        array->retain();

        // remove bogus first entry
        array->removeObject(0);
        --count;

        // translate entries in the array
        for (int i = 0; i < count; ++i) {
            if (OSObject* obj = translateEntry(array->getObject(i))) {
                array->replaceObject(i, obj);
                obj->release();
            }
        }
    } else {
        // array is key/value pairs, so must be even
        if (count & 1)
            return NULL;

        // dictionary constructed to accomodate all pairs
        int size = count >> 1;
        if (!size)
            size = 1;
        OSDictionary* dict = OSDictionary::withCapacity(size);
        if (!dict)
            return NULL;

        // go through each entry two at a time, building the dictionary
        for (int i = 0; i < count; i += 2) {
            OSString* key = OSDynamicCast(OSString, array->getObject(i));
            if (!key) {
                dict->release();
                return NULL;
            }
            // get value, use translated value if translated
            OSObject* obj = array->getObject(i + 1);
            OSObject* trans = translateEntry(obj);
            if (trans)
                obj = trans;
            dict->setObject(key, obj);
            OSSafeReleaseNULL(trans);
        }
        result = dict;
    }

    // Note: result is retained when returned...
    return result;
}
