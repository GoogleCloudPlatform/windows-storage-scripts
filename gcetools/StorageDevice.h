#pragma once

#include <algorithm>
#include <errhandlingapi.h>
#include <exception>
#include <iostream>
#include <nvme.h>
#include <stdexcept>
#include <wtypes.h>
#include "NvmeVersion.h"

class StorageDevice {
public:
	StorageDevice(BSTR deviceNamespace) {
		hDevice_ = OpenDevice(deviceNamespace);
	}
	~StorageDevice() {
		CloseHandle(hDevice_);
	}
	STORAGE_BUS_TYPE GetBusType() {
		STORAGE_DEVICE_DESCRIPTOR* descriptor = GetStorageDeviceDescriptor();
		STORAGE_BUS_TYPE busType = descriptor->BusType;
		free(descriptor);
		return busType;
	}
	STORAGE_ADAPTER_DESCRIPTOR* GetStorageAdapterDescriptor() {
		return IssueIoctl<STORAGE_ADAPTER_DESCRIPTOR>(STORAGE_PROPERTY_ID::StorageAdapterProperty);
	}
	STORAGE_DEVICE_DESCRIPTOR* GetStorageDeviceDescriptor() {
		return IssueIoctl<STORAGE_DEVICE_DESCRIPTOR>(STORAGE_PROPERTY_ID::StorageDeviceProperty);
	}
	STORAGE_DEVICE_ID_DESCRIPTOR* GetStorageDeviceIdDescriptor() {
		return IssueIoctl<STORAGE_DEVICE_ID_DESCRIPTOR>(STORAGE_PROPERTY_ID::StorageDeviceIdProperty);
	}
	NVME_IDENTIFY_CONTROLLER_DATA GetNvmeControllerData() {
		return NvmeIdentify<NVME_IDENTIFY_CONTROLLER_DATA>(
			STORAGE_PROPERTY_ID::StorageAdapterProtocolSpecificProperty,
			NVME_IDENTIFY_CNS_CODES::NVME_IDENTIFY_CNS_CONTROLLER);
	}
	NVME_IDENTIFY_NAMESPACE_DATA GetNvmeNamespaceData(DWORD namespaceId) {
		return NvmeIdentify<NVME_IDENTIFY_NAMESPACE_DATA>(
			STORAGE_PROPERTY_ID::StorageAdapterProtocolSpecificProperty,
			NVME_IDENTIFY_CNS_CODES::NVME_IDENTIFY_CNS_SPECIFIC_NAMESPACE,
			namespaceId);
	}

protected:
	template<typename TResult>
	TResult* IssueIoctl(STORAGE_PROPERTY_ID propertyId) {
		return IssueIoctlHelper<TResult>(propertyId);
	}

	template <typename TResult, typename TAdditionalParameters>
	TResult* IssueIoctl(STORAGE_PROPERTY_ID propertyId, TAdditionalParameters* additionalParameters = nullptr) {
		return IssueIoctlHelper<TResult>(propertyId, additionalParameters, sizeof(TAdditionalParameters));
	}

	template<typename TResult>
	TResult NvmeIdentify(STORAGE_PROPERTY_ID propertyId, NVME_IDENTIFY_CNS_CODES identifyCode, DWORD subValue = 0) {
		STORAGE_PROTOCOL_SPECIFIC_DATA protocolSpecificData = {
			.ProtocolType = STORAGE_PROTOCOL_TYPE::ProtocolTypeNvme,
			.DataType = STORAGE_PROTOCOL_NVME_DATA_TYPE::NVMeDataTypeIdentify,
			.ProtocolDataRequestValue = (DWORD)identifyCode,
			.ProtocolDataRequestSubValue = subValue,
			.ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA),
			.ProtocolDataLength = NVME_MAX_LOG_SIZE
		};

		// The bufferSize is the sum of:
		// - the first N bytes of the STORAGE_PROPERTY_QUERY, up to but not
		//   including the AdditionalParameters field
		// - the size of AdditionalParameters, which varies but in this case
		//   contains STORAGE_PROTOCOL_SPECIFIC_DATA
		// - the maximum size of the response payload, NVME_MAX_LOG_SIZE
		/*size_t suggestedBufferSize = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters) +
			sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) +
			NVME_MAX_LOG_SIZE;*/

		STORAGE_PROTOCOL_DATA_DESCRIPTOR* result = IssueIoctl<STORAGE_PROTOCOL_DATA_DESCRIPTOR, STORAGE_PROTOCOL_SPECIFIC_DATA>(
			propertyId, &protocolSpecificData);
		
		size_t identifyDataOffset = FIELD_OFFSET(STORAGE_PROTOCOL_DATA_DESCRIPTOR, ProtocolSpecificData)
			+ result->ProtocolSpecificData.ProtocolDataOffset;
		
		TResult identifyData = *((TResult*)add(result, identifyDataOffset));

		free(result);

		return identifyData;
	}

private:
	HANDLE hDevice_;
	HANDLE OpenDevice(BSTR deviceNamespace) {
		HANDLE hDevice = CreateFile(deviceNamespace,
			/*dwDesiredAccess=*/ GENERIC_READ | GENERIC_WRITE,
			/*dwShareMode=*/ FILE_SHARE_READ,
			/*lpSecurityAttributes=*/ 0,
			/*dwCreationDisposition=*/ OPEN_EXISTING,
			/*dwFlagsAndAttributes=*/ 0,
			/*hTemplateFile=*/ 0);
		
		if (hDevice == INVALID_HANDLE_VALUE) {
			DWORD err = GetLastError();

			switch (err) {
			case ERROR_ACCESS_DENIED:
				std::cout << "Access denied while attempting to get device handle. Did you run as administrator?" << std::endl;
				break;
			default:
				std::cout << "Failed to get device handle. Error code: " << err << std::endl;
			}

			throw 1;
		}

		return hDevice;
	}

	template<typename TResult>
	TResult* IssueIoctlHelper(STORAGE_PROPERTY_ID propertyId, void* additionalParameters = nullptr, size_t additionalParametersSize = 0) {
		// Prepare the buffer
		size_t inputBufferSize = sizeof(STORAGE_PROPERTY_QUERY) + additionalParametersSize;
		size_t outputBufferSize = 0;

		// We may not always know the size of the query response, such as when it returns a list or a header with a variable sized payload as the next
		// contiguous byte. First, query for a STORAGE_DESCRIPTOR_HEADER (always the first bytes of all storage query responses) and check for what the
		// size of the response will be, then allocate an output buffer accordingly.
		if (std::is_same<TResult, STORAGE_DESCRIPTOR_HEADER>::value) {
			outputBufferSize = sizeof(STORAGE_DESCRIPTOR_HEADER);
		}
		else {
			STORAGE_DESCRIPTOR_HEADER* header = IssueIoctlHelper<STORAGE_DESCRIPTOR_HEADER>(propertyId, additionalParameters, additionalParametersSize);
			outputBufferSize = header->Size;
			free(header);
		}
		
		void* inputBuffer = malloc(inputBufferSize);
		void* outputBuffer = malloc(outputBufferSize);

		if (inputBuffer == nullptr) {
			throw std::bad_alloc("Failed to allocate an input buffer for the driver query.");
		}

		if (outputBuffer == nullptr) {
			throw std::bad_alloc("Failed to allocate an output buffer for the driver response.");
		}

		ZeroMemory(inputBuffer, inputBufferSize);
		ZeroMemory(outputBuffer, outputBufferSize);

		// Prepare the query
		PSTORAGE_PROPERTY_QUERY query = (PSTORAGE_PROPERTY_QUERY)inputBuffer;
		query->PropertyId = propertyId;
		query->QueryType = STORAGE_QUERY_TYPE::PropertyStandardQuery;

		if (additionalParameters != nullptr && additionalParametersSize > 0) {
			ULONG additionalParametersOffset = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters);
			memcpy(add(query, additionalParametersOffset), additionalParameters, additionalParametersSize);
		}

		DWORD written = 0;
		BOOL ok = DeviceIoControl(hDevice_,
			/*dwIoControlCode=*/ IOCTL_STORAGE_QUERY_PROPERTY,
			/*lpInBuffer=*/ inputBuffer,
			/*nInBufferSize=*/ inputBufferSize,
			/*lpOutBuffer=*/ outputBuffer,
			/*nOutBufferSize=*/ outputBufferSize,
			/*lpBytesReturned=*/ &written,
			/*lpOverlapped=*/ nullptr);

		if (!ok) {
			throw std::system_error("Driver query returned a not ok reponse.");
		}

		return (TResult*)outputBuffer;
	}

	void* add(void* ptr, ULONG offset) {
		return ((char*)ptr) + offset;
	}
};