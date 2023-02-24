#pragma once

#include <cstddef>

struct NvmeV1BasedScsiNameString {
	std::byte PciVendorId[4];
	std::byte ModelNumber[40];
	std::byte NamespaceId[4];
	std::byte SerialNumber[20];
};