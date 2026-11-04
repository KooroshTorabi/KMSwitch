#pragma once
#include <stdbool.h>
enum { ITF_KBD = 0, ITF_MOUSE, ITF_MSC, ITF_NET, ITF_NET_DATA, ITF_COUNT };
bool usb_network_enabled(void);
void usb_set_network(bool on);   // re-enumerates if it changes
enum { RID_MOUSE_REL = 1, RID_MOUSE_ABS = 2 };
