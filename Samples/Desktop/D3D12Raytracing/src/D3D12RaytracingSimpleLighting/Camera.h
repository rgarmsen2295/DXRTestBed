#pragma once

#define _USE_MATH_DEFINES
#include <math.h>

#include "DXSample.h"

using namespace DirectX;

// Assumes LH-coordinate system
class Camera {
public:
	// Scale for the W and A key movements
	const float WSScale = 0.05f;

	// Scale for the A and S key movements
	const float ADScale = 0.05f;

	Camera();

	~Camera();

	XMVECTOR &getEye();

	// Returns a reference to the current look-at vector for the camera
	XMVECTOR &getLookAt();

	XMVECTOR &getUp();

	XMVECTOR getTarget();

	void setEye(XMVECTOR newEye);

	void setLookAt(XMVECTOR newLA);

	void setUp(XMVECTOR newUp);

	void setScreenInfo(int width, int height);

	void changeAlpha(float deltaAlpha);

	void changeBeta(float deltaBeta);

	void update(float deltaTime);

private:
	// Vector's the define the camera
	XMVECTOR Eye;
	XMVECTOR LA;
	XMVECTOR Up;

	// Angle values used for pitch and yaw respectively
	float alpha = 0;
	float beta = 90;

	// Radian equivalents for alpha and beta
	float alphaRad = alpha * XM_PI / 180.0f;
	float betaRad = beta * XM_PI / 180.0f;

	// Mouse cursor position data
	// TODO(rgarmsen2295): Move to a window manager class
	float currentMouseX;
	float currentMouseY;

	UINT screenWidth;
	UINT screenHeight;

	bool wasMouseDown = false;
};
