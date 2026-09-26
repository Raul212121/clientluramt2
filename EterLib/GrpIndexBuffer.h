#pragma once

#include "GrpBase.h"

class CGraphicIndexBuffer : public CGraphicBase
{
public:
	CGraphicIndexBuffer();
	virtual ~CGraphicIndexBuffer();

	void Destroy();

	bool Create(
		int idxCount,
		D3DFORMAT d3dFmt
	);

	bool Create(
		int faceCount,
		TFace* faces
	);

	bool CreateDeviceObjects();
	void DestroyDeviceObjects();

	bool Copy(
		int bufSize,
		const void* srcIndices
	);

	bool Lock(
		void** pretIndices
	) const;

	void Unlock() const;

	bool Lock(
		void** pretIndices
	);

	void Unlock();

	void SetIndices(
		int startIndex = 0
	) const;

	ID3D10Buffer* GetD3DIndexBuffer() const;

	int GetIndexCount() const
	{
		return m_iidxCount;
	}

	DXGI_FORMAT GetDX10Format() const
	{
		return m_dxgiFormat;
	}

protected:
	void Initialize();

protected:
	ID3D10Buffer* m_pD3D10IndexBuffer;

	DWORD			m_dwBufferSize;

	D3DFORMAT		m_d3dFmt;
	DXGI_FORMAT		m_dxgiFormat;

	int				m_iidxCount;
};