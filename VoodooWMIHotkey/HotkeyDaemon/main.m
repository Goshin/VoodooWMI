//
//  main.m
//  mainly from AsusSMCDaemon (MIT License)
//
//

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <CoreWLAN/CoreWLAN.h>
#import <CoreServices/CoreServices.h>
#import <sys/ioctl.h>
#import <sys/socket.h>
#import <dlfcn.h>
#import <sys/kern_event.h>
#import "BezelServices.h"
#import "OSD.h"
#import "KernelMessage.h"


extern void RunApplicationEventLoop(void);

// requires IOBluetooth.framework
extern void IOBluetoothPreferenceSetControllerPowerState(int);
extern int IOBluetoothPreferenceGetControllerPowerState(void);

void dispatchMessage(struct VoodooWMIHotkeyMessage *message);

static void *(*_BSDoGraphicWithMeterAndTimeout)(CGDirectDisplayID arg0, BSGraphic arg1, int arg2, float v, int timeout) = NULL;


bool _loadBezelServices() {
    // Load BezelServices framework
    void *handle = dlopen("/System/Library/PrivateFrameworks/BezelServices.framework/Versions/A/BezelServices", RTLD_GLOBAL);
    if (!handle) {
        NSLog(@"Error opening framework");
        return NO;
    } else {
        _BSDoGraphicWithMeterAndTimeout = dlsym(handle, "BSDoGraphicWithMeterAndTimeout");
        return _BSDoGraphicWithMeterAndTimeout != NULL;
    }
}

bool _loadOSDFramework() {
    return [[NSBundle bundleWithPath:@"/System/Library/PrivateFrameworks/OSD.framework"] load];
}

void showBezelServices(BSGraphic image, float filled) {
    CGDirectDisplayID currentDisplayId = [NSScreen.mainScreen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    _BSDoGraphicWithMeterAndTimeout(currentDisplayId, image, 0x0, filled, 1);
}

void showOSD(OSDGraphic image, int filled, int total) {
    CGDirectDisplayID currentDisplayId = [NSScreen.mainScreen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    [[NSClassFromString(@"OSDManager") sharedManager] showImage:image onDisplayID:currentDisplayId priority:OSDPriorityDefault msecUntilFade:1000];
}

void showKBoardBLightStatus(int level, int max) {
    if (_BSDoGraphicWithMeterAndTimeout != NULL) {
        // El Capitan and probably older systems
        if (level)
            showBezelServices(BSGraphicKeyboardBacklightMeter, (float)level/max);
        else
            showBezelServices(BSGraphicKeyboardBacklightDisabledMeter, 0);
    } else {
        // Sierra+
        if (level)
            showOSD(OSDGraphicKeyboardBacklightMeter, level, max);
        else
            showOSD(OSDGraphicKeyboardBacklightDisabledMeter, level, max);
    }
}

BOOL airplaneModeEnabled = NO, lastWifiState;
int lastBluetoothState;
void toggleAirplaneMode() {
    airplaneModeEnabled = !airplaneModeEnabled;

    CWInterface *currentInterface = [CWWiFiClient.sharedWiFiClient interface];
    NSError *err = nil;

    if (airplaneModeEnabled) {
        showOSD(OSDGraphicNoWiFi, 0, 0);
        lastWifiState = currentInterface.powerOn;
        lastBluetoothState = IOBluetoothPreferenceGetControllerPowerState();
        [currentInterface setPower:NO error:&err];
        IOBluetoothPreferenceSetControllerPowerState(0);
    } else {
        showOSD(OSDGraphicHotspot, 0, 0);
        [currentInterface setPower:lastWifiState error:&err];
        IOBluetoothPreferenceSetControllerPowerState(lastBluetoothState);
    }
}

void switchDisplayMode() {
    // open last Privacy subpane viewed:
    NSURL * url = [NSURL fileURLWithPath:@"/System/Library/PreferencePanes/Displays.prefPane"];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

void lockScreen() {
    void *lib = dlopen("/System/Library/PrivateFrameworks/login.framework/Versions/Current/login", RTLD_LAZY);
    void (*SACLockScreenImmediate)(void) = dlsym(lib, "SACLockScreenImmediate");

    SACLockScreenImmediate();
}

int sendMessageToDriver(struct VoodooWMIHotkeyMessage message) {
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("VoodooWMIHotkeyDriver"));
    if (service == IO_OBJECT_NULL) {
        printf("VoodooWMIHotkeyDaemon:: could not find any services matching\n");
        return -1;
    }
    io_connect_t connection;
    int output = -1;
    if (IOServiceOpen(service, mach_task_self(), 0, &connection) == KERN_SUCCESS) {
        size_t outputSize = sizeof(int);
        IOConnectCallStructMethod(connection, kClientSelectorDispatchCommand, &message, sizeof(struct VoodooWMIHotkeyMessage), &output, &outputSize);

        IOServiceClose(connection);
    }
    IOObjectRelease(service);
    return output;
}

void toggleTouchpad() {
    struct VoodooWMIHotkeyMessage message = {.type = kActionToggleTouchpad};
    int result = sendMessageToDriver(message);
    if (result != -1) {
        if (result) {
            showOSD(OSDGraphicKeyboardBacklightDisabledMeter, 0, 0);
        } else {
            showOSD(OSDGraphicKeyboardBacklightDisabledNotConnected, 0, 0);
        }
    }
}

OSStatus onHotKeyEvent(EventHandlerCallRef nextHandler, EventRef theEvent, void *userData) {
    EventHotKeyID eventId;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(eventId), NULL, &eventId);

    union {
        unsigned int integer;
        unsigned char byte[4];
    } signature;
    signature.integer = eventId.signature;
    printf("VoodooWMIHotkeyDaemon:: onHotKeyEvent %c%c%c%c\n", signature.byte[3], signature.byte[2], signature.byte[1], signature.byte[0]);
    struct VoodooWMIHotkeyMessage message = {.type = eventId.id};
    dispatchMessage(&message);

    return noErr;
}

void registerHotKeys() {
    EventHotKeyRef eventHotKeyRef;
    EventHotKeyID eventHotKeyID;
    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;

    InstallApplicationEventHandler(&onHotKeyEvent, 1, &eventType, NULL, NULL);

    eventHotKeyID.signature = 'FnF3';
    eventHotKeyID.id = kActionSwitchScreen;
    // Opt + P
    RegisterEventHotKey(0x23, optionKey, eventHotKeyID, GetApplicationEventTarget(), 0, &eventHotKeyRef);

    eventHotKeyID.signature = 'FnF5';
    eventHotKeyID.id = kActionToggleTouchpad;
    // Ctrl + Opt + F13
    RegisterEventHotKey(0x69, controlKey | optionKey, eventHotKeyID, GetApplicationEventTarget(), 0, &eventHotKeyRef);
    // Ctrl + Cmd + F13
    RegisterEventHotKey(0x69, controlKey | cmdKey, eventHotKeyID, GetApplicationEventTarget(), 0, &eventHotKeyRef);
    // Opt + Cmd + F13
    RegisterEventHotKey(0x69, optionKey | cmdKey, eventHotKeyID, GetApplicationEventTarget(), 0, &eventHotKeyRef);

    printf("VoodooWMIHotkeyDaemon:: Register HotKeys\n");
}

void dispatchMessage(struct VoodooWMIHotkeyMessage *message) {
    printf("VoodooWMIHotkeyDaemon:: type:%d x:%d y:%d\n", message->type, message->arg1, message->arg2);

    switch (message->type) {
        case kActionSleep:
        case kActionScreenBrightnessDown:
        case kActionScreenBrightnessUp:
            sendMessageToDriver(*message);
            break;
        case kActionLockScreen:
            lockScreen();
            break;
        case kActionToggleAirplaneMode:
            toggleAirplaneMode();
            break;
        case kActionSwitchScreen:
            switchDisplayMode();
            break;
        case kActionToggleTouchpad:
            toggleTouchpad();
            break;
        case kActionKeyboardBacklightDown:
            showOSD(OSDGraphicKeyboardBacklightMeter, 0, 0);
            break;
        case kActionKeyboardBacklightUp:
            showOSD(OSDGraphicKeyboardBacklightMeter, 0, 0);
            break;
        default:
            printf("VoodooWMIHotkeyDaemon:: unknown type %d\n", message->type);
    }
}

void kernelMessageLoop() {
    int systemSocket = -1;

    // create system socket to receive kernel event data
    systemSocket = socket(PF_SYSTEM, SOCK_RAW, SYSPROTO_EVENT);

    // struct for vendor code
    struct kev_vendor_code vendorCode = {0};
    strncpy(vendorCode.vendor_string, KERNEL_EVENT_VENDOR_ID, KEV_VENDOR_CODE_MAX_STR_LEN);

    // get vendor name -> vendor code mapping
    // ->vendor id, saved in 'vendorCode' variable
    ioctl(systemSocket, SIOCGKEVVENDOR, &vendorCode);

    // struct for kernel request
    struct kev_request kevRequest = {0};
    kevRequest.kev_class = KEV_ANY_CLASS;
    kevRequest.kev_subclass = KEV_ANY_SUBCLASS;

    // tell kernel what we want to filter on
    ioctl(systemSocket, SIOCSKEVFILT, &kevRequest);

    // bytes received from system socket
    ssize_t bytesReceived = -1;

    // message from kext
    // ->size is cumulation of header, struct, and max length of a proc path
    char kextMsg[KEV_MSG_HEADER_SIZE + sizeof(struct VoodooWMIHotkeyMessage)] = {0};

    struct VoodooWMIHotkeyMessage *message = NULL;

    while (YES) {
        bytesReceived = recv(systemSocket, kextMsg, sizeof(kextMsg), 0);

        if (bytesReceived != sizeof(kextMsg)) {
            continue;
        }

        // struct for broadcast data from the kext
        struct kern_event_msg *kernEventMsg = {0};
        kernEventMsg = (struct kern_event_msg*)kextMsg;

        if (KERNEL_EVENT_CODE != kernEventMsg->event_code) {
            continue;
        }

        message = (struct VoodooWMIHotkeyMessage*)&kernEventMsg->event_data[0];

        dispatchMessage(message);
    }
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        printf("VoodooWMIHotkey:: daemon started...\n");

        if (!_loadBezelServices()) {
            _loadOSDFramework();
        }

        registerHotKeys();

        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
            kernelMessageLoop();
        });

        RunApplicationEventLoop();
    }

    return 0;
}
