#ifndef VoodooWMIController_hpp
#define VoodooWMIController_hpp

#include <IOKit/IOService.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

#define DEBUG_MSG
#ifdef DEBUG_MSG
#define DEBUG_LOG(args...)  IOLog(args)
#else
#define DEBUG_LOG(args...)
#endif

/*
 * If the GUID data block is marked as expensive, we must enable and
 * explicitily disable data collection.
 */
#define ACPI_WMI_EXPENSIVE   0x1
#define ACPI_WMI_METHOD      0x2    /* GUID is a method */
#define ACPI_WMI_STRING      0x4    /* GUID takes & returns a string */
#define ACPI_WMI_EVENT       0x8    /* GUID is an event */

struct WMIBlock {
    char guid[16];
    union {
        char objectId[2];
        struct {
            UInt8 notifyId;
            UInt8 reserved;
        };
    };
    UInt8 instanceCount;
    UInt8 flags;
};

typedef void (*WMIEventAction)(OSObject* target, WMIBlock* block, OSObject* eventData);

struct WMIEventHandler {
    OSObject* target;
    WMIEventAction action;
};

class VoodooWMIController : public IOService {
    OSDeclareDefaultStructors(VoodooWMIController)

    IOACPIPlatformDevice* device = nullptr;
    WMIBlock* blockList = nullptr;
    WMIEventHandler* handlerList = nullptr;
    int blockCount = 0;

    bool loadBlocks();
    WMIBlock* findBlock(const char* guid);

    IOReturn setEventEnable(const char* guid, bool enabled);
    IOReturn setBlockEnable(const char* guid, bool enabled);

    IOReturn getEventData(UInt8 notifyId, OSObject** result);

 public:
    IOService* probe(IOService* provider, SInt32* score) override;
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    IOReturn message(UInt32 type, IOService* provider, void* argument) override;

    bool hasGuid(const char* guid);

    IOReturn registerEventHandler(const char* guid, OSObject* target, WMIEventAction handler);
    IOReturn unregisterEventHandler(const char* guid);

    IOReturn setBlock(const char* guid, UInt8 instanceIndex, OSObject* inputData);
    IOReturn queryBlock(const char* guid, UInt8 instanceIndex, OSObject** result);

    IOReturn evaluateMethod(const char* guid, UInt8 instanceIndex, UInt32 methodId, OSObject* inputData, OSObject** result);
};

#endif /* VoodooWMIController_hpp */
