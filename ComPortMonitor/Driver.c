/*++

Module Name:

    driver.c

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "control.h"
#include "driver.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, ComPortMonitorEvtDeviceAdd)
#pragma alloc_text (PAGE, ComPortMonitorEvtDriverContextCleanup)
#endif

WDFCOLLECTION FilteringDevices = NULL;
WDFWAITLOCK FilteringDevicesLock = NULL;
ULONG NextDeviceNumber = 0;

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
	WDFDRIVER drv;

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = ComPortMonitorEvtDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, ComPortMonitorEvtDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, &attributes, &config, &drv);
	if (!NT_SUCCESS(status))
		return status;

	status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &FilteringDevices);
	if (!NT_SUCCESS(status))
		return status;

	status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &FilteringDevicesLock);
	if (!NT_SUCCESS(status))
		return status;

	return WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &ControlDeviceLock);
}

NTSTATUS
ComPortMonitorEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a device object to
    represent a new instance of the device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
	WDFMEMORY mem;
	PVOID buf;
	UNICODE_STRING str;
	BOOLEAN flag = FALSE;
	ULONG length;
	DECLARE_CONST_UNICODE_STRING(HardwareID, HARDWARE_ID);

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

	status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES, NonPagedPool, 0, 1024, &mem, &buf);
	if (NT_SUCCESS(status))
	{
		status = WdfFdoInitQueryProperty(DeviceInit, DevicePropertyHardwareID, 1024, buf, &length);
		if (NT_SUCCESS(status))
		{
			RtlInitUnicodeString(&str, buf);
			flag = RtlEqualUnicodeString(&HardwareID, &str, FALSE);
		}
		WdfObjectDelete(mem);
	}
	if (flag)
	{
		WdfWaitLockAcquire(ControlDeviceLock, NULL);
		if (ControlDevice == NULL)
			status = CreateControlDevice(Driver, DeviceInit);
		WdfWaitLockRelease(ControlDeviceLock);
	}
	else
		status = ComPortMonitorCreateDevice(DeviceInit);

    return status;
}

VOID
ComPortMonitorEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
/*++
Routine Description:

    Free all the resources allocated in DriverEntry.

Arguments:

    DriverObject - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE ();
}
