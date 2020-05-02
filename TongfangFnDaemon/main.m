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

/*
 *    kAERestart        will cause system to restart
 *    kAEShutDown       will cause system to shutdown
 *    kAEReallyLogout   will cause system to logout
 *    kAESleep          will cause system to sleep
 */
extern OSStatus MDSendAppleEventToSystemProcess(AEEventID eventToSend);

extern void RunApplicationEventLoop(void);

// requires IOBluetooth.framework
void IOBluetoothPreferenceSetControllerPowerState(int);
int IOBluetoothPreferenceGetControllerPowerState(void);

static void *(*_BSDoGraphicWithMeterAndTimeout)(CGDirectDisplayID arg0, BSGraphic arg1, int arg2, float v, int timeout) = NULL;

#define FnEventCode 0x8102
enum {
    kToggleWifi = 1,
    kSwitchDisplay = 2,
    kEnableTouchpad = 3,
    kDisableTouchpad = 4,
    kDecreaseKeyboardBacklight = 5,
    kIncreaseKeyboardBacklight = 6,
};

struct TongfangKeyboardUtilityMessage {
    int type;
    int arg1;
    int arg2;
};

const int kMaxDisplays = 16;
u_int32_t vendorID = 0;

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

OSStatus MDSendAppleEventToSystemProcess(AEEventID eventToSendID) {
    AEAddressDesc targetDesc;
    static const ProcessSerialNumber kPSNOfSystemProcess = {0, kSystemProcess };
    AppleEvent eventReply = {typeNull, NULL};
    AppleEvent eventToSend = {typeNull, NULL};

    OSStatus status = AECreateDesc(typeProcessSerialNumber, &kPSNOfSystemProcess, sizeof(kPSNOfSystemProcess), &targetDesc);

    if (status != noErr)
        return status;

    status = AECreateAppleEvent(kCoreEventClass, eventToSendID, &targetDesc, kAutoGenerateReturnID, kAnyTransactionID, &eventToSend);

    AEDisposeDesc(&targetDesc);

    if (status != noErr)
        return status;

    status = AESendMessage(&eventToSend, &eventReply, kAENormalPriority, kAEDefaultTimeout);

    AEDisposeDesc(&eventToSend);

    if (status != noErr)
        return status;

    AEDisposeDesc(&eventReply);
    return status;
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

void goToSleep() {
    if (_BSDoGraphicWithMeterAndTimeout != NULL) {  // El Capitan and probably older systems
        MDSendAppleEventToSystemProcess(kAESleep);
    } else {
        // Sierra+
        CGDirectDisplayID currentDisplayId = [NSScreen.mainScreen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
        [[NSClassFromString(@"OSDManager") sharedManager] showImage:OSDGraphicSleep onDisplayID:currentDisplayId priority:OSDPriorityDefault msecUntilFade:1000];
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

void setTouchpadProperty(bool isEnabled) {
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("TongfangKeyboardUtility"));
    if (service == IO_OBJECT_NULL) {
        printf("TongfangFnDaemon:: could not find any services matching\n");
        return;
    }
    kern_return_t ret = IORegistryEntrySetCFProperty(service, CFSTR("Touchpad"), isEnabled ? kCFBooleanTrue : kCFBooleanFalse);
    if (ret != KERN_SUCCESS) {
        printf("TongfangFnDaemon:: could not set property: %x\n", ret);
    }
    IOObjectRelease(service);
}

void toggleTouchpad() {
    static bool isEnabled = true;
    isEnabled = !isEnabled;
    if (isEnabled) {
        showOSD(OSDGraphicKeyboardBacklightDisabledMeter, 0, 0);
    } else {
        showOSD(OSDGraphicKeyboardBacklightDisabledNotConnected, 0, 0);
    }
    setTouchpadProperty(isEnabled);
}

OSStatus onHotKeyEvent(EventHandlerCallRef nextHandler, EventRef theEvent, void *userData) {
    EventHotKeyID eventId;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(eventId), NULL, &eventId);

    switch (eventId.id) {
        case 1:
            printf("TongfangFnDaemon:: Fn+F3\n");
            switchDisplayMode();
            break;
        case 2:
            printf("TongfangFnDaemon:: Fn+F5\n");
            toggleTouchpad();
            break;
        default:
            printf("TongfangFnDaemon:: Unknown Command\n");
            break;
    }

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
    eventHotKeyID.id = 1;
    // Opt + P
    RegisterEventHotKey(0x23, optionKey, eventHotKeyID, GetApplicationEventTarget(), 0, &eventHotKeyRef);

    eventHotKeyID.signature = 'FnF5';
    eventHotKeyID.id = 2;
    // Ctrl + Opt + F13
    RegisterEventHotKey(0x69, controlKey | optionKey, eventHotKeyID, GetApplicationEventTarget(), 0, &eventHotKeyRef);

    printf("TongfangFnDaemon:: Register HotKeys\n");
}

void dispatchMessage(struct TongfangKeyboardUtilityMessage *message) {
    printf("TongfangFnDaemon:: type:%d x:%d y:%d\n", message->type, message->arg1, message->arg2);

    switch (message->type) {
        case kToggleWifi:
            toggleAirplaneMode();
            break;
        case kSwitchDisplay:
            switchDisplayMode();
            break;
        case kDisableTouchpad:
            showOSD(OSDGraphicKeyboardBacklightDisabledNotConnected, 0, 0);
            break;
        case kEnableTouchpad:
            showOSD(OSDGraphicKeyboardBacklightDisabledMeter, 0, 0);
            break;
        case kIncreaseKeyboardBacklight:
            showOSD(OSDGraphicKeyboardBacklightMeter, 0, 0);
            break;
        case kDecreaseKeyboardBacklight:
            showOSD(OSDGraphicKeyboardBacklightMeter, 0, 0);
            break;
        default:
            printf("TongfangFnDaemon:: unknown type %d\n", message->type);
    }
}

void kernelMessageLoop() {
    int systemSocket = -1;

    // create system socket to receive kernel event data
    systemSocket = socket(PF_SYSTEM, SOCK_RAW, SYSPROTO_EVENT);

    // struct for vendor code
    struct kev_vendor_code vendorCode = {0};
    strncpy(vendorCode.vendor_string, "tongfang", KEV_VENDOR_CODE_MAX_STR_LEN);

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
    char kextMsg[KEV_MSG_HEADER_SIZE + sizeof(struct TongfangKeyboardUtilityMessage)] = {0};

    struct TongfangKeyboardUtilityMessage *message = NULL;

    while (YES) {
        bytesReceived = recv(systemSocket, kextMsg, sizeof(kextMsg), 0);

        if (bytesReceived != sizeof(kextMsg)) {
            continue;
        }

        // struct for broadcast data from the kext
        struct kern_event_msg *kernEventMsg = {0};
        kernEventMsg = (struct kern_event_msg*)kextMsg;

        if (FnEventCode != kernEventMsg->event_code) {
            continue;
        }

        message = (struct TongfangKeyboardUtilityMessage*)&kernEventMsg->event_data[0];

        dispatchMessage(message);
    }
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        printf("TongfangFnDaemon:: daemon started...\n");

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
