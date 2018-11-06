#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#define NOMINMAX
#include <algorithm>
#include "DXSampleHelper.h"
#include "Texture.h"

#include "ResourceUploadBatch.h"

using namespace DirectX;

class DX12Util
{
public:
	static ComPtr<ID3D12Resource> DX12Util::CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& uploadBuffer);

	static std::shared_ptr<Texture> loadTexture(
		ID3D12Device *device,
		std::string & textureName,
		std::string & mtlPath,
		unsigned char *(loadimage)(char const *, int *, int *, int *, int));
	
	/*static void initTexture(
		ID3D12Device *device,
		ResourceUploadBatch & resourceUploader,
		ComPtr<ID3D12DescriptorHeap> srvHeap,
		UINT srvDescriptorSize,
		std::shared_ptr<Texture> & texture,
		UINT nextSrvHeapIndex);*/

private:
	static const UINT FrameCount = 2;
	static const UINT TextureWidth = 256;
	static const UINT TextureHeight = 256;
	static const UINT TexturePixelSize = 4;
};

