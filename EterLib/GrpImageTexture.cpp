#include "StdAfx.h"
#include "../eterBase/MappedFile.h"
#include "../eterPack/EterPackManager.h"
#include "GrpImageTexture.h"
#include "../eterImageLib/TGAImage.h"
bool CGraphicImageTexture::Lock(
	int* pRetPitch,
	void** ppRetPixels,
	int level)
{
	if (!m_pD3D10Texture)
		return false;

	D3D10_MAPPED_TEXTURE2D mappedTexture;
	ZeroMemory(
		&mappedTexture,
		sizeof(mappedTexture)
	);

	HRESULT hr = m_pD3D10Texture->Map(
		level,
		D3D10_MAP_WRITE_DISCARD,
		0,
		&mappedTexture
	);

	if (FAILED(hr))
		return false;

	*pRetPitch =
		static_cast<int>(
			mappedTexture.RowPitch
			);

	*ppRetPixels =
		mappedTexture.pData;

	return true;
}

void CGraphicImageTexture::Unlock(int level)
{
	if (!m_pD3D10Texture)
		return;

	m_pD3D10Texture->Unmap(
		level
	);
}

void CGraphicImageTexture::Initialize()
{
	CGraphicTexture::Initialize();

	m_stFileName = "";

	m_d3dFmt=D3DFMT_UNKNOWN;
	m_dwFilter=0;
}

void CGraphicImageTexture::Destroy()
{
	CGraphicTexture::Destroy();

	Initialize();
}

bool CGraphicImageTexture::CreateDeviceObjects()
{
	if (!ms_pD3D10Device)
		return false;

	if (m_stFileName.empty())
	{
		DXGI_FORMAT textureFormat =
			DXGI_FORMAT_R8G8B8A8_UNORM;

		if (m_d3dFmt == D3DFMT_A8)
		{
			textureFormat =
				DXGI_FORMAT_A8_UNORM;
		}

		D3D10_TEXTURE2D_DESC textureDesc;
		ZeroMemory(
			&textureDesc,
			sizeof(textureDesc)
		);

		textureDesc.Width =
			m_width;

		textureDesc.Height =
			m_height;

		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;

		textureDesc.Format =
			textureFormat;

		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;

		textureDesc.Usage =
			D3D10_USAGE_DYNAMIC;

		textureDesc.BindFlags =
			D3D10_BIND_SHADER_RESOURCE;

		textureDesc.CPUAccessFlags =
			D3D10_CPU_ACCESS_WRITE;

		textureDesc.MiscFlags = 0;

		HRESULT hr =
			ms_pD3D10Device->CreateTexture2D(
				&textureDesc,
				NULL,
				&m_pD3D10Texture
			);

		if (FAILED(hr))
		{
			TraceError(
				"CGraphicImageTexture::CreateDeviceObjects - "
				"CreateTexture2D failed: 0x%08X",
				hr
			);

			return false;
		}

		hr =
			ms_pD3D10Device->CreateShaderResourceView(
				m_pD3D10Texture,
				NULL,
				&m_pD3D10ShaderResourceView
			);

		if (FAILED(hr))
		{
			m_pD3D10Texture->Release();
			m_pD3D10Texture = NULL;

			TraceError(
				"CGraphicImageTexture::CreateDeviceObjects - "
				"CreateShaderResourceView failed: 0x%08X",
				hr
			);

			return false;
		}

		m_bEmpty = false;

		return true;
	}

	CMappedFile mappedFile;
	LPCVOID c_pvMap;

	if (!CEterPackManager::Instance().Get(
		mappedFile,
		m_stFileName.c_str(),
		&c_pvMap))
	{
		return false;
	}

	return CreateFromMemoryFile(
		mappedFile.Size(),
		c_pvMap,
		m_d3dFmt,
		m_dwFilter
	);
}

bool CGraphicImageTexture::Create(UINT width, UINT height, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	if (!ms_pD3D10Device)
		return false;

	Destroy();

	m_width = width;
	m_height = height;
	m_d3dFmt = d3dFmt;
	m_dwFilter = dwFilter;

	return CreateDeviceObjects();
}

void CGraphicImageTexture::CreateFromTexturePointer(const CGraphicTexture * c_pSrcTexture)
{
	if (m_lpd3dTexture)
		m_lpd3dTexture->Release();
	
	m_width = c_pSrcTexture->GetWidth();
	m_height = c_pSrcTexture->GetHeight();
	m_lpd3dTexture = c_pSrcTexture->GetD3DTexture();
	
	if (m_lpd3dTexture)
		m_lpd3dTexture->AddRef();

	m_bEmpty = false;
}

bool CGraphicImageTexture::CreateDDSTexture(
	CDXTCImage& image,
	const BYTE* /*c_pbBuf*/)
{
	if (!ms_pD3D10Device)
		return false;

	DXGI_FORMAT format =
		DXGI_FORMAT_BC1_UNORM;

	UINT blockSize = 8;

	switch (image.m_CompFormat)
	{
	case PF_DXT1:
		format = DXGI_FORMAT_BC1_UNORM;
		blockSize = 8;
		break;

	case PF_DXT3:
		format = DXGI_FORMAT_BC2_UNORM;
		blockSize = 16;
		break;

	case PF_DXT5:
		format = DXGI_FORMAT_BC3_UNORM;
		blockSize = 16;
		break;

	default:
		TraceError(
			"CGraphicImageTexture::CreateDDSTexture - "
			"Unsupported DDS format"
		);

		return false;
	}

	UINT mipmapCount =
		image.m_dwMipMapCount;

	if (mipmapCount == 0)
		mipmapCount = 1;

	if (mipmapCount > MAX_MIPLEVELS)
		mipmapCount = MAX_MIPLEVELS;

	std::vector<D3D10_SUBRESOURCE_DATA>
		initialData;

	initialData.resize(
		mipmapCount
	);

	for (UINT i = 0; i < mipmapCount; ++i)
	{
		UINT mipWidth =
			static_cast<UINT>(
				image.m_nWidth
				) >> i;

		UINT mipHeight =
			static_cast<UINT>(
				image.m_nHeight
				) >> i;

		if (mipWidth == 0)
			mipWidth = 1;

		if (mipHeight == 0)
			mipHeight = 1;

		UINT blockWidth =
			(mipWidth + 3) / 4;

		UINT blockHeight =
			(mipHeight + 3) / 4;

		if (blockWidth == 0)
			blockWidth = 1;

		if (blockHeight == 0)
			blockHeight = 1;

		ZeroMemory(
			&initialData[i],
			sizeof(D3D10_SUBRESOURCE_DATA)
		);

		initialData[i].pSysMem =
			image.m_pbCompBufferByLevels[i];

		initialData[i].SysMemPitch =
			blockWidth *
			blockSize;

		initialData[i].SysMemSlicePitch =
			initialData[i].SysMemPitch *
			blockHeight;

		if (!initialData[i].pSysMem)
		{
			TraceError(
				"CGraphicImageTexture::CreateDDSTexture - "
				"Missing mip level %u",
				i
			);

			return false;
		}
	}

	D3D10_TEXTURE2D_DESC textureDesc;
	ZeroMemory(
		&textureDesc,
		sizeof(textureDesc)
	);

	textureDesc.Width =
		static_cast<UINT>(
			image.m_nWidth
			);

	textureDesc.Height =
		static_cast<UINT>(
			image.m_nHeight
			);

	textureDesc.MipLevels =
		mipmapCount;

	textureDesc.ArraySize = 1;

	textureDesc.Format =
		format;

	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;

	textureDesc.Usage =
		D3D10_USAGE_IMMUTABLE;

	textureDesc.BindFlags =
		D3D10_BIND_SHADER_RESOURCE;

	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	HRESULT hr =
		ms_pD3D10Device->CreateTexture2D(
			&textureDesc,
			&initialData[0],
			&m_pD3D10Texture
		);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicImageTexture::CreateDDSTexture - "
			"CreateTexture2D failed: 0x%08X",
			hr
		);

		return false;
	}

	D3D10_SHADER_RESOURCE_VIEW_DESC
		srvDesc;

	ZeroMemory(
		&srvDesc,
		sizeof(srvDesc)
	);

	srvDesc.Format =
		format;

	srvDesc.ViewDimension =
		D3D10_SRV_DIMENSION_TEXTURE2D;

	srvDesc.Texture2D.MostDetailedMip = 0;

	srvDesc.Texture2D.MipLevels =
		mipmapCount;

	hr =
		ms_pD3D10Device->CreateShaderResourceView(
			m_pD3D10Texture,
			&srvDesc,
			&m_pD3D10ShaderResourceView
		);

	if (FAILED(hr))
	{
		m_pD3D10Texture->Release();
		m_pD3D10Texture = NULL;

		TraceError(
			"CGraphicImageTexture::CreateDDSTexture - "
			"CreateShaderResourceView failed: 0x%08X",
			hr
		);

		return false;
	}

	m_width =
		image.m_nWidth;

	m_height =
		image.m_nHeight;

	m_bEmpty = false;

	return true;
}

bool CGraphicImageTexture::CreateFromMemoryFile(UINT bufSize, const void * c_pvBuf, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	if (!ms_pD3D10Device)
		return false;

	if (m_pD3D10Texture ||
		m_pD3D10ShaderResourceView)
	{
		return false;
	}

	static CDXTCImage image;

	const char* pExt = strrchr(m_stFileName.c_str(), '.');

	if (pExt &&
		(_stricmp(pExt, ".png") == 0 ||
			_stricmp(pExt, ".jpg") == 0 ||
			_stricmp(pExt, ".jpeg") == 0))
	{
		IWICImagingFactory* pFactory = NULL;
		IWICStream* pStream = NULL;
		IWICBitmapDecoder* pDecoder = NULL;
		IWICBitmapFrameDecode* pFrame = NULL;
		IWICFormatConverter* pConverter = NULL;

		auto CleanupWIC = [&]()
			{
				if (pConverter)
				{
					pConverter->Release();
					pConverter = NULL;
				}

				if (pFrame)
				{
					pFrame->Release();
					pFrame = NULL;
				}

				if (pDecoder)
				{
					pDecoder->Release();
					pDecoder = NULL;
				}

				if (pStream)
				{
					pStream->Release();
					pStream = NULL;
				}

				if (pFactory)
				{
					pFactory->Release();
					pFactory = NULL;
				}
			};

		HRESULT hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pFactory)
		);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot create WIC factory");
			CleanupWIC();
			return false;
		}

		hr = pFactory->CreateStream(&pStream);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot create WIC stream");
			CleanupWIC();
			return false;
		}

		hr = pStream->InitializeFromMemory(
			const_cast<BYTE*>(static_cast<const BYTE*>(c_pvBuf)),
			bufSize
		);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot initialize PNG stream");
			CleanupWIC();
			return false;
		}

		hr = pFactory->CreateDecoderFromStream(
			pStream,
			NULL,
			WICDecodeMetadataCacheOnLoad,
			&pDecoder
		);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot create PNG decoder");
			CleanupWIC();
			return false;
		}

		hr = pDecoder->GetFrame(0, &pFrame);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot get PNG frame");
			CleanupWIC();
			return false;
		}

		UINT width = 0;
		UINT height = 0;

		hr = pFrame->GetSize(&width, &height);

		if (FAILED(hr) || width == 0 || height == 0)
		{
			TraceError("CreateFromMemoryFile: Invalid PNG size");
			CleanupWIC();
			return false;
		}

		hr = pFactory->CreateFormatConverter(&pConverter);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot create PNG converter");
			CleanupWIC();
			return false;
		}

		hr = pConverter->Initialize(
			pFrame,
			GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone,
			NULL,
			0.0,
			WICBitmapPaletteTypeCustom
		);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot convert PNG to BGRA");
			CleanupWIC();
			return false;
		}

		const UINT pitch = width * 4;
		const UINT imageSize = pitch * height;

		std::vector<BYTE> pixels(imageSize);

		hr = pConverter->CopyPixels(
			NULL,
			pitch,
			imageSize,
			&pixels[0]
		);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot copy PNG pixels");
			CleanupWIC();
			return false;
		}

		D3D10_TEXTURE2D_DESC textureDesc = {};
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Usage = D3D10_USAGE_IMMUTABLE;
		textureDesc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
		textureDesc.CPUAccessFlags = 0;
		textureDesc.MiscFlags = 0;

		D3D10_SUBRESOURCE_DATA initialData = {};
		initialData.pSysMem = &pixels[0];
		initialData.SysMemPitch = pitch;
		initialData.SysMemSlicePitch = imageSize;

		hr = ms_pD3D10Device->CreateTexture2D(
			&textureDesc,
			&initialData,
			&m_pD3D10Texture
		);

		if (FAILED(hr))
		{
			TraceError("CreateFromMemoryFile: Cannot create DX10 PNG texture");
			CleanupWIC();
			return false;
		}

		hr = ms_pD3D10Device->CreateShaderResourceView(
			m_pD3D10Texture,
			NULL,
			&m_pD3D10ShaderResourceView
		);

		if (FAILED(hr))
		{
			m_pD3D10Texture->Release();
			m_pD3D10Texture = NULL;

			TraceError("CreateFromMemoryFile: Cannot create PNG shader resource view");
			CleanupWIC();
			return false;
		}

		m_width = width;
		m_height = height;
		m_bEmpty = false;

		CleanupWIC();

		return true;
	}



	if (pExt && _stricmp(pExt, ".tga") == 0)
	{
		CTGAImage tgaImage;

		if (!tgaImage.LoadFromMemory(
			static_cast<int>(bufSize),
			static_cast<const BYTE*>(c_pvBuf)))
		{
			TraceError(
				"CGraphicImageTexture::CreateFromMemoryFile - "
				"Cannot load TGA: %s",
				m_stFileName.c_str()
			);

			return false;
		}

		UINT width =
			static_cast<UINT>(
				tgaImage.GetWidth()
				);

		UINT height =
			static_cast<UINT>(
				tgaImage.GetHeight()
				);

		D3D10_TEXTURE2D_DESC textureDesc;
		ZeroMemory(
			&textureDesc,
			sizeof(textureDesc)
		);

		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;

		textureDesc.Format =
			DXGI_FORMAT_B8G8R8A8_UNORM;

		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;

		textureDesc.Usage =
			D3D10_USAGE_IMMUTABLE;

		textureDesc.BindFlags =
			D3D10_BIND_SHADER_RESOURCE;

		textureDesc.CPUAccessFlags = 0;
		textureDesc.MiscFlags = 0;

		D3D10_SUBRESOURCE_DATA initialData;
		ZeroMemory(
			&initialData,
			sizeof(initialData)
		);

		initialData.pSysMem =
			tgaImage.GetBasePointer();

		initialData.SysMemPitch =
			width * sizeof(DWORD);

		initialData.SysMemSlicePitch =
			initialData.SysMemPitch *
			height;

		HRESULT hr =
			ms_pD3D10Device->CreateTexture2D(
				&textureDesc,
				&initialData,
				&m_pD3D10Texture
			);

		if (FAILED(hr))
		{
			TraceError(
				"CGraphicImageTexture::CreateFromMemoryFile - "
				"CreateTexture2D TGA failed: 0x%08X",
				hr
			);

			return false;
		}

		hr =
			ms_pD3D10Device->CreateShaderResourceView(
				m_pD3D10Texture,
				NULL,
				&m_pD3D10ShaderResourceView
			);

		if (FAILED(hr))
		{
			m_pD3D10Texture->Release();
			m_pD3D10Texture = NULL;

			TraceError(
				"CGraphicImageTexture::CreateFromMemoryFile - "
				"CreateShaderResourceView TGA failed: 0x%08X",
				hr
			);

			return false;
		}

		m_width = width;
		m_height = height;
		m_bEmpty = false;

		return true;
	}

	if (image.LoadHeaderFromMemory(
		(const BYTE*)c_pvBuf))
	{
		return CreateDDSTexture(
			image,
			(const BYTE*)c_pvBuf
		);
	}

	TraceError(
		"CGraphicImageTexture::CreateFromMemoryFile - "
		"Unsupported image format: %s",
		m_stFileName.c_str()
	);

	return false;
}

void CGraphicImageTexture::SetFileName(const char * c_szFileName)
{
	m_stFileName=c_szFileName;
}

bool CGraphicImageTexture::CreateFromDiskFile(const char * c_szFileName, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	Destroy();

	SetFileName(c_szFileName);

	m_d3dFmt = d3dFmt;
	m_dwFilter = dwFilter;
	return CreateDeviceObjects();
}

CGraphicImageTexture::CGraphicImageTexture()
{
	Initialize();
}

CGraphicImageTexture::~CGraphicImageTexture()
{
	Destroy();
}
