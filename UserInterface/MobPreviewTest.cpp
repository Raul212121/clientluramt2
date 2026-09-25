#include "StdAfx.h"
#include "MobPreviewTest.h"
#include "../EterLib/StateManager.h"
#include "../EterPythonLib/PythonGraphic.h"
#include "../EterLib/Camera.h"
#include "../EterPythonLib/PythonWindowManager.h"

CMobPreviewTest::CMobPreviewTest()
	: m_isShow(false),
	m_dwRace(0),
	m_viewX(0),
	m_viewY(0),
	m_viewWidth(400),
	m_viewHeight(500)
{
}


CMobPreviewTest::~CMobPreviewTest()
{
	Destroy();
}

void CMobPreviewTest::Toggle()
{
	m_isShow = !m_isShow;
}

void CMobPreviewTest::Show()
{
	m_isShow = true;
}

void CMobPreviewTest::Hide()
{
	m_isShow = false;
}

bool CMobPreviewTest::IsShow() const
{
	return m_isShow;
}

void CMobPreviewTest::SetViewport(int x, int y, int width, int height)
{
	m_viewX = x;
	m_viewY = y;
	m_viewWidth = width;
	m_viewHeight = height;
}

void CMobPreviewTest::Destroy()
{
	m_Actor.Destroy();
	m_dwRace = 0;
	m_isShow = false;
}

bool CMobPreviewTest::Create(DWORD dwRace)
{
	Destroy();

	m_Actor.SetActorType(CActorInstance::TYPE_ENEMY);

	if (!m_Actor.SetRace(dwRace))
		return false;

	m_Actor.SetShape(0);
	m_Actor.SetLoopMotion(CRaceMotionData::NAME_WAIT);
	m_Actor.SetRotation(180.0f);

	TPixelPosition kPos;
	kPos.x = 0.0f;
	kPos.y = 0.0f;
	kPos.z = 0.0f;

	m_Actor.SetPixelPosition(kPos);

	m_dwRace = dwRace;
	m_isShow = true;

	return true;
}

void CMobPreviewTest::Update()
{
	if (!m_isShow)
		return;

	m_Actor.INSTANCEBASE_Transform();
	m_Actor.INSTANCEBASE_Deform();
}

void CMobPreviewTest::Render()
{
	if (!m_isShow)
		return;
	CPythonGraphic& rkGraphic = CPythonGraphic::Instance();

	DWORD previewX = m_viewX;
	DWORD previewY = m_viewY;
	DWORD previewWidth = m_viewWidth;
	DWORD previewHeight = m_viewHeight;

	CCamera* pCamera = CCameraManager::Instance().GetCurrentCamera();
	if (!pCamera)
		return;

	D3DXVECTOR3 oldEye = pCamera->GetEye();
	D3DXVECTOR3 oldTarget = pCamera->GetTarget();
	D3DXVECTOR3 oldUp = pCamera->GetUp();

	rkGraphic.PushState();

	rkGraphic.SetViewport(
		(float)previewX,
		(float)previewY,
		(float)previewWidth,
		(float)previewHeight
	);

	rkGraphic.SetGameRenderState();
	rkGraphic.ClearDepthBuffer();
	float aspect = (float)previewWidth / (float)previewHeight;
	rkGraphic.SetPerspective(30.0f, aspect, 10.0f, 5000.0f);

	float fHeight = m_Actor.GetHeight();

	if (fHeight < 100.0f)
		fHeight = 100.0f;

	float fDistance = fHeight * 2.4f;

	if (fDistance < 300.0f)
		fDistance = 300.0f;

	if (fDistance > 1800.0f)
		fDistance = 1800.0f;

	float fTargetZ = fHeight * 0.5f;

	pCamera->SetViewParams(
		D3DXVECTOR3(
			0.0f,
			-fDistance,
			fTargetZ + (fDistance * 0.12f)
		),
		D3DXVECTOR3(
			0.0f,
			0.0f,
			fTargetZ
		),
		D3DXVECTOR3(
			0.0f,
			0.0f,
			1.0f
		)
	);

	rkGraphic.UpdateViewMatrix();
	rkGraphic.SetMobPreviewLight();

	m_Actor.Render();
	pCamera->SetViewParams(oldEye, oldTarget, oldUp);

	rkGraphic.RestoreViewport();
	rkGraphic.PopState();

	rkGraphic.SetInterfaceRenderState();
}

