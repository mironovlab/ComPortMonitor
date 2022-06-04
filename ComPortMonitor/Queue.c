/*++

Module Name:

    queue.c

Abstract:

    This file contains the queue entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "Control.h"
#include<wdfstatus.h>
#include "queue.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, ComPortMonitorQueueInitialize)
#endif

NTSTATUS
ComPortMonitorQueueInitialize(
    _In_ WDFDEVICE Device
    )
/*++

Routine Description:


     The I/O dispatch callbacks for the frameworks device object
     are configured in this function.

     A single default I/O Queue is configured for parallel request
     processing, and a driver context memory allocation is created
     to hold our structure QUEUE_CONTEXT.

Arguments:

    Device - Handle to a framework device object.

Return Value:

    VOID

--*/
{
    WDFQUEUE queue;
    WDF_IO_QUEUE_CONFIG queueConfig;

    PAGED_CODE();
    
    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

    queueConfig.EvtIoDeviceControl = ComPortMonitorEvtIoDeviceControl;
	queueConfig.EvtIoRead = ComPortMonitorEvtIoRead;
	queueConfig.EvtIoWrite = ComPortMonitorEvtIoWrite;
    //queueConfig.EvtIoStop = ComPortMonitorEvtIoStop;

    return WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
}

VOID ComPortMonitorEvtIoRead(
	_In_ WDFQUEUE   Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t     Length
)
{
	NTSTATUS status;
	WDFDEVICE device = WdfIoQueueGetDevice(Queue);
	UNREFERENCED_PARAMETER(Length);
	//
	// The following funciton essentially copies the content of
	// current stack location of the underlying IRP to the next one. 
	//
	WdfRequestFormatRequestUsingCurrentType(Request);

	WdfRequestSetCompletionRoutine(Request, ComPortMonitorCompletionRoutine, device);

	if (WdfRequestSend(Request, WdfDeviceGetIoTarget(device), WDF_NO_SEND_OPTIONS) == FALSE)
	{
		status = WdfRequestGetStatus(Request);
		KdPrint(("WdfRequestSend failed: 0x%x\n", status));
		WdfRequestComplete(Request, status);
	}
}

void ComPortMonitorCompletionRoutine(
	_In_ WDFREQUEST                     Request,
	_In_ WDFIOTARGET                    Target,
	_In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
	_In_ WDFCONTEXT                     Context
)
{
	NTSTATUS status;
	WDF_WORKITEM_CONFIG config;
	WDF_OBJECT_ATTRIBUTES attr;
	WDFWORKITEM work;
	PWORKITEM_CONTEXT context;
	PVOID buffer;
	WDF_REQUEST_PARAMETERS params;
	WDFMEMORY mem;
	UNREFERENCED_PARAMETER(Target);

	__try
	{
		if (!NT_SUCCESS(Params->IoStatus.Status) || Params->IoStatus.Information == 0)
			return;

		WDF_WORKITEM_CONFIG_INIT(&config, ComPortMonitorEvtWorkItem);
		WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, WORKITEM_CONTEXT);
		attr.ParentObject = WdfRequestGetFileObject(Request);
		status = WdfWorkItemCreate(&config, &attr, &work);
		if (!NT_SUCCESS(status))
			return;

		WDF_OBJECT_ATTRIBUTES_INIT(&attr);
		attr.ParentObject = work;
		context = WorkItemGetContext(work);
		status = WdfMemoryCreate(&attr, NonPagedPool, 0, Params->IoStatus.Information, &context->data, &buffer);
		if (!NT_SUCCESS(status))
		{
			WdfObjectDelete(work);
			return;
		}

		WdfRequestRetrieveOutputMemory(Request, &mem);
		WdfMemoryCopyToBuffer(mem, 0, buffer, Params->IoStatus.Information);
		context->Device = Context;
		context->BufferSize = (ULONG)Params->IoStatus.Information;
		context->OutputDataOffset = 0;
		WDF_REQUEST_PARAMETERS_INIT(&params);
		WdfRequestGetParameters(Request, &params);
		context->MajorFunctionCode = params.Type;
		context->MinorFunctionCode = params.MinorFunction;
		WdfWorkItemEnqueue(work);
	}
	__finally
	{
		WdfRequestComplete(Request, Params->IoStatus.Status);
	}
}

VOID ComPortMonitorEvtWorkItem(
	_In_ WDFWORKITEM WorkItem
)
{
	PWORKITEM_CONTEXT context;
	MEMORY_CONTEXT mem;

	context = WorkItemGetContext(WorkItem);
	mem.BufferSize = context->BufferSize;
	mem.MajorFunctionCode = context->MajorFunctionCode;
	mem.MinorFunctionCode = context->MinorFunctionCode;
	mem.OutputDataOffset = context->OutputDataOffset;
	ComPortMonitor_EvtNotifyListeners(context->Device, context->data, &mem);
}

VOID ComPortMonitorEvtIoWrite(
	_In_ WDFQUEUE   Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t     Length
)
{
	WDFMEMORY mem;
	MEMORY_CONTEXT context;
	WDF_REQUEST_PARAMETERS params;

	if (NT_SUCCESS(WdfRequestRetrieveInputMemory(Request, &mem)))
	{
		WDF_REQUEST_PARAMETERS_INIT(&params);
		WdfRequestGetParameters(Request, &params);
		memset(&context, 0, sizeof(context));
		context.BufferSize = (ULONG)Length;
		context.MajorFunctionCode = params.Type;
		context.MinorFunctionCode = params.MinorFunction;
		context.OutputDataOffset = (ULONG)Length;
		ComPortMonitor_EvtNotifyListeners(WdfIoQueueGetDevice(Queue), mem, &context);
	}
	ComPortMonitor_ForwardRequest(Request, WdfIoQueueGetDevice(Queue));
}

VOID
ComPortMonitorEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
/*++

Routine Description:

    This event is invoked when the framework receives IRP_MJ_DEVICE_CONTROL request.

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    OutputBufferLength - Size of the output buffer in bytes

    InputBufferLength - Size of the input buffer in bytes

    IoControlCode - I/O control code.

Return Value:

    VOID

--*/
{
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(IoControlCode);

	ComPortMonitor_ForwardRequest(Request, WdfIoQueueGetDevice(Queue));
}

VOID ComPortMonitor_EvtNotifyListeners(WDFDEVICE EventSource, WDFMEMORY Memory, PMEMORY_CONTEXT IrpInfo)
{
	NTSTATUS status;
	ULONG i, ci;
	PDEVICE_CONTEXT devContext;
	PFILEOBJECT_CONTEXT fileContext;
	WDFFILEOBJECT fileObject;
	PMEMORY_CONTEXT memContext;
	WDFREQUEST request;
	WDF_OBJECT_ATTRIBUTES attr;
	WDFMEMORY evtMemory, output;
	PVOID buffer;

	WdfWaitLockAcquire(ControlDeviceLock, NULL);
	__try
	{
		if (ControlDevice == NULL)
			return;

		devContext = DeviceGetContext(EventSource);
		WdfWaitLockAcquire(devContext->ListenersLock, NULL);
		__try
		{
			ci = WdfCollectionGetCount(devContext->Listeners);
			for (i = 0; i < ci; i++)
			{
				fileObject = WdfCollectionGetItem(devContext->Listeners, i);
				fileContext = FileObjectGetContext(fileObject);
				WdfWaitLockAcquire(fileContext->EventsLock, NULL);
				__try
				{
					WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, MEMORY_CONTEXT);
					attr.ParentObject = fileObject;
					status = WdfMemoryCreate(&attr, PagedPool, 0, IrpInfo->BufferSize == 0 ? 1 : IrpInfo->BufferSize, &evtMemory, &buffer);
					if (NT_SUCCESS(status))
					{
						memContext = MemoryGetContext(evtMemory);
						memcpy(memContext, IrpInfo, sizeof(*memContext));
						memContext->DeviceNumber = devContext->Number;
						WdfMemoryCopyToBuffer(Memory, 0, buffer, IrpInfo->BufferSize);
						status = WdfCollectionAdd(fileContext->Events, evtMemory);
						if (NT_SUCCESS(status))
						{
							WdfIoQueueRetrieveNextRequest(fileContext->Queue, &request);
							if (request != NULL)
							{
								status = WdfRequestRetrieveOutputMemory(request, &output);
								if (NT_SUCCESS(status))
								{
									status = WdfMemoryCopyFromBuffer(output, 0, memContext, sizeof(*memContext));
									if (NT_SUCCESS(status) && IrpInfo->BufferSize == 0)
									{
										WdfCollectionRemove(fileContext->Events, evtMemory);
										WdfObjectDelete(evtMemory);
									}
								}
								WdfRequestCompleteWithInformation(request, status, sizeof(*memContext));
							}
						}
						else
							WdfObjectDelete(evtMemory);
					}
				}
				__finally
				{
					WdfWaitLockRelease(fileContext->EventsLock);
				}
			}
		}
		__finally
		{
			WdfWaitLockRelease(devContext->ListenersLock);
		}
	}
	__finally
	{
		WdfWaitLockRelease(ControlDeviceLock);
	}
}

VOID ComPortMonitor_ForwardRequest(_In_ WDFREQUEST Request, _In_ WDFDEVICE Device)
/*++
Routine Description:

Passes a request on to the lower driver.

--*/
{
	WDF_REQUEST_SEND_OPTIONS options;
	BOOLEAN ret;
	NTSTATUS status;

	//
	// We are not interested in post processing the IRP so 
	// fire and forget.
	//
	WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

	ret = WdfRequestSend(Request, WdfDeviceGetIoTarget(Device), &options);

	if (ret == FALSE)
	{
		status = WdfRequestGetStatus(Request);
		KdPrint(("WdfRequestSend failed: 0x%x\n", status));
		WdfRequestComplete(Request, status);
	}

	return;
}

VOID
ComPortMonitorEvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags
)
/*++

Routine Description:

    This event is invoked for a power-managed queue before the device leaves the working state (D0).

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    ActionFlags - A bitwise OR of one or more WDF_REQUEST_STOP_ACTION_FLAGS-typed flags
                  that identify the reason that the callback function is being called
                  and whether the request is cancelable.

Return Value:

    VOID

--*/
{
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(ActionFlags);
    //
    // In most cases, the EvtIoStop callback function completes, cancels, or postpones
    // further processing of the I/O request.
    //
    // Typically, the driver uses the following rules:
    //
    // - If the driver owns the I/O request, it calls WdfRequestUnmarkCancelable
    //   (if the request is cancelable) and either calls WdfRequestStopAcknowledge
    //   with a Requeue value of TRUE, or it calls WdfRequestComplete with a
    //   completion status value of STATUS_SUCCESS or STATUS_CANCELLED.
    //
    //   Before it can call these methods safely, the driver must make sure that
    //   its implementation of EvtIoStop has exclusive access to the request.
    //
    //   In order to do that, the driver must synchronize access to the request
    //   to prevent other threads from manipulating the request concurrently.
    //   The synchronization method you choose will depend on your driver's design.
    //
    //   For example, if the request is held in a shared context, the EvtIoStop callback
    //   might acquire an internal driver lock, take the request from the shared context,
    //   and then release the lock. At this point, the EvtIoStop callback owns the request
    //   and can safely complete or requeue the request.
    //
    // - If the driver has forwarded the I/O request to an I/O target, it either calls
    //   WdfRequestCancelSentRequest to attempt to cancel the request, or it postpones
    //   further processing of the request and calls WdfRequestStopAcknowledge with
    //   a Requeue value of FALSE.
    //
    // A driver might choose to take no action in EvtIoStop for requests that are
    // guaranteed to complete in a small amount of time.
    //
    // In this case, the framework waits until the specified request is complete
    // before moving the device (or system) to a lower power state or removing the device.
    // Potentially, this inaction can prevent a system from entering its hibernation state
    // or another low system power state. In extreme cases, it can cause the system
    // to crash with bugcheck code 9F.
    //

    return;
}

