#include "StdAfx.h"

#include "../eterBase/Stl.h"

#include "GrpIndexBuffer.h"
#include "StateManager.h"

ID3D10Buffer* CGraphicIndexBuffer::GetD3DIndexBuffer() const
{
	assert(m_pD3D10IndexBuffer != NULL);

	return m_pD3D10IndexBuffer;
}

void CGraphicIndexBuffer::SetIndices(
	int startIndex) const
{
	assert(ms_pD3D10Device != NULL);

	STATEMANAGER.SetIndicesDX10(
		m_pD3D10IndexBuffer,
		m_dxgiFormat,
		startIndex
	);
}

bool CGraphicIndexBuffer::Lock(
	void** pretIndices) const
{
	if (!m_pD3D10IndexBuffer)
		return false;

	if (!pretIndices)
		return false;

	HRESULT hr = m_pD3D10IndexBuffer->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		pretIndices
	);

	if (FAILED(hr))
	{
		*pretIndices = NULL;
		return false;
	}

	return true;
}

void CGraphicIndexBuffer::Unlock() const
{
	if (!m_pD3D10IndexBuffer)
		return;

	m_pD3D10IndexBuffer->Unmap();
}

bool CGraphicIndexBuffer::Lock(
	void** pretIndices)
{
	if (!m_pD3D10IndexBuffer)
		return false;

	if (!pretIndices)
		return false;

	HRESULT hr = m_pD3D10IndexBuffer->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		pretIndices
	);

	if (FAILED(hr))
	{
		*pretIndices = NULL;
		return false;
	}

	return true;
}

void CGraphicIndexBuffer::Unlock()
{
	if (!m_pD3D10IndexBuffer)
		return;

	m_pD3D10IndexBuffer->Unmap();
}

bool CGraphicIndexBuffer::Copy(
	int bufSize,
	const void* srcIndices)
{
	if (!m_pD3D10IndexBuffer)
		return false;

	if (!srcIndices)
		return false;

	if (bufSize <= 0)
		return false;

	if (static_cast<DWORD>(bufSize) > m_dwBufferSize)
		return false;

	void* dstIndices = NULL;

	if (!Lock(&dstIndices))
		return false;

	memcpy(
		dstIndices,
		srcIndices,
		bufSize
	);

	Unlock();

	return true;
}

bool CGraphicIndexBuffer::Create(
	int faceCount,
	TFace* faces)
{
	if (!faces)
		return false;

	const int idxCount = faceCount * 3;

	if (!Create(
		idxCount,
		D3DFMT_INDEX16))
	{
		return false;
	}

	WORD* dstIndices = NULL;

	if (!Lock(
		reinterpret_cast<void**>(&dstIndices)))
	{
		return false;
	}

	for (
		int i = 0;
		i < faceCount;
		++i,
		dstIndices += 3)
	{
		TFace* curFace = faces + i;

		dstIndices[0] = curFace->indices[0];
		dstIndices[1] = curFace->indices[1];
		dstIndices[2] = curFace->indices[2];
	}

	Unlock();

	return true;
}

bool CGraphicIndexBuffer::CreateDeviceObjects()
{
	if (!ms_pD3D10Device)
		return false;

	if (m_pD3D10IndexBuffer)
		return false;

	if (m_dwBufferSize == 0)
		return false;

	D3D10_BUFFER_DESC bufferDesc;
	ZeroMemory(
		&bufferDesc,
		sizeof(bufferDesc)
	);

	bufferDesc.ByteWidth =
		m_dwBufferSize;

	// Dynamic for compatibility with the old Lock/Unlock API.
	bufferDesc.Usage =
		D3D10_USAGE_DYNAMIC;

	bufferDesc.BindFlags =
		D3D10_BIND_INDEX_BUFFER;

	bufferDesc.CPUAccessFlags =
		D3D10_CPU_ACCESS_WRITE;

	bufferDesc.MiscFlags = 0;

	HRESULT hr =
		ms_pD3D10Device->CreateBuffer(
			&bufferDesc,
			NULL,
			&m_pD3D10IndexBuffer
		);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicIndexBuffer::CreateDeviceObjects - "
			"CreateBuffer failed: 0x%08X",
			hr
		);

		return false;
	}

	return true;
}

void CGraphicIndexBuffer::DestroyDeviceObjects()
{
	safe_release(
		m_pD3D10IndexBuffer
	);
}

bool CGraphicIndexBuffer::Create(
	int idxCount,
	D3DFORMAT d3dFmt)
{
	if (!ms_pD3D10Device)
		return false;

	if (idxCount <= 0)
		return false;

	Destroy();

	m_iidxCount = idxCount;
	m_d3dFmt = d3dFmt;

	switch (d3dFmt)
	{
	case D3DFMT_INDEX16:
		m_dxgiFormat =
			DXGI_FORMAT_R16_UINT;

		m_dwBufferSize =
			sizeof(WORD) *
			idxCount;
		break;

	case D3DFMT_INDEX32:
		m_dxgiFormat =
			DXGI_FORMAT_R32_UINT;

		m_dwBufferSize =
			sizeof(DWORD) *
			idxCount;
		break;

	default:
		TraceError(
			"CGraphicIndexBuffer::Create - "
			"Unsupported index format: %d",
			d3dFmt
		);

		return false;
	}

	return CreateDeviceObjects();
}

void CGraphicIndexBuffer::Destroy()
{
	DestroyDeviceObjects();
}

void CGraphicIndexBuffer::Initialize()
{
	m_pD3D10IndexBuffer = NULL;

	m_dwBufferSize = 0;

	m_d3dFmt = D3DFMT_INDEX16;
	m_dxgiFormat = DXGI_FORMAT_R16_UINT;

	m_iidxCount = 0;
}

CGraphicIndexBuffer::CGraphicIndexBuffer()
{
	Initialize();
}

CGraphicIndexBuffer::~CGraphicIndexBuffer()
{
	Destroy();
}