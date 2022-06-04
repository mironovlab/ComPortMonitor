/*++

Module Name:

    driver.h

Abstract:

    This file contains the driver definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#define INITGUID

#include <ntddk.h>
#include <wdf.h>

#include "device.h"
#include "queue.h"
#include "trace.h"

EXTERN_C_START

#define HARDWARE_ID L"Root\\ComPortMonitor"

//
// WDFDRIVER Events
//

WDFCOLLECTION FilteringDevices;
WDFWAITLOCK FilteringDevicesLock;
ULONG NextDeviceNumber;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD ComPortMonitorEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP ComPortMonitorEvtDriverContextCleanup;

EXTERN_C_END
