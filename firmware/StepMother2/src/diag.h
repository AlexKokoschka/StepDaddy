#ifndef DIAG_H_
#define DIAG_H_

// Returns a short text if the previous run crashed or hung, else NULL.
const char *diag_init(void);
void diag_start_watchdog(void);
void diag_feed(void);
void diag_leaving(void);       // call before reset_usb_boot()

#endif
