#pragma once

#include <string>

#include "DXSampleHelper.h"

// TODO(rgarmsen2295): See if this is merge-able with MaterialCBuffer
struct Material
{
	XMVECTOR ambient = XMVECTOR();
	XMVECTOR diffuse = XMVECTOR();
	XMVECTOR specular = XMVECTOR();
	float shininess = 0.0f;

	std::string diffuseTex = "";
	std::string specularTex = "";
	std::string normalTex = "";
};

