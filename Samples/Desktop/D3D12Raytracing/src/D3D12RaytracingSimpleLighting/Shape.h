#pragma once


#include <string>
#include <vector>
#include <memory>

#include "DXSampleHelper.h"
#include "RaytracingHlslCompat.h"
#include "DX12Util.h"
#include "Material.h"
#include "Texture.h"
#include "MaterialCBuffer.h"
//#include "ObjectCBuffer.h"

using Microsoft::WRL::ComPtr;

class Shape
{

public:
	void loadMesh(const std::string & meshName, std::string * mtlName = nullptr);
	void init(ComPtr<ID3D12Device> device, ComPtr<ID3D12GraphicsCommandList> cmdList);
	void finalizeInit();
	void computeNormals(std::vector<float> & positions, std::vector<UINT> & indices, std::vector<float> & normals);
	void computeTangents();
	void resize();
	void draw(ComPtr<ID3D12GraphicsCommandList> cmdList,
		ComPtr<ID3D12Resource> materialBufferUpload,
		std::unordered_map<std::string, std::shared_ptr<Texture>> & diffuseTextures,
		std::unordered_map<std::string, std::shared_ptr<Texture>> & specularTextures,
		std::unordered_map<std::string, std::shared_ptr<Texture>> & normalTextures,
		ComPtr<ID3D12DescriptorHeap> srvHeap,
		UINT srvDescriptorSize) const;

	// Access functions
	std::vector<Material> & getMaterials() { return m_materials; }
	UINT numMaterials() { return m_materials.size(); }
	bool GetDiffuseTextureGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE & gpuHandle, UINT shapeIndex);
	bool GetNormalTextureGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE & gpuHandle, UINT shapeIndex);

	// CPU Resources
	int m_obj_count = 0;
	std::vector<Vertex> * m_vertBuf = nullptr;
	std::vector<Index> * m_indexBuf = nullptr;

	// GPU Resources
	std::vector<D3DBuffer> m_vertexBuffer = std::vector<D3DBuffer>();
	std::vector<D3DBuffer> m_indexBuffer = std::vector<D3DBuffer>();

	std::unordered_map<std::string, std::shared_ptr<Texture>> m_diffuseTextures;
	std::unordered_map<std::string, std::shared_ptr<Texture>> m_normalTextures;

private:

	// CPU Resources
	std::vector<Material> m_materials = std::vector<Material>();
	std::vector<UINT> m_materialIDs = std::vector<UINT>();
	std::vector<UINT> m_textureIDs = std::vector<UINT>();
	
	// GPU Resources
	std::vector<ComPtr<ID3D12Resource>> m_vertexBufferUpload = std::vector<ComPtr<ID3D12Resource>>();
	std::vector<D3D12_VERTEX_BUFFER_VIEW> m_vertexBufferView = std::vector<D3D12_VERTEX_BUFFER_VIEW>();

	std::vector<ComPtr<ID3D12Resource>> m_indexBufferUpload = std::vector<ComPtr<ID3D12Resource>>();
	std::vector<D3D12_INDEX_BUFFER_VIEW> m_indexBufferView = std::vector<D3D12_INDEX_BUFFER_VIEW>();

	std::vector<ComPtr<ID3D12Resource>> m_texture = std::vector<ComPtr<ID3D12Resource>>();
};
