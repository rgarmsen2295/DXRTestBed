#pragma once

#include "DXSampleHelper.h"

struct MaterialCBuffer
{
	XMVECTOR ambientMat;
	XMVECTOR diffuseMat;
	XMVECTOR specularMat;
	float shininess;
	int useDiffuseTexture;
	int useSpecularTexture;
	int useNormalTexture;

	MaterialCBuffer()
	{
		ambientMat = XMVECTOR();
		diffuseMat = XMVECTOR();
		specularMat = XMVECTOR();
		shininess = 0.0f;
		useDiffuseTexture = 0;
		useSpecularTexture = 0;
		useNormalTexture = 0;
	}
};
