/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that app can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_ComPortMonitor,
    0x792229cb,0xff25,0x4ce1,0xa2,0xb7,0x37,0xe6,0x13,0x23,0xa0,0x05);
// {792229cb-ff25-4ce1-a2b7-37e61323a005}
