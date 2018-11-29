#include "stdafx.h"
#include "Camera.h"

// Ninety degrees in radians
constexpr float ninetyRad = 90.0f * XM_PI / 180.0f;

Camera::Camera() {
	XMFLOAT3 eyePos(0, 0, -1);
	Eye = XMLoadFloat3(&eyePos);

	XMFLOAT3 upVec(0, 1, 0);
	Up = XMLoadFloat3(&upVec);
}

Camera::~Camera() {}

XMVECTOR &Camera::getEye() { return Eye; }

XMVECTOR &Camera::getLookAt() {
	XMFLOAT3 laVec(cos(alphaRad) * cos(betaRad), sin(alphaRad),
		cos(alphaRad) * cos(ninetyRad - betaRad));

	LA = XMLoadFloat3(&laVec);
	return LA;
}

XMVECTOR &Camera::getUp() { return Up; }

XMVECTOR Camera::getTarget() { return getEye() + getLookAt();  }

void Camera::setEye(XMVECTOR newEye) { Eye = newEye; }

void Camera::setLookAt(XMVECTOR newLA) { LA = newLA; }

void Camera::setUp(XMVECTOR newUp) { Up = newUp; }

void Camera::setScreenInfo(int width, int height) {
	screenWidth = width;
	screenHeight = height;

	currentMouseX = width / 2.0f;
	currentMouseY = width / 2.0f;
}

void Camera::changeAlpha(float deltaAlpha) {
	alpha += deltaAlpha;

	// Prevents cross product explosions
	if (alpha >= 80) {
		alpha = 80;
	}
	else if (alpha <= -80) {
		alpha = -80;
	}

	alphaRad = alpha * XM_PI / 180.0f;
}

void Camera::changeBeta(float deltaBeta) {
	beta += deltaBeta;
	betaRad = beta * XM_PI / 180.0f;
}

void Camera::update(float deltaTime) {

	XMVECTOR gazeVec = XMVector3Normalize(getLookAt());

	XMVECTOR w = gazeVec;
	XMVECTOR upCrossW = XMVector3Normalize(XMVector3Cross(Up, w));
	XMVECTOR u = upCrossW;

	if (GetKeyState('W') & 0x8000 ||
		GetKeyState(VK_UP) & 0x8000) {

		// Move in the neg dir along the w camera axis (forward)
		setEye(Eye + (WSScale * w * deltaTime));
	}

	if (GetKeyState('A') & 0x8000 ||
		GetKeyState(VK_LEFT) & 0x8000) {

		// Move in the neg dir along the u camera axis (left)
		setEye(Eye - (ADScale * u * deltaTime));
	}

	if (GetKeyState('S') & 0x8000 ||
		GetKeyState(VK_DOWN) & 0x8000) {

		// Move in the npos dir along the w camera axis (backwards)
		setEye(Eye - (WSScale * w * deltaTime));
	}

	if (GetKeyState('D') & 0x8000 ||
		GetKeyState(VK_RIGHT) & 0x8000) {

		// Move in the pos dir along the u camera axis (right)
		setEye(Eye + (ADScale * u * deltaTime));
	}

	// Update mouse cursor position data
	if (GetKeyState(VK_RBUTTON) < 0) {
		POINT newMousePos;
		GetCursorPos(&newMousePos);
		float newMouseX = static_cast<float>(newMousePos.x);
		float newMouseY = static_cast<float>(newMousePos.y);

		if (!wasMouseDown) {
			wasMouseDown = true;
			currentMouseX = newMouseX;
			currentMouseY = newMouseY;
		}

		// Update angle info
		float deltaAlphaScale = 180.0f / screenHeight;
		float deltaBetaScale = 360.0f / screenWidth;

		// Negative to prevent inverted control
		float deltaX = -(newMouseX - currentMouseX);
		float deltaY = -(newMouseY - currentMouseY);

		currentMouseX = newMouseX;
		currentMouseY = newMouseY;

		float deltaAlpha = deltaY * deltaAlphaScale;
		float deltaBeta = deltaX * deltaBetaScale;
		changeAlpha(deltaAlpha);
		changeBeta(deltaBeta);
	}
	else {
		wasMouseDown = false;
	}
}
