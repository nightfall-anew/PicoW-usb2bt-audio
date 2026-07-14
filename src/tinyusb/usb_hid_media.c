//
// USB HID Consumer Control media-key helper.
//

#include "usb_hid_media.h"

#include "tusb.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

// Report has no Report ID; payload is a single 16-bit Consumer usage.
// 0 = all keys released.

static volatile uint16_t s_pending_usage = 0;
static bool s_need_release = false;
static absolute_time_t s_release_at;

void usb_hid_media_send(uint16_t usage) {
    if (usage == 0) {
        return;
    }

    // Keep only the latest key if the previous one is still queued.
    uint32_t irq = save_and_disable_interrupts();
    s_pending_usage = usage;
    restore_interrupts(irq);
}

void usb_hid_media_task(void) {
    if (!tud_mounted() || !tud_hid_ready()) {
        return;
    }

    // Send release after a short hold so the host sees a full press/release.
    if (s_need_release) {
        if (!time_reached(s_release_at)) {
            return;
        }
        uint16_t empty = 0;
        if (tud_hid_report(0, &empty, sizeof(empty))) {
            s_need_release = false;
        }
        return;
    }

    uint32_t irq = save_and_disable_interrupts();
    uint16_t usage = s_pending_usage;
    s_pending_usage = 0;
    restore_interrupts(irq);

    if (usage == 0) {
        return;
    }

    if (tud_hid_report(0, &usage, sizeof(usage))) {
        s_need_release = true;
        s_release_at = make_timeout_time_ms(15);
    } else {
        // Endpoint busy — put it back for the next pass.
        irq = save_and_disable_interrupts();
        if (s_pending_usage == 0) {
            s_pending_usage = usage;
        }
        restore_interrupts(irq);
    }
}

// Invoked when received GET_REPORT control request.
// Application must fill buffer with report contents and return its length.
// Return zero will cause the stack to STALL the request.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint (Report ID = 0, Type = 0).
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}
