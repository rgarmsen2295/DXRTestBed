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

#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

#ifdef HLSL
#include "HlslCompat.h"
#else
using namespace DirectX;
#endif

// Shader will use byte encoding to access indices.
typedef UINT Index;

struct ProceduralPrimitiveAttributes
{
	XMFLOAT3 normal;
};

struct RayPayload
{
	XMFLOAT4 color;
	UINT   recursionDepth;
	bool isMiss;
};

struct ShadowRayPayload
{
	XMFLOAT3 origin;
	XMFLOAT3 direction;
	bool hit;
};

struct SceneConstantBuffer
{
    XMMATRIX projectionToWorld;
    XMVECTOR cameraPosition;
    XMVECTOR lightPosition;
    XMVECTOR lightAmbientColor;
    XMVECTOR lightDiffuseColor;
	UINT	 useGlobalIllumination;
	UINT	 useNormalTexturing;
	UINT	 numGISamples;
	UINT	 useCharacterSpheresForPrimary;
	UINT	 useCharacterMeshForGI;
};

struct CubeConstantBuffer
{
    XMFLOAT4 albedo;
	UINT	 useDiffuseTexture;
	UINT	 useNormalTexture;
	UINT	 isDynamic;
};

struct Vertex
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
	XMFLOAT3 tangent;
	XMFLOAT3 bitangent;
};

struct Sphere
{
	XMFLOAT4 info; // xyz - center position, w - radius
	XMMATRIX transform;
	XMMATRIX invTransform;
};

// Ray types traced in this sample.
namespace RayType {
	enum Enum {
		Radiance = 0,   // ~ Primary, reflected camera/view rays calculating color for each hit.
		Shadow,         // ~ Shadow/visibility rays, only testing for occlusion
		Count
	};
}

namespace TraceRayParameters
{
	static const UINT InstanceMask = ~0;   // Everything is visible.
	namespace HitGroup {
		static const UINT Offset[RayType::Count] =
		{
			0, // Radiance ray
			1  // Shadow ray
		};
		static const UINT GeometryStride = RayType::Count;
	}
	namespace MissShader {
		static const UINT Offset[RayType::Count] =
		{
			0, // Radiance ray
			1  // Shadow ray
		};
	}
}

//typedef UINT SphereIndex;

// Attributes per primitive type.
struct PrimitiveConstantBuffer
{
	XMFLOAT4 albedo;
	float reflectanceCoef;
	float diffuseCoef;
	float specularCoef;
	float specularPower;
	float stepScale;                      // Step scale for ray marching of signed distance primitives. 
										  // - Some object transformations don't preserve the distances and 
										  //   thus require shorter steps.
	XMFLOAT3 padding;
};

// Attributes per primitive instance.
struct PrimitiveInstanceConstantBuffer
{
	UINT instanceIndex;
	UINT primitiveType; // Procedural primitive type
};

// Dynamic attributes per primitive instance.
struct PrimitiveInstancePerFrameBuffer
{
	XMMATRIX localSpaceToBottomLevelAS;   // Matrix from local primitive space to bottom-level object space.
	XMMATRIX bottomLevelASToLocalSpace;   // Matrix from bottom-level object space to local primitive space.
};

#endif // RAYTRACINGHLSLCOMPAT_H