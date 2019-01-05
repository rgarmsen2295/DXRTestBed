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

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

#define MAX_RAY_RECURSION_DEPTH 3

struct Ray
{
    float3 origin;
    float3 direction;
};

static const float PI = 3.14159265f;

// Global Resources
RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
//ByteAddressBuffer Indices : register(t1, space0);
SamplerState g_sampler : register(s0, space0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);

// Triangle Local Resources
ConstantBuffer<CubeConstantBuffer> l_cubeCB : register(b1);
StructuredBuffer<Index> Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);
Texture2D<float4> l_triangleDiffuseTex : register(t3, space0);
Texture2D<float4> l_triangleNormalTex : register(t4, space0);

// Sphere Local Resources
ConstantBuffer<Sphere> l_sphereCB : register(b2);
Texture2D<float4> l_sphereDiffuseTex : register(t5, space0);

typedef BuiltInTriangleIntersectionAttributes TriangleAttributes;

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's barycentrics.
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
    }

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline Ray GenerateCameraRay(uint2 index, in float3 cameraPosition, in float4x4 projectionToWorld)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a world positon.
    float4 world = mul(float4(screenPos, 0, 1), projectionToWorld);
    world.xyz /= world.w;

    Ray ray;
    ray.origin = cameraPosition;
    ray.direction = normalize(world.xyz - ray.origin);

    return ray;
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);
}

// Diffuse lighting calculation.
float4 CalculateDiffuseLighting(float3 hitPosition, float3 normal)
{
    float3 pixelToLight = normalize(g_sceneCB.lightPosition.xyz - hitPosition);

    // Diffuse contribution.
    float fNDotL = max(0.0f, dot(pixelToLight, normal));

    return g_sceneCB.lightDiffuseColor * fNDotL;
    }

// Calculate ray differentials.
void CalculateRayDifferentials(out float2 ddx_uv, out float2 ddy_uv, in float2 uv, in float3 hitPosition, in float3 surfaceNormal, in float3 cameraPosition, in float4x4 projectionToWorld)
{
    // Compute ray differentials by intersecting the tangent plane to the  surface.
    Ray ddx = GenerateCameraRay(DispatchRaysIndex().xy + uint2(1, 0), cameraPosition, projectionToWorld);
    Ray ddy = GenerateCameraRay(DispatchRaysIndex().xy + uint2(0, 1), cameraPosition, projectionToWorld);

    // Compute ray differentials.
    float3 ddx_pos = ddx.origin - ddx.direction * dot(ddx.origin - hitPosition, surfaceNormal) / dot(ddx.direction, surfaceNormal);
    float3 ddy_pos = ddy.origin - ddy.direction * dot(ddy.origin - hitPosition, surfaceNormal) / dot(ddy.direction, surfaceNormal);

    // Calculate texture sampling footprint.
    ddx_uv = /*TexCoords(ddx_pos) -*/ uv;
    ddy_uv = /*TexCoords(ddy_pos) -*/ uv;
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;
    
    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0), 1, false };
    TraceRay(Scene,
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
		TraceRayParameters::InstanceMask,
        TraceRayParameters::HitGroup::Offset[RayType::Radiance],
        TraceRayParameters::HitGroup::GeometryStride,
        TraceRayParameters::MissShader::Offset[RayType::Radiance],
        ray,
        payload
    );
    
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

// Trace a shadow ray and return true if it hits any geometry.
bool TraceShadowRayAndReportIfHit(in Ray ray, UINT currentRayRecursionDepth)
{
    if (currentRayRecursionDepth > MAX_RAY_RECURSION_DEPTH)
    {
        return false;
    }

    // Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    // Set TMin to a zero value to avoid aliasing artifcats along contact areas.
    // Note: make sure to enable back-face culling so as to avoid surface face fighting.
    rayDesc.TMin = 0.1;
    rayDesc.TMax = 10000;

    // Initialize shadow ray payload.
    // Set the initial value to true since closest and any hit shaders are skipped. 
    // Shadow miss shader, if called, will set it to false.
    ShadowRayPayload shadowPayload = { true };
    TraceRay(Scene,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES
        | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
        | RAY_FLAG_FORCE_OPAQUE // ~skip any hit shaders
        | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, // ~skip closest hit shaders,
        TraceRayParameters::InstanceMask,
        TraceRayParameters::HitGroup::Offset[RayType::Shadow],
        TraceRayParameters::HitGroup::GeometryStride,
        TraceRayParameters::MissShader::Offset[RayType::Shadow],
        rayDesc,
        shadowPayload
    );

    return shadowPayload.hit;
}

// Trace a shadow ray and return true if it hits any geometry.
RayPayload TraceGIRay(in Ray ray, UINT currentRayRecursionDepth)
{
    // Set the ray's extents.
    RayDesc rayDesc;
    rayDesc.Origin = ray.origin;
    rayDesc.Direction = ray.direction;
    // Set TMin to a zero value to avoid aliasing artifcats along contact areas.
    // Note: make sure to enable back-face culling so as to avoid surface face fighting.
    rayDesc.TMin = 0.1;
    rayDesc.TMax = 1.0;//000; //0.5

    // Initialize shadow ray payload.
    // Set the initial value to true since closest and any hit shaders are skipped. 
    // Shadow miss shader, if called, will set it to false.
    RayPayload giPayload = { float4(0, 0, 0, 0), currentRayRecursionDepth + 1, false };
    TraceRay(Scene,
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
		TraceRayParameters::InstanceMask,
        TraceRayParameters::HitGroup::Offset[RayType::Radiance],
        TraceRayParameters::HitGroup::GeometryStride,
        TraceRayParameters::MissShader::Offset[RayType::Radiance],
        rayDesc,
        giPayload
    );

    return giPayload;
}

// Returns the x and y components of a ray according to the values u1 and u2 (should be passed as random values
// between 0 and 1)
float3 SampleDisk(in float u1, in float u2)
{
    float r = sqrt(1.0 - u1 * u1);//sqrt(u1);
    float theta = 2.0f * PI * u2;
    float dX = r * cos(theta);
    //float dY = r * sin(theta);
    float dZ = r * sin(theta);

    return float3(dX, u1, dZ);
}

// Source: http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
float InterleavedGradientNoise(float2 pixelPos)
{
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    
    return frac(magic.z * frac(dot(pixelPos, magic.xy)));
}

// Source: http://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
uint rand_xorshift()
{
    static uint seed = DispatchRaysIndex().x * 480 + DispatchRaysIndex().y;

    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

// Converted to hlsl from source: http://www.neilmendoza.com/glsl-rotation-about-an-arbitrary-axis/
matrix RotationMatrix(float3 axis, float angle)
{
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;
    
    return matrix(oc * axis.x * axis.x + c, oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s, 0.0,
            oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c, oc * axis.y * axis.z - axis.x * s, 0.0,
            oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c, 0.0,
            0.0, 0.0, 0.0, 0.0);
}

void createCoordinateSystem(in float3 N, out float3 Nt, out float3 Nb) 
{ 
    if (abs(N.x) > abs(N.y)) 
        Nt = float3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z); 
    else 
        Nt = float3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z); 
    Nb = cross(N, Nt); 
}

float3 normalSampleToWorldSpace(float3 normalMapSample, float3 normalInWorld, float3 tangentInWorld, float3 bitangentInWorld)
{
    float3 normalT = 2.0f * normalMapSample - 1.0f;
	
    float3 N = normalInWorld;
    float3 T = tangentInWorld;
    float3 B = bitangentInWorld;

    float3x3 TBN = float3x3(T.x, B.x, N.x,
							T.y, B.y, N.y,
							T.z, B.z, N.z);

    float3 normalMapSampleInWorld = mul(TBN, normalT);

    return -normalMapSampleInWorld;
}

float4 calculateDiffuseGI(in RayPayload payload, in float3 hitPosition, in float3 normal)
{
    float4 diffuseGIColor = float4(0.0, 0.0, 0.0, 0.0);

    int numGIRays = g_sceneCB.numGISamples;
    int numGIRaysHit = 0;
            
    // based parially from source provided at: https://www.scratchapixel.com/code.php?id=34&origin=/lessons/3d-basic-rendering/global-illumination-path-tracing
    float3 Nt, Nb;
    createCoordinateSystem(normal, Nt, Nb); 
    float pdf = 1 / (2 * PI); 
    for (int i = 0; i < numGIRays; i++)
    {
        float2 u = float2(float(rand_xorshift()) * (1.0 / 4294967296.0), float(rand_xorshift()) * (1.0 / 4294967296.0)); //InterleavedGradientNoise((hitPosition.xy + i) / numGIRays);
        //diffuseGIColor.rg = u;
        //break;
        float3 dir;
        dir = SampleDisk(u.x, u.y/*, dir.x, dir.y*/);

        float3 dirInWorld = float3(
            dir.x * Nb.x + dir.y * normal.x + dir.z * Nt.x,
            dir.x * Nb.y + dir.y * normal.y + dir.z * Nt.y,
            dir.x * Nb.z + dir.y * normal.z + dir.z * Nt.z);
        dirInWorld = normalize(dirInWorld);

        // Set up and trace GI ray.
        Ray giRay = { hitPosition, dirInWorld };
        RayPayload giPayload = TraceGIRay(giRay, payload.recursionDepth);

        if (!giPayload.isMiss)
        {
            diffuseGIColor += giPayload.color * u.x / pdf;
            numGIRaysHit++;
        }
    }
        
    diffuseGIColor /= max(numGIRaysHit, 1);

    return diffuseGIColor;
}

[shader("closesthit")]
void TriangleClosestHitShader(inout RayPayload payload, in TriangleAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    // Get the base index of the triangle's first 16 bit index.
    //uint indexSizeInBytes = 2;
    //uint indicesPerTriangle = 3;
    //uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * 3;//triangleIndexStride;

    // Load up 3 16 bit indices for the triangle.
    //const uint3 indices = Load3x16BitIndices(baseIndex);
    const uint3 indices = { Indices[baseIndex], Indices[baseIndex + 1], Indices[baseIndex + 2] };

    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 vertexNormals[3] =
    {
        Vertices[indices[0]].normal,
        Vertices[indices[1]].normal,
        Vertices[indices[2]].normal 
    };
    float3 vertexTangents[3] =
    {
        Vertices[indices[0]].tangent,
        Vertices[indices[1]].tangent,
        Vertices[indices[2]].tangent 
    };
    float3 vertexBitangents[3] =
    {
        Vertices[indices[0]].bitangent,
        Vertices[indices[1]].bitangent,
        Vertices[indices[2]].bitangent 
    };

    float3 vertexUVs[3] =
    {
        float3(Vertices[indices[0]].uv, 0.0),
        float3(Vertices[indices[1]].uv, 0.0),
        float3(Vertices[indices[2]].uv, 0.0)
    };

    float2 textureUV = HitAttribute(vertexUVs, attr).rg;

    // Get texture values.
    float3 triangleNormal;
    if (g_sceneCB.useNormalTexturing && l_cubeCB.useNormalTexture > 0)
    {
        uint2 texDimsTemp;
        l_triangleNormalTex.GetDimensions(texDimsTemp.x, texDimsTemp.y);
        int2 texDims = texDimsTemp - 1;
             
        triangleNormal = normalize(l_triangleDiffuseTex.Load(int3(abs(int2(textureUV * texDims)) % texDims, 0.0)));
        triangleNormal = normalize(normalSampleToWorldSpace(triangleNormal, HitAttribute(vertexNormals, attr), HitAttribute(vertexTangents, attr), HitAttribute(vertexBitangents, attr)));


        // Disable normal mapping for now.    
        //triangleNormal = HitAttribute(vertexNormals, attr);
    }
    else
    {
        // Compute the triangle's normal.
        // This is redundant and done for illustration purposes 
        // as all the per-vertex normals are the same and match triangle's normal in this sample. 
        triangleNormal = normalize(HitAttribute(vertexNormals, attr));
    }

    // Shadow component.
    // Trace a shadow ray.
    float3 shadowDir = normalize(g_sceneCB.lightPosition.xyz - hitPosition);
    Ray shadowRay = { hitPosition /*+ (0.01 * shadowDir)*/, shadowDir };
    float shadowRayHit = TraceShadowRayAndReportIfHit(shadowRay, payload.recursionDepth + 1) ? 0.0 : 1.0;

    float4 diffuseColor = CalculateDiffuseLighting(hitPosition, triangleNormal);
    
    float4 diffuseTextureColor = float4(1.0, 1.0, 1.0, 1.0);
    float alpha = 1.0;
    if (l_cubeCB.useDiffuseTexture > 0)
    {
        uint2 texDimsTemp;
        l_triangleDiffuseTex.GetDimensions(texDimsTemp.x, texDimsTemp.y);
        int2 texDims = texDimsTemp - 1;

        //float2 ddx_uv;
        //float2 ddy_uv;
        //CalculateRayDifferentials(ddx_uv, ddy_uv, textureUV, hitPosition, triangleNormal, g_sceneCB.cameraPosition, g_sceneCB.projectionToWorld);

        diffuseTextureColor = l_triangleDiffuseTex.Load(int3(abs(int2(textureUV * texDims)) % texDims, 0.0));
        alpha = diffuseTextureColor.a;
    }

    // Calculate diffuse GI.
    float4 diffuseGIColor = float4(0.0, 0.0, 0.0, 0.0);
    if (payload.recursionDepth < 2 && g_sceneCB.useGlobalIllumination)
    {
        diffuseGIColor = calculateDiffuseGI(payload, hitPosition, triangleNormal);
    }
    else
    {
        // Use basic ambient "guess" if we're past the max ray depth.
        //diffuseGIColor = 0.0;//g_sceneCB.lightAmbientColor * (payload.recursionDepth > 1 ? 0.2 : 1.0);
        diffuseGIColor = g_sceneCB.lightAmbientColor * (payload.recursionDepth > 1 ? 0.0 : 1.0);
    }

    //float4 color = g_sceneCB.lightAmbientColor * diffuseTextureColor + diffuseColor * diffuseTextureColor * shadowRayHit;
    //float4 color = (payload.recursionDepth > 1 ? 0.0 : (diffuseGIColor * diffuseTextureColor)) + (diffuseColor * diffuseTextureColor * shadowRayHit);
    float4 color = (diffuseGIColor * diffuseTextureColor) + (diffuseColor * diffuseTextureColor * shadowRayHit);
    color.a = alpha;

        //color.rgb = triangleNormal;

    payload.color = color;
}

// Source: https://gamedev.stackexchange.com/questions/114412/how-to-get-uv-coordinates-for-sphere-cylindrical-projection
float2 GetSphereTexCoords(in float3 surfacePosition, in float3 normal)
{
    float2 uv;
    uv.x = atan2(-normal.x, normal.z) / (2 * PI) + 0.5;
    uv.y = normal.y * 0.5 + 0.5;

    return uv;
}

float4 GetSphereTextureColor(in float3 hitPosition, in float3 normal)
{
    float2 ddx_uv;
    float2 ddy_uv;
    float2 uv = GetSphereTexCoords(hitPosition, normal);

    uint2 texDimsTemp;
    l_sphereDiffuseTex.GetDimensions(texDimsTemp.x, texDimsTemp.y);

    int2 texDims = texDimsTemp - 1;

    //CalculateRayDifferentials(ddx_uv, ddy_uv, uv, hitPosition, surfaceNormal, cameraPosition, projectionToWorld);
    return l_sphereDiffuseTex.Load(int3(abs(int2(uv * texDims)) % texDims, 0.0));
}

[shader("closesthit")]
void AABBClosestHitShader(inout RayPayload payload, in ProceduralPrimitiveAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
    float4 textureColor = GetSphereTextureColor(hitPosition, attr.normal);

    // Shadow component.
    // Trace a shadow ray.
    float3 shadowDir = normalize(g_sceneCB.lightPosition.xyz - hitPosition);
    Ray shadowRay = { hitPosition /*+ (0.01 * shadowDir)*/, shadowDir };
    float shadowRayHit = TraceShadowRayAndReportIfHit(shadowRay, payload.recursionDepth) ? 0.0 : 1.0;

    payload.color = textureColor * g_sceneCB.lightAmbientColor + textureColor * CalculateDiffuseLighting(HitWorldPosition(), attr.normal) * shadowRayHit;
    payload.color.a = 1.0;

}

void swap(inout float a, inout float b)
{
    float temp = a;
    a = b;
    b = temp;
}

bool IsInRange(in float val, in float min, in float max)
{
    return (val >= min && val <= max);
}

// Test if a hit is culled based on specified RayFlags.
bool IsCulled(in Ray ray, in float3 hitSurfaceNormal)
{
    float rayDirectionNormalDot = dot(ray.direction, hitSurfaceNormal);

    bool isCulled =
        ((RayFlags() & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) && (rayDirectionNormalDot > 0))
        ||
        ((RayFlags() & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) && (rayDirectionNormalDot < 0));

    return isCulled;
}

// Solve a quadratic equation.
// Ref: https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection
bool SolveQuadraticEqn(float a, float b, float c, out float x0, out float x1)
{
    float discr = b * b - 4 * a * c;
    if (discr < 0)
        return false;
    else if (discr == 0)
        x0 = x1 = -0.5 * b / a;
    else
    {
        float q = (b > 0) ?
            -0.5 * (b + sqrt(discr)) :
            -0.5 * (b - sqrt(discr));
        x0 = q / a;
        x1 = c / q;
    }
    if (x0 > x1)
        swap(x0, x1);

    return true;
}

// Calculate a normal for a hit point on a sphere.
float3 CalculateNormalForARaySphereHit(in Ray ray, in float thit, float3 center)
{
    float3 hitPosition = ray.origin + thit * ray.direction;
    return normalize(hitPosition - center);
}

// Analytic solution of an unbounded ray sphere intersection points.
// Ref: https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection
bool SolveRaySphereIntersectionEquation(in Ray ray, out float tmin, out float tmax, in float3 center, in float radius)
{
    float3 L = ray.origin - center;
    float a = dot(ray.direction, ray.direction);
    float b = 2 * dot(ray.direction, L);
    float c = dot(L, L) - radius * radius;
    return SolveQuadraticEqn(a, b, c, tmin, tmax);
}

// Test if a hit is valid based on specified RayFlags and <RayTMin, RayTCurrent> range.
bool IsAValidHit(in Ray ray, in float thit, in float3 hitSurfaceNormal)
{
    return IsInRange(thit, RayTMin(), RayTCurrent()) && !IsCulled(ray, hitSurfaceNormal);
}

// Get ray in AABB's local space.
Ray GetRayInAABBPrimitiveLocalSpace()
{
    //PrimitiveInstancePerFrameBuffer attr = g_AABBPrimitiveAttributes[l_aabbCB.instanceIndex];

    // Retrieve a ray origin position and direction in bottom level AS space 
    // and transform them into the AABB primitive's local space.
    Ray ray;
    ray.origin = ObjectRayOrigin();//mul(float4(ObjectRayOrigin(), 1), attr.bottomLevelASToLocalSpace).xyz;
    ray.direction = ObjectRayDirection();//mul(ObjectRayDirection(), (float3x3) attr.bottomLevelASToLocalSpace);
    return ray;
}

[shader("intersection")]
void SphereIntersectionShader()
{
    ProceduralPrimitiveAttributes attr;
    
    //attr.normal = float3(1.0, 1.0, 1.0);
    //ReportHit(RayTMin() + 1.0, /*hitKind*/0, attr);
    //return;

    float3 center = l_sphereCB.info.xyz;
    float radius = l_sphereCB.info.w;
    Ray ray = GetRayInAABBPrimitiveLocalSpace();

    float t0, t1; // solutions for t if the ray intersects 

    if (!SolveRaySphereIntersectionEquation(ray, t0, t1, center, radius))
    {
        return;
    }
    float tmax = t1;
    if (t0 < RayTMin())
    {
        // t0 is before RayTMin, let's use t1 instead .
        if (t1 < RayTMin())
            return; // both t0 and t1 are before RayTMin

        attr.normal = CalculateNormalForARaySphereHit(ray, t1, center);
        if (IsAValidHit(ray, t1, attr.normal))
        {
            float thit = t1;
            ReportHit(thit, /*hitKind*/0, attr);
        }
    }
    else
    {
        attr.normal = CalculateNormalForARaySphereHit(ray, t0, center);
        if (IsAValidHit(ray, t0, attr.normal))
        {
            float thit = t0;
            ReportHit(thit, /*hitKind*/0, attr);
        }

        attr.normal = CalculateNormalForARaySphereHit(ray, t1, center);
        if (IsAValidHit(ray, t1, attr.normal))
        {
            float thit = t1;
            ReportHit(thit, /*hitKind*/0, attr);
        }
    }
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float4 background = g_sceneCB.lightAmbientColor;//float4(0.1f, 0.1f, 0.1f, 1.0f);//float4(1.0f, 1.0f, 1.0f, 1.0f); float4(0.0f, 0.2f, 0.4f, 1.0f);
    payload.color = background;
    payload.isMiss = true;
}

[shader("miss")]
void MyMissShader_ShadowRay(inout ShadowRayPayload rayPayload)
{
    rayPayload.hit = false;
}

#endif // RAYTRACING_HLSL
