/* Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "NvmeV1BasedScsiNameString.h"
#include "StorageDevice.h"

class GoogleStorageDevice : public StorageDevice {
public:
	GoogleStorageDevice(BSTR deviceNamespace) : StorageDevice(deviceNamespace)
	{
	}
	~GoogleStorageDevice()
	{
	}

	std::string GetDeviceName() {
		STORAGE_BUS_TYPE busType = GetBusType();

		switch (busType) {
		case STORAGE_BUS_TYPE::BusTypeNvme:
			return GetNvmeDeviceName();
		case STORAGE_BUS_TYPE::BusTypeScsi:
			return GetScsiDeviceName();
		default:
			std::cout << "Unsupported device type. Device must be attached as an NVMe or SCSI device." << std::endl;
			throw 9;
		}
	}

private:
	std::string GetNvmeDeviceName() {
		// Google writes metadata about the disk as a JSON string to the start
		// of the vendor-specific (VS) section of the NVMe Identify Specific
		// Namespace response struct.
		// 
		// We do a simple search for the field we care about to avoid taking a
		// dependency on a JSON library. The JSON string (and VS field) is
		// assumed to have the following properties:
		// - The VS field is null terminated.
		// - The JSON is 'packed', (i.e., no spaces or newlines between the
		//   key-value pairs, colons, brackets, and curly braces)
		int namespaceId = GetNvmeNamespaceId();
		NVME_IDENTIFY_NAMESPACE_DATA namespaceData = GetNvmeNamespaceData(namespaceId);

		std::string vendorSpecificData((char*)(namespaceData.VS));
		std::string deviceNameKey("\"device_name\":\"");
		size_t deviceNameStart = vendorSpecificData.find(deviceNameKey, 0);
		size_t deviceNameEnd = vendorSpecificData.find("\"", deviceNameStart + deviceNameKey.size());
		return vendorSpecificData.substr(deviceNameStart + deviceNameKey.size(), deviceNameEnd - (deviceNameStart + deviceNameKey.size()));
	}

	int GetNvmeNamespaceId() {
		STORAGE_DEVICE_ID_DESCRIPTOR* deviceIdDescriptor = GetStorageDeviceIdDescriptor();
		STORAGE_IDENTIFIER* firstIdentifier = (STORAGE_IDENTIFIER*)deviceIdDescriptor->Identifiers;
		NvmeV1BasedScsiNameString* scsiNameString = (NvmeV1BasedScsiNameString*)firstIdentifier->Identifier;
		int namespaceId = std::atoi((char*)scsiNameString->NamespaceId);
		return namespaceId;
	}

	std::string GetScsiDeviceName() {
		const std::string googleScsiPrefix("Google  ");
		STORAGE_DEVICE_ID_DESCRIPTOR* deviceIdDescriptor = GetStorageDeviceIdDescriptor();
		STORAGE_IDENTIFIER* storageIdentifier = (STORAGE_IDENTIFIER*)deviceIdDescriptor->Identifiers;

		for (DWORD i = 0; i < deviceIdDescriptor->NumberOfIdentifiers; i++) {
			if (storageIdentifier->Type == STORAGE_IDENTIFIER_TYPE::StorageIdTypeVendorId &&
				storageIdentifier->Association == STORAGE_ASSOCIATION_TYPE::StorageIdAssocDevice) {
				std::string identifier((char*)storageIdentifier->Identifier);
				if (identifier.starts_with(googleScsiPrefix)) {
					return identifier.substr(googleScsiPrefix.size());
				}
			}
		}

		std::cout << "Device is not a Google Persistent Disk." << std::endl;
		return std::string("");
	}
};
