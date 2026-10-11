#include <SPI.h>
#include <hidcomposite.h>
#include <PluggableUSB.h>
#include "native_descriptors.h"
#include "report_pipeline.h"
#include "host_transport.h"

extern void USB_BridgeService();
extern uint8_t USB_NativeEpoch();
extern uint8_t USBGetConfiguration();

USB usb;
ReportPipeline pipeline;
HostTransport transport(usb, pipeline);
bool initialized = false;

class NativeReceiver : public HIDComposite {
    uint32_t nextPollUs[3] = {};
    bool nativeReady = false;
public:
    explicit NativeReceiver(USB *controller) : HIDComposite(controller) {}
    bool SelectInterface(uint8_t iface, uint8_t) override { return iface < 3; }
    bool isReady() override { return bPollEnable && nativeReady; }
    uint8_t OnInitSuccessful() override {
        nativeReady = false;
        if (VID != 0x046D || PID != 0xC54D) return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
        for (uint8_t iface = 0; iface < 2; ++iface) {
            uint8_t error = SetProtocol(iface, 1);
            if (error) return error;
        }
        // Only support the measured, directly attached full-speed receiver.
        for (uint8_t ep = 1; ep <= 3; ++ep) {
            if (!usb.getEpInfoEntry(bAddress, ep)) return USB_ERROR_EP_NOT_FOUND_IN_TBL;
            nextPollUs[ep - 1] = micros();
        }
        nativeReady = true;
        return 0;
    }
    uint8_t Poll() override {
        if (!isReady() || transport.busy() || !USBGetConfiguration()) return 0;
        uint32_t now = micros();
        for (uint8_t ep = 1; ep <= 3; ++ep) {
            uint8_t i = ep - 1;
            uint32_t period = ep == 1 ? 1000 : 10000;
            int32_t late = now - nextPollUs[i];
            if (late < 0) continue;
            if (!pipeline.canReceive(ep)) {
                nextPollUs[i] += ((uint32_t)late / period + 1) * period;
                ++pipeline.stats.blockedPolls;
                continue;
            }
            if (transport.beginInput(bAddress, ep)) {
                if (ep == 1) pipeline.stats.missedMouseSlots += (uint32_t)late / period;
                // Absolute deadlines do not accumulate transfer duration drift.
                nextPollUs[i] += ((uint32_t)late / period + 1) * period;
                return 0;
            }
        }
        return 0;
    }
    uint8_t Release() override {
        nativeReady = false; pipeline.clear(); transport.reset();
        return HIDComposite::Release();
    }
};

NativeReceiver receiver(&usb);

class NativeUsbInterfaces : public PluggableUSBModule {
    uint8_t endpointTypes[3] = {EP_TYPE_INTERRUPT_IN, EP_TYPE_INTERRUPT_IN, EP_TYPE_INTERRUPT_IN};
public:
    NativeUsbInterfaces() : PluggableUSBModule(3, 3, endpointTypes) { PluggableUSB().plug(this); }
    int getInterface(uint8_t *count) override {
        *count += 3;
        return USB_SendControl(TRANSFER_PGM, nativeConfiguration, sizeof(nativeConfiguration));
    }
    int getDescriptor(USBSetup &setup) override {
        if (setup.bmRequestType != 0x81 || setup.wIndex >= 3) return 0;
        uint8_t iface = setup.wIndex;
        if (setup.wValueH == 0x21)
            return USB_SendControl(TRANSFER_PGM, nativeConfiguration + iface * 25 + 9, 9);
        if (setup.wValueH != 0x22) return 0;
        if (iface == 0) return USB_SendControl(TRANSFER_PGM, nativeMouseDescriptor, sizeof(nativeMouseDescriptor));
        if (iface == 1) return USB_SendControl(TRANSFER_PGM, nativeKeyboardDescriptor, sizeof(nativeKeyboardDescriptor));
        return USB_SendControl(TRANSFER_PGM, nativeHidppDescriptor, sizeof(nativeHidppDescriptor));
    }
    bool setup(USBSetup &) override { return false; }
    uint8_t getShortName(char *name) override {
        const char serial[] = "3176385A3433";
        memcpy(name, serial, sizeof(serial) - 1);
        return sizeof(serial) - 1;
    }
};

NativeUsbInterfaces nativeUsb;

bool USB_BridgeStart(const USBSetup &setup, const uint8_t *payload) {
    if (!receiver.isReady() || (setup.bmRequestType != 0x21 && setup.bmRequestType != 0xA1)) return false;
    return transport.beginControl(receiver.GetAddress(), setup, payload);
}
int USB_BridgePoll(uint8_t *reply, uint8_t &length) { return transport.finishControl(reply, length); }
void USB_BridgeCancel() { transport.cancelControl(); }

#include "diagnostics.h"

void setup() {
    Serial.begin(115200);
    UsbDEBUGlvl = 0;
    initialized = usb.Init() == 0;
}

void loop() {
    static uint8_t previousEpoch = USB_NativeEpoch();
    uint8_t epoch = USB_NativeEpoch();
    if (epoch != previousEpoch) {
        pipeline.clear(); transport.cancelControl(); previousEpoch = epoch;
    }
    transport.service();
    pipeline.service();
    if (transport.needsRecovery) {
        pipeline.clear(); transport.reset();
        initialized = usb.Init() == 0;
        usb.setUsbTaskState(USB_DETACHED_SUBSTATE_INITIALIZE);
    }
    if (initialized && !transport.busy()) usb.Task();
    USB_BridgeService();
    pipeline.service();
    diagnostics();
}
