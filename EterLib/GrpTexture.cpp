#include "StdAfx.h"
#include "../eterBase/Stl.h"
#include "GrpTexture.h"
#include "StateManager.h"

void CGraphicTexture::DestroyDeviceObjects()
{
	safe_release(m_pD3D10ShaderResourceView);
	safe_release(m_pD3D10Texture);

	safe_release(m_lpd3dTexture);
}

void CGraphicTexture::Destroy()
{
	DestroyDeviceObjects();

	Initialize();
}

void CGraphicTexture::Initialize()
{
	m_lpd3dTexture = NULL;

	m_pD3D10Texture = NULL;
	m_pD3D10ShaderResourceView = NULL;

	m_width = 0;
	m_height = 0;
	m_bEmpty = true;
}

bool CGraphicTexture::IsEmpty() const
{
	return m_bEmpty;
}

void CGraphicTexture::SetTextureStage(int stage) const
{
	if (!ms_pD3D10Device)
		return;

	STATEMANAGER.SetTextureDX10(
		static_cast<DWORD>(stage),
		m_pD3D10ShaderResourceView
	);
}

LPDIRECT3DTEXTURE8 CGraphicTexture::GetD3DTexture() const
{
	return m_lpd3dTexture;
}

ID3D10Texture2D* CGraphicTexture::GetD3D10Texture() const
{
	return m_pD3D10Texture;
}

ID3D10ShaderResourceView* CGraphicTexture::GetD3D10ShaderResourceView() const
{
	return m_pD3D10ShaderResourceView;
}

int CGraphicTexture::GetWidth() const
{
	return m_width;
}

int CGraphicTexture::GetHeight() const
{
	return m_height;
}

CGraphicTexture::CGraphicTexture()
{
	Initialize();
}

CGraphicTexture::~CGraphicTexture()	
{
}
