#pragma once
#include <wdf.h>
#include <ntdef.h>
#include <wdfobject.h>
#include <wdftypes.h>

EXTERN_C_START

//
// The device context performs the same job as
// a WDM device extension in the driver frameworks
//
typedef struct _CONTROL_DEVICE_CONTEXT
{
	WDFCOLLECTION FileObjects;
	WDFWAITLOCK FileObjectsLock;

} CONTROL_DEVICE_CONTEXT, *PCONTROL_DEVICE_CONTEXT;

//
// This macro will generate an inline function called DeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CONTROL_DEVICE_CONTEXT, ControlDeviceGetContext)

typedef struct _FILEOBJECT_CONTEXT
{
	WDFQUEUE Queue;
	WDFCOLLECTION Events;
	WDFWAITLOCK EventsLock;
	WDFDEVICE DevicePosition;

} FILEOBJECT_CONTEXT, *PFILEOBJECT_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FILEOBJECT_CONTEXT, FileObjectGetContext)

typedef struct _MEMORY_CONTEXT
{
	ULONG DeviceNumber;
	ULONG BufferSize;
	BYTE MajorFunctionCode;
	BYTE MinorFunctionCode;
	ULONG OutputDataOffset;
} MEMORY_CONTEXT, *PMEMORY_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MEMORY_CONTEXT, MemoryGetContext)

typedef struct _DEVICE_INFO
{
	ULONG DeviceNumber;
	CHAR DeviceName;
} DEVICE_INFO, *PDEVICE_INFO;

WDFDEVICE ControlDevice;
WDFWAITLOCK ControlDeviceLock;

#define NTDEVICE_NAME_STRING	L"\\Device\\ComPortMonitor"
#define SYMBOLIC_NAME_STRING	L"\\DosDevices\\Global\\ComPortMonitor"
#define DEVICEINFO_BUFSIZE 1024 + sizeof(DEVICE_INFO)

#define IOCTL_CPM_BASE 0x800
#define IOCTL_CPM_GET_DEVICE_FIRST			CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_CPM_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CPM_GET_DEVICE_NEXT			CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_CPM_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CPM_ATTACH_TO_DEVICE			CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_CPM_BASE + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CPM_DETACH_FROM_DEVICE		CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_CPM_BASE + 4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CPM_GET_DATA_INFO				CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_CPM_BASE + 5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CPM_GET_DEVICE_PROCESS_ID		CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_CPM_BASE + 6, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define NOT_FOUND (ULONG)-1

//
// Function to initialize the device and its callbacks
//
NTSTATUS CreateControlDevice(_In_ WDFDRIVER Driver, _In_ PWDFDEVICE_INIT DeviceInit);

EVT_WDF_DEVICE_FILE_CREATE ControlDevice_EvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE ControlDevice_EvtFileClose;
EVT_WDF_IO_QUEUE_IO_READ ControlDevice_EvtIoRead;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ControlDevice_EvtIoDeviceControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP ControlDevice_EvtCleanupCallback;

ULONG WdfCollectionFindItemIndex(WDFCOLLECTION Collection, WDFOBJECT Item);

EXTERN_C_END