#pragma once
#include <hidcomposite.h>
#include "report_pipeline.h"
extern uint8_t USB_NativeEpoch();

// One MAX3421E token in flight. Runtime interrupt and control transfers are
// cooperative: no dispatchPkt()/ctrlReq() busy loops and no delay() retries.
// Library control transfers are still used during USB enumeration only.
class HostTransport {
    USB &usb;
    ReportPipeline &pipeline;
    enum Stage : uint8_t { Idle, Input, Setup, Data, Status, Done, Failed } stage = Idle, resumeStage = Idle;
    bool flight = false, directionIn = false, cancelled = false;
    uint8_t address = 0, controlAddress = 0, endpoint = 0, retries = 0, toggle = 1;
    uint32_t launchedUs = 0, requestUs = 0, retryUs = 0;
    SETUP_PKT request;
    EpInfo *inputInfo = nullptr;
    uint8_t buffer[64], inputBuffer[64], length = 0, inputLength = 0, requested = 0;
    uint8_t result = 0, inputEpoch = 0;

    void selectAddress() {
        usb.regWr(rPERADDR, address);
        usb.regWr(rMODE, usb.regRd(rMODE) & ~(bmLOWSPEED | bmHUBPRE));
    }
    void launch(uint32_t now) {
        selectAddress();
        uint8_t token;
        if (stage == Input) {
            usb.regWr(rHCTL, inputInfo->bmRcvToggle ? bmRCVTOG1 : bmRCVTOG0);
            token = tokIN | endpoint;
        } else if (stage == Setup) {
            usb.bytesWr(rSUDFIFO, sizeof(request), (uint8_t *)&request);
            token = tokSETUP;
        } else if (stage == Data) {
            if (directionIn) {
                usb.regWr(rHCTL, toggle ? bmRCVTOG1 : bmRCVTOG0);
                token = tokIN;
            } else {
                usb.regWr(rHCTL, bmSNDTOG1);
                usb.bytesWr(rSNDFIFO, length, buffer);
                usb.regWr(rSNDBC, length);
                token = tokOUT;
            }
        } else token = directionIn ? tokOUTHS : tokINHS;
        usb.regWr(rHIRQ, bmHXFRDNIRQ);
        usb.regWr(rHXFR, token);
        flight = true; launchedUs = now;
    }
    void consumeIn() {
        uint8_t available = usb.regRd(rRCVBC);
        if (available <= sizeof(buffer)) {
            usb.bytesRd(rRCVFIFO, available, stage == Input ? inputBuffer : buffer);
            if (stage == Input) inputLength = available; else length = available;
        } else { inputLength = length = 0; result = 0xF0; }
        usb.regWr(rHIRQ, bmRCVDAVIRQ);
    }
public:
    bool needsRecovery = false;
    uint32_t controlOut = 0, controlIn = 0, controlErrors = 0, cancelledControls = 0;
    uint8_t lastControlError = 0;
    explicit HostTransport(USB &controller, ReportPipeline &reports) : usb(controller), pipeline(reports) {}
    bool busy() const { return flight; }
    bool idle() const { return stage == Idle; }
    bool beginInput(uint8_t addr, uint8_t ep) {
        if (flight || stage == Done || stage == Failed || stage == Input) return false;
        inputInfo = usb.getEpInfoEntry(addr, ep);
        if (!inputInfo) return false;
        resumeStage = stage;
        inputEpoch = USB_NativeEpoch();
        address = addr; endpoint = ep; stage = Input; result = 0; inputLength = 0;
        ++pipeline.stats.polls[ep - 1];
        launch(micros());
        return true;
    }
    bool beginControl(uint8_t addr, const USBSetup &setup, const uint8_t *payload) {
        if (stage != Idle || flight || setup.wLength > sizeof(buffer)) return false;
        address = controlAddress = addr; endpoint = 0; requested = setup.wLength; length = requested;
        memcpy(&request, &setup, sizeof(request));
        directionIn = setup.bmRequestType & 0x80;
        if (!directionIn && length) memcpy(buffer, payload, length);
        if (directionIn) ++controlIn; else ++controlOut;
        cancelled = false; result = 0; retries = 0; toggle = 1;
        requestUs = micros(); retryUs = requestUs; stage = Setup;
        return true;
    }
    void cancelControl() {
        if (stage == Input) {
            if (resumeStage != Idle) { ++cancelledControls; resumeStage = Idle; }
            return;
        }
        if (stage == Idle) return;
        ++cancelledControls;
        if (flight) cancelled = true; else stage = Idle;
    }
    void reset() { flight = false; cancelled = false; stage = resumeStage = Idle; needsRecovery = false; }
    void service() {
        uint32_t now = micros();
        if (!flight) {
            if (stage >= Setup && stage <= Status && (int32_t)(now - retryUs) >= 0) launch(now);
            return;
        }
        uint8_t irq = usb.regRd(rHIRQ);
        if (!(irq & bmHXFRDNIRQ)) {
            if (now - launchedUs > 10000) {
                result = USB_ERROR_TRANSFER_TIMEOUT; flight = false; needsRecovery = true;
                if (stage == Input) { ++pipeline.stats.transferErrors; stage = Idle; }
                else { ++controlErrors; lastControlError = result; stage = Failed; }
            }
            return;
        }
        usb.regWr(rHIRQ, bmHXFRDNIRQ);
        flight = false;
        uint8_t hrsl = usb.regRd(rHRSL);
        result = hrsl & 0x0F;
        bool receivedIn = stage == Input || (stage == Data && directionIn);
        if (!result && receivedIn && (irq & bmRCVDAVIRQ)) consumeIn();
        else if (!result && receivedIn) result = 0xF0;
        if (stage == Input) {
            if (!result) {
                inputInfo->bmRcvToggle = (hrsl & bmRCVTOGRD) ? 1 : 0;
                if (inputLength) {
                    if (inputEpoch == USB_NativeEpoch()) pipeline.receive(endpoint, inputLength, inputBuffer);
                    else ++pipeline.stats.discarded;
                }
            } else if (result == hrNAK) ++pipeline.stats.naks[endpoint - 1];
            else {
                if (result == hrTOGERR) inputInfo->bmRcvToggle = (hrsl & bmRCVTOGRD) ? 0 : 1;
                ++pipeline.stats.transferErrors;
            }
            stage = resumeStage; address = controlAddress; return;
        }
        if (cancelled) { cancelled = false; stage = Idle; return; }
        if (result) {
            if (now - requestUs < 240000 && (result == hrNAK ||
                    ((result == hrTIMEOUT || result == hrTOGERR) && ++retries < 3))) {
                if (result == hrTOGERR && directionIn) toggle = (hrsl & bmRCVTOGRD) ? 0 : 1;
                retryUs = now + 1000; return;
            }
            ++controlErrors; lastControlError = result; stage = Failed; return;
        }
        if (stage == Setup) stage = requested ? Data : Status;
        else if (stage == Data) {
            if (directionIn && length > requested) {
                result = 0xF0; ++controlErrors; lastControlError = result; stage = Failed; return;
            }
            stage = Status;
        } else stage = Done;
        retryUs = now;
    }
    int finishControl(uint8_t *reply, uint8_t &replyLength) {
        if (stage != Done && stage != Failed) return 0;
        bool success = stage == Done;
        replyLength = directionIn ? length : 0;
        if (success && directionIn && length) memcpy(reply, buffer, length);
        stage = Idle;
        return success ? 1 : -1;
    }
};
