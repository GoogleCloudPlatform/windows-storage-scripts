#include <exception>
#include <wtypes.h>
#include <stdexcept>
#include <nvme.h>
#include <iostream>

class StorageDevice {
public:
	StorageDevice(BSTR deviceNamespace):deviceNamespace_(deviceNamespace) {
		hDevice_ = OpenDevice(deviceNamespace);
	}
	~StorageDevice() {
		CloseHandle(hDevice_);
	}
	STORAGE_BUS_TYPE GetBusType() {
		STORAGE_DEVICE_DESCRIPTOR descriptor = GetStorageDeviceDescriptor();
		return descriptor.BusType;
	}
	STORAGE_DEVICE_DESCRIPTOR GetStorageDeviceDescriptor() {

		return IssueIoctl(STORAGE_PROPERTY_ID::StorageDeviceProperty,
			/*bufferSize=*/ 1024,
			)

		// STORAGE_PROTOCOL_SPECIFIC_DATA forms the AdditionalParameters field of
		// the STORAGE_PROPERTY_QUERY, so we find where AdditionalParameters is in
		// order to write protocolSpecificData to memory at that address.
		ULONG additionalParametersOffset = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters);

		// The bufferSize is the sum of:
		// - the first N bytes of the STORAGE_PROPERTY_QUERY, up to but not
		//   including the AdditionalParameters field
		// - the size of AdditionalParameters, which varies but in this case
		//   contains STORAGE_PROTOCOL_SPECIFIC_DATA
		// - the maximum size of the response payload, NVME_MAX_LOG_SIZE
		int bufferSize = additionalParametersOffset
			+ sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)
			+ NVME_MAX_LOG_SIZE;

		PSTORAGE_PROPERTY_QUERY query = NULL;
		PSTORAGE_PROTOCOL_SPECIFIC_DATA protocolData = NULL;
		PSTORAGE_PROTOCOL_DATA_DESCRIPTOR protocolDataDescr = NULL;

	}
protected:
	const BSTR deviceNamespace_;
private:
	HANDLE hDevice_;
	HANDLE OpenDevice(BSTR deviceNamespace) {
		HANDLE hDevice = CreateFile(deviceNamespace_,
			/*dwDesiredAccess=*/ GENERIC_READ | GENERIC_WRITE,
			/*dwShareMode=*/ FILE_SHARE_READ,
			/*lpSecurityAttributes=*/ 0,
			/*dwCreationDisposition=*/ OPEN_EXISTING,
			/*dwFlagsAndAttributes=*/ 0,
			/*hTemplateFile=*/ 0);
		
		if (hDevice == INVALID_HANDLE_VALUE) {
			std::cout << "Failed to get device handle, exiting." << std::endl;
			throw 1;
		}

		return hDevice;
	}
	void IssueIoctl(STORAGE_PROPERTY_ID propertyId, size_t bufferSize, PVOID additionalParameters, size_t addParamSize) {
		// Prepare the buffer
		PVOID buffer = NULL;

		buffer = malloc(bufferSize);

		if (buffer == NULL) {
			std::cout << "Failed to allocate a buffer, exiting." << std::endl;
			throw 2;
		}

		ZeroMemory(buffer, bufferSize);

		// Prepare the query
		PSTORAGE_PROPERTY_QUERY query = (PSTORAGE_PROPERTY_QUERY)buffer;
		query->PropertyId = propertyId;
		query->QueryType = STORAGE_QUERY_TYPE::PropertyStandardQuery;

		if (additionalParameters != NULL) {
			ULONG additionalParametersOffset = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters);
			memcpy(query + additionalParametersOffset, additionalParameters, addParamSize);
		}

		DWORD written = 0;
		BOOL ok = DeviceIoControl(hDevice_,
			/*dwIoControlCode=*/ IOCTL_STORAGE_QUERY_PROPERTY,
			/*lpInBuffer=*/ buffer,
			/*nInBufferSize=*/ bufferSize,
			/*lpOutBuffer=*/ buffer,
			/*nOutBufferSize=*/ bufferSize,
			/*lpBytesReturned=*/ &written,
			/*lpOverlapped=*/ 0);

		if (!ok) {
			std::cout << "DeviceIoControl failed, exiting." << std::endl;
			throw 3;
		}


	}
};