#pragma once
#include <Arduino.h>

extern int USB_TrySendPacket(uint8_t endpoint, const void *data, uint8_t length);

// Sizes follow this receiver's report descriptors, not arbitrary truncation.
// Never merge relative movements or overwrite a queued button transition.
template<uint8_t Depth, uint8_t MaxReport>
class ReportQueue {
    struct Packet { uint8_t length; uint8_t bytes[MaxReport]; } packets[Depth];
    uint8_t head = 0, tail = 0, count = 0;
public:
    uint8_t peak = 0;
    bool full() const { return count == Depth; }
    uint8_t size() const { return count; }
    bool push(const uint8_t *bytes, uint8_t length) {
        if (full() || !length || length > MaxReport) return false;
        packets[tail].length = length;
        memcpy(packets[tail].bytes, bytes, length);
        tail = (tail + 1) % Depth;
        if (++count > peak) peak = count;
        return true;
    }
    int submit(uint8_t endpoint) {
        if (!count) return 0;
        int result = USB_TrySendPacket(endpoint, packets[head].bytes, packets[head].length);
        if (result > 0) { head = (head + 1) % Depth; --count; }
        return result;
    }
    uint8_t clear() { uint8_t removed = count; head = tail = count = 0; return removed; }
};

struct PipelineStats {
    uint32_t received[3] = {}, submitted[3] = {}, polls[3] = {}, naks[3] = {};
    uint32_t transferErrors = 0, invalidReports = 0, overflow = 0, discarded = 0;
    uint32_t blockedPolls = 0, missedMouseSlots = 0;
    uint32_t movingMouseReports = 0;
    uint32_t lastMouseUs = 0, mouseIntervals = 0, mouseIntervalSumUs = 0;
    uint16_t mouseIntervalMinUs = 65535, mouseIntervalMaxUs = 0;
    uint16_t mouseIntervalBins[6] = {};
    uint8_t lastLength[3] = {};

    void mouseArrival(uint32_t now) {
        if (lastMouseUs) {
            uint32_t interval = now - lastMouseUs;
            // Long inactivity is not a polling jitter sample.
            if (interval < 20000) {
                ++mouseIntervals;
                mouseIntervalSumUs += interval;
                if (interval < mouseIntervalMinUs) mouseIntervalMinUs = interval;
                if (interval > mouseIntervalMaxUs) mouseIntervalMaxUs = interval;
                uint8_t bin = interval < 750 ? 0 : interval < 1250 ? 1 :
                    interval < 1750 ? 2 : interval < 2500 ? 3 : interval < 5000 ? 4 : 5;
                ++mouseIntervalBins[bin];
            }
        }
        lastMouseUs = now;
    }
    void resetIntervals() {
        mouseIntervals = mouseIntervalSumUs = 0;
        mouseIntervalMinUs = 65535; mouseIntervalMaxUs = 0;
        memset(mouseIntervalBins, 0, sizeof(mouseIntervalBins));
    }
};

class ReportPipeline {
    ReportQueue<32, 13> mouse;
    ReportQueue<4, 17> media;
    ReportQueue<8, 20> hidpp;
public:
    PipelineStats stats;
    bool canReceive(uint8_t ep) const {
        return ep == 1 ? !mouse.full() : ep == 2 ? !media.full() : ep == 3 && !hidpp.full();
    }
    void receive(uint8_t ep, uint8_t length, const uint8_t *bytes) {
        if (ep < 1 || ep > 3 || !length) return;
        uint8_t i = ep - 1;
        ++stats.received[i]; stats.lastLength[i] = length;
        bool valid = ep == 1 ? length == 13 : ep == 2 ? length <= 17 :
            ((bytes[0] == 0x10 && length == 7) || (bytes[0] == 0x11 && length == 20));
        if (!valid) { ++stats.invalidReports; return; }
        bool queued = ep == 1 ? mouse.push(bytes, length) : ep == 2 ? media.push(bytes, length) : hidpp.push(bytes, length);
        if (!queued) ++stats.overflow;
        if (ep == 1) {
            if (bytes[2] || bytes[3] || bytes[4] || bytes[5]) ++stats.movingMouseReports;
            stats.mouseArrival(micros());
        }
    }
    void service() {
        // Each call only submits ready packets; no waiting for an IN bank.
        if (mouse.submit(1) > 0) ++stats.submitted[0];
        if (media.submit(2) > 0) ++stats.submitted[1];
        if (hidpp.submit(3) > 0) ++stats.submitted[2];
    }
    void clear() { stats.discarded += mouse.clear() + media.clear() + hidpp.clear(); }
    uint8_t queued(uint8_t ep) const { return ep == 1 ? mouse.size() : ep == 2 ? media.size() : hidpp.size(); }
    uint8_t peak(uint8_t ep) const { return ep == 1 ? mouse.peak : ep == 2 ? media.peak : hidpp.peak; }
};
