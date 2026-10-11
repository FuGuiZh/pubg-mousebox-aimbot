#pragma once

// Diagnostic USB uses the same try-send path, so logging cannot stall input.
void diagnostics() {
    static char line[256];
    static uint16_t length = 0, cursor = 0;
    static uint8_t phase = 0;
    static uint32_t lastUs = 0, elapsedUs = 0;
    // Arduino Serial::operator bool() contains delay(10), even when closed!
    // Reading DTR directly is essential; never put that boolean cast here.
    if (!Serial.dtr()) { length = cursor = 0; phase = 0; return; }
    if (cursor < length) {
        uint8_t size = min((uint16_t)64, (uint16_t)(length - cursor));
        if (USB_TrySendPacket(CDC_TX, line + cursor, size) > 0) cursor += size;
        return;
    }
    auto &s = pipeline.stats;
    int size;
    if (phase == 0) {
        uint32_t now = micros();
        if (now - lastUs < 1000000) return;
        elapsedUs = now - lastUs; lastUs = now;
        size = snprintf_P(line, sizeof(line), PSTR(
            "NATIVE v2 t_ms=%lu dt_us=%lu ready=%d rx=%lu,%lu,%lu submit=%lu,%lu,%lu len=%u,%u,%u poll=%lu nak=%lu ctrl=%lu,%lu cerr=%lu cancel=%lu\r\n"),
            millis(), elapsedUs, receiver.isReady(), s.received[0], s.received[1], s.received[2],
            s.submitted[0], s.submitted[1], s.submitted[2], s.lastLength[0], s.lastLength[1], s.lastLength[2],
            s.polls[0], s.naks[0], transport.controlOut, transport.controlIn, transport.controlErrors, transport.cancelledControls);
        phase = 1;
    } else if (phase == 1) {
        size = snprintf_P(line, sizeof(line), PSTR(
            "FLOW v2 queue=%u,%u,%u peak=%u,%u,%u overflow=%lu invalid=%lu discard=%lu blocked=%lu missed=%lu xerr=%lu last=%02X moving=%lu\r\n"),
            pipeline.queued(1), pipeline.queued(2), pipeline.queued(3), pipeline.peak(1), pipeline.peak(2), pipeline.peak(3),
            s.overflow, s.invalidReports, s.discarded, s.blockedPolls, s.missedMouseSlots, s.transferErrors, transport.lastControlError, s.movingMouseReports);
        phase = 2;
    } else {
        size = snprintf_P(line, sizeof(line), PSTR(
            "TIMING v2 samples=%lu mean_us=%lu min_us=%u max_us=%u bins=%u,%u,%u,%u,%u,%u\r\n"),
            s.mouseIntervals, s.mouseIntervals ? s.mouseIntervalSumUs / s.mouseIntervals : 0,
            s.mouseIntervals ? s.mouseIntervalMinUs : 0, s.mouseIntervalMaxUs,
            s.mouseIntervalBins[0], s.mouseIntervalBins[1], s.mouseIntervalBins[2],
            s.mouseIntervalBins[3], s.mouseIntervalBins[4], s.mouseIntervalBins[5]);
        s.resetIntervals(); phase = 0;
    }
    length = size > 0 ? min((uint16_t)size, (uint16_t)(sizeof(line) - 1)) : 0;
    cursor = 0;
}
