#include "stdafx.h"
#include "DX12Util.h"

#define NOMINMAX
#include <algorithm>
#include <cmath>

//using Microsoft::WRL::ComPtr;

ComPtr<ID3D12Resource> DX12Util::CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	UINT64 byteSize,
	ComPtr<ID3D12Resource>& uploadBuffer)
{
	ComPtr<ID3D12Resource> defaultBuffer;

	// Create the actual default buffer resource.
	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

	// In order to copy CPU memory data into our default buffer, we need to create
	// an intermediate upload heap. 
 	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


	// Describe the data we want to copy into the default buffer.
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	// Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
	// will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
	// the intermediate upload heap data will be copied to mBuffer.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
	UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

	// Note: uploadBuffer has to be kept alive after the above function calls because
	// the command list has not been executed yet that performs the actual copy.
	// The caller can Release the uploadBuffer after it knows the copy has been executed.
	return defaultBuffer;
}

std::shared_ptr<Texture> DX12Util::loadTexture(
	ComPtr<ID3D12Device> device,
	std::string & textureName,
	std::string & mtlPath,
	unsigned char *(loadimage)(char const *, int *, int *, int *, int))
{
	// Load the image file.
	std::unique_ptr<Texture> texture = std::make_unique<Texture>();
	texture->name = textureName;

	char filepath[1000];

	int subdir = textureName.rfind("\\");
	if (subdir > 0) {
		textureName = textureName.substr(subdir + 1);
	}

	std::string str = mtlPath + textureName;
	strcpy_s(filepath, str.c_str());

	int width, height, channels;
	unsigned char* data = loadimage(filepath, &width, &height, &channels, 4);

	// Calculate the number of mip maps required for the given texture as a function of its size
	texture->numMipMaps = 1 + std::floor(log2(max(width, height)));

	// Describe and create a Texture2D.
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = texture->numMipMaps; //1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	texture->desc = textureDesc;

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&texture->Resource)));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = data;
	textureData.RowPitch = width * 4;
	textureData.SlicePitch = textureData.RowPitch * height;

	// Setup texture struct
	texture->width = width;
	texture->height = height;
	texture->data = textureData;

	return texture;
}

void DX12Util::initTextures(
	ComPtr<ID3D12Device> device,
	ResourceUploadBatch & resourceUploader,
	ComPtr<ID3D12DescriptorHeap> srvHeap,
	UINT srvDescriptorSize,
	std::shared_ptr<Texture> & texture,
	UINT nextSrvHeapIndex)
{	
	// Upload base texture.
	resourceUploader.Upload(
		texture->Resource.Get(),
		0,
		&texture->data,
		1
	);

	// Create mip maps.
	resourceUploader.Transition(
		texture->Resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	resourceUploader.GenerateMips(texture->Resource.Get());

	// Assign SRV location in heap to texture for later look up.
	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHeapHandle(srvHeap->GetCPUDescriptorHandleForHeapStart(), nextSrvHeapIndex, srvDescriptorSize);
	texture->srvHeapIndex = nextSrvHeapIndex;
	nextSrvHeapIndex++;

	// Describe and create a SRV for the texture (and mip maps).
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = texture->desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	device->CreateShaderResourceView(texture->Resource.Get(), &srvDesc, srvHeapHandle);
}
