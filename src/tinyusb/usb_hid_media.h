//
// USB HID Consumer Control media keys (play/pause, next, prev, stop, mute, volume).
// AVRCP Target commands from the Bluetooth headset are translated into HID reports
// so the USB host's media player can be controlled.
//
// Usage values are defined here (not via class/hid/hid.h) so this header can be
// included from BTstack code without clashing with btstack_hid.h enums.
//

#ifndef USB_HID_MEDIA_H
#define USB_HID_MEDIA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// HID Consumer Control usage IDs (USB HID Usage Tables)
#define USB_HID_USAGE_SCAN_NEXT        0x00B5
#define USB_HID_USAGE_SCAN_PREVIOUS    0x00B6
#define USB_HID_USAGE_STOP             0x00B7
#define USB_HID_USAGE_PLAY_PAUSE       0x00CD
#define USB_HID_USAGE_MUTE             0x00E2
#define USB_HID_USAGE_VOLUME_INCREMENT 0x00E9
#define USB_HID_USAGE_VOLUME_DECREMENT 0x00EA

// Queue a Consumer Control usage for press + release on the USB host.
// Safe to call from BTstack callbacks; the actual transfer is done in usb_hid_media_task().
void usb_hid_media_send(uint16_t usage);

// Drive pending press/release reports. Call from the TinyUSB task path.
void usb_hid_media_task(void);

#ifdef __cplusplus
}
#endif

#endif // USB_HID_MEDIA_H
