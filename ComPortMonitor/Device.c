/*++

Module Name:

    device.c - Device handling events for example driver.

Abstract:

   This file contains the device entry points and callbacks.
    
Environment:

    Kernel-mode Driver Framework

--*/
#include <ntifs.h>
#include "driver.h"
#include "Control.h"

#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, ComPortMonitorCreateDevice)
#endif


NTSTATUS
ComPortMonitorCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++

Routine Description:

    Worker routine called to create a device and its software resources.

Arguments:

    DeviceInit - Pointer to an opaque init structure. Memory for this
                    structure will be freed by the framework when the WdfDeviceCreate
                    succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
{
    WDF_OBJECT_ATTRIBUTES attr;
    PDEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
	WDF_FILEOBJECT_CONFIG config;

    PAGED_CODE();

	WdfFdoInitSetFilter(DeviceInit);

	WDF_FILEOBJECT_CONFIG_INIT(&config, ComPortMonitor_EvtDeviceFileCreate, ComPortMonitor_EvtFileClose, WDF_NO_EVENT_CALLBACK);
	WdfDeviceInitSetFileObjectConfig(DeviceInit, &config, WDF_NO_OBJECT_ATTRIBUTES);
	WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_SERIAL_PORT);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, DEVICE_CONTEXT);
	attr.EvtCleanupCallback = ComPortMonitorEvtCleanupCallback;

	status = WdfDeviceCreate(&DeviceInit, &attr, &device);
	if (!NT_SUCCESS(status))
		return status;
	//
	// Get a pointer to the device context structure that we just associated
	// with the device object. We define this structure in the device.h
	// header file. DeviceGetContext is an inline function generated by
	// using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
	// This function will do the type checking and return the device context.
	// If you pass a wrong object handle it will return NULL and assert if
	// run under framework verifier mode.
	//
	deviceContext = DeviceGetContext(device);
	deviceContext->Number = NextDeviceNumber++;
	//
	// Initialize the context.
	//
	WDF_OBJECT_ATTRIBUTES_INIT(&attr);
	attr.ParentObject = device;
	status = WdfCollectionCreate(&attr, &deviceContext->Listeners);
	if (!NT_SUCCESS(status))
		return status;

	WDF_OBJECT_ATTRIBUTES_INIT(&attr);
	attr.ParentObject = deviceContext->Listeners;
	status = WdfWaitLockCreate(&attr, &deviceContext->ListenersLock);
	if (!NT_SUCCESS(status))
		return status;

	WdfWaitLockAcquire(FilteringDevicesLock, NULL);
	status = WdfCollectionAdd(FilteringDevices, device);
	WdfWaitLockRelease(FilteringDevicesLock);

	if (!NT_SUCCESS(status))
		return status;

	//
	// Initialize the I/O Package and any Queues
	//
	return ComPortMonitorQueueInitialize(device);
}

VOID ComPortMonitor_EvtDeviceFileCreate(
	_In_ WDFDEVICE		Device,
	_In_ WDFREQUEST		Request,
	_In_ WDFFILEOBJECT	FileObject
)
{
	WDFMEMORY mem;
	PULONG buf;
	MEMORY_CONTEXT context;
	WDF_REQUEST_PARAMETERS params;
	ULONG processId;
	UNREFERENCED_PARAMETER(FileObject);
	if (NT_SUCCESS(WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES, PagedPool, 0, sizeof(*buf), &mem, &buf)))
	{
		WDF_REQUEST_PARAMETERS_INIT(&params);
		WdfRequestGetParameters(Request, &params);
		memset(&context, 0, sizeof(context));
		context.BufferSize = sizeof(processId);
		context.MajorFunctionCode = params.Type;
		context.MinorFunctionCode = params.MinorFunction;
		context.OutputDataOffset = 0;
		processId = IoGetRequestorProcessId(WdfRequestWdmGetIrp(Request));
		*buf = processId;
		ComPortMonitor_EvtNotifyListeners(Device, mem, &context);
		WdfObjectDelete(mem);
	}
	ComPortMonitor_ForwardRequest(Request, Device);
}

VOID ComPortMonitor_EvtFileClose(
	_In_ WDFFILEOBJECT FileObject
)
{
	WDFMEMORY mem;
	PCHAR buf;
	MEMORY_CONTEXT context;

	if (NT_SUCCESS(WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES, PagedPool, 0, sizeof(*buf), &mem, &buf)))
	{
		memset(&context, 0, sizeof(context));
		context.MajorFunctionCode = IRP_MJ_CLOSE;
		ComPortMonitor_EvtNotifyListeners(WdfFileObjectGetDevice(FileObject), mem, &context);
		WdfObjectDelete(mem);
	}
}

VOID ComPortMonitorEvtCleanupCallback(_In_ WDFOBJECT Object)
{
	PCONTROL_DEVICE_CONTEXT ctrlContext;
	WDFFILEOBJECT file;
	PFILEOBJECT_CONTEXT fileContext;
	ULONG i, c, pos;

	KdPrint(("FilterDevice cleanup start\n"));
	WdfWaitLockAcquire(FilteringDevicesLock, NULL);
	__try
	{
		if (ControlDevice != NULL)
		{
			ctrlContext = ControlDeviceGetContext(ControlDevice);
			WdfWaitLockAcquire(ctrlContext->FileObjectsLock, NULL);
			__try
			{
				c = WdfCollectionGetCount(ctrlContext->FileObjects);
				for (i = 0; i < c; i++)
				{
					file = WdfCollectionGetItem(ctrlContext->FileObjects, i);
					if (file != NULL && (fileContext = FileObjectGetContext(file))->DevicePosition == Object)
					{
						pos = WdfCollectionFindItemIndex(FilteringDevices, fileContext->DevicePosition);
						fileContext->DevicePosition = pos > 0 ? WdfCollectionGetItem(FilteringDevices, pos - 1) : NULL;
					}
				}
			}
			__finally
			{
				WdfWaitLockRelease(ctrlContext->FileObjectsLock);
			}
		}
		WdfCollectionRemove(FilteringDevices, Object);
	}
	__finally
	{
		WdfWaitLockRelease(FilteringDevicesLock);
	}
	KdPrint(("FilterDevice cleanup end\n"));
}