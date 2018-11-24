#include "stdafx.h"
#include "Shape.h"
#include <iostream>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

using namespace std;

void Shape::loadMesh(const string & meshName, string * mtlpath)
{
	// Load geometry
	// Some obj files contain material information.
	vector<tinyobj::shape_t> shapes;
	vector<tinyobj::material_t> objMaterials;
	string errStr;
	bool rc = false;
	if (mtlpath)
		rc = tinyobj::LoadObj(shapes, objMaterials, errStr, meshName.c_str(), mtlpath->c_str());
	else
		rc = tinyobj::LoadObj(shapes, objMaterials, errStr, meshName.c_str());

	if (!rc)
	{
		cerr << errStr << endl;
		exit(EXIT_FAILURE);
	}
	else if (shapes.size())
	{
		m_obj_count = shapes.size();
		m_vertBuf = new std::vector<Vertex>[m_obj_count];
		m_indexBuf = new std::vector<Index>[m_obj_count];
		m_materialIDs.resize(m_obj_count);
		for (int i = 0; i < m_obj_count; i++)
		{
			std::vector<float> & positions = shapes[i].mesh.positions;
			std::vector<float> & normals = shapes[i].mesh.normals;
			std::vector<float> & texcoords = shapes[i].mesh.texcoords;
			std::vector<UINT> & indices = shapes[i].mesh.indices;
			for (int j = 0; j < positions.size() / 3; j++) {
				Vertex vert;
				if (normals.size() == 0) {
					computeNormals(positions, indices, normals);
				}
				if (texcoords.size() / 2 == positions.size() / 3) {
					vert = {
						{ positions[(j * 3) + 0], positions[(j * 3) + 1], positions[(j * 3) + 2] },
						{ normals[(j * 3) + 0], normals[(j * 3) + 1], normals[(j * 3) + 2] },
						{ texcoords[(j * 2) + 0], texcoords[(j * 2) + 1] },
						{ 0.0f, 0.0f, 0.0f},
						{ 0.0f, 0.0f, 0.0f }
					};
				}
				else {
					vert = {
						{ positions[(j * 3) + 0], positions[(j * 3) + 1], positions[(j * 3) + 2] },
						{ normals[(j * 3) + 0], normals[(j * 3) + 1], normals[(j * 3) + 2] },
						{ 0.0f, 0.0f },
						{ 0.0f, 0.0f, 0.0f},
						{ 0.0f, 0.0f, 0.0f }
					};
				}
				m_vertBuf[i].push_back(vert);
			}
			m_indexBuf[i] = std::vector<Index>(indices.begin(), indices.end());
			//m_indexBuf[i] = indices;

			if (shapes[i].mesh.material_ids.size() > 0) {
				m_materialIDs[i] = shapes[i].mesh.material_ids[0];
			}
			else {
				m_materialIDs[i] = -1;
			}
		}
	}

	// Load object materials.
	for (int i = 0; i < objMaterials.size(); i++) {
		Material newMaterial;

		XMFLOAT3 ambVec = XMFLOAT3(objMaterials[i].ambient);
		newMaterial.ambient = XMLoadFloat3(&ambVec);

		XMFLOAT3 diffVec = XMFLOAT3(objMaterials[i].diffuse);
		newMaterial.diffuse = XMLoadFloat3(&diffVec);

		XMFLOAT3 specVec = XMFLOAT3(objMaterials[i].specular);
		newMaterial.specular = XMLoadFloat3(&specVec);

		newMaterial.shininess = objMaterials[i].shininess;
		newMaterial.diffuseTex = objMaterials[i].diffuse_texname;
		newMaterial.specularTex = objMaterials[i].specular_texname;
		newMaterial.normalTex = objMaterials[i].bump_texname;

		m_materials.push_back(newMaterial);
	}
}

void Shape::computeNormals(std::vector<float> & positions, std::vector<UINT> & indices, std::vector<float> & normals) {
	float v1[3], v2[3], nor[3];
	static const int x = 0;
	static const int y = 1;
	static const int z = 2;
	float length;

	// Set all vertex normals to zero so we can add the adjacent face normals to
	// each
	normals.clear();
	normals.resize(positions.size());

	for (size_t v = 0; v < positions.size(); ++v) {
		normals[v] = 0.0;
	}

	// Calculate the normal for each face; add to adjacent vertices
	for (size_t v = 0; v < indices.size() / 3; ++v) {
		int idx1 = indices[3 * v + 0];
		int idx2 = indices[3 * v + 1];
		int idx3 = indices[3 * v + 2];

		// Calculate two vectors from the three points
		v1[x] = positions[3 * idx1 + x] - positions[3 * idx2 + x];
		v1[y] = positions[3 * idx1 + y] - positions[3 * idx2 + y];
		v1[z] = positions[3 * idx1 + z] - positions[3 * idx2 + z];

		v2[x] = positions[3 * idx2 + x] - positions[3 * idx3 + x];
		v2[y] = positions[3 * idx2 + y] - positions[3 * idx3 + y];
		v2[z] = positions[3 * idx2 + z] - positions[3 * idx3 + z];

		// Take the cross product of the two vectors to get
		// the normal vector which will be stored in out
		nor[x] = v1[y] * v2[z] - v1[z] * v2[y];
		nor[y] = v1[z] * v2[x] - v1[x] * v2[z];
		nor[z] = v1[x] * v2[y] - v1[y] * v2[x];

		// Normalize the vector
		length = sqrt(nor[x] * nor[x] + nor[y] * nor[y] + nor[z] * nor[z]);

		nor[x] /= length;
		nor[y] /= length;
		nor[z] /= length;

		// Set the normal into the shape's normal buffer
		normals[3 * idx1 + x] += nor[x];
		normals[3 * idx1 + y] += nor[y];
		normals[3 * idx1 + z] += nor[z];

		normals[3 * idx2 + x] += nor[x];
		normals[3 * idx2 + y] += nor[y];
		normals[3 * idx2 + z] += nor[z];

		normals[3 * idx3 + x] += nor[x];
		normals[3 * idx3 + y] += nor[y];
		normals[3 * idx3 + z] += nor[z];
	}

	// Normalize each vector's normal, effectively giving us a weighted average
	// based on the area of each face adjacent to each vertex
	for (size_t v = 0; v < normals.size() / 3; ++v) {
		nor[x] = normals[(v * 3) + x];
		nor[y] = normals[(v * 3) + y];
		nor[z] = normals[(v * 3) + z];

		length = sqrt(nor[x] * nor[x] + nor[y] * nor[y] + nor[z] * nor[z]);

		nor[x] /= length;
		nor[y] /= length;
		nor[z] /= length;

		normals[(v * 3) + x] = nor[x];
		normals[(v * 3) + y] = nor[y];
		normals[(v * 3) + z] = nor[z];
	}
}

void Shape::computeTangents()
{
	for (int i = 0; i < m_obj_count; i++) {
		for (int j = 0; j < m_indexBuf[i].size(); j += 3) {
			Vertex & v1 = m_vertBuf[i].at(m_indexBuf[i].at(j));
			Vertex & v2 = m_vertBuf[i].at(m_indexBuf[i].at(j + 1));
			Vertex & v3 = m_vertBuf[i].at(m_indexBuf[i].at(j + 2));

			XMVECTOR pos1 = XMLoadFloat3(&v1.position);
			XMVECTOR pos2 = XMLoadFloat3(&v2.position);
			XMVECTOR pos3 = XMLoadFloat3(&v3.position);

			XMVECTOR uv1 = XMLoadFloat2(&v1.uv);
			XMVECTOR uv2 = XMLoadFloat2(&v2.uv);
			XMVECTOR uv3 = XMLoadFloat2(&v3.uv);

			XMVECTOR tempEdge1 = pos2 - pos1;
			XMVECTOR tempEdge2 = pos3 - pos1;
			XMVECTOR tempDeltaUV1 = uv2 - uv1;
			XMVECTOR tempDeltaUV2 = uv3 - uv1;

			XMFLOAT3 edge1;
			XMFLOAT3 edge2;
			XMFLOAT2 deltaUV1;
			XMFLOAT2 deltaUV2;
			XMStoreFloat3(&edge1, tempEdge1);
			XMStoreFloat3(&edge2, tempEdge2);
			XMStoreFloat2(&deltaUV1, tempDeltaUV1);
			XMStoreFloat2(&deltaUV2, tempDeltaUV2);

			float invDeterminant = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

			// Calculate vertex tangent.
			{
				/*XMFLOAT3 tangent;
				tangent.x = invDeterminant * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
				tangent.y = invDeterminant * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
				tangent.z = invDeterminant * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);*/

				/*XMVECTOR tempTangent = XMLoadFloat3(&tangent);
				XMVECTOR updatedTangent1 = XMLoadFloat3(&v1.tangent) + tempTangent;
				XMVECTOR updatedTangent2 = XMLoadFloat3(&v2.tangent) + tempTangent;
				XMVECTOR updatedTangent3 = XMLoadFloat3(&v3.tangent) + tempTangent;

				XMStoreFloat3(&v1.tangent, updatedTangent1);
				XMStoreFloat3(&v2.tangent, updatedTangent2);
				XMStoreFloat3(&v3.tangent, updatedTangent3);*/
			}

			// Calculate vertex bitangent.
			{
				XMFLOAT3 bitangent;
				bitangent.x = invDeterminant * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
				bitangent.y = invDeterminant * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
				bitangent.z = invDeterminant * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

				XMVECTOR tempBitangent = XMLoadFloat3(&bitangent);
				XMVECTOR updatedBitangent1 = XMLoadFloat3(&v1.bitangent) + tempBitangent;
				XMVECTOR updatedBitangent2 = XMLoadFloat3(&v2.bitangent) + tempBitangent;
				XMVECTOR updatedBitangent3 = XMLoadFloat3(&v3.bitangent) + tempBitangent;

				XMStoreFloat3(&v1.bitangent, updatedBitangent1);
				XMStoreFloat3(&v2.bitangent, updatedBitangent2);
				XMStoreFloat3(&v3.bitangent, updatedBitangent3);
			}
		}
	}

	for (int i = 0; i < m_obj_count; i++) {
		for (auto & vertex : m_vertBuf[i]) {
			{
				XMVECTOR tempTangent = XMLoadFloat3(&vertex.tangent);
				XMVector3Normalize(tempTangent);
				XMStoreFloat3(&vertex.tangent, tempTangent);
			}
			{
				XMVECTOR tempBitangent = XMLoadFloat3(&vertex.bitangent);
				XMVector3Normalize(tempBitangent);
				XMStoreFloat3(&vertex.bitangent, tempBitangent);
			}
		}
	}
}

void Shape::resize()
{
	float minX, minY, minZ;
	float maxX, maxY, maxZ;
	float scaleX, scaleY, scaleZ;
	float shiftX, shiftY, shiftZ;
	float epsilon = 0.001f;

	minX = minY = minZ = 1.1754E+38F;
	maxX = maxY = maxZ = -1.1754E+38F;

	// Go through all vertices to determine min and max of each dimension
	for (int i = 0; i < m_obj_count; i++) {
		for (size_t v = 0; v < m_vertBuf[i].size(); v++)
		{
			if (m_vertBuf[i][v].position.x < minX) minX = m_vertBuf[i][v].position.x;
			if (m_vertBuf[i][v].position.x > maxX) maxX = m_vertBuf[i][v].position.x;

			if (m_vertBuf[i][v].position.y < minY) minY = m_vertBuf[i][v].position.y;
			if (m_vertBuf[i][v].position.y > maxY) maxY = m_vertBuf[i][v].position.y;

			if (m_vertBuf[i][v].position.z < minZ) minZ = m_vertBuf[i][v].position.z;
			if (m_vertBuf[i][v].position.z > maxZ) maxZ = m_vertBuf[i][v].position.z;
		}
	}

	// From min and max compute necessary scale and shift for each dimension
	float maxExtent, xExtent, yExtent, zExtent;
	xExtent = maxX - minX;
	yExtent = maxY - minY;
	zExtent = maxZ - minZ;
	if (xExtent >= yExtent && xExtent >= zExtent)
	{
		maxExtent = xExtent;
	}
	if (yExtent >= xExtent && yExtent >= zExtent)
	{
		maxExtent = yExtent;
	}
	if (zExtent >= xExtent && zExtent >= yExtent)
	{
		maxExtent = zExtent;
	}
	scaleX = 2.0f / maxExtent;
	shiftX = minX + (xExtent / 2.0f);
	scaleY = 2.0f / maxExtent;
	shiftY = minY + (yExtent / 2.0f);
	scaleZ = 2.0f / maxExtent;
	shiftZ = minZ + (zExtent / 2.0f);

	// Go through all verticies shift and scale them
	for (int i = 0; i < m_obj_count; i++) {
		for (size_t v = 0; v < m_vertBuf[i].size(); v++)
		{
			m_vertBuf[i][v].position.x = (m_vertBuf[i][v].position.x - shiftX) * scaleX;
			assert(m_vertBuf[i][v].position.x >= -1.0f - epsilon);
			assert(m_vertBuf[i][v].position.x <= 1.0f + epsilon);
			m_vertBuf[i][v].position.y = (m_vertBuf[i][v].position.y - shiftY) * scaleY;
			assert(m_vertBuf[i][v].position.y >= -1.0f - epsilon);
			assert(m_vertBuf[i][v].position.y <= 1.0f + epsilon);
			m_vertBuf[i][v].position.z = (m_vertBuf[i][v].position.z - shiftZ) * scaleZ;
			assert(m_vertBuf[i][v].position.z >= -1.0f - epsilon);
			assert(m_vertBuf[i][v].position.z <= 1.0f + epsilon);
		}
	}
}

void Shape::init(ComPtr<ID3D12Device> device,
	ComPtr<ID3D12GraphicsCommandList> cmdList)
{
	m_vertexBuffer.resize(m_obj_count);
	m_indexBuffer.resize(m_obj_count);

	m_vertexBufferUpload.resize(m_obj_count);
	m_indexBufferUpload.resize(m_obj_count);
	for (int i = 0; i < m_obj_count; i++)
	{
		// Create necessary vertex buffers
		const UINT vertexBufferSize = m_vertBuf[i].size() * sizeof(Vertex);
		m_vertexBuffer[i].resource = DX12Util::CreateDefaultBuffer(
			device.Get(),
			cmdList.Get(),
			m_vertBuf[i].data(),
			vertexBufferSize,
			m_vertexBufferUpload[i]);
	
		// Create necessary index buffers
		const UINT indexBufferSize = m_indexBuf[i].size() * sizeof(Index);
		m_indexBuffer[i].resource = DX12Util::CreateDefaultBuffer(
			device.Get(),
			cmdList.Get(),
			m_indexBuf[i].data(),
			indexBufferSize,
			m_indexBufferUpload[i]);
	}
}

void Shape::finalizeInit() {
	for (int i = 0; i < m_obj_count; i++) {
		m_vertexBufferUpload[i] = nullptr;
		m_indexBufferUpload[i] = nullptr;
	}
}

void Shape::draw(ComPtr<ID3D12GraphicsCommandList> commandList,
	ComPtr<ID3D12Resource> materialBufferUpload,
	std::unordered_map<std::string, std::shared_ptr<Texture>> & diffuseTextures,
	std::unordered_map<std::string, std::shared_ptr<Texture>> & specularTextures,
	std::unordered_map<std::string, std::shared_ptr<Texture>> & normalTextures,
	ComPtr<ID3D12DescriptorHeap> srvHeap,
	UINT srvDescriptorSize) const
{
	for (int i = 0; i < m_obj_count; i++) {
		// Get the vertex and index buffer views
		const UINT vertexBufferSize = m_vertBuf[i].size() * sizeof(Vertex);
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = m_vertexBuffer[i].resource->GetGPUVirtualAddress();
		vbv.StrideInBytes = sizeof(Vertex);
		vbv.SizeInBytes = vertexBufferSize;

		const UINT indexBufferSize = m_indexBuf[i].size() * sizeof(Index);
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = m_indexBuffer[i].resource->GetGPUVirtualAddress();
		ibv.Format = DXGI_FORMAT_R16_UINT; // Needs to match struct Index !
		ibv.SizeInBytes = indexBufferSize;

		// Use zero as the default index.
		UINT materialId = 0;
		UINT diffuseTextureId = 0;
		UINT specularTextureId = 0;
		UINT normalTextureId = 0;
		if (m_materialIDs[i] != -1) {
			materialId = m_materialIDs[i];
			
			const Material & mat = m_materials[materialId];
			std::string diffuseTexName = mat.diffuseTex;
			std::string specularTexName = mat.specularTex;
			std::string normalTexName = mat.normalTex;

			if (diffuseTexName != "") {
				// Get texture from map
				std::shared_ptr<Texture> diffuseTex = diffuseTextures[diffuseTexName];
				diffuseTextureId = diffuseTex->srvHeapIndex;

				// Bind texture.
				CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(srvHeap->GetGPUDescriptorHandleForHeapStart(), diffuseTextureId, srvDescriptorSize);
				commandList->SetGraphicsRootDescriptorTable(1, srvHandle);
			}
			if (specularTexName != "") {
				// Get texture from map
				std::shared_ptr<Texture> specularTex = specularTextures[specularTexName];
				specularTextureId = specularTex->srvHeapIndex;

				// Bind texture.
				CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(srvHeap->GetGPUDescriptorHandleForHeapStart(), specularTextureId, srvDescriptorSize);
				commandList->SetGraphicsRootDescriptorTable(2, srvHandle);
			}
			if (normalTexName != "") {
				// Get texture from map
				std::shared_ptr<Texture> normalTex = normalTextures[normalTexName];
				normalTextureId = normalTex->srvHeapIndex;

				// Bind texture.
				CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(srvHeap->GetGPUDescriptorHandleForHeapStart(), normalTextureId, srvDescriptorSize);
				commandList->SetGraphicsRootDescriptorTable(3, srvHandle);
			}
		}
		
		// Bind material.
		UINT materialBufferSize = (sizeof(MaterialCBuffer) + 255) & ~255;
		D3D12_GPU_VIRTUAL_ADDRESS materialBufferAddress = materialBufferUpload->GetGPUVirtualAddress() + (materialId * materialBufferSize);
		commandList->SetGraphicsRootConstantBufferView(6, materialBufferAddress);

		// Add draw commands to command list
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &vbv);
		commandList->IASetIndexBuffer(&ibv);
		commandList->DrawIndexedInstanced(m_indexBuf[i].size(), 1, 0, 0, 0);
	}
}

bool Shape::GetDiffuseTextureGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE & gpuHandle, UINT shapeIndex)
{
	UINT materialId = 0;
	if (m_materialIDs[shapeIndex] != -1) {
		materialId = m_materialIDs[shapeIndex];

		const Material & mat = m_materials[materialId];
		std::string diffuseTexName = mat.diffuseTex;
		if (diffuseTexName != "") {
			// Get texture from map
			std::shared_ptr<Texture> diffuseTex = m_diffuseTextures[diffuseTexName];

			// Texture found.
			gpuHandle = diffuseTex->gpuHandle;
			return true;
		}
	}

	// Texture not found.
	return false;
}

bool Shape::GetNormalTextureGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE & gpuHandle, UINT shapeIndex)
{
	UINT materialId = 0;
	if (m_materialIDs[shapeIndex] != -1) {
		materialId = m_materialIDs[shapeIndex];

		const Material & mat = m_materials[materialId];
		std::string diffuseTexName = mat.diffuseTex;
		if (diffuseTexName != "") {
			// Get texture from map
			std::shared_ptr<Texture> diffuseTex = m_diffuseTextures[diffuseTexName];

			// Texture found.
			gpuHandle = diffuseTex->gpuHandle;
			return true;
		}
	}

	// Texture not found.
	return false;
}