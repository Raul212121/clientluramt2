#pragma once

#include "GrpBase.h"

class CGraphicVertexBuffer : public CGraphicBase
{
public:
	CGraphicVertexBuffer();
	virtual ~CGraphicVertexBuffer();

	void Destroy();

	virtual bool Create(
		int vtxCount,
		DWORD fvf,
		DWORD usage,
		D3DPOOL d3dPool
	);

	bool CreateDeviceObjects();
	void DestroyDeviceObjects();

	bool Copy(
		int bufSize,
		const void* srcVertices
	);

	bool LockRange(
		unsigned count,
		void** pretVertices
	) const;

	bool Lock(
		void** pretVertices
	) const;

	bool Unlock() const;

	bool LockDynamic(
		void** pretVertices
	);

	virtual bool Lock(
		void** pretVertices
	);

	bool Unlock();

	void SetStream(
		int stride,
		int layer = 0
	) const;

	int GetVertexCount() const;
	int GetVertexStride() const;

	DWORD GetFlexibleVertexFormat() const;

	inline ID3D10Buffer* GetD3DVertexBuffer() const
	{
		return m_pD3D10VB;
	}

	inline DWORD GetBufferSize() const
	{
		return m_dwBufferSize;
	}

	bool IsEmpty() const;

protected:
	void Initialize();

protected:
	ID3D10Buffer* m_pD3D10VB;

	DWORD			m_dwBufferSize;
	DWORD			m_dwFVF;
	DWORD			m_dwUsage;
	D3DPOOL			m_d3dPool;

	int				m_vtxCount;

	DWORD			m_dwLockFlag;
};