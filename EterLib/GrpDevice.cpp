#include "StdAfx.h"
#include "GrpDevice.h"
#include "../eterBase/Stl.h"
#include "../eterBase/Debug.h"

#include "../UserInterface/PythonSystem.h"

bool GRAPHICS_CAPS_CAN_NOT_DRAW_LINE = false;
bool GRAPHICS_CAPS_CAN_NOT_DRAW_SHADOW = false;
bool GRAPHICS_CAPS_HALF_SIZE_IMAGE = false;
bool GRAPHICS_CAPS_CAN_NOT_TEXTURE_ADDRESS_BORDER = false;
bool GRAPHICS_CAPS_SOFTWARE_TILING = false;

D3DPRESENT_PARAMETERS g_kD3DPP;
bool g_isBrowserMode=false;
RECT g_rcBrowser;

CGraphicDevice::CGraphicDevice()
: m_uBackBufferCount(0)
{
	__Initialize();
}

CGraphicDevice::~CGraphicDevice()
{
	Destroy();
}

void CGraphicDevice::__Initialize()
{
	ms_iD3DAdapterInfo = D3DADAPTER_DEFAULT;
	ms_iD3DDevInfo = D3DADAPTER_DEFAULT;
	ms_iD3DModeInfo = D3DADAPTER_DEFAULT;

	ms_lpd3d = NULL;
	ms_lpd3dDevice = NULL;

	ms_pD3D10Device = NULL;
	ms_pSwapChain = NULL;
	ms_pRenderTargetView = NULL;
	ms_pDepthStencilTexture = NULL;
	ms_pDepthStencilView = NULL;

	ZeroMemory(&ms_DX10Viewport, sizeof(ms_DX10Viewport));

	ms_lpd3dMatStack = NULL;

	ms_dwWavingEndTime = 0;
	ms_dwFlashingEndTime = 0;

	m_pStateManager = NULL;

	__InitializeDefaultIndexBufferList();
	__InitializePDTVertexBufferList();
}

void CGraphicDevice::RegisterWarningString(UINT uiMsg, const char * c_szString)
{
	m_kMap_strWarningMessage[uiMsg] = c_szString;
}

void CGraphicDevice::__WarningMessage(HWND hWnd, UINT uiMsg)
{
	if (m_kMap_strWarningMessage.end() == m_kMap_strWarningMessage.find(uiMsg))
		return;
	MessageBox(hWnd, m_kMap_strWarningMessage[uiMsg].c_str(), "Warning", MB_OK|MB_TOPMOST);
}

void CGraphicDevice::MoveWebBrowserRect(const RECT& c_rcWebPage)
{
	g_rcBrowser=c_rcWebPage;
}

void CGraphicDevice::EnableWebBrowserMode(const RECT& c_rcWebPage)
{
	if (!ms_lpd3dDevice)
		return;

	D3DPRESENT_PARAMETERS& rkD3DPP=ms_d3dPresentParameter;
	
	g_isBrowserMode=true;

	if (D3DSWAPEFFECT_COPY==rkD3DPP.SwapEffect)
		return;

	g_kD3DPP=rkD3DPP;
	g_rcBrowser=c_rcWebPage;
	
	//rkD3DPP.Windowed=TRUE;
	rkD3DPP.SwapEffect=D3DSWAPEFFECT_COPY;
	rkD3DPP.BackBufferCount = 1;
	rkD3DPP.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	
	IDirect3DDevice8& rkD3DDev=*ms_lpd3dDevice;
	HRESULT hr=rkD3DDev.Reset(&rkD3DPP);
	if (FAILED(hr))
		return;
	
	STATEMANAGER.SetDefaultState();	
}

void CGraphicDevice::DisableWebBrowserMode()
{
	if (!ms_lpd3dDevice)
		return;

	D3DPRESENT_PARAMETERS& rkD3DPP=ms_d3dPresentParameter;
	
	g_isBrowserMode=false;

	rkD3DPP=g_kD3DPP;

	IDirect3DDevice8& rkD3DDev=*ms_lpd3dDevice;
	HRESULT hr=rkD3DDev.Reset(&rkD3DPP);
	if (FAILED(hr))
		return;
	
	STATEMANAGER.SetDefaultState();	
}
		
bool CGraphicDevice::ResizeBackBuffer(
	UINT uWidth,
	UINT uHeight)
{
	if (!ms_pD3D10Device || !ms_pSwapChain)
		return false;

	if (uWidth == 0 || uHeight == 0)
		return true;

	if (ms_iWidth == static_cast<int>(uWidth) &&
		ms_iHeight == static_cast<int>(uHeight))
	{
		return true;
	}

	ms_pD3D10Device->OMSetRenderTargets(
		0,
		NULL,
		NULL
	);

	safe_release(ms_pDepthStencilView);
	safe_release(ms_pDepthStencilTexture);
	safe_release(ms_pRenderTargetView);

	HRESULT hr = ms_pSwapChain->ResizeBuffers(
		0,
		uWidth,
		uHeight,
		DXGI_FORMAT_UNKNOWN,
		0
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::ResizeBackBuffer - ResizeBuffers failed: 0x%08X",
			hr
		);

		return false;
	}

	ID3D10Texture2D* pBackBuffer = NULL;

	hr = ms_pSwapChain->GetBuffer(
		0,
		__uuidof(ID3D10Texture2D),
		reinterpret_cast<void**>(&pBackBuffer)
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::ResizeBackBuffer - GetBuffer failed: 0x%08X",
			hr
		);

		return false;
	}

	hr = ms_pD3D10Device->CreateRenderTargetView(
		pBackBuffer,
		NULL,
		&ms_pRenderTargetView
	);

	safe_release(pBackBuffer);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::ResizeBackBuffer - CreateRenderTargetView failed: 0x%08X",
			hr
		);

		return false;
	}

	D3D10_TEXTURE2D_DESC depthDesc;
	ZeroMemory(&depthDesc, sizeof(depthDesc));

	depthDesc.Width = uWidth;
	depthDesc.Height = uHeight;

	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;

	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;

	depthDesc.Usage = D3D10_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D10_BIND_DEPTH_STENCIL;

	hr = ms_pD3D10Device->CreateTexture2D(
		&depthDesc,
		NULL,
		&ms_pDepthStencilTexture
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::ResizeBackBuffer - CreateTexture2D failed: 0x%08X",
			hr
		);

		return false;
	}

	hr = ms_pD3D10Device->CreateDepthStencilView(
		ms_pDepthStencilTexture,
		NULL,
		&ms_pDepthStencilView
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::ResizeBackBuffer - CreateDepthStencilView failed: 0x%08X",
			hr
		);

		return false;
	}

	ms_pD3D10Device->OMSetRenderTargets(
		1,
		&ms_pRenderTargetView,
		ms_pDepthStencilView
	);

	ZeroMemory(
		&ms_DX10Viewport,
		sizeof(ms_DX10Viewport)
	);

	ms_DX10Viewport.TopLeftX = 0;
	ms_DX10Viewport.TopLeftY = 0;

	ms_DX10Viewport.Width = uWidth;
	ms_DX10Viewport.Height = uHeight;

	ms_DX10Viewport.MinDepth = 0.0f;
	ms_DX10Viewport.MaxDepth = 1.0f;

	ms_pD3D10Device->RSSetViewports(
		1,
		&ms_DX10Viewport
	);

	m_pStateManager = new CStateManager(
		ms_pD3D10Device
	);

	if (!m_pStateManager)
	{
		TraceError(
			"CGraphicDevice::Create - Failed to create StateManager"
		);

		Destroy();
		return CREATE_DEVICE;
	}

	if (!m_pStateManager->CreateDX10Resources())
	{
		TraceError(
			"CGraphicDevice::Create - "
			"Failed to create DX10 shader resources"
		);

		Destroy();
		return CREATE_DEVICE;
	}

	if (!__CreateDefaultIndexBufferList())
	{
		TraceError(
			"CGraphicDevice::Create - "
			"Failed to create default index buffers"
		);

		Destroy();
		return CREATE_DEVICE;
	}

	if (!__CreatePDTVertexBufferList())
	{
		TraceError(
			"CGraphicDevice::Create - "
			"Failed to create PDT vertex buffers"
		);

		Destroy();
		return CREATE_DEVICE;
	}
	ms_iWidth = static_cast<int>(uWidth);
	ms_iHeight = static_cast<int>(uHeight);

	return true;
}

DWORD CGraphicDevice::CreatePNTStreamVertexShader()
{
	assert(ms_lpd3dDevice != NULL);
	
	DWORD declVector[] =
	{
		D3DVSD_STREAM(0),
		D3DVSD_REG(0, D3DVSDT_FLOAT3),
		D3DVSD_REG(3, D3DVSDT_FLOAT3),
		D3DVSD_REG(7, D3DVSDT_FLOAT2),
		D3DVSD_END()
	};
	
	DWORD ret;
	
	if (FAILED(ms_lpd3dDevice->CreateVertexShader(&declVector[0], NULL, &ret, 0)))
		return 0;
	
	return ret;
}

DWORD CGraphicDevice::CreatePNT2StreamVertexShader()
{
	assert(ms_lpd3dDevice != NULL);

	DWORD declVector[] =
	{
		D3DVSD_STREAM(0),
		D3DVSD_REG(0, D3DVSDT_FLOAT3),
		D3DVSD_REG(3, D3DVSDT_FLOAT3),
		D3DVSD_REG(7, D3DVSDT_FLOAT2),
		D3DVSD_REG(D3DVSDE_TEXCOORD1, D3DVSDT_FLOAT2),
//		D3DVSD_STREAM(1),
		D3DVSD_END()
	};

	DWORD ret;

	if (FAILED(ms_lpd3dDevice->CreateVertexShader(&declVector[0], NULL, &ret, 0)))
		return 0;

	return ret;
}

DWORD CGraphicDevice::CreatePTStreamVertexShader()
{
	assert(ms_lpd3dDevice != NULL);

	DWORD declVector[] = 
	{
		D3DVSD_STREAM(0),
		D3DVSD_REG(0, D3DVSDT_FLOAT3),
		D3DVSD_STREAM(1),
		D3DVSD_REG(7, D3DVSDT_FLOAT2),
		D3DVSD_END()
	};

	DWORD ret;

	if (FAILED(ms_lpd3dDevice->CreateVertexShader(&declVector[0], NULL, &ret, 0)))
		return 0;

	return (ret);
}

DWORD CGraphicDevice::CreateDoublePNTStreamVertexShader()
{
	assert(ms_lpd3dDevice != NULL);

	DWORD declVector[] = 
	{
		D3DVSD_STREAM(0),
		D3DVSD_REG(0, D3DVSDT_FLOAT3),
		D3DVSD_REG(3, D3DVSDT_FLOAT3),
		D3DVSD_REG(7, D3DVSDT_FLOAT2),
		D3DVSD_STREAM(1),
		D3DVSD_REG(D3DVSDE_POSITION2, D3DVSDT_FLOAT3),
		D3DVSD_REG(D3DVSDE_NORMAL2, D3DVSDT_FLOAT3),
		D3DVSD_REG(D3DVSDE_TEXCOORD1, D3DVSDT_FLOAT2),
		D3DVSD_END()
	};

	DWORD ret;

	if (FAILED(ms_lpd3dDevice->CreateVertexShader(&declVector[0], NULL, &ret, 0)))
		return 0;

	return ret;
}

CGraphicDevice::EDeviceState CGraphicDevice::GetDeviceState()
{
	if (!ms_pD3D10Device || !ms_pSwapChain)
		return DEVICESTATE_NULL;

	HRESULT hr = ms_pD3D10Device->GetDeviceRemovedReason();

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::GetDeviceState - Device removed: 0x%08X",
			hr
		);

		return DEVICESTATE_BROKEN;
	}

	return DEVICESTATE_OK;
}

bool CGraphicDevice::Reset()
{
	if (!ms_pD3D10Device || !ms_pSwapChain)
		return false;

	return ResizeBackBuffer(
		static_cast<UINT>(ms_iWidth),
		static_cast<UINT>(ms_iHeight)
	);
}

static LPDIRECT3DSURFACE8 s_lpStencil;
static DWORD   s_MaxTextureWidth, s_MaxTextureHeight;

BOOL EL3D_ConfirmDevice(D3DCAPS8& rkD3DCaps, UINT uBehavior, D3DFORMAT /*eD3DFmt*/)
{
	// PUREDEVICE는 GetTransform / GetViewport 등이 되지 않는다.
	if (uBehavior & D3DCREATE_PUREDEVICE) 
        return FALSE;
	
	if (uBehavior & D3DCREATE_HARDWARE_VERTEXPROCESSING) 
	{	
		// DirectionalLight
		if (!(rkD3DCaps.VertexProcessingCaps & D3DVTXPCAPS_DIRECTIONALLIGHTS))
			return FALSE;
		
		// PositionalLight
		if (!(rkD3DCaps.VertexProcessingCaps & D3DVTXPCAPS_POSITIONALLIGHTS))
			return FALSE;

		// Software T&L Support - ATI NOT SUPPORT CLIP, USE DIRECTX SOFTWARE PROCESSING CLIPPING
		if (GRAPHICS_CAPS_SOFTWARE_TILING)
		{
			if (!(rkD3DCaps.PrimitiveMiscCaps & D3DPMISCCAPS_CLIPTLVERTS))
				return FALSE;
		}
		else
		{
			// Shadow/Terrain
			if (!(rkD3DCaps.VertexProcessingCaps & D3DVTXPCAPS_TEXGEN))
				return FALSE;
		}
	}

	s_MaxTextureWidth = rkD3DCaps.MaxTextureWidth;
	s_MaxTextureHeight = rkD3DCaps.MaxTextureHeight;
	
	return TRUE;
}

DWORD GetMaxTextureWidth()
{
	return s_MaxTextureWidth;
}

DWORD GetMaxTextureHeight()
{
	return s_MaxTextureHeight;
}

bool CGraphicDevice::__IsInDriverBlackList(D3D_CAdapterInfo& rkD3DAdapterInfo)
{
	D3DADAPTER_IDENTIFIER8& rkD3DAdapterIdentifier=rkD3DAdapterInfo.GetIdentifier();

	char szSrcDriver[256];
	strncpy(szSrcDriver, rkD3DAdapterIdentifier.Driver, sizeof(szSrcDriver)-1);
	DWORD dwSrcHighVersion=rkD3DAdapterIdentifier.DriverVersion.QuadPart>>32;
	DWORD dwSrcLowVersion=rkD3DAdapterIdentifier.DriverVersion.QuadPart&0xffffffff;

	bool ret=false;
		
	FILE* fp=fopen("grpblk.txt", "r");
	if (fp)
	{
		DWORD dwChkHighVersion;
		DWORD dwChkLowVersion;

		char szChkDriver[256];

		char szLine[256];
		while (fgets(szLine, sizeof(szLine)-1, fp))
		{			
			sscanf(szLine, "%s %x %x", szChkDriver, &dwChkHighVersion, &dwChkLowVersion);
			
			if (strcmp(szSrcDriver, szChkDriver)==0)
				if (dwSrcHighVersion==dwChkHighVersion)
					if (dwSrcLowVersion==dwChkLowVersion)
					{
						ret=true;				
						break;
					}

			szLine[0]='\0';
		}
		fclose(fp);
	}

	return ret;
}

int CGraphicDevice::Create(
	HWND hWnd,
	int iHres,
	int iVres,
	bool Windowed,
	int /*iBit*/,
	int iReflashRate)
{
	Destroy();

	ms_iWidth = iHres;
	ms_iHeight = iVres;

	ms_hWnd = hWnd;
	ms_hDC = GetDC(hWnd);

	DXGI_SWAP_CHAIN_DESC swapDesc;
	ZeroMemory(&swapDesc, sizeof(swapDesc));

	swapDesc.BufferCount = 1;

	swapDesc.BufferDesc.Width = iHres;
	swapDesc.BufferDesc.Height = iVres;
	swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	if (!Windowed && iReflashRate > 0)
	{
		swapDesc.BufferDesc.RefreshRate.Numerator = iReflashRate;
		swapDesc.BufferDesc.RefreshRate.Denominator = 1;
	}
	else
	{
		swapDesc.BufferDesc.RefreshRate.Numerator = 0;
		swapDesc.BufferDesc.RefreshRate.Denominator = 1;
	}

	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	swapDesc.OutputWindow = hWnd;

	// MSAA will be added after the base DX10 renderer is stable.
	swapDesc.SampleDesc.Count = 1;
	swapDesc.SampleDesc.Quality = 0;

	swapDesc.Windowed = Windowed ? TRUE : FALSE;

	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapDesc.Flags = 0;

	HRESULT hr = D3D10CreateDeviceAndSwapChain(
		NULL,
		D3D10_DRIVER_TYPE_HARDWARE,
		NULL,
		D3D10_CREATE_DEVICE_BGRA_SUPPORT,
		D3D10_SDK_VERSION,
		&swapDesc,
		&ms_pSwapChain,
		&ms_pD3D10Device
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::Create - D3D10CreateDeviceAndSwapChain failed: 0x%08X",
			hr
		);

		Destroy();
		return CREATE_DEVICE;
	}

	ID3D10Texture2D* pBackBuffer = NULL;

	hr = ms_pSwapChain->GetBuffer(
		0,
		__uuidof(ID3D10Texture2D),
		reinterpret_cast<void**>(&pBackBuffer)
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::Create - GetBuffer failed: 0x%08X",
			hr
		);

		Destroy();
		return CREATE_DEVICE;
	}

	hr = ms_pD3D10Device->CreateRenderTargetView(
		pBackBuffer,
		NULL,
		&ms_pRenderTargetView
	);

	safe_release(pBackBuffer);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::Create - CreateRenderTargetView failed: 0x%08X",
			hr
		);

		Destroy();
		return CREATE_DEVICE;
	}

	D3D10_TEXTURE2D_DESC depthDesc;
	ZeroMemory(&depthDesc, sizeof(depthDesc));

	depthDesc.Width = iHres;
	depthDesc.Height = iVres;

	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;

	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;

	depthDesc.Usage = D3D10_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D10_BIND_DEPTH_STENCIL;

	hr = ms_pD3D10Device->CreateTexture2D(
		&depthDesc,
		NULL,
		&ms_pDepthStencilTexture
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::Create - CreateTexture2D depth failed: 0x%08X",
			hr
		);

		Destroy();
		return CREATE_DEVICE;
	}

	hr = ms_pD3D10Device->CreateDepthStencilView(
		ms_pDepthStencilTexture,
		NULL,
		&ms_pDepthStencilView
	);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::Create - CreateDepthStencilView failed: 0x%08X",
			hr
		);

		Destroy();
		return CREATE_DEVICE;
	}

	ms_pD3D10Device->OMSetRenderTargets(
		1,
		&ms_pRenderTargetView,
		ms_pDepthStencilView
	);

	ZeroMemory(
		&ms_DX10Viewport,
		sizeof(ms_DX10Viewport)
	);

	ms_DX10Viewport.TopLeftX = 0;
	ms_DX10Viewport.TopLeftY = 0;

	ms_DX10Viewport.Width = iHres;
	ms_DX10Viewport.Height = iVres;

	ms_DX10Viewport.MinDepth = 0.0f;
	ms_DX10Viewport.MaxDepth = 1.0f;

	ms_pD3D10Device->RSSetViewports(
		1,
		&ms_DX10Viewport
	);

	if (FAILED(D3DXCreateMatrixStack(
		0,
		&ms_lpd3dMatStack)))
	{
		TraceError(
			"CGraphicDevice::Create - D3DXCreateMatrixStack failed"
		);

		Destroy();
		return CREATE_DEVICE;
	}

	ms_lpd3dMatStack->LoadIdentity();

	D3DXMatrixIdentity(&ms_matIdentity);
	D3DXMatrixIdentity(&ms_matView);
	D3DXMatrixIdentity(&ms_matProj);
	D3DXMatrixIdentity(&ms_matInverseView);
	D3DXMatrixIdentity(&ms_matInverseViewYAxis);

	D3DXMatrixIdentity(&ms_matScreen0);
	D3DXMatrixIdentity(&ms_matScreen1);
	D3DXMatrixIdentity(&ms_matScreen2);

	ms_matScreen0._11 = 1.0f;
	ms_matScreen0._22 = -1.0f;

	ms_matScreen1._41 = 1.0f;
	ms_matScreen1._42 = 1.0f;

	ms_matScreen2._11 = static_cast<float>(iHres) / 2.0f;
	ms_matScreen2._22 = static_cast<float>(iVres) / 2.0f;

	ms_bSupportDXT = true;
	ms_isLowTextureMemory = false;
	ms_isHighTextureMemory = true;
	m_pStateManager = new CStateManager(ms_pD3D10Device);
	if (!Windowed)
	{
		SetWindowPos(
			hWnd,
			HWND_TOPMOST,
			0,
			0,
			iHres,
			iVres,
			SWP_SHOWWINDOW
		);
	}

	Tracef(
		"DirectX 10 device created successfully: %dx%d\n",
		iHres,
		iVres
	);

	return CREATE_OK;
}

void CGraphicDevice::__InitializePDTVertexBufferList()
{
	for (UINT i=0; i<PDT_VERTEXBUFFER_NUM; ++i)
		ms_alpd3dPDTVB[i]=NULL;	
}
		
void CGraphicDevice::__DestroyPDTVertexBufferList()
{
	for (UINT i=0; i<PDT_VERTEXBUFFER_NUM; ++i)
	{
		if (ms_alpd3dPDTVB[i])
		{
			ms_alpd3dPDTVB[i]->Release();
			ms_alpd3dPDTVB[i]=NULL;
		}
	}
}

bool CGraphicDevice::__CreatePDTVertexBufferList()
{
	if (!ms_pD3D10Device)
		return false;

	for (UINT i = 0; i < PDT_VERTEXBUFFER_NUM; ++i)
	{
		D3D10_BUFFER_DESC bufferDesc;
		ZeroMemory(
			&bufferDesc,
			sizeof(bufferDesc)
		);

		bufferDesc.ByteWidth =
			sizeof(TPDTVertex) *
			PDT_VERTEX_NUM;

		bufferDesc.Usage =
			D3D10_USAGE_DYNAMIC;

		bufferDesc.BindFlags =
			D3D10_BIND_VERTEX_BUFFER;

		bufferDesc.CPUAccessFlags =
			D3D10_CPU_ACCESS_WRITE;

		bufferDesc.MiscFlags = 0;

		HRESULT hr =
			ms_pD3D10Device->CreateBuffer(
				&bufferDesc,
				NULL,
				&ms_alpd3dPDTVB[i]
			);

		if (FAILED(hr))
		{
			TraceError(
				"CGraphicDevice::__CreatePDTVertexBufferList - "
				"CreateBuffer failed: 0x%08X",
				hr
			);

			return false;
		}
	}

	return true;
}

void CGraphicDevice::__InitializeDefaultIndexBufferList()
{
	for (UINT i=0; i<DEFAULT_IB_NUM; ++i)
		ms_alpd3dDefIB[i]=NULL;
}

void CGraphicDevice::__DestroyDefaultIndexBufferList()
{
	for (UINT i=0; i<DEFAULT_IB_NUM; ++i)
		if (ms_alpd3dDefIB[i])
		{
			ms_alpd3dDefIB[i]->Release();
			ms_alpd3dDefIB[i]=NULL;
		}	
}

bool CGraphicDevice::__CreateDefaultIndexBuffer(
	UINT eDefIB,
	UINT uIdxCount,
	const WORD* c_awIndices)
{
	if (!ms_pD3D10Device)
		return false;

	if (eDefIB >= DEFAULT_IB_NUM)
		return false;

	if (!c_awIndices || uIdxCount == 0)
		return false;

	assert(
		ms_alpd3dDefIB[eDefIB] == NULL
	);

	D3D10_BUFFER_DESC bufferDesc;
	ZeroMemory(
		&bufferDesc,
		sizeof(bufferDesc)
	);

	bufferDesc.ByteWidth =
		sizeof(WORD) * uIdxCount;

	bufferDesc.Usage =
		D3D10_USAGE_IMMUTABLE;

	bufferDesc.BindFlags =
		D3D10_BIND_INDEX_BUFFER;

	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;

	D3D10_SUBRESOURCE_DATA initialData;
	ZeroMemory(
		&initialData,
		sizeof(initialData)
	);

	initialData.pSysMem =
		c_awIndices;

	HRESULT hr =
		ms_pD3D10Device->CreateBuffer(
			&bufferDesc,
			&initialData,
			&ms_alpd3dDefIB[eDefIB]
		);

	if (FAILED(hr))
	{
		TraceError(
			"CGraphicDevice::__CreateDefaultIndexBuffer - "
			"CreateBuffer failed: 0x%08X",
			hr
		);

		return false;
	}

	return true;
}

bool CGraphicDevice::__CreateDefaultIndexBufferList()
{
	static const WORD c_awLineIndices[2] = { 0, 1, };
	static const WORD c_awLineTriIndices[6] = { 0, 1, 0, 2, 1, 2, };
	static const WORD c_awLineRectIndices[8] = { 0, 1, 0, 2, 1, 3, 2, 3,};
	static const WORD c_awLineCubeIndices[24] = { 
		0, 1, 0, 2, 1, 3, 2, 3,
		0, 4, 1, 5, 2, 6, 3, 7,
		4, 5, 4, 6, 5, 7, 6, 7,
	};
	static const WORD c_awFillTriIndices[3]= { 0, 1, 2, };
	static const WORD c_awFillRectIndices[6] = { 0, 2, 1, 2, 3, 1, };
	static const WORD c_awFillCubeIndices[36] = { 
		0, 1, 2, 1, 3, 2,
		2, 0, 6, 0, 4, 6,
		0, 1, 4, 1, 5, 4,
		1, 3, 5, 3, 7, 5,
		3, 2, 7, 2, 6, 7,
		4, 5, 6, 5, 7, 6,
	};
	
	if (!__CreateDefaultIndexBuffer(DEFAULT_IB_LINE, 2, c_awLineIndices))
		return false;
	if (!__CreateDefaultIndexBuffer(DEFAULT_IB_LINE_TRI, 6, c_awLineTriIndices))
		return false;
	if (!__CreateDefaultIndexBuffer(DEFAULT_IB_LINE_RECT, 8, c_awLineRectIndices))
		return false;
	if (!__CreateDefaultIndexBuffer(DEFAULT_IB_LINE_CUBE, 24, c_awLineCubeIndices))
		return false;
	if (!__CreateDefaultIndexBuffer(DEFAULT_IB_FILL_TRI, 3, c_awFillTriIndices))
		return false;
	if (!__CreateDefaultIndexBuffer(DEFAULT_IB_FILL_RECT, 6, c_awFillRectIndices))
		return false;
	if (!__CreateDefaultIndexBuffer(DEFAULT_IB_FILL_CUBE, 36, c_awFillCubeIndices))
		return false;
	
	return true;
}

void CGraphicDevice::InitBackBufferCount(UINT uBackBufferCount)
{
	m_uBackBufferCount=uBackBufferCount;
}

void CGraphicDevice::Destroy()
{
	__DestroyPDTVertexBufferList();
	__DestroyDefaultIndexBufferList();

	if (m_pStateManager)
	{
		delete m_pStateManager;
		m_pStateManager = NULL;
	}

	if (ms_pD3D10Device)
	{
		ms_pD3D10Device->OMSetRenderTargets(
			0,
			NULL,
			NULL
		);

		ms_pD3D10Device->ClearState();
	}

	safe_release(ms_pDepthStencilView);
	safe_release(ms_pDepthStencilTexture);

	safe_release(ms_pRenderTargetView);

	safe_release(ms_pSwapChain);
	safe_release(ms_pD3D10Device);

	safe_release(ms_lpSphereMesh);
	safe_release(ms_lpCylinderMesh);

	safe_release(ms_lpd3dMatStack);

	safe_release(ms_lpd3dDevice);
	safe_release(ms_lpd3d);

	ms_ptVS = 0;
	ms_pntVS = 0;
	ms_pnt2VS = 0;

	if (ms_hDC)
	{
		ReleaseDC(ms_hWnd, ms_hDC);
		ms_hDC = NULL;
	}

	__Initialize();
}
