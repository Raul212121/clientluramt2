#include "StdAfx.h"

#include "../eterBase/Stl.h"

#include "GrpVertexBuffer.h"
#include "StateManager.h"

int CGraphicVertexBuffer::GetVertexStride() const
{
	return D3DXGetFVFVertexSize(m_dwFVF);
}

DWORD CGraphicVertexBuffer::GetFlexibleVertexFormat() const
{
	return m_dwFVF;
}

int CGraphicVertexBuffer::GetVertexCount() const
{
	return m_vtxCount;
}

bool CGraphicVertexBuffer::IsEmpty() const
{
	return m_pD3D10VB == NULL;
}

void CGraphicVertexBuffer::SetStream(
	int stride,
	int layer) const
{
	assert(ms_pD3D10Device != NULL);

	STATEMANAGER.SetStreamSourceDX10(
		layer,
		m_pD3D10VB,
		stride
	);
}

bool CGraphicVertexBuffer::LockRange(
	unsigned count,
	void** pretVertices) const
{
	if (!m_pD3D10VB)
		return false;

	if (!pretVertices)
		return false;

	if (count > static_cast<unsigned>(m_vtxCount))
		return false;

	HRESULT hr = m_pD3D10VB->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		pretVertices
	);

	if (FAILED(hr))
	{
		*pretVertices = NULL;
		return false;
	}

	return true;
}

bool CGraphicVertexBuffer::Lock(
	void** pretVertices) const
{
	if (!m_pD3D10VB)
		return false;

	if (!pretVertices)
		return false;

	HRESULT hr = m_pD3D10VB->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		pretVertices
	);

	if (FAILED(hr))
	{
		*pretVertices = NULL;
		return false;
	}

	return true;
}

bool CGraphicVertexBuffer::Unlock() const
{
	if (!m_pD3D10VB)
		return false;

	m_pD3D10VB->Unmap();

	return true;
}

bool CGraphicVertexBuffer::LockDynamic(
	void** pretVertices)
{
	if (!m_pD3D10VB)
		return false;

	if (!pretVertices)
		return false;

	HRESULT hr = m_pD3D10VB->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		pretVertices
	);

	if (FAILED(hr))
	{
		*pretVertices = NULL;
		return false;
	}

	return true;
}

bool CGraphicVertexBuffer::Lock(
	void** pretVertices)
{
	if (!m_pD3D10VB)
		return false;

	if (!pretVertices)
		return false;

	HRESULT hr = m_pD3D10VB->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		pretVertices
	);

	if (FAILED(hr))
	{
		*pretVertices = NULL;
		return false;
	}

	return true;
}

bool CGraphicVertexBuffer::Unlock()
{
	if (!m_pD3D10VB)
		return false;

	m_pD3D10VB->Unmap();

	return true;
}

bool CGraphicVertexBuffer::Copy(
	int bufSize,
	const void* srcVertices)
{
	if (!srcVertices)
		return false;

	if (bufSize <= 0)
		return false;

	if (static_cast<DWORD>(bufSize) > m_dwBufferSize)
		return false;

	void* dstVertices = NULL;

	if (!Lock(&dstVertices))
		return false;

	memcpy(
		dstVertices,
		srcVertices,
		bufSize
	);

	Unlock();

	return true;
}

bool CGraphicVertexBuffer::CreateDeviceObjects()
{
	assert(ms_pD3D10Device != NULL);
	assert(m_pD3D10VB == NULL);

	if (!ms_pD3D10Device)
		return false;

	if (m_dwBufferSize == 0)
		return false;

	D3D10_BUFFER_DESC bufferDesc;
	ZeroMemory(
		&bufferDesc,
		sizeof(bufferDesc)
	);

	bufferDesc.ByteWidth = m_dwBufferSize;

	// Use dynamic buffers during the migration so the old
	// Lock/Unlock API can continue to work through Map/Unmap.
	bufferDesc.Usage = D3D10_USAGE_DYNAMIC;

	bufferDesc.BindFlags =
		D3D10_BIND_VERTEX_BUFFER;

	bufferDesc.CPUAccessFlags =
		D3D10_CPU_ACCESS_WRITE;

	bufferDesc.MiscFlags = 0;

	HRESULT hr = ms_pD3D10Device->CreateBuffer(
		&bufferDesc,
		NULL,
		&m_pD3D10VB
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicVertexBuffer::CreateDeviceObjects - "
			"CreateBuffer failed: 0x%08X",
			hr
		);

		return false;
	}

	return true;
}

void CGraphicVertexBuffer::DestroyDeviceObjects()
{
	safe_release(m_pD3D10VB);
}

bool CGraphicVertexBuffer::Create(
	int vtxCount,
	DWORD fvf,
	DWORD usage,
	D3DPOOL d3dPool)
{
	assert(ms_pD3D10Device != NULL);
	assert(vtxCount > 0);

	if (!ms_pD3D10Device)
		return false;

	if (vtxCount <= 0)
		return false;

	Destroy();

	m_vtxCount = vtxCount;

	m_dwFVF = fvf;
	m_dwUsage = usage;
	m_d3dPool = d3dPool;

	const int vertexStride =
		D3DXGetFVFVertexSize(fvf);

	if (vertexStride <= 0)
	{
		TraceError(
			"CGraphicVertexBuffer::Create - "
			"Invalid FVF: 0x%08X",
			fvf
		);

		return false;
	}

	m_dwBufferSize =
		vertexStride * m_vtxCount;

	m_dwLockFlag = 0;

	return CreateDeviceObjects();
}

void CGraphicVertexBuffer::Destroy()
{
	DestroyDeviceObjects();
}

void CGraphicVertexBuffer::Initialize()
{
	m_pD3D10VB = NULL;

	m_vtxCount = 0;

	m_dwBufferSize = 0;
	m_dwFVF = 0;
	m_dwUsage = 0;

	m_d3dPool = D3DPOOL_DEFAULT;

	m_dwLockFlag = 0;
}

CGraphicVertexBuffer::CGraphicVertexBuffer()
{
	Initialize();
}

CGraphicVertexBuffer::~CGraphicVertexBuffer()
{
	Destroy();
}