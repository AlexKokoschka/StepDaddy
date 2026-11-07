#include "usb_names.h"
#include "firmware_version.h"

// The board shows up as "Step Mother" whichever editing level is in use (the
// device, its MIDI port and its audio interface). The version is in the
// image's tag (firmware_version.h), not in the name, so a computer that
// remembers the device by name doesn't see a new one after each update.
const char firmware_tag[] = FIRMWARE_TAG;

struct usb_string_descriptor_struct usb_string_product_name={
 2+11*2,3,{'S','t','e','p',' ','M','o','t','h','e','r'}};
struct usb_string_descriptor_struct usb_string_manufacturer_name={
 2+11*2,3,{'S','t','e','p',' ','M','o','t','h','e','r'}};
