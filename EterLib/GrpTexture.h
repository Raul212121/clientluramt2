#pragma once

#include "GrpBase.h"

class CGraphicTexture : public CGraphicBase
{
	public:
		virtual bool IsEmpty() const;

		int GetWidth() const;
		int GetHeight() const;

		void SetTextureStage(int stage) const;
		LPDIRECT3DTEXTURE8 GetD3DTexture() const;
		ID3D10Texture2D* GetD3D10Texture() const;
		ID3D10ShaderResourceView* GetD3D10ShaderResourceView() const;

		void DestroyDeviceObjects();
		
	protected:
		CGraphicTexture();
		virtual	~CGraphicTexture();

		void Destroy();
		void Initialize();

	protected:
		bool m_bEmpty;

		int m_width;
		int m_height;

		LPDIRECT3DTEXTURE8 m_lpd3dTexture;
		ID3D10Texture2D* m_pD3D10Texture;
		ID3D10ShaderResourceView* m_pD3D10ShaderResourceView;
};
