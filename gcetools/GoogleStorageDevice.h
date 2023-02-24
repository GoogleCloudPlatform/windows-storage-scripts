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
		std::string vendorSpecificData((char*)GetNvmeNamespaceData(GetNvmeNamespaceId()).VS);
		size_t deviceNameStart = vendorSpecificData.find("\"device_name\":\"", 0);
		size_t deviceNameEnd = vendorSpecificData.find("\"", deviceNameStart);
		return vendorSpecificData.substr(deviceNameStart, deviceNameEnd - deviceNameStart);
	}

	int GetNvmeNamespaceId() {
		NVME_IDENTIFY_CONTROLLER_DATA controller = GetNvmeControllerData();

		if (controller.VER < NvmeVersion::Version_1_0) {
			std::cout << "NVMe versions prior to v1.0 are not supported by this application. The drive reported NVMe Version " << controller.VER << std::endl;
			throw 10;
		}
		else if (controller.VER == NvmeVersion::Version_1_0) {
			STORAGE_DEVICE_ID_DESCRIPTOR* deviceIdDescriptor = GetStorageDeviceIdDescriptor();
			STORAGE_IDENTIFIER* firstIdentifier = (STORAGE_IDENTIFIER*)deviceIdDescriptor->Identifiers;
			NvmeV1BasedScsiNameString* scsiNameString = (NvmeV1BasedScsiNameString*)firstIdentifier->Identifier;
			int namespaceId = std::atoi((char*)scsiNameString->NamespaceId);
			return namespaceId;
		}
		else {
			// When the NVMe Version is greater than 1.0, we would expect this
			// to NOT be an NvmeV1BasedScsiNameString due to the StorNVMe
			// driver on Windows Builds 1903 or later complying with SCSI-NVMe
			// Translation Layer (SNTL) Reference Revision 1.5. However, for
			// all builds, both before and after Build 1903, we are observing
			// a return value with the shape of an NvmeV1BasedScsiNameString.
			//
			// No documentation on StorNVMe behavior on builds prior to 1903
			// have been found and behavior has been detected experimentally.
			//
			// For Windows Builds 1903 or later, once StorNVMe starts to live
			// up to the version of the specification it claims to comply with,
			// this will need to be expanded to detect and support other
			// identifier types in SNTL Reference Revision 1.5, including:
			// - 6.1.4.1 NAA IEEE Registered Extended designator format
			// - 6.1.4.3 T10 Vendor ID based designator format
			// - 6.1.4.5 EUI-64 designator format
			STORAGE_DEVICE_ID_DESCRIPTOR* deviceIdDescriptor = GetStorageDeviceIdDescriptor();
			STORAGE_IDENTIFIER* firstIdentifier = (STORAGE_IDENTIFIER*)deviceIdDescriptor->Identifiers;
			NvmeV1BasedScsiNameString* scsiNameString = (NvmeV1BasedScsiNameString*)firstIdentifier->Identifier;
			int namespaceId = std::atoi((char*)scsiNameString->NamespaceId);
			return namespaceId;
		} 
	}

	std::string GetScsiDeviceName() {
		const std::string googleScsiPrefix("Google  ");
		STORAGE_DEVICE_ID_DESCRIPTOR* deviceIdDescriptor = GetStorageDeviceIdDescriptor();
		STORAGE_IDENTIFIER* storageIdentifier = (STORAGE_IDENTIFIER*)deviceIdDescriptor->Identifiers;

		for (int i = 0; i < deviceIdDescriptor->NumberOfIdentifiers; i++) {
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