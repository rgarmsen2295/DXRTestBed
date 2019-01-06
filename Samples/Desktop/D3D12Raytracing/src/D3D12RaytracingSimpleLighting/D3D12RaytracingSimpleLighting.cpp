//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12RaytracingSimpleLighting.h"
#include "DirectXRaytracingHelper.h"
#include "CompiledShaders\Raytracing.hlsl.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;
using namespace DX;

const wchar_t* D3D12RaytracingSimpleLighting::c_hitGroupNames_TriangleGeometry[] =
{
	L"MyHitGroup_Triangle", L"MyHitGroup_Triangle_ShadowRay"
};
const wchar_t* D3D12RaytracingSimpleLighting::c_hitGroupNames_AABBGeometry[] =
{
	L"MyHitGroup_AABB", L"MyHitGroup_AABB_ShadowRay"
};
const wchar_t* D3D12RaytracingSimpleLighting::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* D3D12RaytracingSimpleLighting::c_triangleClosestHitShaderName = L"TriangleClosestHitShader";
const wchar_t* D3D12RaytracingSimpleLighting::c_aabbClosestHitShaderName = L"AABBClosestHitShader";
const wchar_t* D3D12RaytracingSimpleLighting::c_sphereIntersectionShaderName = L"SphereIntersectionShader";
const wchar_t* D3D12RaytracingSimpleLighting::c_missShaderNames[] =
{
	L"MyMissShader", L"MyMissShader_ShadowRay"
};

D3D12RaytracingSimpleLighting::D3D12RaytracingSimpleLighting(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_raytracingOutputResourceUAVDescriptorHeapIndex(UINT_MAX),
    m_curRotationAngleRad(0.0f),
    m_isDxrSupported(false)
{
    m_forceComputeFallback = false;
    SelectRaytracingAPI(RaytracingAPI::FallbackLayer);
    UpdateForSizeChange(width, height);
}

void D3D12RaytracingSimpleLighting::EnableDirectXRaytracing(IDXGIAdapter1* adapter)
{
    // Fallback Layer uses an experimental feature and needs to be enabled before creating a D3D12 device.
    bool isFallbackSupported = EnableComputeRaytracingFallback(adapter);

    if (!isFallbackSupported)
    {
        OutputDebugString(
            L"Warning: Could not enable Compute Raytracing Fallback (D3D12EnableExperimentalFeatures() failed).\n" \
            L"         Possible reasons: your OS is not in developer mode.\n\n");
    }

    m_isDxrSupported = IsDirectXRaytracingSupported(adapter);

    if (!m_isDxrSupported)
    {
        OutputDebugString(L"Warning: DirectX Raytracing is not supported by your GPU and driver.\n\n");

        ThrowIfFalse(isFallbackSupported, 
            L"Could not enable compute based fallback raytracing support (D3D12EnableExperimentalFeatures() failed).\n"\
            L"Possible reasons: your OS is not in developer mode.\n\n");
        m_raytracingAPI = RaytracingAPI::FallbackLayer;
    }
	else
	{
		m_raytracingAPI = RaytracingAPI::DirectXRaytracing;
	}
}

void D3D12RaytracingSimpleLighting::OnInit()
{
    m_deviceResources = std::make_unique<DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        FrameCount,
        D3D_FEATURE_LEVEL_12_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the Fallback Layer requires Fall Creator's update (RS3), we don't need to handle non-tearing cases.
        DeviceResources::c_RequireTearingSupport,
        m_adapterIDoverride
        );
    m_deviceResources->RegisterDeviceNotify(this);
    m_deviceResources->SetWindow(Win32Application::GetHwnd(), m_width, m_height);
    m_deviceResources->InitializeDXGIAdapter();
    EnableDirectXRaytracing(m_deviceResources->GetAdapter());

    m_deviceResources->CreateDeviceResources();
    m_deviceResources->CreateWindowSizeDependentResources();

    InitializeScene();

    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

// Update camera matrices passed into the shader.
void D3D12RaytracingSimpleLighting::UpdateCameraMatrices()
{
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

	m_sceneCB[frameIndex].cameraPosition = m_camera.getEye();//m_eye;
    float fovAngleY = 45.0f;
    XMMATRIX view = XMMatrixLookAtLH(m_camera.getEye(), m_camera.getTarget(), m_camera.getUp());
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovAngleY), m_aspectRatio, 1.0f, 125.0f);
    XMMATRIX viewProj = view * proj;

    m_sceneCB[frameIndex].projectionToWorld = XMMatrixInverse(nullptr, viewProj);
}

// Initialize scene rendering parameters.
void D3D12RaytracingSimpleLighting::InitializeScene()
{
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

    // Setup materials.
    {
        m_cubeCB.albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Setup camera.
    {
		m_camera.setScreenInfo(m_width, m_height);
		m_camera.setEye({ 0.0f, 0.0f, -5.0f, 1.0f });
		m_camera.setLookAt({ 0.0f, 0.0f, 0.0f, 1.0f });

		XMVECTOR right = { 1.0f, 0.0f, 0.0f, 0.0f };
		XMVECTOR direction = XMVector4Normalize(m_camera.getLookAt() - m_camera.getEye());
		m_camera.setUp(XMVector3Normalize(XMVector3Cross(direction, right)));

        // Initialize the view and projection inverse matrices.
        m_eye = { 0.0f, 2.0f, -5.0f, 1.0f };
        m_at = { 0.0f, 0.0f, 0.0f, 1.0f };
        //XMVECTOR right = { 1.0f, 0.0f, 0.0f, 0.0f };

        //XMVECTOR direction = XMVector4Normalize(m_at - m_eye);
        m_up = XMVector3Normalize(XMVector3Cross(direction, right));

        // Rotate camera around Y axis.
        XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(45.0f));
        m_eye = XMVector3Transform(m_eye, rotate);
        m_up = XMVector3Transform(m_up, rotate);
        
        UpdateCameraMatrices();
    }

    // Setup lights.
    {
        // Initialize the lighting parameters.
        XMFLOAT4 lightPosition;
        XMFLOAT4 lightAmbientColor;
        XMFLOAT4 lightDiffuseColor;

        lightPosition = XMFLOAT4(0.0f, 1.8f, 0.0f/*-3.0f*/, 0.0f);
        m_sceneCB[frameIndex].lightPosition = XMLoadFloat4(&lightPosition);

        lightAmbientColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
        m_sceneCB[frameIndex].lightAmbientColor = XMLoadFloat4(&lightAmbientColor);

        lightDiffuseColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        m_sceneCB[frameIndex].lightDiffuseColor = XMLoadFloat4(&lightDiffuseColor);

		m_sceneCB[frameIndex].useGlobalIllumination = 0;
		m_sceneCB[frameIndex].useNormalTexturing = 0;

		m_sceneCB[frameIndex].numGISamples = 2;

		m_sceneCB[frameIndex].showCharacterGISpheres = false;
    }

    // Apply the initial values to all frames' buffer instances.
    for (auto& sceneCB : m_sceneCB)
    {
        sceneCB = m_sceneCB[frameIndex];
    }

	XMFLOAT3 initialCharacterPosition = XMFLOAT3(1.0f, -0.6025f, -0.15f);
	m_characterPosition = XMLoadFloat3(&initialCharacterPosition);
}

// Create constant buffers.
void D3D12RaytracingSimpleLighting::CreateConstantBuffers()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto frameCount = m_deviceResources->GetBackBufferCount();
    
    // Create the constant buffer memory and map the CPU and GPU addresses
    const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    // Allocate one constant buffer per frame, since it gets updated every frame.
    size_t cbSize = frameCount * sizeof(AlignedSceneConstantBuffer);
    const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &constantBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_perFrameConstants)));

    // Map the constant buffer and cache its heap pointers.
    // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(m_perFrameConstants->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData)));
}

UINT D3D12RaytracingSimpleLighting::UploadTexture(ID3D12Device *device, ResourceUploadBatch &resourceUploader, std::shared_ptr<Texture> &texture)
{
	// Upload base texture via DirectX toolkit helper library.
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
	resourceUploader.Transition(
		texture->Resource.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptorHandle;
	UINT descriptorIndex = AllocateDescriptor(&srvDescriptorHandle);
	texture->srvHeapIndex = descriptorIndex;

	// Describe and create a SRV for the texture (and mip maps).
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = texture->desc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	device->CreateShaderResourceView(texture->Resource.Get(), &srvDesc, srvDescriptorHandle);

	return descriptorIndex;

	// Also tried a manual upload process (not using the toolkit and no mipmaps).
	{
		//auto cmdList = m_deviceResources->GetCommandList();// Reset the command list for the acceleration structure construction.
		//auto commandAllocator = m_deviceResources->GetCommandAllocator();

		//cmdList->Reset(commandAllocator, nullptr);

		//const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture->Resource.Get(), 0, 1);

		//// Describe the resource
		//D3D12_RESOURCE_DESC resourceDesc = {};
		//resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		//resourceDesc.Alignment = 0;
		//resourceDesc.Width = uploadBufferSize;
		//resourceDesc.Height = 1;
		//resourceDesc.DepthOrArraySize = 1;
		//resourceDesc.MipLevels = 1;
		//resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		//resourceDesc.SampleDesc.Count = 1;
		//resourceDesc.SampleDesc.Quality = 0;
		//resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		//resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		//// Create the upload heap
		//ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(texture->UploadHeap.GetAddressOf())));

		//// Schedule a copy from the upload heap to the Texture2D resource
		//UpdateSubresources(cmdList, texture->Resource.Get(), texture->UploadHeap.Get(), 0, 0, 1, &texture->data);
		////UpdateSubresources<1>(commandList, texture->Resource.Get(), texture->UploadHeap.Get(), 0, 0, 1, &subResourceData);

		//// Transition the texture to a shader resource
		//D3D12_RESOURCE_BARRIER barrier = {};
		//barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		//barrier.Transition.pResource = texture->Resource.Get();
		//barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		//barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		//barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		//barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

		//cmdList->ResourceBarrier(1, &barrier);

		//// Kick off acceleration structure construction.
		//m_deviceResources->ExecuteCommandList();

		//// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
		//m_deviceResources->WaitForGpu();
	}
}

void D3D12RaytracingSimpleLighting::LoadTextures()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandQueue = m_deviceResources->GetCommandQueue();

	// Flip images loaded by stbi to match DX12 expected orientation.
	stbi_set_flip_vertically_on_load(true);

	// Load image file.
	std::string sphereTexName = "earth_big.jpg"; // Random image from sponza for now.
	std::string mtlPath = "resources/";

	// Upload texture to GPU (and create things like mip-maps).
	ResourceUploadBatch resourceUploader(device);
	resourceUploader.Begin();

	// Load Sponza Textures.
	{
		// Load from file.
		auto & shapeSponza = m_sponza;
		for (auto & material : shapeSponza->getMaterials()) {
			if (material.diffuseTex != "") {
				if (shapeSponza->m_diffuseTextures.find(material.diffuseTex) == shapeSponza->m_diffuseTextures.end()) {
					shapeSponza->m_diffuseTextures.insert(
						std::pair<std::string, std::shared_ptr<Texture>>(
							material.diffuseTex,
							DX12Util::loadTexture(device, material.diffuseTex, mtlPath + "textures/", stbi_load)));
				}
			}
			/*if (material.specularTex != "") {
				if (m_specularTextures.find(material.specularTex) == m_specularTextures.end()) {
					m_specularTextures.insert(
						std::pair<std::string, std::shared_ptr<Texture>>(
							material.specularTex,
							DX12Util::loadTexture(m_device, material.specularTex, resourcePath + "textures/", stbi_load)));
				}
			}*/
			if (material.normalTex != "") {
				if (shapeSponza->m_normalTextures.find(material.normalTex) == shapeSponza->m_normalTextures.end()) {
					shapeSponza->m_normalTextures.insert(
						std::pair<std::string, std::shared_ptr<Texture>>(
							material.normalTex,
							DX12Util::loadTexture(device, material.normalTex, mtlPath + "textures/", stbi_load)));
				}
			}
		}

		auto & shapeCharacter = m_character;
		for (auto & material : shapeCharacter->getMaterials()) {
			if (material.diffuseTex != "") {
				if (shapeCharacter->m_diffuseTextures.find(material.diffuseTex) == shapeCharacter->m_diffuseTextures.end()) {
					shapeCharacter->m_diffuseTextures.insert(
						std::pair<std::string, std::shared_ptr<Texture>>(
							material.diffuseTex,
							DX12Util::loadTexture(device, material.diffuseTex, mtlPath + "textures/", stbi_load)));
				}
			}
			/*if (material.specularTex != "") {
				if (m_specularTextures.find(material.specularTex) == m_specularTextures.end()) {
					m_specularTextures.insert(
						std::pair<std::string, std::shared_ptr<Texture>>(
							material.specularTex,
							DX12Util::loadTexture(m_device, material.specularTex, resourcePath + "textures/", stbi_load)));
				}
			}*/
			if (material.normalTex != "") {
				if (shapeCharacter->m_normalTextures.find(material.normalTex) == shapeCharacter->m_normalTextures.end()) {
					shapeCharacter->m_normalTextures.insert(
						std::pair<std::string, std::shared_ptr<Texture>>(
							material.normalTex,
							DX12Util::loadTexture(device, material.normalTex, mtlPath + "textures/", stbi_load)));
				}
			}
		}

		// Upload to GPU.
		for (auto & texturePair : shapeSponza->m_diffuseTextures) {
			std::shared_ptr<Texture> texture = texturePair.second;

			// Upload texture.
			UINT diffuseTextureHeapIndex = UploadTexture(device, resourceUploader, texture);

			texture->gpuHandle =
				CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), diffuseTextureHeapIndex, m_descriptorSize);
		}
		/*for (auto & texturePair : m_specularTextures) {
			std::shared_ptr<Texture> texture = texturePair.second;
			DX12Util::initTextures(m_device, resourceUploader, m_srvHeap, m_srvDescriptorSize, texture, m_nextSrvHeapIndex);
			m_nextSrvHeapIndex++;
		}*/
		for (auto & texturePair : shapeSponza->m_normalTextures) {
			std::shared_ptr<Texture> texture = texturePair.second;

			// Upload texture.
			UINT normalTextureHeapIndex = UploadTexture(device, resourceUploader, texture);

			texture->gpuHandle =
				CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), normalTextureHeapIndex, m_descriptorSize);
		}

		for (auto & texturePair : shapeCharacter->m_diffuseTextures) {
			std::shared_ptr<Texture> texture = texturePair.second;

			// Upload texture.
			UINT diffuseTextureHeapIndex = UploadTexture(device, resourceUploader, texture);

			texture->gpuHandle =
				CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), diffuseTextureHeapIndex, m_descriptorSize);
		}
		/*for (auto & texturePair : m_specularTextures) {
			std::shared_ptr<Texture> texture = texturePair.second;
			DX12Util::initTextures(m_device, resourceUploader, m_srvHeap, m_srvDescriptorSize, texture, m_nextSrvHeapIndex);
			m_nextSrvHeapIndex++;
		}*/
		for (auto & texturePair : shapeCharacter->m_normalTextures) {
			std::shared_ptr<Texture> texture = texturePair.second;

			// Upload texture.
			UINT normalTextureHeapIndex = UploadTexture(device, resourceUploader, texture);

			texture->gpuHandle =
				CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), normalTextureHeapIndex, m_descriptorSize);
		}
	}

	// Load Sphere Texture(s).
	{
		m_sphereTexture = DX12Util::loadTexture(device, sphereTexName, mtlPath, stbi_load);

		// Upload Sphere texture
		UINT sphereDiffuseHeapIndex = UploadTexture(device, resourceUploader, m_sphereTexture);

		m_sphereTexture->gpuHandle =
			CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), sphereDiffuseHeapIndex, m_descriptorSize);
	}

	// Use synchronously for now.
	std::future<void> uploadFinish = resourceUploader.End(commandQueue);
	uploadFinish.wait();
}

// Create resources that depend on the device.
void D3D12RaytracingSimpleLighting::CreateDeviceDependentResources()
{
    // Initialize raytracing pipeline.

    // Create raytracing interfaces: raytracing device and commandlist.
    CreateRaytracingInterfaces();

    // Create root signatures for the shaders.
    CreateRootSignatures();

    // Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
    CreateRaytracingPipelineStateObject();

	// Build geometry to be used in the sample.
	BuildGeometry();

    // Create a heap for descriptors.
    CreateDescriptorHeap();

    // Build raytracing acceleration structures from the generated geometry.
    BuildAccelerationStructures(/* isUpdate */ false);

    // Create constant buffers for the geometry and the scene.
    CreateConstantBuffers();

	// Upload Textures.
	LoadTextures();

    // Build shader tables, which define shaders and their local root arguments.
    BuildShaderTables();

    // Create an output 2D texture to store the raytracing result to.
    CreateRaytracingOutputResource();
}

void D3D12RaytracingSimpleLighting::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
{
    auto device = m_deviceResources->GetD3DDevice();
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        ThrowIfFailed(m_fallbackDevice->D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
        ThrowIfFailed(m_fallbackDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
    }
    else // DirectX Raytracing
    {
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
        ThrowIfFailed(device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
    }
}

void D3D12RaytracingSimpleLighting::CreateRootSignatures()
{
    auto device = m_deviceResources->GetD3DDevice();

    // Global Root Signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    {
        CD3DX12_DESCRIPTOR_RANGE ranges[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // 1 output texture

        CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
        rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
        rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
		rootParameters[GlobalRootSignatureParams::GIAccelerationStructureSlot].InitAsShaderResourceView(1);
        rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);
		//rootParameters[GlobalRootSignatureParams::DiffuseTextureSlot].InitAsDescriptorTable(1, &ranges[1]);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
        SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
    }

    // Triangle Local Root Signature
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    {
		CD3DX12_DESCRIPTOR_RANGE ranges[4]; // Perfomance TIP: Order from most frequent to least frequent.
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);  // 2 static index and vertex buffers.
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // 2 static index and vertex buffers.
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);  // 1 diffuse texture slot.
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);  // 1 normal map/texture slot.

        CD3DX12_ROOT_PARAMETER rootParameters[TriangleLocalRootSignatureParams::Count];
        rootParameters[TriangleLocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_cubeCB), 1);
		rootParameters[TriangleLocalRootSignatureParams::IndexBuffers].InitAsDescriptorTable(1, &ranges[0]);
		rootParameters[TriangleLocalRootSignatureParams::VertexBuffers].InitAsDescriptorTable(1, &ranges[1]);
		rootParameters[TriangleLocalRootSignatureParams::DiffuseTexture].InitAsDescriptorTable(1, &ranges[2]);
		rootParameters[TriangleLocalRootSignatureParams::NormalTexture].InitAsDescriptorTable(1, &ranges[3]);

        CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
        localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature[0]);

		// Character model has the same root signature.
		SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature[1]);
    }

	// AABB geometry (sphere only right now)
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);  // Sphere texture slot

		CD3DX12_ROOT_PARAMETER rootParameters[AABBLocalRootSignatureParams::Count];
		rootParameters[AABBLocalRootSignatureParams::SphereConstantSlot].InitAsConstants(SizeOfInUint32(Sphere), 2);
		rootParameters[AABBLocalRootSignatureParams::DiffuseTextureSlot].InitAsDescriptorTable(1, &ranges[0]);

		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature[2]);
	}
}

// Create raytracing device and command list.
void D3D12RaytracingSimpleLighting::CreateRaytracingInterfaces()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();

    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        CreateRaytracingFallbackDeviceFlags createDeviceFlags = (m_forceComputeFallback ? 
													CreateRaytracingFallbackDeviceFlags::EnableRootDescriptorsInShaderRecords :
													CreateRaytracingFallbackDeviceFlags::EnableRootDescriptorsInShaderRecords);
        ThrowIfFailed(D3D12CreateRaytracingFallbackDevice(device, createDeviceFlags, 0, IID_PPV_ARGS(&m_fallbackDevice)));
        m_fallbackDevice->QueryRaytracingCommandList(commandList, IID_PPV_ARGS(&m_fallbackCommandList));
    }
    else // DirectX Raytracing
    {
        ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
        ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
    }
}

// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void D3D12RaytracingSimpleLighting::CreateLocalRootSignatureSubobjects(CD3D12_STATE_OBJECT_DESC* raytracingPipeline)
{
	// Hit groups
	// Sponza Triangle geometry
	{
		// Local root signature to be used in a hit group.
		auto localRootSignature = raytracingPipeline->CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
		localRootSignature->SetRootSignature(m_raytracingLocalRootSignature[0].Get());
		// Define explicit shader association for the local root signature. 
		{
			auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
			rootSignatureAssociation->AddExports(c_hitGroupNames_TriangleGeometry);
		}
	}

	// Character Triangle geometry
	{
		// Local root signature to be used in a hit group.
		auto localRootSignature = raytracingPipeline->CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
		localRootSignature->SetRootSignature(m_raytracingLocalRootSignature[1].Get());
		// Define explicit shader association for the local root signature. 
		{
			auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
			rootSignatureAssociation->AddExports(c_hitGroupNames_TriangleGeometry);
		}
	}

	// AABB geometry
	{
		auto localRootSignature = raytracingPipeline->CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
		localRootSignature->SetRootSignature(m_raytracingLocalRootSignature[2].Get());
		// Define explicit shader association for the local root signature. 
		{
			auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
			rootSignatureAssociation->AddExports(c_hitGroupNames_AABBGeometry);
		}
	}
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a DispatchRays() call,
// with all configuration options resolved, such as local signatures and other state.
void D3D12RaytracingSimpleLighting::CreateRaytracingPipelineStateObject()
{
    // Create 7 subobjects that combine into a RTPSO:
    // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
    // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
    // This simple sample utilizes default shader association except for local root signature subobject
    // which has an explicit association specified purely for demonstration purposes.
    // 1 - DXIL library
    // 1 - Triangle hit group
	// 1 - AABB hit group
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config
    CD3D12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

    // DXIL library
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
    auto lib = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void *)g_pRaytracing, ARRAYSIZE(g_pRaytracing));
    lib->SetDXILLibrary(&libdxil);
    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
    // In this sample, this could be ommited for convenience since the sample uses all shaders in the library. 
    {
       /* lib->DefineExport(c_raygenShaderName);
        lib->DefineExport(c_closestHitShaderName);
		lib->DefineExport(c_aabbClosestHitShaderName);
		lib->DefineExport(c_sphereIntersectionShaderName);
        lib->DefineExport(c_missShaderName);*/
    }
    
	// Triangle geometry hit groups
	{
		for (UINT rayType = 0; rayType < RayType::Count; rayType++)
		{
			auto hitGroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
			if (rayType == RayType::Radiance)
			{
				hitGroup->SetClosestHitShaderImport(c_triangleClosestHitShaderName);
			}
			hitGroup->SetHitGroupExport(c_hitGroupNames_TriangleGeometry[rayType]);
			hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
		}
	}

	// AABB geometry hit groups
	{
		// Create hit groups for each intersection shader.
		for (UINT rayType = 0; rayType < RayType::Count; rayType++)
		{
			auto hitGroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
			hitGroup->SetIntersectionShaderImport(c_sphereIntersectionShaderName);
			if (rayType == RayType::Radiance)
			{
				hitGroup->SetClosestHitShaderImport(c_aabbClosestHitShaderName);
			}
			hitGroup->SetHitGroupExport(c_hitGroupNames_AABBGeometry[rayType]);
			hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);
		}
	}
    
    // Shader config
    // Defines the maximum sizes in bytes for the ray payload and attribute structure.
    auto shaderConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	UINT payloadSize = max(sizeof(RayPayload), sizeof(ShadowRayPayload));
    UINT attributeSize = sizeof(struct ProceduralPrimitiveAttributes);  // float2 barycentrics OR float3 normal (need largest possible)
    shaderConfig->Config(payloadSize, attributeSize);

    // Local root signature and shader association
    // This is a root signature that enables a shader to have unique arguments that come from shader tables.
    CreateLocalRootSignatureSubobjects(&raytracingPipeline);

    // Global root signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3D12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

    // Pipeline config
    // Defines the maximum TraceRay() recursion depth.
    auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    // PERFOMANCE TIP: Set max recursion depth as low as needed 
    // as drivers may apply optimization strategies for low recursion depths.
    UINT maxRecursionDepth = 3; // primary ray + GI ray + shadow ray (for GI ray); max 3 rays. 
    pipelineConfig->Config(maxRecursionDepth);

#if _DEBUG
    PrintStateObjectDesc(raytracingPipeline);
#endif

    // Create the state object.
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        ThrowIfFailed(m_fallbackDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_fallbackStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
    }
    else // DirectX Raytracing
    {
        ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
    }
}

// Create 2D output texture for raytracing.
void D3D12RaytracingSimpleLighting::CreateRaytracingOutputResource()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

    // Create the output resource. The dimensions and format should match the swap-chain.
    auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(backbufferFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput)));
    NAME_D3D12_OBJECT(m_raytracingOutput);

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
    m_raytracingOutputResourceUAVDescriptorHeapIndex = AllocateDescriptor(&uavDescriptorHandle, m_raytracingOutputResourceUAVDescriptorHeapIndex);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
    m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), m_raytracingOutputResourceUAVDescriptorHeapIndex, m_descriptorSize);
}

// Build AABBs for procedural geometry within a bottom-level acceleration structure.
void D3D12RaytracingSimpleLighting::BuildProceduralGeometryAABBs()
{
	auto device = m_deviceResources->GetD3DDevice();

	// Set up Sphere geometry info.
	{
		m_numSpheres = IntersectionShaderType::TotalPrimitiveCount;

		m_spheres.resize(m_numSpheres);
		
		// Head
		m_spheres[0].info = XMFLOAT4(0.00, 0.50, 0.015, 0.125);

		// Body
		m_spheres[1].info = XMFLOAT4(0.00, 0.25, 0.000, 0.1875);
		m_spheres[2].info = XMFLOAT4(0.00, 0.00, 0.000, 0.1875);

		// Right Arm
		m_spheres[3].info = XMFLOAT4(0.150, 0.250, 0.000, 0.125);
		m_spheres[4].info = XMFLOAT4(0.235, 0.075, 0.000, 0.125);

		// Left Arm
		m_spheres[5].info = XMFLOAT4(-0.150, 0.250, 0.000, 0.125);
		m_spheres[6].info = XMFLOAT4(-0.235, 0.075, 0.000, 0.125);

		// Right Leg
		m_spheres[7].info = XMFLOAT4(0.06, -0.05, 0.0, 0.125);
		m_spheres[8].info = XMFLOAT4(0.09, -0.20, 0.0, 0.125);
		m_spheres[9].info = XMFLOAT4(0.12, -0.35, 0.0, 0.125);
		m_spheres[10].info = XMFLOAT4(0.15, -0.50, 0.0, 0.125);

		// Left Leg
		m_spheres[11].info = XMFLOAT4(-0.06, -0.05, 0.0, 0.125);
		m_spheres[12].info = XMFLOAT4(-0.09, -0.20, 0.0, 0.125);
		m_spheres[13].info = XMFLOAT4(-0.12, -0.35, 0.0, 0.125);
		m_spheres[14].info = XMFLOAT4(-0.15, -0.50, 0.0, 0.125);
	}

	// Set up Sphere AABBs.
	{
		m_aabbs.resize(IntersectionShaderType::TotalPrimitiveCount);
		UINT offset = 0;

		// Analytic primitives.
		{
			for (int i = 0; i < m_numSpheres; i++)
			{
				m_aabbs[i] = D3D12_RAYTRACING_AABB{
					m_spheres[i].info.x - m_spheres[i].info.w,
					m_spheres[i].info.y - m_spheres[i].info.w,
					m_spheres[i].info.z - m_spheres[i].info.w,
					m_spheres[i].info.x + m_spheres[i].info.w,
					m_spheres[i].info.y + m_spheres[i].info.w,
					m_spheres[i].info.z + m_spheres[i].info.w
				};
			}
		}
		AllocateUploadBuffer(device, m_aabbs.data(), m_aabbs.size() * sizeof(m_aabbs[0]), &m_aabbBuffer.resource);
	}
}

// Build geometry used in the sample.
void D3D12RaytracingSimpleLighting::BuildGeometry()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	auto commandQueue = m_deviceResources->GetCommandQueue();
	auto commandAllocator = m_deviceResources->GetCommandAllocator();

	m_sponza = std::make_shared<Shape>();

	std::string resourcePath = "resources/";
	std::string sponzaObj = resourcePath + "sponza.obj";

	m_sponza->loadMesh(sponzaObj, &resourcePath);
	m_sponza->resize();

	m_character = std::make_shared<Shape>();

	std::string characterObj = resourcePath + "muro.obj";

	m_character->loadMesh(characterObj, &resourcePath);
	m_character->resize();

	BuildProceduralGeometryAABBs();
}

// Build geometry descs for bottom-level AS.
void D3D12RaytracingSimpleLighting::BuildGeometryDescsForBottomLevelAS(std::array<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>, BottomLevelASType::Count>& geometryDescs)
{
	// Mark the geometry as opaque. 
	// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
	// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
	D3D12_RAYTRACING_GEOMETRY_FLAGS geometryFlags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	//D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

	// Triangle geometry desc
	{
		// Triangle bottom-level AS contains # of shapes in given object.
		geometryDescs[BottomLevelASType::TriangleSponza].resize(m_sponza->m_obj_count);

		for (int i = 0; i < m_sponza->m_obj_count; i++)
		{
			auto& geometryDesc = geometryDescs[BottomLevelASType::TriangleSponza][i];
			geometryDesc = {};

			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometryDesc.Triangles.IndexBuffer = m_sponza->m_indexBuffer[i].resource->GetGPUVirtualAddress();
			geometryDesc.Triangles.IndexCount = static_cast<UINT>(m_sponza->m_indexBuffer[i].resource->GetDesc().Width) / sizeof(Index);
			geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT; // Needs to match struct Index !
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // Needs to match struct Vertex !
			geometryDesc.Triangles.VertexCount = static_cast<UINT>(m_sponza->m_vertexBuffer[i].resource->GetDesc().Width) / sizeof(Vertex);
			geometryDesc.Triangles.VertexBuffer.StartAddress = m_sponza->m_vertexBuffer[i].resource->GetGPUVirtualAddress();
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

			// Mark the geometry as opaque. 
			// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
			// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

			//sponzaGeometryDescs.push_back(geometryDesc);
		}

		//if (!m_sceneCB->showCharacterGISpheres) {
			// Triangle bottom-level AS contains # of shapes in given object.
			geometryDescs[BottomLevelASType::TriangleCharacter].resize(m_character->m_obj_count);

			for (int i = 0; i < m_character->m_obj_count; i++)
			{
				auto& geometryDesc = geometryDescs[BottomLevelASType::TriangleCharacter][i];
				geometryDesc = {};

				geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				geometryDesc.Triangles.IndexBuffer = m_character->m_indexBuffer[i].resource->GetGPUVirtualAddress();
				geometryDesc.Triangles.IndexCount = static_cast<UINT>(m_character->m_indexBuffer[i].resource->GetDesc().Width) / sizeof(Index);
				geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT; // Needs to match struct Index !
				geometryDesc.Triangles.Transform3x4 = 0;
				geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // Needs to match struct Vertex !
				geometryDesc.Triangles.VertexCount = static_cast<UINT>(m_character->m_vertexBuffer[i].resource->GetDesc().Width) / sizeof(Vertex);
				geometryDesc.Triangles.VertexBuffer.StartAddress = m_character->m_vertexBuffer[i].resource->GetGPUVirtualAddress();
				geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

				// Mark the geometry as opaque. 
				// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
				// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
				geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

				//sponzaGeometryDescs.push_back(geometryDesc);
			}
		//}

		/*D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &bottomLevelInputs = bottomLevelBuildDesc.Inputs;
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		bottomLevelInputs.Flags = buildFlags;
		bottomLevelInputs.NumDescs = geometryDescs[BottomLevelASType::Triangle].size();
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.pGeometryDescs = geometryDescs[BottomLevelASType::Triangle].data();*/
	}

	// AABB geometry desc
	//if (m_sceneCB->showCharacterGISpheres)
	//{
		D3D12_RAYTRACING_GEOMETRY_DESC aabbDescTemplate = {};
		aabbDescTemplate.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
		aabbDescTemplate.AABBs.AABBCount = 1;
		aabbDescTemplate.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
		aabbDescTemplate.Flags = geometryFlags;

		// One AABB primitive per geometry.
		geometryDescs[BottomLevelASType::AABB].resize(IntersectionShaderType::TotalPrimitiveCount, aabbDescTemplate);

		// Create AABB geometries.
		// Having separate geometries allows of separate shader record binding per geometry.
		// In this sample, this lets us specify custom hit groups per AABB geometry.
		for (UINT i = 0; i < IntersectionShaderType::TotalPrimitiveCount; i++)
		{
			auto& geometryDesc = geometryDescs[BottomLevelASType::AABB][i];
			geometryDesc.AABBs.AABBs.StartAddress = m_aabbBuffer.resource->GetGPUVirtualAddress() + i * sizeof(D3D12_RAYTRACING_AABB);
		}
	//}
}

AccelerationStructureBuffers D3D12RaytracingSimpleLighting::BuildBottomLevelAS(const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geometryDescs, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags)
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	ComPtr<ID3D12Resource> scratch;
	ComPtr<ID3D12Resource> bottomLevelAS;

	// Get the size requirements for the scratch and AS buffers.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &bottomLevelInputs = bottomLevelBuildDesc.Inputs;
	bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	bottomLevelInputs.Flags = buildFlags;
	bottomLevelInputs.NumDescs = static_cast<UINT>(geometryDescs.size());
	bottomLevelInputs.pGeometryDescs = geometryDescs.data();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
	}
	else // DirectX Raytracing
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
	}
	ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	// Create a scratch buffer.
	AllocateUAVBuffer(device, bottomLevelPrebuildInfo.ScratchDataSizeInBytes, &scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

	// Allocate resources for acceleration structures.
	// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
	// Default heap is OK since the application doesn’t need CPU read/write access to them. 
	// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
	// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
	//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
	//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
	{
		D3D12_RESOURCE_STATES initialResourceState;
		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			initialResourceState = m_fallbackDevice->GetAccelerationStructureResourceState();
		}
		else // DirectX Raytracing
		{
			initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		}
		AllocateUAVBuffer(device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &bottomLevelAS, initialResourceState, L"BottomLevelAccelerationStructure");
	}

	// bottom-level AS desc.
	{
		bottomLevelBuildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
		bottomLevelBuildDesc.DestAccelerationStructureData = bottomLevelAS->GetGPUVirtualAddress();
	}

	// Build the acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		// Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
		ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_descriptorHeap.Get() };
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		m_fallbackCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
	}
	else // DirectX Raytracing
	{
		m_dxrCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
	}

	AccelerationStructureBuffers bottomLevelASBuffers;
	bottomLevelASBuffers.accelerationStructure = bottomLevelAS;
	bottomLevelASBuffers.scratch = scratch;
	bottomLevelASBuffers.ResultDataMaxSizeInBytes = bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes;
	return bottomLevelASBuffers;
}

template <class BLASPtrType>
ComPtr<ID3D12Resource> D3D12RaytracingSimpleLighting::BuildBotomLevelASInstanceDescs(BLASPtrType *bottomLevelASaddresses, ComPtr<ID3D12Resource> instanceDescsResource, bool isUpdate, bool isGI)
{
	auto device = m_deviceResources->GetD3DDevice();

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
	instanceDescs.resize(NUM_BLAS - 1);

	// Bottom-level AS for the Sponza scene.
	{
		auto& instanceDesc = instanceDescs[BottomLevelASType::TriangleSponza];
		instanceDesc = {};
		instanceDesc.InstanceMask = 1;
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.AccelerationStructure = bottomLevelASaddresses[BottomLevelASType::TriangleSponza];
		//instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;

		// Calculate transformation matrix.
		const XMVECTOR vBasePosition = XMLoadFloat3(&XMFLOAT3(0.0f, 0.0f, 0.0f));

		// Scale in XZ dimensions.
		XMMATRIX mScale = XMMatrixScaling(2.0f, 2.0f, 2.0f);
		XMMATRIX mTranslation = XMMatrixTranslationFromVector(vBasePosition);
		XMMATRIX mTransform = mScale * mTranslation;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instanceDesc.Transform), mTransform);
	}

	// Calculate transformation matrix.
	//const XMVECTOR vBasePosition = XMLoadFloat3(&XMFLOAT3(1.0f, -0.6025f, -0.15f));

	// Bottom-level AS for a character model.
	if (!isGI)
	{
		auto& instanceDesc = instanceDescs[1/*BottomLevelASType::TriangleCharacter*/];
		instanceDesc = {};
		instanceDesc.InstanceMask = 1;
		instanceDesc.InstanceContributionToHitGroupIndex = m_sponza->m_obj_count * RayType::Count;
		instanceDesc.AccelerationStructure = bottomLevelASaddresses[BottomLevelASType::TriangleCharacter];
		//instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;

		// Scale in XZ dimensions.
		XMMATRIX mScale = XMMatrixScaling(0.1f, 0.1f, 0.1f);
		XMMATRIX mTranslation = XMMatrixTranslationFromVector(m_characterPosition);
		XMMATRIX mTransform = mScale * mTranslation;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instanceDesc.Transform), mTransform);
	}

	// Create instanced bottom-level AS with procedural geometry AABBs.
	// Instances share all the data, except for a transform.
	if (isGI)
	{
		auto& instanceDesc = instanceDescs[0/*BottomLevelASType::AABB*/];
		instanceDesc = {};
		instanceDesc.InstanceMask = 1;

		// Set hit group offset to beyond the shader records for the triangle AABB.
		instanceDesc.InstanceContributionToHitGroupIndex = /*BottomLevelASType::AABB **/ (m_sponza->m_obj_count + m_character->m_obj_count) * RayType::Count; //change if shadow rays, etc introduced.
		instanceDesc.AccelerationStructure = bottomLevelASaddresses[BottomLevelASType::AABB];
		//instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;

		// Scale in XZ dimensions.
		XMMATRIX mScale = XMMatrixScaling(0.175f, 0.175f, 0.175f);
		XMMATRIX mTranslation = XMMatrixTranslationFromVector(m_characterPosition);
		XMMATRIX mTransform = mScale * mTranslation;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instanceDesc.Transform), mTransform);

		// Move all AABBS above the ground plane.
		//XMMATRIX mTranslation = XMMatrixTranslationFromVector(XMLoadFloat3(&XMFLOAT3(0, c_aabbWidth / 2, 0)));
		//XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instanceDesc.Transform), mTranslation);
	}

	UINT64 bufferSize = static_cast<UINT64>(instanceDescs.size() * sizeof(instanceDescs[0]));
	if (!isUpdate)
	{
		ID3D12Resource* newInstanceDescsResource;
		//instanceDescsResource->Reset();
		//AllocateUploadBuffer(device, instanceDescs.data(), bufferSize, &(*instanceDescsResource), L"InstanceDescs");
		
		auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
		ThrowIfFailed(device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&newInstanceDescsResource)));
		newInstanceDescsResource->SetName(L"InstanceDescs");

		void *pMappedData;
		newInstanceDescsResource->Map(0, nullptr, &pMappedData);
		memcpy(pMappedData, instanceDescs.data(), bufferSize);
		newInstanceDescsResource->Unmap(0, nullptr);

		instanceDescsResource = newInstanceDescsResource;
	}
	else
	{
		void *pMappedData;
		instanceDescsResource->Map(0, nullptr, &pMappedData);
		memcpy(pMappedData, instanceDescs.data(), bufferSize);
		instanceDescsResource->Unmap(0, nullptr);
	}

	return instanceDescsResource;
};

AccelerationStructureBuffers D3D12RaytracingSimpleLighting::BuildTopLevelASGI(AccelerationStructureBuffers bottomLevelAS[BottomLevelASType::Count], D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags, bool isUpdate)
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	ComPtr<ID3D12Resource> scratch;
	ComPtr<ID3D12Resource> topLevelAS;

	// Get required sizes for an acceleration structure.
	//D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
	m_topLevelBuildDescGI = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &topLevelInputs = m_topLevelBuildDescGI.Inputs;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = buildFlags;
	topLevelInputs.NumDescs = NUM_BLAS - 1;

	// TODO: Save pre-build information on update.
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}
	else // DirectX Raytracing
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}
	ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	if (!isUpdate)
	{
		AllocateUAVBuffer(device, topLevelPrebuildInfo.ScratchDataSizeInBytes, &scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

		// Allocate resources for acceleration structures.
		// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
		// Default heap is OK since the application doesn’t need CPU read/write access to them. 
		// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
		// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
		//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
		//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
		{
			D3D12_RESOURCE_STATES initialResourceState;
			if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
			{
				initialResourceState = m_fallbackDevice->GetAccelerationStructureResourceState();
			}
			else // DirectX Raytracing
			{
				initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
			}

			AllocateUAVBuffer(device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &topLevelAS, initialResourceState, L"TopLevelAccelerationStructure");
		}
	}
	else
	{
		m_topLevelBuildDescGI.SourceAccelerationStructureData = m_topLevelASGI->GetGPUVirtualAddress();
		scratch = m_topLevelScratchGI;
		topLevelAS = m_topLevelASGI;
	}

	// Note on Emulated GPU pointers (AKA Wrapped pointers) requirement in Fallback Layer:
	// The primary point of divergence between the DXR API and the compute-based Fallback layer is the handling of GPU pointers. 
	// DXR fundamentally requires that GPUs be able to dynamically read from arbitrary addresses in GPU memory. 
	// The existing Direct Compute API today is more rigid than DXR and requires apps to explicitly inform the GPU what blocks of memory it will access with SRVs/UAVs.
	// In order to handle the requirements of DXR, the Fallback Layer uses the concept of Emulated GPU pointers, 
	// which requires apps to create views around all memory they will access for raytracing, 
	// but retains the DXR-like flexibility of only needing to bind the top level acceleration structure at DispatchRays.
	//
	// The Fallback Layer interface uses WRAPPED_GPU_POINTER to encapsulate the underlying pointer
	// which will either be an emulated GPU pointer for the compute - based path or a GPU_VIRTUAL_ADDRESS for the DXR path.

	// Create instance descs for the bottom-level acceleration structures.
	ComPtr<ID3D12Resource> instanceDescsResource;
	if (isUpdate)
	{
		instanceDescsResource = m_instanceDescsResourceGI;
	}

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
	}
	else // DirectX Raytracing
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDescs[BottomLevelASType::Count] = {};
		D3D12_GPU_VIRTUAL_ADDRESS bottomLevelASaddresses[BottomLevelASType::Count] =
		{
			bottomLevelAS[0].accelerationStructure->GetGPUVirtualAddress(),
			bottomLevelAS[1].accelerationStructure->GetGPUVirtualAddress(),
			bottomLevelAS[2].accelerationStructure->GetGPUVirtualAddress()
		};
		instanceDescsResource = BuildBotomLevelASInstanceDescs(bottomLevelASaddresses, instanceDescsResource, isUpdate, true /* isGI */);
	}

	// Create a wrapped pointer to the acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		UINT numBufferElements = static_cast<UINT>(topLevelPrebuildInfo.ResultDataMaxSizeInBytes) / sizeof(UINT32);
		m_fallbackTopLevelAccelerationStructurePointer = CreateFallbackWrappedPointer(topLevelAS.Get(), numBufferElements);
	}

	// Top-level AS desc
	{
		m_topLevelBuildDescGI.DestAccelerationStructureData = topLevelAS->GetGPUVirtualAddress();
		topLevelInputs.InstanceDescs = instanceDescsResource->GetGPUVirtualAddress();
		m_topLevelBuildDescGI.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
	}

	// Build acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		// Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
		ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_descriptorHeap.Get() };
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		m_fallbackCommandList->BuildRaytracingAccelerationStructure(&m_topLevelBuildDesc, 0, nullptr);
	}
	else // DirectX Raytracing
	{
		m_dxrCommandList->BuildRaytracingAccelerationStructure(&m_topLevelBuildDescGI, 0, nullptr);
	}

	AccelerationStructureBuffers topLevelASBuffers;
	topLevelASBuffers.accelerationStructure = topLevelAS;
	topLevelASBuffers.instanceDesc = instanceDescsResource;
	topLevelASBuffers.scratch = scratch;
	topLevelASBuffers.ResultDataMaxSizeInBytes = topLevelPrebuildInfo.ResultDataMaxSizeInBytes;
	return topLevelASBuffers;
}

AccelerationStructureBuffers D3D12RaytracingSimpleLighting::BuildTopLevelAS(AccelerationStructureBuffers bottomLevelAS[BottomLevelASType::Count], D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags, bool isUpdate)
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	ComPtr<ID3D12Resource> scratch;
	ComPtr<ID3D12Resource> topLevelAS;

	// Get required sizes for an acceleration structure.
	//D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
	m_topLevelBuildDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &topLevelInputs = m_topLevelBuildDesc.Inputs;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = buildFlags;
	topLevelInputs.NumDescs = NUM_BLAS - 1;

	// TODO: Save pre-build information on update.
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}
	else // DirectX Raytracing
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}
	ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	if (!isUpdate)
	{
		AllocateUAVBuffer(device, topLevelPrebuildInfo.ScratchDataSizeInBytes, &scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

		// Allocate resources for acceleration structures.
		// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
		// Default heap is OK since the application doesn’t need CPU read/write access to them. 
		// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
		// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
		//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
		//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
		{
			D3D12_RESOURCE_STATES initialResourceState;
			if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
			{
				initialResourceState = m_fallbackDevice->GetAccelerationStructureResourceState();
			}
			else // DirectX Raytracing
			{
				initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
			}

			AllocateUAVBuffer(device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &topLevelAS, initialResourceState, L"TopLevelAccelerationStructure");
		}
	}
	else
	{
		m_topLevelBuildDesc.SourceAccelerationStructureData = m_topLevelAS->GetGPUVirtualAddress();
		scratch = m_topLevelScratch;
		topLevelAS = m_topLevelAS;
	}

	// Note on Emulated GPU pointers (AKA Wrapped pointers) requirement in Fallback Layer:
	// The primary point of divergence between the DXR API and the compute-based Fallback layer is the handling of GPU pointers. 
	// DXR fundamentally requires that GPUs be able to dynamically read from arbitrary addresses in GPU memory. 
	// The existing Direct Compute API today is more rigid than DXR and requires apps to explicitly inform the GPU what blocks of memory it will access with SRVs/UAVs.
	// In order to handle the requirements of DXR, the Fallback Layer uses the concept of Emulated GPU pointers, 
	// which requires apps to create views around all memory they will access for raytracing, 
	// but retains the DXR-like flexibility of only needing to bind the top level acceleration structure at DispatchRays.
	//
	// The Fallback Layer interface uses WRAPPED_GPU_POINTER to encapsulate the underlying pointer
	// which will either be an emulated GPU pointer for the compute - based path or a GPU_VIRTUAL_ADDRESS for the DXR path.

	// Create instance descs for the bottom-level acceleration structures.
	ComPtr<ID3D12Resource> instanceDescsResource;
	if (isUpdate)
	{
		instanceDescsResource = m_instanceDescsResource;
	}

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		/*D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC instanceDescs[BottomLevelASType::Count] = {};
		WRAPPED_GPU_POINTER bottomLevelASaddresses[BottomLevelASType::Count] =
		{
			CreateFallbackWrappedPointer(bottomLevelAS[0].accelerationStructure.Get(), static_cast<UINT>(bottomLevelAS[0].ResultDataMaxSizeInBytes) / sizeof(UINT32)),
			CreateFallbackWrappedPointer(bottomLevelAS[1].accelerationStructure.Get(), static_cast<UINT>(bottomLevelAS[1].ResultDataMaxSizeInBytes) / sizeof(UINT32)),
			CreateFallbackWrappedPointer(bottomLevelAS[2].accelerationStructure.Get(), static_cast<UINT>(bottomLevelAS[2].ResultDataMaxSizeInBytes) / sizeof(UINT32))
		};*/
		//BuildBotomLevelASInstanceDescs<D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC>(bottomLevelASaddresses, &instanceDescsResource);
	}
	else // DirectX Raytracing
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDescs[BottomLevelASType::Count] = {};
		D3D12_GPU_VIRTUAL_ADDRESS bottomLevelASaddresses[BottomLevelASType::Count] =
		{
			bottomLevelAS[0].accelerationStructure->GetGPUVirtualAddress(),
			bottomLevelAS[1].accelerationStructure->GetGPUVirtualAddress(),
			bottomLevelAS[2].accelerationStructure->GetGPUVirtualAddress()
		};
		instanceDescsResource = BuildBotomLevelASInstanceDescs(bottomLevelASaddresses, instanceDescsResource, isUpdate, false /* isGI */);
	}

	// Create a wrapped pointer to the acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		UINT numBufferElements = static_cast<UINT>(topLevelPrebuildInfo.ResultDataMaxSizeInBytes) / sizeof(UINT32);
		m_fallbackTopLevelAccelerationStructurePointer = CreateFallbackWrappedPointer(topLevelAS.Get(), numBufferElements);
	}

	// Top-level AS desc
	{
		m_topLevelBuildDesc.DestAccelerationStructureData = topLevelAS->GetGPUVirtualAddress();
		topLevelInputs.InstanceDescs = instanceDescsResource->GetGPUVirtualAddress();
		m_topLevelBuildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
	}

	// Build acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		// Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
		ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_descriptorHeap.Get() };
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		m_fallbackCommandList->BuildRaytracingAccelerationStructure(&m_topLevelBuildDesc, 0, nullptr);
	}
	else // DirectX Raytracing
	{
		m_dxrCommandList->BuildRaytracingAccelerationStructure(&m_topLevelBuildDesc, 0, nullptr);
	}

	AccelerationStructureBuffers topLevelASBuffers;
	topLevelASBuffers.accelerationStructure = topLevelAS;
	topLevelASBuffers.instanceDesc = instanceDescsResource;
	topLevelASBuffers.scratch = scratch;
	topLevelASBuffers.ResultDataMaxSizeInBytes = topLevelPrebuildInfo.ResultDataMaxSizeInBytes;
	return topLevelASBuffers;
}

void D3D12RaytracingSimpleLighting::CreateDescriptorHeap()
{
    auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	auto commandQueue = m_deviceResources->GetCommandQueue();
	auto commandAllocator = m_deviceResources->GetCommandAllocator();

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    // Allocate a heap for 5 descriptors:
    // numGeometry * 3 - vertex buffers, index buffers, and diffuse texture SRVs
    // 1 - raytracing output texture SRV
    // 3 - 2 bottom and 1 top level acceleration structure fallback wrapped pointer UAVs
	// 1 - texture for sphere
	UINT numGeometry = m_sponza->m_obj_count + m_character->m_obj_count;

	descriptorHeapDesc.NumDescriptors = numGeometry * 3 + 1 + 3 + 1;
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptorHeapDesc.NodeMask = 0;
    device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_descriptorHeap));
    NAME_D3D12_OBJECT(m_descriptorHeap);

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Vertex buffer is passed to the shader along with index buffer as a descriptor table.
	// Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
	// Build resources for rendering Sponza geometry.
	commandList->Reset(commandAllocator, nullptr);

	m_sponza->init(device, commandList);
	m_character->init(device, commandList);

	// Kick off acceleration structure construction.
	m_deviceResources->ExecuteCommandList();

	// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
	m_deviceResources->WaitForGpu();

	for (int i = 0; i < m_sponza->m_obj_count; i++)
	{
		UINT descriptorIndexIB = CreateBufferSRV(&m_sponza->m_indexBuffer[i], m_sponza->m_indexBuf[i].size(), sizeof(m_sponza->m_indexBuf[i][0]));
		UINT descriptorIndexVB = CreateBufferSRV(&m_sponza->m_vertexBuffer[i], m_sponza->m_vertBuf[i].size(), sizeof(m_sponza->m_vertBuf[i][0]));
		ThrowIfFalse(descriptorIndexVB == descriptorIndexIB + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor index!");

		m_geometryDescriptorIndex = descriptorIndexIB - i * 2;
	}

	for (int i = 0; i < m_character->m_obj_count; i++)
	{
		UINT descriptorIndexIB = CreateBufferSRV(&m_character->m_indexBuffer[i], m_character->m_indexBuf[i].size(), sizeof(m_character->m_indexBuf[i][0]));
		UINT descriptorIndexVB = CreateBufferSRV(&m_character->m_vertexBuffer[i], m_character->m_vertBuf[i].size(), sizeof(m_character->m_vertBuf[i][0]));
		ThrowIfFalse(descriptorIndexVB == descriptorIndexIB + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor index!");

		m_geometryDescriptorIndex = descriptorIndexIB - i * 2;
	}

	// Clean-up upload buffers.
	m_sponza->finalizeInit();
	m_character->finalizeInit();
}

void D3D12RaytracingSimpleLighting::UpdateCharacter(float deltaTime)
{
	XMFLOAT3 prevCharacterPosition;
	XMStoreFloat3(&prevCharacterPosition, m_characterPosition);

	XMFLOAT3 newCharacterPosition;
	newCharacterPosition = prevCharacterPosition;

	static int movementDirection = 1;
	if (prevCharacterPosition.z >= 0.15)
	{
		movementDirection = -1;
	}
	else if (prevCharacterPosition.z <= -0.15)
	{
		movementDirection = 1;
	}
	newCharacterPosition.z += 0.001 * movementDirection;

	m_characterPosition = XMLoadFloat3(&newCharacterPosition);

	// Top-level acceleration structure updating.
	// Resource: https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial/Extra/dxr_tutorial_extra_refit
#ifdef false
	{
		auto commandList = m_deviceResources->GetCommandList();
		auto commandQueue = m_deviceResources->GetCommandQueue();
		auto commandAllocator = m_deviceResources->GetCommandAllocator();

		// Reset the command list for the acceleration structure construction.
		commandList->Reset(commandAllocator, nullptr);

		//m_instanceDescs;
		//D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;
		m_topLevelAS.instanceDesc->Map(0, nullptr, reinterpret_cast<void**>(&m_instanceDescs));

		// Bottom-level AS for a character model.
		{
			auto& instanceDesc = m_instanceDescs[BottomLevelASType::TriangleCharacter];
			//instanceDesc = {};
			//instanceDesc.InstanceMask = 1;
			//instanceDesc.InstanceContributionToHitGroupIndex = m_sponza->m_obj_count * RayType::Count;
			//instanceDesc.AccelerationStructure = bottomLevelASaddresses[BottomLevelASType::TriangleCharacter];
			//instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;

			// Scale in XZ dimensions.
			XMMATRIX mScale = XMMatrixScaling(0.1f, 0.1f, 0.1f);
			XMMATRIX mTranslation = XMMatrixTranslationFromVector(m_characterPosition);
			XMMATRIX mTransform = mScale * mTranslation;
			XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instanceDesc.Transform), mTransform);
		}

		// Create instanced bottom-level AS with procedural geometry AABBs.
		// Instances share all the data, except for a transform.
		{
			auto& instanceDesc = m_instanceDescs[BottomLevelASType::AABB];
			//instanceDesc = {};
			//instanceDesc.InstanceMask = 1;

			// Set hit group offset to beyond the shader records for the triangle AABB.
			//instanceDesc.InstanceContributionToHitGroupIndex = /*BottomLevelASType::AABB **/ (m_sponza->m_obj_count + m_character->m_obj_count) * RayType::Count; //change if shadow rays, etc introduced.
			//instanceDesc.AccelerationStructure = bottomLevelASaddresses[BottomLevelASType::AABB];
			//instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;

			// Scale in XZ dimensions.
			XMMATRIX mScale = XMMatrixScaling(0.175f, 0.175f, 0.175f);
			XMMATRIX mTranslation = XMMatrixTranslationFromVector(m_characterPosition);
			XMMATRIX mTransform = mScale * mTranslation;
			XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instanceDesc.Transform), mTransform);
		}

		m_topLevelAS.instanceDesc->Unmap(0, nullptr);

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
		m_topLevelBuildDesc.SourceAccelerationStructureData = m_topLevelAS.accelerationStructure.Get()->GetGPUVirtualAddress();
		m_topLevelBuildDesc.Inputs.Flags = flags;
		//m_topLevelBuildDesc

		// Build the top-level AS
		m_dxrCommandList->BuildRaytracingAccelerationStructure(&m_topLevelBuildDesc, 0, nullptr);

		// Kick off acceleration structure construction.
		m_deviceResources->ExecuteCommandList();

		// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
		m_deviceResources->WaitForGpu();
	}
#else
	{
		BuildAccelerationStructures(true);
	}
#endif

	// Batch all resource barriers for bottom-level AS builds.
	//D3D12_RESOURCE_BARRIER resourceBarriers[BottomLevelASType::Count];
	//for (UINT i = 0; i < BottomLevelASType::Count; i++)
	//{
	//	resourceBarriers[i] = CD3DX12_RESOURCE_BARRIER::UAV(bottomLevelAS[i].accelerationStructure.Get());
	//}
	//commandList->ResourceBarrier(BottomLevelASType::Count, resourceBarriers);

	//// Build top-level AS.
	///*m_topLevelAS.scratch.Reset();
	//m_topLevelAS.accelerationStructure.Reset();
	//m_topLevelAS.instanceDesc.Reset();*/
	//// TODO: Fix issue where deleting after resizing window causes crash.
	//m_topLevelAS = BuildTopLevelAS(bottomLevelAS, buildFlags);
}

// Build acceleration structures needed for raytracing.
void D3D12RaytracingSimpleLighting::BuildAccelerationStructures(bool isUpdate)
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	auto commandQueue = m_deviceResources->GetCommandQueue();
	auto commandAllocator = m_deviceResources->GetCommandAllocator();

	//D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

	// Reset the command list for the acceleration structure construction.
	commandList->Reset(commandAllocator, nullptr);

	// Build bottom-level AS.
	AccelerationStructureBuffers bottomLevelAS[BottomLevelASType::Count];
	std::array<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>, BottomLevelASType::Count> geometryDescs;
	{
		BuildGeometryDescsForBottomLevelAS(geometryDescs);

		// Build all bottom-level AS.
		for (UINT i = 0; i < BottomLevelASType::Count; i++)
		{
			bottomLevelAS[i] = BuildBottomLevelAS(geometryDescs[i], buildFlags);
		}
	}

	// Batch all resource barriers for bottom-level AS builds.
	D3D12_RESOURCE_BARRIER resourceBarriers[BottomLevelASType::Count];
	for (UINT i = 0; i < BottomLevelASType::Count; i++)
	{
		resourceBarriers[i] = CD3DX12_RESOURCE_BARRIER::UAV(bottomLevelAS[i].accelerationStructure.Get());
	}
	commandList->ResourceBarrier(BottomLevelASType::Count, resourceBarriers);

	// TODO: Move above bottom-level when supported.
	if (isUpdate)
	{
		buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	}

	// Build top-level AS.
	/*m_topLevelAS.scratch.Reset();
	m_topLevelAS.accelerationStructure.Reset();
	m_topLevelAS.instanceDesc.Reset();*/
	// TODO: Fix issue where deleting after resizing window causes crash.
	//m_topLevelAS = BuildTopLevelAS(bottomLevelAS, buildFlags);
	AccelerationStructureBuffers topLevelAS = BuildTopLevelAS(bottomLevelAS, buildFlags, isUpdate);
	AccelerationStructureBuffers topLevelASGI = BuildTopLevelASGI(bottomLevelAS, buildFlags, isUpdate);

	// Kick off acceleration structure construction.
	m_deviceResources->ExecuteCommandList();

	// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
	m_deviceResources->WaitForGpu();

	if (!isUpdate)
	{
		m_instanceDescsResource = topLevelAS.instanceDesc;
		m_topLevelScratch = topLevelAS.scratch;
		m_topLevelAS = topLevelAS.accelerationStructure;

		m_instanceDescsResourceGI = topLevelASGI.instanceDesc;
		m_topLevelScratchGI = topLevelASGI.scratch;
		m_topLevelASGI = topLevelASGI.accelerationStructure;
	}

	// Store the AS buffers. The rest of the buffers will be released once we exit the function.
	for (UINT i = 0; i < BottomLevelASType::Count; i++)
	{
		m_bottomLevelAS[i] = bottomLevelAS[i].accelerationStructure;
	}
	m_topLevelAS = topLevelAS.accelerationStructure;
	m_topLevelASGI = topLevelASGI.accelerationStructure;
}

// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
void D3D12RaytracingSimpleLighting::BuildShaderTables()
{
    auto device = m_deviceResources->GetD3DDevice();

    void* rayGenShaderIdentifier;
	void* missShaderIDs[RayType::Count];
	void* hitGroupShaderIDs_TriangleGeometry[RayType::Count];
	void* hitGroupShaderIDs_AABBGeometry[RayType::Count];

    auto GetShaderIdentifiers = [&](auto* stateObjectProperties)
    {
        rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
        /*missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
        hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_hitGroupName);
		aabbHitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_aabbHitGroupName);*/

		for (UINT i = 0; i < RayType::Count; i++)
		{
			missShaderIDs[i] = stateObjectProperties->GetShaderIdentifier(c_missShaderNames[i]);
			//shaderIdToStringMap[missShaderIDs[i]] = c_missShaderNames[i];
		}
		for (UINT i = 0; i < RayType::Count; i++)
		{
			hitGroupShaderIDs_TriangleGeometry[i] = stateObjectProperties->GetShaderIdentifier(c_hitGroupNames_TriangleGeometry[i]);
			//shaderIdToStringMap[hitGroupShaderIDs_TriangleGeometry[i]] = c_hitGroupNames_TriangleGeometry[i];
		}
		for (UINT i = 0; i < RayType::Count; i++)
		{
			hitGroupShaderIDs_AABBGeometry[i] = stateObjectProperties->GetShaderIdentifier(c_hitGroupNames_AABBGeometry[i]);
			//shaderIdToStringMap[hitGroupShaderIDs_AABBGeometry[r][c]] = c_hitGroupNames_AABBGeometry[r][c];
		}
    };

    // Get shader identifiers.
    UINT shaderIdentifierSize;
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        GetShaderIdentifiers(m_fallbackStateObject.Get());
        shaderIdentifierSize = m_fallbackDevice->GetShaderIdentifierSize();
    }
    else // DirectX Raytracing
    {
        ComPtr<ID3D12StateObjectPropertiesPrototype> stateObjectProperties;
        ThrowIfFailed(m_dxrStateObject.As(&stateObjectProperties));
        GetShaderIdentifiers(stateObjectProperties.Get());
        shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    // Ray gen shader table.
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;

        ShaderTable rayGenShaderTable(device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
        rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
        m_rayGenShaderTable = rayGenShaderTable.GetResource();
    }

    // Miss shader table.
    {
		UINT numShaderRecords = RayType::Count;
        UINT shaderRecordSize = shaderIdentifierSize;

		ShaderTable missShaderTable(device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
		for (UINT i = 0; i < RayType::Count; i++)
		{
			missShaderTable.push_back(ShaderRecord(missShaderIDs[i], shaderRecordSize, nullptr, 0));
		}
		//missShaderTable.DebugPrint(shaderIdToStringMap);
		m_missShaderTableStrideInBytes = missShaderTable.GetShaderRecordSize();
		m_missShaderTable = missShaderTable.GetResource();
    }

	// Hit group shader table.
    {
		struct TriangleRootArguments {
			CubeConstantBuffer cb;
			D3D12_GPU_DESCRIPTOR_HANDLE indexBufferGPUHandle;
			D3D12_GPU_DESCRIPTOR_HANDLE vertexBufferGPUHandle;
			D3D12_GPU_DESCRIPTOR_HANDLE diffuseTextureGPUHandle;
			D3D12_GPU_DESCRIPTOR_HANDLE normalTextureGPUHandle;
		} sponzaTriangleRootArguments;

		TriangleRootArguments characterTriangleRootArguments;

		struct AABBRootArguments {
			Sphere sphereConstant;
			D3D12_GPU_DESCRIPTOR_HANDLE diffuseTexture;
		} aabbRootArguments;

		UINT numSponzaTriangleShaderRecords = m_sponza->m_obj_count;
		UINT numCharacterTriangleShaderRecords = m_character->m_obj_count;

		UINT numAABBShaderRecords = m_numSpheres; // Only 1 sphere right now.

		UINT numShaderRecords = (numSponzaTriangleShaderRecords + numCharacterTriangleShaderRecords + numAABBShaderRecords) * RayType::Count;

		UINT shaderRecordSize = shaderIdentifierSize + max(sizeof(sponzaTriangleRootArguments), sizeof(aabbRootArguments));
		ShaderTable hitGroupShaderTable(device, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");

		// Triangle Hit group shader table for Sponza scene.
		{
			sponzaTriangleRootArguments.cb = m_cubeCB;

			for (UINT i = 0; i < numSponzaTriangleShaderRecords; i++)
			{
				sponzaTriangleRootArguments.indexBufferGPUHandle = m_sponza->m_indexBuffer[i].gpuDescriptorHandle;
				sponzaTriangleRootArguments.vertexBufferGPUHandle = m_sponza->m_vertexBuffer[i].gpuDescriptorHandle;

				// Attach diffuse texture.
				sponzaTriangleRootArguments.cb.useDiffuseTexture = m_sponza->GetDiffuseTextureGPUHandle(sponzaTriangleRootArguments.diffuseTextureGPUHandle, i) ? 1 : 0;

				// Attach normal texture.
				sponzaTriangleRootArguments.cb.useNormalTexture = m_sponza->GetNormalTextureGPUHandle(sponzaTriangleRootArguments.normalTextureGPUHandle, i) ? 1 : 0;
				//hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &triangleRootArguments, sizeof(triangleRootArguments)));
				for (auto& hitGroupShaderID : hitGroupShaderIDs_TriangleGeometry)
				{
					hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderID, shaderIdentifierSize, &sponzaTriangleRootArguments, sizeof(sponzaTriangleRootArguments)));
				}
			}
		}

		// Triangle Hit group shader table for character.
		{
			characterTriangleRootArguments.cb = m_cubeCB;
			for (UINT i = 0; i < numCharacterTriangleShaderRecords; i++)
			{
				characterTriangleRootArguments.indexBufferGPUHandle = m_character->m_indexBuffer[i].gpuDescriptorHandle;
				characterTriangleRootArguments.vertexBufferGPUHandle = m_character->m_vertexBuffer[i].gpuDescriptorHandle;

				// Attach diffuse texture.
				characterTriangleRootArguments.cb.useDiffuseTexture = m_character->GetDiffuseTextureGPUHandle(characterTriangleRootArguments.diffuseTextureGPUHandle, i) ? 1 : 0;

				// Attach normal texture.
				characterTriangleRootArguments.cb.useNormalTexture = m_character->GetNormalTextureGPUHandle(characterTriangleRootArguments.normalTextureGPUHandle, i) ? 1 : 0;

				//hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &triangleRootArguments, sizeof(triangleRootArguments)));
				for (auto& hitGroupShaderID : hitGroupShaderIDs_TriangleGeometry)
				{
					hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderID, shaderIdentifierSize, &characterTriangleRootArguments, sizeof(characterTriangleRootArguments)));
				}
			}
		}

		// AABB geometry hit groups.
		{
			// Create a shader record for each primitive.
			for (UINT i = 0; i < numAABBShaderRecords; i++)
			{
				aabbRootArguments.sphereConstant = m_spheres[i];
				aabbRootArguments.diffuseTexture = m_sphereTexture->gpuHandle;

				for (auto& hitGroupShaderID : hitGroupShaderIDs_AABBGeometry)
				{
					hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderID, shaderIdentifierSize, &aabbRootArguments, sizeof(aabbRootArguments)));
				}
				//hitGroupShaderTable.push_back(ShaderRecord(aabbHitGroupShaderIdentifier, shaderIdentifierSize, &aabbRootArguments, sizeof(aabbRootArguments)));
			}
		}

		m_hitGroupShaderTableStrideInBytes = hitGroupShaderTable.GetShaderRecordSize();
        m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
    }
}

void D3D12RaytracingSimpleLighting::SelectRaytracingAPI(RaytracingAPI type)
{
    if (type == RaytracingAPI::FallbackLayer)
    {
        m_raytracingAPI = type;
    }
    else // DirectX Raytracing
    {
        if (m_isDxrSupported)
        {
            m_raytracingAPI = type;
        }
        else
        {
            OutputDebugString(L"Invalid selection - DXR is not available.\n");
        }
    }
}

void D3D12RaytracingSimpleLighting::OnKeyDown(UINT8 key)
{
    // Store previous values.
    RaytracingAPI previousRaytracingAPI = m_raytracingAPI;
    bool previousForceComputeFallback = m_forceComputeFallback;

    switch (key)
    {
    case VK_NUMPAD1:
    case '1': // Fallback Layer
        m_forceComputeFallback = false;
        SelectRaytracingAPI(RaytracingAPI::FallbackLayer);
        break;
    case VK_NUMPAD2:
    case '2': // Fallback Layer + force compute path
        m_forceComputeFallback = true;
        SelectRaytracingAPI(RaytracingAPI::FallbackLayer);
        break;
    case VK_NUMPAD3:
    case '3': // DirectX Raytracing
        SelectRaytracingAPI(RaytracingAPI::DirectXRaytracing);
        break;
    default:
        break;
    }
    
    if (m_raytracingAPI != previousRaytracingAPI ||
        m_forceComputeFallback != previousForceComputeFallback)
    {
        // Raytracing API selection changed, recreate everything.
        RecreateD3D();
    }
}

// Update frame-based values.
void D3D12RaytracingSimpleLighting::OnUpdate()
{
	m_timer.Tick();
	CalculateFrameStats();
	float elapsedTime = static_cast<float>(m_timer.GetElapsedSeconds());
	auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
	auto prevFrameIndex = m_deviceResources->GetPreviousFrameIndex();

	// Update camera.
	{
		//float secondsToRotateAround = 24.0f;
		//float angleToRotateBy = 360.0f * (elapsedTime / secondsToRotateAround);
		//XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(angleToRotateBy));
		//m_eye = XMVector3Transform(m_eye, rotate);
		//m_up = XMVector3Transform(m_up, rotate);
		//m_at = XMVector3Transform(m_at, rotate);
		m_camera.update(elapsedTime);
		UpdateCameraMatrices();
	}

	// Rotate the second light around Y axis.
	{
		float secondsToRotateAround = 8.0f;
		float angleToRotateBy = 0.0;// -360.0f * (elapsedTime / secondsToRotateAround);
		//XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(angleToRotateBy));
		//XMMATRIX translate = XMMatrixTranslationFromVector()

		XMFLOAT3 lightOffset = XMFLOAT3(0.0, 0.0, 0.0);
		if (GetKeyState('I') & 0x8000)
		{
			lightOffset.x += elapsedTime;
		}
		if (GetKeyState('K') & 0x8000)
		{
			lightOffset.x -= elapsedTime;
		}
		if (GetKeyState('L') & 0x8000)
		{
			lightOffset.z += elapsedTime;
		}
		if (GetKeyState('J') & 0x8000)
		{
			lightOffset.z -= elapsedTime;
		}
		XMMATRIX translate = XMMatrixTranslationFromVector(XMLoadFloat3(&lightOffset));

		const XMVECTOR& prevLightPosition = m_sceneCB[prevFrameIndex].lightPosition;
		m_sceneCB[frameIndex].lightPosition = XMVector3Transform(prevLightPosition, translate);

		static bool useGlobalIllumination = false;
		static bool useNormalTexturing = false;
		if (GetKeyState('G') & 0x8000)
		{
			useGlobalIllumination = true;
		}
		if (GetKeyState('H') & 0x8000)
		{
			useGlobalIllumination = false;
		}
		if (GetKeyState('N') & 0x8000)
		{
			useNormalTexturing = true;
		}
		if (GetKeyState('M') & 0x8000)
		{
			useNormalTexturing = false;
		}
		m_sceneCB[frameIndex].useGlobalIllumination = useGlobalIllumination ? 1 : 0;
		m_sceneCB[frameIndex].useNormalTexturing = useNormalTexturing ? 1 : 0;

		static UINT numGISamples = 2;
		if (GetKeyState('O') & 0x8000)
		{
			numGISamples = max(numGISamples - 1, 1);
		}
		if (GetKeyState('P') & 0x8000)
		{
			numGISamples = min(numGISamples + 1, 64);
		}
		m_sceneCB[frameIndex].numGISamples = numGISamples;

		static bool showCharacterGISpheres = false;
		if (GetKeyState('V') & 0x8000)
		{
			showCharacterGISpheres = true;
			BuildAccelerationStructures(/* isUpdate */ false);
		}
		if (GetKeyState('B') & 0x8000)
		{
			showCharacterGISpheres = false;
			BuildAccelerationStructures(/* isUpdate */ false);
		}
		m_sceneCB[frameIndex].showCharacterGISpheres = showCharacterGISpheres;
	}

	UpdateCharacter(elapsedTime);
}

// Parse supplied command line args.
void D3D12RaytracingSimpleLighting::ParseCommandLineArgs(WCHAR* argv[], int argc)
{
    DXSample::ParseCommandLineArgs(argv, argc);

    if (argc > 1)
    {
        if (_wcsnicmp(argv[1], L"-FL", wcslen(argv[1])) == 0 )
        {
            m_forceComputeFallback = true;
            m_raytracingAPI = RaytracingAPI::FallbackLayer;
        }
        else if (_wcsnicmp(argv[1], L"-DXR", wcslen(argv[1])) == 0)
        {
            m_raytracingAPI = RaytracingAPI::DirectXRaytracing;
        }
    }
}

void D3D12RaytracingSimpleLighting::DoRaytracing()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    
    auto DispatchRays = [&](auto* commandList, auto* stateObject, auto* dispatchDesc)
    {
        // Since each shader table has only one shader record, the stride is same as the size.
        dispatchDesc->HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
        dispatchDesc->HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
        dispatchDesc->HitGroupTable.StrideInBytes = m_hitGroupShaderTableStrideInBytes;
        dispatchDesc->MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
        dispatchDesc->MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
        dispatchDesc->MissShaderTable.StrideInBytes = m_missShaderTableStrideInBytes;
        dispatchDesc->RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
        dispatchDesc->RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;
        dispatchDesc->Width = m_width;
        dispatchDesc->Height = m_height;
        dispatchDesc->Depth = 1;
        commandList->SetPipelineState1(stateObject);

        commandList->DispatchRays(dispatchDesc);
    };

    auto SetCommonPipelineState = [&](auto* descriptorSetCommandList)
    {
        descriptorSetCommandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

		commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, m_raytracingOutputResourceUAVGpuDescriptor);
		//commandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::DiffuseTextureSlot, m_sphereDiffuseTextureResourceGpuDescriptor);
    };

    commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

    // Copy the updated scene constant buffer to GPU.
    memcpy(&m_mappedConstantData[frameIndex].constants, &m_sceneCB[frameIndex], sizeof(m_sceneCB[frameIndex]));
    auto cbGpuAddress = m_perFrameConstants->GetGPUVirtualAddress() + frameIndex * sizeof(m_mappedConstantData[0]);
    commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);
   
    // Bind the heaps, acceleration structure and dispatch rays.
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
    {
        SetCommonPipelineState(m_fallbackCommandList.Get());
        m_fallbackCommandList->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::AccelerationStructureSlot, m_fallbackTopLevelAccelerationStructurePointer);
		m_fallbackCommandList->SetTopLevelAccelerationStructure(GlobalRootSignatureParams::GIAccelerationStructureSlot, m_fallbackTopLevelAccelerationStructurePointer);
        DispatchRays(m_fallbackCommandList.Get(), m_fallbackStateObject.Get(), &dispatchDesc);
    }
    else // DirectX Raytracing
    {
        SetCommonPipelineState(commandList);
        //commandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, m_topLevelAccelerationStructure->GetGPUVirtualAddress());
		commandList->SetComputeRootShaderResourceView(GlobalRootSignature::Slot::AccelerationStructure, m_topLevelAS->GetGPUVirtualAddress());
		commandList->SetComputeRootShaderResourceView(GlobalRootSignature::Slot::GIAccelerationStructure, m_topLevelASGI->GetGPUVirtualAddress());
        DispatchRays(m_dxrCommandList.Get(), m_dxrStateObject.Get(), &dispatchDesc);
    }
}

// Update the application state with the new resolution.
void D3D12RaytracingSimpleLighting::UpdateForSizeChange(UINT width, UINT height)
{
    DXSample::UpdateForSizeChange(width, height);
}

// Copy the raytracing output to the backbuffer.
void D3D12RaytracingSimpleLighting::CopyRaytracingOutputToBackbuffer()
{
    auto commandList= m_deviceResources->GetCommandList();
    auto renderTarget = m_deviceResources->GetRenderTarget();

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    commandList->CopyResource(renderTarget, m_raytracingOutput.Get());

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

// Create resources that are dependent on the size of the main window.
void D3D12RaytracingSimpleLighting::CreateWindowSizeDependentResources()
{
    CreateRaytracingOutputResource(); 
    UpdateCameraMatrices();
}

// Release resources that are dependent on the size of the main window.
void D3D12RaytracingSimpleLighting::ReleaseWindowSizeDependentResources()
{
    m_raytracingOutput.Reset();
}

// Release all resources that depend on the device.
void D3D12RaytracingSimpleLighting::ReleaseDeviceDependentResources()
{
    m_fallbackDevice.Reset();
    m_fallbackCommandList.Reset();
    m_fallbackStateObject.Reset();
    m_raytracingGlobalRootSignature.Reset();
	ResetComPtrArray(&m_raytracingLocalRootSignature);

    m_dxrDevice.Reset();
    m_dxrCommandList.Reset();
    m_dxrStateObject.Reset();

    m_descriptorHeap.Reset();
    m_descriptorsAllocated = 0;
    m_raytracingOutputResourceUAVDescriptorHeapIndex = UINT_MAX;
    m_cubeIndexBuffer.resource.Reset();
    m_cubeVertexBuffer.resource.Reset();
    m_perFrameConstants.Reset();
    m_rayGenShaderTable.Reset();
    m_missShaderTable.Reset();
    m_hitGroupShaderTable.Reset();

    /*m_bottomLevelAccelerationStructure.Reset();
    m_topLevelAccelerationStructure.Reset();*/

	ResetComPtrArray(&m_bottomLevelAS);
	m_topLevelAS/*.accelerationStructure*/.Reset();
}

void D3D12RaytracingSimpleLighting::RecreateD3D()
{
    // Give GPU a chance to finish its execution in progress.
    try
    {
        m_deviceResources->WaitForGpu();
    }
    catch (HrException&)
    {
        // Do nothing, currently attached adapter is unresponsive.
    }
    m_deviceResources->HandleDeviceLost();
}

// Render the scene.
void D3D12RaytracingSimpleLighting::OnRender()
{
    if (!m_deviceResources->IsWindowVisible())
    {
        return;
    }

    m_deviceResources->Prepare();
    DoRaytracing();
    CopyRaytracingOutputToBackbuffer();

    m_deviceResources->Present(D3D12_RESOURCE_STATE_PRESENT);
}

void D3D12RaytracingSimpleLighting::OnDestroy()
{
    // Let GPU finish before releasing D3D resources.
    m_deviceResources->WaitForGpu();
    OnDeviceLost();
}

// Release all device dependent resouces when a device is lost.
void D3D12RaytracingSimpleLighting::OnDeviceLost()
{
    ReleaseWindowSizeDependentResources();
    ReleaseDeviceDependentResources();
}

// Create all device dependent resources when a device is restored.
void D3D12RaytracingSimpleLighting::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}

// Compute the average frames per second and million rays per second.
void D3D12RaytracingSimpleLighting::CalculateFrameStats()
{
    static int frameCnt = 0;
    static double elapsedTime = 0.0f;
    double totalTime = m_timer.GetTotalSeconds();
    frameCnt++;

    // Compute averages over one second period.
    if ((totalTime - elapsedTime) >= 1.0f)
    {
        float diff = static_cast<float>(totalTime - elapsedTime);
        float fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

        frameCnt = 0;
        elapsedTime = totalTime;

        float MRaysPerSecond = (m_width * m_height * fps) / static_cast<float>(1e6);

        wstringstream windowText;

        if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
        {
            /*if (m_fallbackDevice->UsingRaytracingDriver())
            {
                windowText << L"(FL-DXR)";
            }
            else
            {
                windowText << L"(FL)";
            }*/
        }
        else
        {
            //windowText << L"(DXR)";
        }
		windowText << setprecision(2) << fixed
			<< L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
			<< L"    GI samples: " << m_sceneCB[0].numGISamples;
            //<< L"    GPU[" << m_deviceResources->GetAdapterID() << L"]: " << m_deviceResources->GetAdapterDescription();
        SetCustomWindowText(windowText.str().c_str());
    }
}

// Handle OnSizeChanged message event.
void D3D12RaytracingSimpleLighting::OnSizeChanged(UINT width, UINT height, bool minimized)
{
    if (!m_deviceResources->WindowSizeChanged(width, height, minimized))
    {
        return;
    }

    UpdateForSizeChange(width, height);

    ReleaseWindowSizeDependentResources();
    CreateWindowSizeDependentResources();
}

// Create a wrapped pointer for the Fallback Layer path.
WRAPPED_GPU_POINTER D3D12RaytracingSimpleLighting::CreateFallbackWrappedPointer(ID3D12Resource* resource, UINT bufferNumElements)
{
    auto device = m_deviceResources->GetD3DDevice();

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawBufferUavDesc = {};
    rawBufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawBufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    rawBufferUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawBufferUavDesc.Buffer.NumElements = bufferNumElements;

    D3D12_CPU_DESCRIPTOR_HANDLE bottomLevelDescriptor;
   
    // Only compute fallback requires a valid descriptor index when creating a wrapped pointer.
    UINT descriptorHeapIndex = 0;
    if (!m_fallbackDevice->UsingRaytracingDriver())
    {
        descriptorHeapIndex = AllocateDescriptor(&bottomLevelDescriptor);
        device->CreateUnorderedAccessView(resource, nullptr, &rawBufferUavDesc, bottomLevelDescriptor);
    }
    return m_fallbackDevice->GetWrappedPointerSimple(descriptorHeapIndex, resource->GetGPUVirtualAddress());
}

// Create a wrapped pointer for the Fallback Layer path.
WRAPPED_GPU_POINTER D3D12RaytracingSimpleLighting::CreateFallbackWrappedPointerForBuffer(D3DBuffer* buffer, UINT numElements, UINT elementSize)
{
	auto device = m_deviceResources->GetD3DDevice();

	// SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = numElements;
	if (elementSize == 0)
	{
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		srvDesc.Buffer.StructureByteStride = 0;
	}
	else
	{
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.StructureByteStride = elementSize;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE bufferDescriptor;

	// Only compute fallback requires a valid descriptor index when creating a wrapped pointer.
	UINT descriptorHeapIndex = 0;
	if (!m_fallbackDevice->UsingRaytracingDriver())
	{
		descriptorHeapIndex = AllocateDescriptor(&bufferDescriptor);
		device->CreateShaderResourceView(buffer->resource.Get(), &srvDesc, buffer->cpuDescriptorHandle);
	}
	return m_fallbackDevice->GetWrappedPointerSimple(descriptorHeapIndex, buffer->resource->GetGPUVirtualAddress());
}

// Allocate a descriptor and return its index. 
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT D3D12RaytracingSimpleLighting::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
    auto descriptorHeapCpuBase = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (descriptorIndexToUse >= m_descriptorHeap->GetDesc().NumDescriptors)
    {
		ThrowIfFalse(m_descriptorsAllocated < m_descriptorHeap->GetDesc().NumDescriptors, L"Ran out of descriptors on the heap!");
        descriptorIndexToUse = m_descriptorsAllocated++;
    }
    *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, m_descriptorSize);
    return descriptorIndexToUse;
}

// Create SRV for a buffer.
UINT D3D12RaytracingSimpleLighting::CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize)
{
    auto device = m_deviceResources->GetD3DDevice();

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements = numElements;
    if (elementSize == 0)
    {
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srvDesc.Buffer.StructureByteStride = 0;
    }
    else
    {
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.StructureByteStride = elementSize;
    }
    UINT descriptorIndex = AllocateDescriptor(&buffer->cpuDescriptorHandle);
    device->CreateShaderResourceView(buffer->resource.Get(), &srvDesc, buffer->cpuDescriptorHandle);
    buffer->gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_descriptorSize);
    return descriptorIndex;
};