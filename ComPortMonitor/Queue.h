#include "Control.h"
/*++

Module Name:

    queue.h

Abstract:

    This file contains the queue definitions.

Environment:

    Kernel-mode Driver Framework

--*/

EXTERN_C_START

//
// This is the context that can be placed per queue
// and would contain per queue information.
//
typedef struct _QUEUE_CONTEXT {

    ULONG PrivateDeviceData;  // just a placeholder

} QUEUE_CONTEXT, *PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, QueueGetContext)

typedef struct _WORKITEM_CONTEXT
{
	WDFDEVICE Device;
	ULONG BufferSize;
	BYTE MajorFunctionCode;
	BYTE MinorFunctionCode;
	USHORT OutputDataOffset;
	WDFMEMORY data;

} WORKITEM_CONTEXT, *PWORKITEM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORKITEM_CONTEXT, WorkItemGetContext)

NTSTATUS
ComPortMonitorQueueInitialize(
    _In_ WDFDEVICE hDevice
    );

//
// Events from the IoQueue object
//
EVT_WDF_IO_QUEUE_IO_READ ComPortMonitorEvtIoRead;
EVT_WDF_REQUEST_COMPLETION_ROUTINE ComPortMonitorCompletionRoutine;
EVT_WDF_WORKITEM ComPortMonitorEvtWorkItem;
EVT_WDF_IO_QUEUE_IO_WRITE ComPortMonitorEvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ComPortMonitorEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP ComPortMonitorEvtIoStop;

VOID ComPortMonitor_EvtNotifyListeners(WDFDEVICE EventSource, WDFMEMORY Memory, PMEMORY_CONTEXT IrpInfo);
VOID ComPortMonitor_ForwardRequest(_In_ WDFREQUEST Request, _In_ WDFDEVICE Device);

EXTERN_C_END
