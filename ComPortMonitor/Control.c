#include <ntifs.h>
#include <stddef.h>
#include "driver.h"

#include "Control.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, CreateControlDevice)
#pragma alloc_text (PAGE, ControlDevice_EvtDeviceFileCreate)
#pragma alloc_text (PAGE, ControlDevice_EvtFileClose)
#pragma alloc_text (PAGE, ControlDevice_EvtIoDeviceControl)
#endif

WDFDEVICE ControlDevice = NULL;
WDFWAITLOCK ControlDeviceLock;

NTSTATUS CreateControlDevice(_In_ WDFDRIVER Driver, _In_ PWDFDEVICE_INIT DeviceInit)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	WDF_OBJECT_ATTRIBUTES attr;
	WDF_IO_QUEUE_CONFIG queueConfig;
	WDFQUEUE queue;
	WDF_FILEOBJECT_CONFIG config;
	WDFDEVICE control = NULL;
	PCONTROL_DEVICE_CONTEXT context;
	DECLARE_CONST_UNICODE_STRING(ntDeviceName, NTDEVICE_NAME_STRING);
	DECLARE_CONST_UNICODE_STRING(symbolicLinkName, SYMBOLIC_NAME_STRING);

	UNREFERENCED_PARAMETER(Driver);
	__try
	{
		KdPrint(("ControlDevice Create start\n"));

		WdfDeviceInitSetExclusive(DeviceInit, FALSE);
		status = WdfDeviceInitAssignName(DeviceInit, &ntDeviceName);
		if (!NT_SUCCESS(status))
			return status;

		WdfDeviceInitAssignSDDLString(DeviceInit, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX);
		if (!NT_SUCCESS(status))
			return status;

		WDF_FILEOBJECT_CONFIG_INIT(&config, ControlDevice_EvtDeviceFileCreate, ControlDevice_EvtFileClose, WDF_NO_EVENT_CALLBACK);
		WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, FILEOBJECT_CONTEXT);
		WdfDeviceInitSetFileObjectConfig(DeviceInit, &config, &attr);

		WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, CONTROL_DEVICE_CONTEXT);
		attr.EvtCleanupCallback = ControlDevice_EvtCleanupCallback;
		status = WdfDeviceCreate(&DeviceInit, &attr, &control);
		if (!NT_SUCCESS(status))
			return status;

		context = ControlDeviceGetContext(control);

		WDF_OBJECT_ATTRIBUTES_INIT(&attr);
		attr.ParentObject = control;
		status = WdfCollectionCreate(&attr, &context->FileObjects);
		if (!NT_SUCCESS(status))
			return status;

		WDF_OBJECT_ATTRIBUTES_INIT(&attr);
		attr.ParentObject = context->FileObjects;
		status = WdfWaitLockCreate(&attr, &context->FileObjectsLock);
		if (!NT_SUCCESS(status))
			return status;

		//
		// Create a device interface so that applications can find and talk
		// to us.
		//
		//status = WdfDeviceCreateDeviceInterface(control, &GUID_DEVINTERFACE_ComPortMonitor,	NULL);
		//if (!NT_SUCCESS(status))
		//	goto error;

		status = WdfDeviceCreateSymbolicLink(control, &symbolicLinkName);
		if (!NT_SUCCESS(status))
			return status;

		WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
		queueConfig.EvtIoRead = ControlDevice_EvtIoRead;
		queueConfig.EvtIoDeviceControl = ControlDevice_EvtIoDeviceControl;
		status = WdfIoQueueCreate(control, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
		if (!NT_SUCCESS(status))
			return status;

		ControlDevice = control;

		WdfControlFinishInitializing(control);

		KdPrint(("ControlDevice Create end\n"));
		return status;
	}
	__finally
	{
		if (!NT_SUCCESS(status))
		{
			KdPrint(("ControlDevice Create error!\n"));
			if (DeviceInit != NULL)
				WdfDeviceInitFree(DeviceInit);
			if (control != NULL)
				WdfObjectDelete(control);
		}
	}
}

VOID ControlDevice_EvtDeviceFileCreate(
	_In_ WDFDEVICE		Device,
	_In_ WDFREQUEST		Request,
	_In_ WDFFILEOBJECT	FileObject
)
{
	WDF_IO_QUEUE_CONFIG qconfig;
	PFILEOBJECT_CONTEXT fileContext;
	PCONTROL_DEVICE_CONTEXT controlContext;
	WDF_OBJECT_ATTRIBUTES attr;
	NTSTATUS status = STATUS_CANCELLED;

	KdBreakPoint();
	__try
	{
		fileContext = FileObjectGetContext(FileObject);

		WDF_OBJECT_ATTRIBUTES_INIT(&attr);
		attr.ParentObject = FileObject;
		WDF_IO_QUEUE_CONFIG_INIT(&qconfig, WdfIoQueueDispatchManual);
		status = WdfIoQueueCreate(Device, &qconfig, &attr, &fileContext->Queue);
		if (!NT_SUCCESS(status))
			return;

		WDF_OBJECT_ATTRIBUTES_INIT(&attr);
		attr.ParentObject = FileObject;
		status = WdfCollectionCreate(&attr, &fileContext->Events);
		if (!NT_SUCCESS(status))
			return;

		WDF_OBJECT_ATTRIBUTES_INIT(&attr);
		attr.ParentObject = fileContext->Events;
		status = WdfWaitLockCreate(&attr, &fileContext->EventsLock);
		if (!NT_SUCCESS(status))
			return;

		controlContext = ControlDeviceGetContext(ControlDevice);
		WdfWaitLockAcquire(controlContext->FileObjectsLock, NULL);
		status = WdfCollectionAdd(controlContext->FileObjects, FileObject);
		WdfWaitLockRelease(controlContext->FileObjectsLock);
	}
	__finally
	{
		WdfRequestComplete(Request, status);
	}
}

VOID ControlDevice_EvtFileClose(
	_In_ WDFFILEOBJECT FileObject
)
{
	PCONTROL_DEVICE_CONTEXT controlContext;
	PDEVICE_CONTEXT devContext;
	ULONG i, j, ci, cj;

	controlContext = ControlDeviceGetContext(ControlDevice);

	WdfWaitLockAcquire(FilteringDevicesLock, NULL);
	__try
	{
		ci = WdfCollectionGetCount(FilteringDevices);
		for (i = 0; i < ci; i++)
		{
			devContext = DeviceGetContext(WdfCollectionGetItem(FilteringDevices, i));
			WdfWaitLockAcquire(devContext->ListenersLock, NULL);
			__try
			{
				cj = WdfCollectionGetCount(devContext->Listeners);
				for (j = 0; j < cj; j++)
					if (WdfCollectionGetItem(devContext->Listeners, j) == FileObject)
					{
						WdfCollectionRemove(devContext->Listeners, FileObject);
						break;
					}
			}
			__finally
			{
				WdfWaitLockRelease(devContext->ListenersLock);
			}
		}
		WdfWaitLockAcquire(controlContext->FileObjectsLock, NULL);
		WdfCollectionRemove(controlContext->FileObjects, FileObject);
		WdfWaitLockRelease(controlContext->FileObjectsLock);
	}
	__finally
	{
		WdfWaitLockRelease(FilteringDevicesLock);
	}
}

VOID ControlDevice_EvtIoRead(
	_In_ WDFQUEUE   Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t     Length
)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PFILEOBJECT_CONTEXT context;
	PMEMORY_CONTEXT memContext;
	WDFMEMORY data, output;
	ULONG written = 0;
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Length);
	__try
	{
		context = FileObjectGetContext(WdfRequestGetFileObject(Request));
		WdfWaitLockAcquire(context->EventsLock, NULL);
		__try
		{
			data = WdfCollectionGetFirstItem(context->Events);
			if (data == NULL)
			{
				status = STATUS_NO_MORE_ENTRIES;
				return;
			}
			memContext = MemoryGetContext(data);
			status = WdfRequestRetrieveOutputMemory(Request, &output);
			if (!NT_SUCCESS(status))
				return;

			status = WdfMemoryCopyFromBuffer(output, 0, WdfMemoryGetBuffer(data, NULL), memContext->BufferSize);
			if (!NT_SUCCESS(status))
				return;

			written = memContext->BufferSize;
			WdfCollectionRemove(context->Events, data);
			WdfObjectDelete(data);
		}
		__finally
		{
			WdfWaitLockRelease(context->EventsLock);
		}
	}
	__finally
	{
		WdfRequestCompleteWithInformation(Request, status, written);
	}
}

VOID ControlDevice_EvtIoDeviceControl(
	_In_ WDFQUEUE	Queue,
	_In_ WDFREQUEST	Request,
	_In_ size_t		OutputBufferLength,
	_In_ size_t		InputBufferLength,
	_In_ ULONG		IoControlCode
)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	WDFMEMORY input, output;
	ULONG intvar;
	WDFFILEOBJECT fileObject;
	PFILEOBJECT_CONTEXT context;
	WDFMEMORY buffer;
	POBJECT_NAME_INFORMATION buf_ptr;
	ANSI_STRING ansi;
	PMEMORY_CONTEXT memContext;
	PCONTROL_DEVICE_CONTEXT ctrlContext;
	PDEVICE_CONTEXT devContext;
	DEVICE_INFO devinfo;
	PDEVICE_OBJECT nextlower;
	ULONG written = 0, i, ci, j, cj;
	BOOLEAN flag = FALSE;
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(OutputBufferLength);

	fileObject = WdfRequestGetFileObject(Request);
	context = FileObjectGetContext(fileObject);
	ctrlContext = ControlDeviceGetContext(ControlDevice);

	switch (IoControlCode)
	{
	case IOCTL_CPM_GET_DEVICE_FIRST:
	case IOCTL_CPM_GET_DEVICE_NEXT:
		WdfWaitLockAcquire(FilteringDevicesLock, NULL);
		__try
		{
			if (IoControlCode == IOCTL_CPM_GET_DEVICE_FIRST)
				context->DevicePosition = WdfCollectionGetFirstItem(FilteringDevices);
			else
			{
				if (context->DevicePosition != NULL)
				{
					intvar = WdfCollectionFindItemIndex(FilteringDevices, context->DevicePosition);
					if (intvar != NOT_FOUND)
						context->DevicePosition = WdfCollectionGetItem(FilteringDevices, intvar + 1);
				}
				else
					context->DevicePosition = WdfCollectionGetFirstItem(FilteringDevices);
			}
			if (context->DevicePosition != NULL)
			{
				status = WdfRequestRetrieveOutputMemory(Request, &output);
				if (!NT_SUCCESS(status))
					break;

				devinfo.DeviceNumber = DeviceGetContext(context->DevicePosition)->Number;
				nextlower = WdfDeviceWdmGetAttachedDevice(context->DevicePosition);
				status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES, PagedPool, 0, DEVICEINFO_BUFSIZE, &buffer, &buf_ptr);
				if (!NT_SUCCESS(status))
					break;
				__try
				{
					do
					{
						status = ObQueryNameString(nextlower, buf_ptr, DEVICEINFO_BUFSIZE, &intvar);
						if (!NT_SUCCESS(status))
							break;

						nextlower = IoGetLowerDeviceObject(nextlower);
						if (nextlower != NULL)
							ObDereferenceObject(nextlower);
					} while (buf_ptr->Name.Length == 0 && nextlower != NULL);

					if (!NT_SUCCESS(status))
						break;

					status = RtlUnicodeStringToAnsiString(&ansi, &buf_ptr->Name, TRUE);
					if (!NT_SUCCESS(status))
						break;
					__try
					{
						status = WdfMemoryCopyFromBuffer(output, 0, &devinfo, sizeof(devinfo));
						if (NT_SUCCESS(status))
						{
							status = WdfMemoryCopyFromBuffer(output, offsetof(DEVICE_INFO, DeviceName), ansi.Buffer, ansi.Length + 1);
							if (NT_SUCCESS(status))
								written = offsetof(DEVICE_INFO, DeviceName) + ansi.Length + 1;
						}
					}
					__finally
					{
						RtlFreeAnsiString(&ansi);
					}
				}
				__finally
				{
					WdfObjectDelete(buffer);
				}
			}
			else
				status = STATUS_NO_MORE_ENTRIES;
		}
		__finally
		{
			WdfWaitLockRelease(FilteringDevicesLock);
		}
		break;
	case IOCTL_CPM_ATTACH_TO_DEVICE:
	case IOCTL_CPM_DETACH_FROM_DEVICE:
		if (InputBufferLength < sizeof(intvar))
		{
			status = STATUS_INFO_LENGTH_MISMATCH;
			break;
		}
		status = WdfRequestRetrieveInputMemory(Request, &input);
		if (!NT_SUCCESS(status))
			break;

		status = WdfMemoryCopyToBuffer(input, 0, &intvar, sizeof(intvar));
		if (!NT_SUCCESS(status))
			break;

		status = STATUS_DEVICE_DOES_NOT_EXIST;

		WdfWaitLockAcquire(FilteringDevicesLock, NULL);
		ci = WdfCollectionGetCount(FilteringDevices);
		for (i = 0; i < ci; i++)
		{
			devContext = DeviceGetContext(WdfCollectionGetItem(FilteringDevices, i));
			if (devContext->Number == intvar)
			{
				WdfWaitLockAcquire(devContext->ListenersLock, NULL);
				__try
				{
					cj = WdfCollectionGetCount(devContext->Listeners);
					for (j = 0; j < cj; j++)
						if (WdfCollectionGetItem(devContext->Listeners, j) == fileObject)
						{
							if (IoControlCode == IOCTL_CPM_DETACH_FROM_DEVICE)
							{
								WdfCollectionRemoveItem(devContext->Listeners, j);
								status = STATUS_SUCCESS;
							}
							else
								flag = TRUE;
							break;
						}
					if (IoControlCode == IOCTL_CPM_ATTACH_TO_DEVICE)
					{
						if (flag)
							status = STATUS_ALREADY_REGISTERED;
						else
							status = WdfCollectionAdd(devContext->Listeners, fileObject);
						break;
					}
				}
				__finally
				{
					WdfWaitLockRelease(devContext->ListenersLock);
				}
			}
		}
		WdfWaitLockRelease(FilteringDevicesLock);
		break;
	case IOCTL_CPM_GET_DATA_INFO:
		WdfWaitLockAcquire(context->EventsLock, NULL);
		__try
		{
			buffer = WdfCollectionGetFirstItem(context->Events);
			if (buffer != NULL)
			{
				memContext = MemoryGetContext(buffer);
				status = WdfRequestRetrieveOutputMemory(Request, &output);
				if (NT_SUCCESS(status))
				{
					status = WdfMemoryCopyFromBuffer(output, 0, memContext, sizeof(*memContext));
					written = sizeof(*memContext);
					if (memContext->BufferSize == 0)
					{
						WdfCollectionRemove(context->Events, buffer);
						WdfObjectDelete(buffer);
					}
				}
			}
			else
			{
				status = WdfRequestForwardToIoQueue(Request, context->Queue);
				return;
			}
		}
		__finally
		{
			WdfWaitLockRelease(context->EventsLock);
		}
		break;
	default:
		status = STATUS_NOT_SUPPORTED;
	}
	WdfRequestCompleteWithInformation(Request, status, written);
}

ULONG WdfCollectionFindItemIndex(WDFCOLLECTION Collection, WDFOBJECT Item)
{
	ULONG index, count;
	count = WdfCollectionGetCount(Collection);
	for (index = 0; index < count; index++)
		if (WdfCollectionGetItem(Collection, index) == Item)
			return index;
	return NOT_FOUND;
}

VOID ControlDevice_EvtCleanupCallback(_In_ WDFOBJECT Object)
{
	UNREFERENCED_PARAMETER(Object);
	WdfWaitLockAcquire(ControlDeviceLock, NULL);
	ControlDevice = NULL;
	WdfWaitLockRelease(ControlDeviceLock);
}