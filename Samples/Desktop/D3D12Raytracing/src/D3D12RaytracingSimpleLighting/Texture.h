#pragma once

#include <string>

#include "DXSampleHelper.h"

struct Texture
{
	std::string name;
	UINT width;
	UINT height;
	UINT numMipMaps = 0;

	D3D12_RESOURCE_DESC desc;
	D3D12_SUBRESOURCE_DATA data;

	UINT srvHeapIndex = 0;

	ComPtr<ID3D12Resource> Resource = nullptr;
	ComPtr<ID3D12Resource> UploadHeap = nullptr;
};
