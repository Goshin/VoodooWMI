#ifndef ACPIPS2NubProxy_hpp
#define ACPIPS2NubProxy_hpp

#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

/* An ACPI device proxy to inject a key map for VoodooPS2 keyboard driver */
class ACPIPS2NubProxy : public IOACPIPlatformDevice {
    OSDeclareDefaultStructors(ACPIPS2NubProxy)

    bool debug = false;

 public:
    IOService* probe(IOService* provider, SInt32* score) override;
    bool start(IOService* provider) override;

    bool compareName(OSString* name, OSString** matched = NULL) const override;
    IOService* matchLocation(IOService* client) override;
    IOReturn getResources(void) override;

    IOReturn message(UInt32 type, IOService* provider, void* argument) override;

    IOReturn validateObject(const OSSymbol* objectName) override;
    IOReturn validateObject(const char* objectName) override;

    IOReturn evaluateObject(const OSSymbol* objectName, OSObject** result = 0, OSObject* params[] = 0,
        IOItemCount paramCount = 0, IOOptionBits options = 0) override;

    IOReturn evaluateObject(const char* objectName, OSObject** result = 0, OSObject* params[] = 0,
        IOItemCount paramCount = 0, IOOptionBits options = 0) override;

    OSObject* injectKeymap();

    OSObject* translateArray(OSArray* array);  /* copy from VoodooPS2Controller */
    OSObject* translateEntry(OSObject* obj);  /* copy from VoodooPS2Controller */

    OSObject* encodeObjToArray(OSObject* obj);
};

#endif /* ACPIPS2NubProxy_hpp */
