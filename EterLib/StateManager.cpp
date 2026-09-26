#include "StdAfx.h"
#include "StateManager.h"

static HRESULT CompileDX10Shader(
	const char* pSource,
	const char* pEntryPoint,
	const char* pTarget,
	ID3DBlob** ppShaderBlob)
{
	if (!pSource ||
		!pEntryPoint ||
		!pTarget ||
		!ppShaderBlob)
	{
		return E_INVALIDARG;
	}

	*ppShaderBlob = NULL;

	ID3DBlob* pErrorBlob = NULL;

	UINT flags =
		D3DCOMPILE_ENABLE_STRICTNESS;

#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG;
#endif

	HRESULT hr = D3DCompile(
		pSource,
		strlen(pSource),
		NULL,
		NULL,
		NULL,
		pEntryPoint,
		pTarget,
		flags,
		0,
		ppShaderBlob,
		&pErrorBlob
	);

	if (FAILED(hr))
	{
		if (pErrorBlob)
		{
			TraceError(
				"DX10 shader compile error: %s",
				static_cast<const char*>(
					pErrorBlob->GetBufferPointer()
					)
			);
		}
	}

	if (pErrorBlob)
	{
		pErrorBlob->Release();
		pErrorBlob = NULL;
	}

	return hr;
}

static D3D10_PRIMITIVE_TOPOLOGY ConvertPrimitiveTopology(
	D3DPRIMITIVETYPE primitiveType)
{
	switch (primitiveType)
	{
	case D3DPT_POINTLIST:
		return D3D10_PRIMITIVE_TOPOLOGY_POINTLIST;

	case D3DPT_LINELIST:
		return D3D10_PRIMITIVE_TOPOLOGY_LINELIST;

	case D3DPT_LINESTRIP:
		return D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP;

	case D3DPT_TRIANGLELIST:
		return D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	case D3DPT_TRIANGLESTRIP:
		return D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

	default:
		return D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED;
	}
}

static UINT GetPrimitiveElementCount(
	D3DPRIMITIVETYPE primitiveType,
	UINT primitiveCount)
{
	switch (primitiveType)
	{
	case D3DPT_POINTLIST:
		return primitiveCount;

	case D3DPT_LINELIST:
		return primitiveCount * 2;

	case D3DPT_LINESTRIP:
		return primitiveCount + 1;

	case D3DPT_TRIANGLELIST:
		return primitiveCount * 3;

	case D3DPT_TRIANGLESTRIP:
		return primitiveCount + 2;

	case D3DPT_TRIANGLEFAN:
		return primitiveCount + 2;

	default:
		return 0;
	}
}

//#define StateManager_Assert(a) if (!(a)) puts("assert"#a)
#define StateManager_Assert(a) assert(a)

struct SLightData
{
	enum
	{
		LIGHT_NUM = 8,
	};
	D3DLIGHT8 m_akD3DLight[LIGHT_NUM];
} m_kLightData;



void CStateManager::SetLight(
	DWORD index,
	CONST D3DLIGHT8* pLight)
{
	assert(index < SLightData::LIGHT_NUM);

	m_kLightData.m_akD3DLight[index] = *pLight;
}

void CStateManager::GetLight(DWORD index, D3DLIGHT8* pLight)
{
	assert(index<8);
	*pLight=m_kLightData.m_akD3DLight[index];
}

bool CStateManager::BeginScene()
{

	m_bScene = true;
	return true;
}

void CStateManager::EndScene()
{
	m_bScene = false;
}

CStateManager::CStateManager(ID3D10Device* pDevice)
	: m_lpD3DDev(NULL),
	m_pD3D10Device(NULL),
	m_pPDTVertexShader(NULL),
	m_pPDTPixelShader(NULL),
	m_pPDTInputLayout(NULL),
	m_pPDTConstantBuffer(NULL),
	m_pPDTSamplerState(NULL),
	m_pPDTBlendStateAlpha(NULL),
	m_pPDTBlendStateOpaque(NULL),
	m_pPDTDepthStateDisabled(NULL),
	m_pPDTDepthStateEnabled(NULL),
	m_bPDTShaderPipelineActive(false),
	m_pDX10IndexBuffer(NULL),
	m_DX10IndexFormat(DXGI_FORMAT_R16_UINT),
	m_DX10BaseVertexIndex(0)
{
	TraceError("CStateManager CREATED this=%p", this);
	m_bScene = false;

	m_dwBestMinFilter = D3DTEXF_LINEAR;
	m_dwBestMagFilter = D3DTEXF_LINEAR;

	SetDevice(pDevice);
}

CStateManager::~CStateManager()
{
	TraceError("CStateManager DESTROYED this=%p", this);

	DestroyDX10Resources();

	if (m_lpD3DDev)
	{
		m_lpD3DDev->Release();
		m_lpD3DDev = NULL;
	}

	if (m_pD3D10Device)
	{
		m_pD3D10Device->Release();
		m_pD3D10Device = NULL;
	}
}

bool CStateManager::CreateDX10Resources()
{
	if (!m_pD3D10Device)
		return false;

	DestroyDX10Resources();

	static const char* c_szPDTShader =
		"cbuffer PDTConstants : register(b0)\n"
		"{\n"
		"    row_major float4x4 gWorldViewProj;\n"
		"};\n"
		"\n"
		"Texture2D gTexture : register(t0);\n"
		"SamplerState gSampler : register(s0);\n"
		"\n"
		"struct VS_INPUT\n"
		"{\n"
		"    float3 position : POSITION;\n"
		"    float4 color : COLOR0;\n"
		"    float2 texCoord : TEXCOORD0;\n"
		"};\n"
		"struct VS_OUTPUT\n"
		"{\n"
		"    float4 position : SV_POSITION;\n"
		"    float4 color : COLOR0;\n"
		"    float2 texCoord : TEXCOORD0;\n"
		"};\n"
		"\n"
		"VS_OUTPUT VSMain(VS_INPUT input)\n"
		"{\n"
		"    VS_OUTPUT output;\n"
		"    output.position = mul(float4(input.position, 1.0f), gWorldViewProj);\n"
		"    output.color = input.color.bgra;\n"
		"    output.texCoord = input.texCoord;\n"
		"    return output;\n"
		"}\n"
		"\n"
		"float4 PSMain(VS_OUTPUT input) : SV_TARGET\n"
		"{\n"
		"    return float4(0.0f, 1.0f, 0.0f, 1.0f);\n"
		"}\n";

	ID3DBlob* pVSBlob = NULL;
	ID3DBlob* pPSBlob = NULL;

	HRESULT hr = CompileDX10Shader(
		c_szPDTShader,
		"VSMain",
		"vs_4_0",
		&pVSBlob
	);

	if (FAILED(hr))
	{
		TraceError(
			"CStateManager::CreateDX10Resources - "
			"VS compilation failed: 0x%08X",
			hr
		);

		return false;
	}

	hr = CompileDX10Shader(
		c_szPDTShader,
		"PSMain",
		"ps_4_0",
		&pPSBlob
	);

	if (FAILED(hr))
	{
		pVSBlob->Release();
		pVSBlob = NULL;

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"PS compilation failed: 0x%08X",
			hr
		);

		return false;
	}

	hr = m_pD3D10Device->CreateVertexShader(
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		&m_pPDTVertexShader
	);

	if (FAILED(hr))
	{
		pPSBlob->Release();
		pPSBlob = NULL;

		pVSBlob->Release();
		pVSBlob = NULL;

		return false;
	}

	hr = m_pD3D10Device->CreatePixelShader(
		pPSBlob->GetBufferPointer(),
		pPSBlob->GetBufferSize(),
		&m_pPDTPixelShader
	);

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		pPSBlob->Release();
		pPSBlob = NULL;

		pVSBlob->Release();
		pVSBlob = NULL;

		return false;
	}

	D3D10_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D10_INPUT_PER_VERTEX_DATA,
			0
		},

		{
			"COLOR",
			0,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			0,
			12,
			D3D10_INPUT_PER_VERTEX_DATA,
			0
		},

		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			16,
			D3D10_INPUT_PER_VERTEX_DATA,
			0
		},
	};

	hr = m_pD3D10Device->CreateInputLayout(
		inputLayout,
		sizeof(inputLayout) /
		sizeof(inputLayout[0]),
		pVSBlob->GetBufferPointer(),
		pVSBlob->GetBufferSize(),
		&m_pPDTInputLayout
	);

	pPSBlob->Release();
	pPSBlob = NULL;

	pVSBlob->Release();
	pVSBlob = NULL;

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"CreateInputLayout failed: 0x%08X",
			hr
		);

		return false;
	}

	D3D10_BUFFER_DESC constantBufferDesc;
	ZeroMemory(
		&constantBufferDesc,
		sizeof(constantBufferDesc)
	);

	constantBufferDesc.ByteWidth =
		sizeof(D3DXMATRIX);

	constantBufferDesc.Usage =
		D3D10_USAGE_DYNAMIC;

	constantBufferDesc.BindFlags =
		D3D10_BIND_CONSTANT_BUFFER;

	constantBufferDesc.CPUAccessFlags =
		D3D10_CPU_ACCESS_WRITE;

	constantBufferDesc.MiscFlags = 0;

	hr = m_pD3D10Device->CreateBuffer(
		&constantBufferDesc,
		NULL,
		&m_pPDTConstantBuffer
	);

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"Create constant buffer failed: 0x%08X",
			hr
		);

		return false;
	}

	D3D10_SAMPLER_DESC samplerDesc;
	ZeroMemory(
		&samplerDesc,
		sizeof(samplerDesc)
	);

	samplerDesc.Filter =
		D3D10_FILTER_MIN_MAG_MIP_LINEAR;

	samplerDesc.AddressU =
		D3D10_TEXTURE_ADDRESS_CLAMP;

	samplerDesc.AddressV =
		D3D10_TEXTURE_ADDRESS_CLAMP;

	samplerDesc.AddressW =
		D3D10_TEXTURE_ADDRESS_CLAMP;

	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;

	samplerDesc.ComparisonFunc =
		D3D10_COMPARISON_NEVER;

	samplerDesc.BorderColor[0] = 0.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 0.0f;
	samplerDesc.BorderColor[3] = 0.0f;

	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D10_FLOAT32_MAX;

	hr = m_pD3D10Device->CreateSamplerState(
		&samplerDesc,
		&m_pPDTSamplerState
	);

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"CreateSamplerState failed: 0x%08X",
			hr
		);

		return false;
	}

	D3D10_BLEND_DESC blendDesc;
	ZeroMemory(
		&blendDesc,
		sizeof(blendDesc)
	);

	blendDesc.AlphaToCoverageEnable = FALSE;

	for (int i = 0; i < 8; ++i)
	{
		blendDesc.BlendEnable[i] = FALSE;
		blendDesc.RenderTargetWriteMask[i] =
			D3D10_COLOR_WRITE_ENABLE_ALL;
	}

	blendDesc.BlendEnable[0] = TRUE;

	blendDesc.SrcBlend =
		D3D10_BLEND_SRC_ALPHA;

	blendDesc.DestBlend =
		D3D10_BLEND_INV_SRC_ALPHA;

	blendDesc.BlendOp =
		D3D10_BLEND_OP_ADD;

	blendDesc.SrcBlendAlpha =
		D3D10_BLEND_ONE;

	blendDesc.DestBlendAlpha =
		D3D10_BLEND_INV_SRC_ALPHA;

	blendDesc.BlendOpAlpha =
		D3D10_BLEND_OP_ADD;

	hr = m_pD3D10Device->CreateBlendState(
		&blendDesc,
		&m_pPDTBlendStateAlpha
	);

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"CreateBlendState alpha failed: 0x%08X",
			hr
		);

		return false;
	}

	ZeroMemory(
		&blendDesc,
		sizeof(blendDesc)
	);

	blendDesc.AlphaToCoverageEnable = FALSE;

	for (int i = 0; i < 8; ++i)
	{
		blendDesc.BlendEnable[i] = FALSE;
		blendDesc.RenderTargetWriteMask[i] =
			D3D10_COLOR_WRITE_ENABLE_ALL;
	}

	blendDesc.SrcBlend =
		D3D10_BLEND_ONE;

	blendDesc.DestBlend =
		D3D10_BLEND_ZERO;

	blendDesc.BlendOp =
		D3D10_BLEND_OP_ADD;

	blendDesc.SrcBlendAlpha =
		D3D10_BLEND_ONE;

	blendDesc.DestBlendAlpha =
		D3D10_BLEND_ZERO;

	blendDesc.BlendOpAlpha =
		D3D10_BLEND_OP_ADD;

	hr = m_pD3D10Device->CreateBlendState(
		&blendDesc,
		&m_pPDTBlendStateOpaque
	);

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"CreateBlendState opaque failed: 0x%08X",
			hr
		);

		return false;
	}

	D3D10_DEPTH_STENCIL_DESC depthDesc;
	ZeroMemory(
		&depthDesc,
		sizeof(depthDesc)
	);

	depthDesc.DepthEnable = FALSE;
	depthDesc.DepthWriteMask =
		D3D10_DEPTH_WRITE_MASK_ZERO;

	depthDesc.DepthFunc =
		D3D10_COMPARISON_ALWAYS;

	depthDesc.StencilEnable = FALSE;

	hr = m_pD3D10Device->CreateDepthStencilState(
		&depthDesc,
		&m_pPDTDepthStateDisabled
	);

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"CreateDepthStencilState disabled failed: 0x%08X",
			hr
		);

		return false;
	}

	ZeroMemory(
		&depthDesc,
		sizeof(depthDesc)
	);

	depthDesc.DepthEnable = TRUE;
	depthDesc.DepthWriteMask =
		D3D10_DEPTH_WRITE_MASK_ALL;

	depthDesc.DepthFunc =
		D3D10_COMPARISON_LESS_EQUAL;

	depthDesc.StencilEnable = FALSE;

	hr = m_pD3D10Device->CreateDepthStencilState(
		&depthDesc,
		&m_pPDTDepthStateEnabled
	);

	if (FAILED(hr))
	{
		DestroyDX10Resources();

		TraceError(
			"CStateManager::CreateDX10Resources - "
			"CreateDepthStencilState enabled failed: 0x%08X",
			hr
		);

		return false;
	}

	Tracef(
		"DX10 PDT shader pipeline created successfully\n"
	);

	return true;
}

void CStateManager::DestroyDX10Resources()
{
	if (m_pPDTDepthStateEnabled)
	{
		m_pPDTDepthStateEnabled->Release();
		m_pPDTDepthStateEnabled = NULL;
	}

	if (m_pPDTDepthStateDisabled)
	{
		m_pPDTDepthStateDisabled->Release();
		m_pPDTDepthStateDisabled = NULL;
	}
	if (m_pPDTBlendStateOpaque)
	{
		m_pPDTBlendStateOpaque->Release();
		m_pPDTBlendStateOpaque = NULL;
	}

	if (m_pPDTBlendStateAlpha)
	{
		m_pPDTBlendStateAlpha->Release();
		m_pPDTBlendStateAlpha = NULL;
	}
	if (m_pPDTSamplerState)
	{
		m_pPDTSamplerState->Release();
		m_pPDTSamplerState = NULL;
	}
	if (m_pPDTConstantBuffer)
	{
		m_pPDTConstantBuffer->Release();
		m_pPDTConstantBuffer = NULL;
	}
	if (m_pPDTInputLayout)
	{
		m_pPDTInputLayout->Release();
		m_pPDTInputLayout = NULL;
	}

	if (m_pPDTPixelShader)
	{
		m_pPDTPixelShader->Release();
		m_pPDTPixelShader = NULL;
	}

	if (m_pPDTVertexShader)
	{
		m_pPDTVertexShader->Release();
		m_pPDTVertexShader = NULL;
	}
}

void CStateManager::ApplyPDTShaderPipeline()
{
	if (!m_pD3D10Device)
		return;

	if (!m_pPDTConstantBuffer)
		return;

	D3DXMATRIX matWorldView;
	D3DXMATRIX matWorldViewProj;

	D3DXMatrixMultiply(
		&matWorldView,
		&m_CurrentState.m_Matrices[D3DTS_WORLD],
		&m_CurrentState.m_Matrices[D3DTS_VIEW]
	);

	D3DXMatrixMultiply(
		&matWorldViewProj,
		&matWorldView,
		&m_CurrentState.m_Matrices[D3DTS_PROJECTION]
	);

	void* pConstantData = NULL;

	HRESULT hr = m_pPDTConstantBuffer->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		&pConstantData
	);

	if (FAILED(hr))
	{
		TraceError(
			"CStateManager::ApplyPDTShaderPipeline - "
			"Constant buffer Map failed: 0x%08X",
			hr
		);

		return;
	}

	memcpy(
		pConstantData,
		&matWorldViewProj,
		sizeof(matWorldViewProj)
	);

	m_pPDTConstantBuffer->Unmap();

	m_pD3D10Device->IASetInputLayout(
		m_pPDTInputLayout
	);

	m_pD3D10Device->VSSetShader(
		m_pPDTVertexShader
	);

	m_pD3D10Device->VSSetConstantBuffers(
		0,
		1,
		&m_pPDTConstantBuffer
	);

	m_pD3D10Device->PSSetShader(
		m_pPDTPixelShader
	);

	m_pD3D10Device->PSSetSamplers(
		0,
		1,
		&m_pPDTSamplerState
	);

	ID3D10BlendState* pBlendState =
		m_pPDTBlendStateOpaque;

	if (m_CurrentState.m_RenderStates[
		D3DRS_ALPHABLENDENABLE])
	{
		pBlendState =
			m_pPDTBlendStateAlpha;
	}

	static ID3D10RasterizerState* s_pNoCullState = NULL;

	if (!s_pNoCullState)
	{
		D3D10_RASTERIZER_DESC rasterDesc;
		ZeroMemory(&rasterDesc, sizeof(rasterDesc));

		rasterDesc.FillMode = D3D10_FILL_SOLID;
		rasterDesc.CullMode = D3D10_CULL_NONE;
		rasterDesc.FrontCounterClockwise = FALSE;
		rasterDesc.DepthClipEnable = TRUE;

		HRESULT rasterHr = m_pD3D10Device->CreateRasterizerState(
			&rasterDesc,
			&s_pNoCullState
		);

		if (FAILED(rasterHr))
		{
			TraceError("CreateRasterizerState failed: 0x%08X", rasterHr);
		}
	}

	if (s_pNoCullState)
		m_pD3D10Device->RSSetState(s_pNoCullState);

	m_pD3D10Device->OMSetBlendState(
		pBlendState,
		NULL,
		0xFFFFFFFF
	);

	ID3D10DepthStencilState* pDepthState =
		m_pPDTDepthStateEnabled;

	if (!m_CurrentState.m_RenderStates[
		D3DRS_ZENABLE])
	{
		pDepthState =
			m_pPDTDepthStateDisabled;
	}

	m_pD3D10Device->OMSetDepthStencilState(
		pDepthState,
		0
	);
}

void CStateManager::SetPDTShaderPipelineActive(bool bActive)
{
	m_bPDTShaderPipelineActive = bActive;
}

void CStateManager::SetDevice(ID3D10Device* pDevice)
{
	StateManager_Assert(pDevice);

	pDevice->AddRef();

	if (m_pD3D10Device)
	{
		m_pD3D10Device->Release();
		m_pD3D10Device = NULL;
	}

	m_pD3D10Device = pDevice;

	m_CurrentState.ResetState();
	m_CopyState.ResetState();
	m_ChipState.ResetState();

	m_bScene = false;
	m_bForce = false;

	SetDefaultState();

	if (!CreateDX10Resources())
		TraceError("CStateManager::SetDevice - CreateDX10Resources failed");
}

void CStateManager::SetBestFiltering(DWORD dwStage)
{
	SetTextureStageState(dwStage, D3DTSS_MINFILTER,	m_dwBestMinFilter);
	SetTextureStageState(dwStage, D3DTSS_MAGFILTER,	m_dwBestMagFilter);
	SetTextureStageState(dwStage, D3DTSS_MIPFILTER,	D3DTEXF_LINEAR);
}

void CStateManager::Restore()
{
	int i, j;

	m_bForce = true;

	for (i = 0; i < STATEMANAGER_MAX_RENDERSTATES; ++i)
		SetRenderState(D3DRENDERSTATETYPE(i), m_CurrentState.m_RenderStates[i]);

	for (i = 0; i < STATEMANAGER_MAX_STAGES; ++i)
		for (j = 0; j < STATEMANAGER_MAX_TEXTURESTATES; ++j)
			SetTextureStageState(i, D3DTEXTURESTAGESTATETYPE(j), m_CurrentState.m_TextureStates[i][j]);

	for (i = 0; i < STATEMANAGER_MAX_STAGES; ++i)
		SetTexture(i, m_CurrentState.m_Textures[i]);
	
	m_bForce = false;
}

void CStateManager::SetDefaultState()
{
	m_CurrentState.ResetState();
	m_CopyState.ResetState();
	m_ChipState.ResetState();

	m_bScene = false;
	m_bForce = true;

	D3DXMATRIX Identity;
	D3DXMatrixIdentity(&Identity);

	SetTransform(D3DTS_WORLD, &Identity);
	SetTransform(D3DTS_VIEW, &Identity);
	SetTransform(D3DTS_PROJECTION, &Identity);

	D3DMATERIAL8 DefaultMat;
	ZeroMemory(&DefaultMat, sizeof(D3DMATERIAL8));

	DefaultMat.Diffuse.r = 1.0f;
	DefaultMat.Diffuse.g = 1.0f;
	DefaultMat.Diffuse.b = 1.0f;
	DefaultMat.Diffuse.a = 1.0f;
	DefaultMat.Ambient.r = 1.0f;
	DefaultMat.Ambient.g = 1.0f;
	DefaultMat.Ambient.b = 1.0f;
	DefaultMat.Ambient.a = 1.0f;
	DefaultMat.Emissive.r = 0.0f;
	DefaultMat.Emissive.g = 0.0f;
	DefaultMat.Emissive.b = 0.0f;
	DefaultMat.Emissive.a = 0.0f;
	DefaultMat.Specular.r = 0.0f;
	DefaultMat.Specular.g = 0.0f;
	DefaultMat.Specular.b = 0.0f;
	DefaultMat.Specular.a = 0.0f;
	DefaultMat.Power = 0.0f;

	SetMaterial(&DefaultMat);

	SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
	SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
	SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);

	SetRenderState(D3DRS_LINEPATTERN, 0xFFFFFFFF);
	SetRenderState(D3DRS_LASTPIXEL, FALSE);
	SetRenderState(D3DRS_ALPHAREF, 1);
	SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
	SetRenderState(D3DRS_ZVISIBLE, FALSE);
	SetRenderState(D3DRS_FOGSTART, 0);
	SetRenderState(D3DRS_FOGEND, 0);
	SetRenderState(D3DRS_FOGDENSITY, 0);
	SetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
	SetRenderState(D3DRS_ZBIAS, 0);
	SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
	SetRenderState(D3DRS_AMBIENT, 0x00000000);
	SetRenderState(D3DRS_LOCALVIEWER, FALSE);
	SetRenderState(D3DRS_NORMALIZENORMALS, FALSE);
	SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
	SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
	SetRenderState(D3DRS_SOFTWAREVERTEXPROCESSING, FALSE);
	SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	SetRenderState(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFF);
	SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	SetRenderState(D3DRS_COLORWRITEENABLE, 0xFFFFFFFF);
	SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
	SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	SetRenderState(D3DRS_FOGENABLE, FALSE);
	SetRenderState(D3DRS_FOGCOLOR, 0xFF000000);
	SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
	SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
	SetRenderState(D3DRS_ZENABLE, TRUE);
	SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	SetRenderState(D3DRS_DITHERENABLE, TRUE);
	SetRenderState(D3DRS_STENCILENABLE, FALSE);
	SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	SetRenderState(D3DRS_CLIPPING, TRUE);
	SetRenderState(D3DRS_LIGHTING, FALSE);
	SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	SetRenderState(D3DRS_COLORVERTEX, FALSE);
	SetRenderState(D3DRS_WRAP0, 0);
	SetRenderState(D3DRS_WRAP1, 0);
	SetRenderState(D3DRS_WRAP2, 0);
	SetRenderState(D3DRS_WRAP3, 0);
	SetRenderState(D3DRS_WRAP4, 0);
	SetRenderState(D3DRS_WRAP5, 0);
	SetRenderState(D3DRS_WRAP6, 0);
	SetRenderState(D3DRS_WRAP7, 0);

	SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
	SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
	SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);

	SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(2, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(3, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(3, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(3, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(3, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(4, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(4, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(4, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(4, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(4, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(4, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(5, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(5, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(5, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(5, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(5, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(5, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(6, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(6, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(6, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(6, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(6, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(6, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(7, D3DTSS_COLOROP, D3DTOP_DISABLE);
	SetTextureStageState(7, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	SetTextureStageState(7, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	SetTextureStageState(7, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetTextureStageState(7, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	SetTextureStageState(7, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
	SetTextureStageState(2, D3DTSS_TEXCOORDINDEX, 2);
	SetTextureStageState(3, D3DTSS_TEXCOORDINDEX, 3);
	SetTextureStageState(4, D3DTSS_TEXCOORDINDEX, 4);
	SetTextureStageState(5, D3DTSS_TEXCOORDINDEX, 5);
	SetTextureStageState(6, D3DTSS_TEXCOORDINDEX, 6);
	SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, 7);

	SetBestFiltering(0);
	SetBestFiltering(1);
	SetBestFiltering(2);
	SetBestFiltering(3);
	SetBestFiltering(4);
	SetBestFiltering(5);
	SetBestFiltering(6);
	SetBestFiltering(7);

	/* {
		SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

		SetTextureStageState(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

		SetTextureStageState(2, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(2, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(2, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

		SetTextureStageState(3, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(3, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(3, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

		SetTextureStageState(4, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(4, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(4, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

		SetTextureStageState(5, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(5, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(5, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

		SetTextureStageState(6, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(6, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(6, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

		SetTextureStageState(7, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(7, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		SetTextureStageState(7, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	}*/
	SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetTextureStageState(1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetTextureStageState(2, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(2, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetTextureStageState(3, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(3, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetTextureStageState(4, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(4, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetTextureStageState(5, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(5, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetTextureStageState(6, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(6, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	SetTextureStageState(7, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	SetTextureStageState(7, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

	SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
	SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
	SetTextureStageState(2, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
	SetTextureStageState(3, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
	SetTextureStageState(4, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
	SetTextureStageState(5, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
	SetTextureStageState(6, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
	SetTextureStageState(7, D3DTSS_TEXTURETRANSFORMFLAGS, 0);

	SetTexture(0, NULL);
	SetTexture(1, NULL);
	SetTexture(2, NULL);
	SetTexture(3, NULL);
	SetTexture(4, NULL);
	SetTexture(5, NULL);
	SetTexture(6, NULL);
	SetTexture(7, NULL);

	SetPixelShader(0);
	SetVertexShader(D3DFVF_XYZ);

	D3DXVECTOR4 av4Null[STATEMANAGER_MAX_VCONSTANTS];
	memset(av4Null, 0, sizeof(av4Null));
	SetVertexShaderConstant(0, av4Null, STATEMANAGER_MAX_VCONSTANTS);
	SetPixelShaderConstant(0, av4Null, STATEMANAGER_MAX_PCONSTANTS);

	m_bForce = false;

#ifdef _DEBUG
	int i, j;
	for (i = 0; i < STATEMANAGER_MAX_RENDERSTATES; i++)
		m_bRenderStateSavingFlag[i] = FALSE;

	for (j = 0; j < STATEMANAGER_MAX_TRANSFORMSTATES; j++)
		m_bTransformSavingFlag[j] = FALSE;

	for (j = 0; j < STATEMANAGER_MAX_STAGES; ++j)
		for (i = 0; i < STATEMANAGER_MAX_TEXTURESTATES; ++i)
			m_bTextureStageStateSavingFlag[j][i] = FALSE;
#endif _DEBUG
}

// Material
void CStateManager::SaveMaterial()
{
	m_CopyState.m_D3DMaterial = m_CurrentState.m_D3DMaterial;
}

void CStateManager::SaveMaterial(const D3DMATERIAL8 * pMaterial)
{
	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_D3DMaterial = m_CurrentState.m_D3DMaterial;
	SetMaterial(pMaterial);
}

void CStateManager::RestoreMaterial()
{
	SetMaterial(&m_CopyState.m_D3DMaterial);
}

void CStateManager::SetMaterial(
	const D3DMATERIAL8* pMaterial)
{
	m_CurrentState.m_D3DMaterial = *pMaterial;
}

void CStateManager::GetMaterial(D3DMATERIAL8 * pMaterial)
{
	// Set the renderstate and remember it.
	*pMaterial = m_CurrentState.m_D3DMaterial;
}

// Renderstates
DWORD CStateManager::GetRenderState(D3DRENDERSTATETYPE Type)
{
	return m_CurrentState.m_RenderStates[Type];
}

void CStateManager::SaveRenderState(D3DRENDERSTATETYPE Type, DWORD dwValue)
{
#ifdef _DEBUG
	if (m_bRenderStateSavingFlag[Type])
	{
		Tracef(" CStateManager::SaveRenderState - This render state is already saved [%d, %d]\n", Type, dwValue);
		StateManager_Assert(!" This render state is already saved!");
	}
	m_bRenderStateSavingFlag[Type] = TRUE;
#endif _DEBUG

	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_RenderStates[Type] = m_CurrentState.m_RenderStates[Type];
	SetRenderState(Type, dwValue);
}

void CStateManager::RestoreRenderState(D3DRENDERSTATETYPE Type)
{
#ifdef _DEBUG
	if (!m_bRenderStateSavingFlag[Type])
	{
		Tracef(" CStateManager::SaveRenderState - This render state was not saved [%d, %d]\n", Type);
		StateManager_Assert(!" This render state was not saved!");
	}
	m_bRenderStateSavingFlag[Type] = FALSE;
#endif _DEBUG

	SetRenderState(Type, m_CopyState.m_RenderStates[Type]);
}

void CStateManager::SetRenderState(
	D3DRENDERSTATETYPE Type,
	DWORD Value)
{
	if (m_CurrentState.m_RenderStates[Type] == Value)
		return;

	m_CurrentState.m_RenderStates[Type] = Value;
}

void CStateManager::GetRenderState(D3DRENDERSTATETYPE Type, DWORD * pdwValue)
{
	*pdwValue = m_CurrentState.m_RenderStates[Type];
}

// Textures
void CStateManager::SaveTexture(DWORD dwStage, LPDIRECT3DBASETEXTURE8 pTexture)
{
	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_Textures[dwStage] = m_CurrentState.m_Textures[dwStage];
	SetTexture(dwStage, pTexture);
}

void CStateManager::RestoreTexture(DWORD dwStage)
{
	SetTexture(dwStage, m_CopyState.m_Textures[dwStage]);
}

void CStateManager::SetTexture(
	DWORD dwStage,
	LPDIRECT3DBASETEXTURE8 pTexture)
{
	if (pTexture == m_CurrentState.m_Textures[dwStage])
		return;

	m_CurrentState.m_Textures[dwStage] = pTexture;
}

void CStateManager::SetTextureDX10(
	DWORD dwStage,
	ID3D10ShaderResourceView* pTextureView)
{
	if (!m_pD3D10Device)
		return;

	if (dwStage >= STATEMANAGER_MAX_STAGES)
		return;

	m_pD3D10Device->PSSetShaderResources(
		dwStage,
		1,
		&pTextureView
	);
}

void CStateManager::GetTexture(DWORD dwStage, LPDIRECT3DBASETEXTURE8 * ppTexture)
{
	*ppTexture = m_CurrentState.m_Textures[dwStage];
}

// Texture stage states
void CStateManager::SaveTextureStageState(DWORD dwStage,D3DTEXTURESTAGESTATETYPE Type, DWORD dwValue)
{
	// Check that we have set this up before, if not, the default is this.
#ifdef _DEBUG
	if (m_bTextureStageStateSavingFlag[dwStage][Type])
	{
		Tracef(" CStateManager::SaveTextureStageState - This texture stage state is already saved [%d, %d]\n", dwStage, Type);
		StateManager_Assert(!" This texture stage state is already saved!");
	}
	m_bTextureStageStateSavingFlag[dwStage][Type] = TRUE;
#endif _DEBUG
	m_CopyState.m_TextureStates[dwStage][Type] = m_CurrentState.m_TextureStates[dwStage][Type];
	SetTextureStageState(dwStage, Type, dwValue);
}

void CStateManager::RestoreTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE Type)
{
#ifdef _DEBUG
	if (!m_bTextureStageStateSavingFlag[dwStage][Type])
	{
		Tracef(" CStateManager::RestoreTextureStageState - This texture stage state was not saved [%d, %d]\n", dwStage, Type);
		StateManager_Assert(!" This texture stage state was not saved!");
	}
	m_bTextureStageStateSavingFlag[dwStage][Type] = FALSE;
#endif _DEBUG
	SetTextureStageState(dwStage, Type, m_CopyState.m_TextureStates[dwStage][Type]);
}

void CStateManager::SetTextureStageState(
	DWORD dwStage,
	D3DTEXTURESTAGESTATETYPE Type,
	DWORD dwValue)
{
	if (m_CurrentState.m_TextureStates[dwStage][Type] == dwValue)
		return;

	m_CurrentState.m_TextureStates[dwStage][Type] = dwValue;
}

void CStateManager::GetTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE Type, DWORD * pdwValue)
{
	*pdwValue = m_CurrentState.m_TextureStates[dwStage][Type];
}

// Vertex Shader
void CStateManager::SaveVertexShader(DWORD dwShader)
{
	m_CopyState.m_dwVertexShader = m_CurrentState.m_dwVertexShader;
	SetVertexShader(dwShader);
}

void CStateManager::RestoreVertexShader()
{
	SetVertexShader(m_CopyState.m_dwVertexShader);
}

void CStateManager::SetVertexShader(DWORD dwShader)
{
	if (m_CurrentState.m_dwVertexShader == dwShader)
		return;

	m_CurrentState.m_dwVertexShader = dwShader;
}

void CStateManager::GetVertexShader(DWORD * pdwShader)
{
	*pdwShader = m_CurrentState.m_dwVertexShader;
}

// Pixel Shader
void CStateManager::SavePixelShader(DWORD dwShader)
{
	m_CopyState.m_dwPixelShader = m_CurrentState.m_dwPixelShader;
	SetPixelShader(dwShader);
}

void CStateManager::RestorePixelShader()
{
	SetPixelShader(m_CopyState.m_dwPixelShader);
}

void CStateManager::SetPixelShader(DWORD dwShader)
{
	if (m_CurrentState.m_dwPixelShader == dwShader)
		return;

	m_CurrentState.m_dwPixelShader = dwShader;
}

void CStateManager::GetPixelShader(DWORD * pdwShader)
{
	*pdwShader = m_CurrentState.m_dwPixelShader;
}

// *** These states are cached, but not protected from multiple sends of the same value.
// Transform
void CStateManager::SaveTransform(D3DTRANSFORMSTATETYPE Type, const D3DMATRIX* pMatrix)
{
#ifdef _DEBUG
	if (m_bTransformSavingFlag[Type])
	{
		Tracef(" CStateManager::SaveTransform - This transform is already saved [%d]\n", Type);
		StateManager_Assert(!" This trasform is already saved!");
	}
	m_bTransformSavingFlag[Type] = TRUE;
#endif _DEBUG

	m_CopyState.m_Matrices[Type] = m_CurrentState.m_Matrices[Type];
	SetTransform(Type, (D3DXMATRIX *)pMatrix);
}

void CStateManager::RestoreTransform(D3DTRANSFORMSTATETYPE Type)
{
#ifdef _DEBUG
	if (!m_bTransformSavingFlag[Type])
	{
		Tracef(" CStateManager::RestoreTransform - This transform was not saved [%d]\n", Type);
		StateManager_Assert(!" This render state was not saved!");
	}
	m_bTransformSavingFlag[Type] = FALSE;
#endif _DEBUG

	SetTransform(Type, &m_CopyState.m_Matrices[Type]);
}

// Don't cache-check the transform.  To much to do
void CStateManager::SetTransform(
	D3DTRANSFORMSTATETYPE Type,
	const D3DMATRIX* pMatrix)
{
	StateManager_Assert(
		Type < STATEMANAGER_MAX_TRANSFORMSTATES
	);

	m_CurrentState.m_Matrices[Type] = *pMatrix;
}

void CStateManager::GetTransform(D3DTRANSFORMSTATETYPE Type, D3DMATRIX * pMatrix)
{
	*pMatrix = m_CurrentState.m_Matrices[Type];
}

// SetVertexShaderConstant
void CStateManager::SaveVertexShaderConstant(DWORD dwRegister,CONST void* pConstantData,DWORD dwConstantCount)
{
	DWORD i;

	for (i = 0; i < dwConstantCount; i++)
	{
		StateManager_Assert((dwRegister + i) < STATEMANAGER_MAX_VCONSTANTS);
		m_CopyState.m_VertexShaderConstants[dwRegister + i] = m_CurrentState.m_VertexShaderConstants[dwRegister + i];
	}

	SetVertexShaderConstant(dwRegister, pConstantData, dwConstantCount);
}

void CStateManager::RestoreVertexShaderConstant(DWORD dwRegister, DWORD dwConstantCount)
{
	SetVertexShaderConstant(dwRegister, &m_CopyState.m_VertexShaderConstants[dwRegister], dwConstantCount);
}

void CStateManager::SetVertexShaderConstant(
	DWORD dwRegister,
	CONST void* pConstantData,
	DWORD dwConstantCount)
{
	for (DWORD i = 0; i < dwConstantCount; ++i)
	{
		StateManager_Assert(
			(dwRegister + i) <
			STATEMANAGER_MAX_VCONSTANTS
		);

		m_CurrentState.m_VertexShaderConstants[
			dwRegister + i
		] = *(
			((D3DXVECTOR4*)pConstantData) + i
			);
	}
}

// SetPixelShaderConstant
void CStateManager::SavePixelShaderConstant(DWORD dwRegister,CONST void* pConstantData,DWORD dwConstantCount)
{
	DWORD i;

	for (i = 0; i < dwConstantCount; i++)
	{
		StateManager_Assert((dwRegister + i) < STATEMANAGER_MAX_VCONSTANTS);
		m_CopyState.m_PixelShaderConstants[dwRegister + i] = *(((D3DXVECTOR4*)pConstantData) + i);
	}

	SetPixelShaderConstant(dwRegister, pConstantData, dwConstantCount);
}

void CStateManager::RestorePixelShaderConstant(DWORD dwRegister, DWORD dwConstantCount)
{
	SetPixelShaderConstant(dwRegister, &m_CopyState.m_PixelShaderConstants[dwRegister], dwConstantCount);
}

void CStateManager::SetPixelShaderConstant(
	DWORD dwRegister,
	CONST void* pConstantData,
	DWORD dwConstantCount)
{
	for (DWORD i = 0; i < dwConstantCount; ++i)
	{
		StateManager_Assert(
			(dwRegister + i) <
			STATEMANAGER_MAX_PCONSTANTS
		);

		m_CurrentState.m_PixelShaderConstants[
			dwRegister + i
		] = *(
			((D3DXVECTOR4*)pConstantData) + i
			);
	}
}

void CStateManager::SaveStreamSource(UINT StreamNumber, LPDIRECT3DVERTEXBUFFER8 pStreamData,UINT Stride)
{
	// Check that we have set this up before, if not, the default is this.
	m_CopyState.m_StreamData[StreamNumber] = m_CurrentState.m_StreamData[StreamNumber];
	SetStreamSource(StreamNumber, pStreamData, Stride);
}

void CStateManager::RestoreStreamSource(UINT StreamNumber)
{
	SetStreamSource(StreamNumber, 
					m_CopyState.m_StreamData[StreamNumber].m_lpStreamData, 
					m_CopyState.m_StreamData[StreamNumber].m_Stride);
}

void CStateManager::SetStreamSource(
	UINT StreamNumber,
	LPDIRECT3DVERTEXBUFFER8 pStreamData,
	UINT Stride)
{
	CStreamData kStreamData(
		pStreamData,
		Stride
	);

	if (m_CurrentState.m_StreamData[StreamNumber] == kStreamData)
		return;

	m_CurrentState.m_StreamData[StreamNumber] = kStreamData;
}

void CStateManager::SetStreamSourceDX10(
	UINT StreamNumber,
	ID3D10Buffer* pStreamData,
	UINT Stride)
{
	if (!m_pD3D10Device)
		return;
	m_bPDTShaderPipelineActive = false;
	UINT offset = 0;

	m_pD3D10Device->IASetVertexBuffers(
		StreamNumber,
		1,
		&pStreamData,
		&Stride,
		&offset
	);
}

void CStateManager::SaveIndices(LPDIRECT3DINDEXBUFFER8 pIndexData, UINT BaseVertexIndex)
{
	m_CopyState.m_IndexData = m_CurrentState.m_IndexData;
	SetIndices(pIndexData, BaseVertexIndex);
}

void CStateManager::RestoreIndices()
{
	SetIndices(m_CopyState.m_IndexData.m_lpIndexData, m_CopyState.m_IndexData.m_BaseVertexIndex);
}

void CStateManager::SetIndices(
	LPDIRECT3DINDEXBUFFER8 pIndexData,
	UINT BaseVertexIndex)
{
	CIndexData kIndexData(
		pIndexData,
		BaseVertexIndex
	);

	if (m_CurrentState.m_IndexData == kIndexData)
		return;

	m_CurrentState.m_IndexData = kIndexData;
}

void CStateManager::SetIndicesDX10(
	ID3D10Buffer* pIndexData,
	DXGI_FORMAT format,
	UINT BaseVertexIndex)
{
	if (!m_pD3D10Device)
		return;

	m_pDX10IndexBuffer = pIndexData;
	m_DX10IndexFormat = format;
	m_DX10BaseVertexIndex = BaseVertexIndex;

	m_pD3D10Device->IASetIndexBuffer(
		pIndexData,
		format,
		0
	);
}

UINT CStateManager::GetDX10BaseVertexIndex() const
{
	return m_DX10BaseVertexIndex;
}

HRESULT CStateManager::DrawPrimitive(
	D3DPRIMITIVETYPE PrimitiveType,
	UINT StartVertex,
	UINT PrimitiveCount)
{
	if (!m_pD3D10Device)
		return E_FAIL;

	if (m_bPDTShaderPipelineActive)
	{
		ApplyPDTShaderPipeline();
	}

	D3D10_PRIMITIVE_TOPOLOGY topology =
		ConvertPrimitiveTopology(
			PrimitiveType
		);

	if (topology == D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED)
	{
		TraceError(
			"CStateManager::DrawPrimitive - "
			"Unsupported primitive type: %d",
			PrimitiveType
		);

		return E_FAIL;
	}

	UINT vertexCount =
		GetPrimitiveElementCount(
			PrimitiveType,
			PrimitiveCount
		);

	if (vertexCount == 0)
		return E_FAIL;

	m_pD3D10Device->IASetPrimitiveTopology(
		topology
	);

	m_pD3D10Device->Draw(
		vertexCount,
		StartVertex
	);

	return S_OK;
}

HRESULT CStateManager::DrawPrimitiveUP(
	D3DPRIMITIVETYPE PrimitiveType,
	UINT PrimitiveCount,
	const void* pVertexStreamZeroData,
	UINT VertexStreamZeroStride)
{
	if (!m_pD3D10Device)
		return E_FAIL;

	if (!pVertexStreamZeroData)
		return E_INVALIDARG;

	if (VertexStreamZeroStride == 0)
		return E_INVALIDARG;

	D3D10_PRIMITIVE_TOPOLOGY topology =
		ConvertPrimitiveTopology(
			PrimitiveType
		);

	if (topology == D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED)
	{
		TraceError(
			"CStateManager::DrawPrimitiveUP - "
			"Unsupported primitive type: %d",
			PrimitiveType
		);

		return E_FAIL;
	}

	const UINT vertexCount =
		GetPrimitiveElementCount(
			PrimitiveType,
			PrimitiveCount
		);

	if (vertexCount == 0)
		return E_FAIL;

	const UINT bufferSize =
		vertexCount *
		VertexStreamZeroStride;

	D3D10_BUFFER_DESC bufferDesc;
	ZeroMemory(
		&bufferDesc,
		sizeof(bufferDesc)
	);

	bufferDesc.ByteWidth =
		bufferSize;

	bufferDesc.Usage =
		D3D10_USAGE_DYNAMIC;

	bufferDesc.BindFlags =
		D3D10_BIND_VERTEX_BUFFER;

	bufferDesc.CPUAccessFlags =
		D3D10_CPU_ACCESS_WRITE;

	bufferDesc.MiscFlags = 0;

	ID3D10Buffer* pVertexBuffer = NULL;

	HRESULT hr =
		m_pD3D10Device->CreateBuffer(
			&bufferDesc,
			NULL,
			&pVertexBuffer
		);

	if (FAILED(hr))
	{
		TraceError(
			"CStateManager::DrawPrimitiveUP - "
			"CreateBuffer failed: 0x%08X",
			hr
		);

		return hr;
	}

	void* pMappedData = NULL;

	hr = pVertexBuffer->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		&pMappedData
	);

	if (FAILED(hr))
	{
		if (pVertexBuffer)
		{
			pVertexBuffer->Release();
			pVertexBuffer = NULL;
		}

		TraceError(
			"CStateManager::DrawPrimitiveUP - "
			"Map failed: 0x%08X",
			hr
		);

		return hr;
	}

	memcpy(
		pMappedData,
		pVertexStreamZeroData,
		bufferSize
	);

	pVertexBuffer->Unmap();

	UINT offset = 0;

	m_pD3D10Device->IASetVertexBuffers(
		0,
		1,
		&pVertexBuffer,
		&VertexStreamZeroStride,
		&offset
	);

	m_pD3D10Device->IASetPrimitiveTopology(
		topology
	);

	m_pD3D10Device->Draw(
		vertexCount,
		0
	);

	ID3D10Buffer* pNullBuffer = NULL;
	UINT nullStride = 0;
	UINT nullOffset = 0;

	m_pD3D10Device->IASetVertexBuffers(
		0,
		1,
		&pNullBuffer,
		&nullStride,
		&nullOffset
	);

	if (pVertexBuffer)
	{
		pVertexBuffer->Release();
		pVertexBuffer = NULL;
	}

	return S_OK;
}

HRESULT CStateManager::DrawIndexedPrimitive(
	D3DPRIMITIVETYPE PrimitiveType,
	UINT minIndex,
	UINT NumVertices,
	UINT startIndex,
	UINT primCount)
{
	if (!m_pD3D10Device)
		return E_FAIL;
	static bool s_bLoggedPDT = false;
	if (!s_bLoggedPDT)
	{
		TraceError(
			"DrawIndexed PDT=%d VS=%p PS=%p Layout=%p CB=%p",
			m_bPDTShaderPipelineActive ? 1 : 0,
			m_pPDTVertexShader,
			m_pPDTPixelShader,
			m_pPDTInputLayout,
			m_pPDTConstantBuffer
		);

		s_bLoggedPDT = true;
	}
	if (m_bPDTShaderPipelineActive)
	{
		ApplyPDTShaderPipeline();
	}

	D3D10_PRIMITIVE_TOPOLOGY topology =
		ConvertPrimitiveTopology(
			PrimitiveType
		);

	if (topology == D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED)
	{
		TraceError(
			"CStateManager::DrawIndexedPrimitive - "
			"Unsupported primitive type: %d",
			PrimitiveType
		);

		return E_FAIL;
	}

	UINT indexCount =
		GetPrimitiveElementCount(
			PrimitiveType,
			primCount
		);

	if (indexCount == 0)
		return E_FAIL;

	m_pD3D10Device->IASetPrimitiveTopology(
		topology
	);

	m_pD3D10Device->DrawIndexed(
		indexCount,
		startIndex,
		static_cast<INT>(
			m_DX10BaseVertexIndex
			)
	);

	return S_OK;
}

HRESULT CStateManager::DrawIndexedPrimitiveUP(
	D3DPRIMITIVETYPE PrimitiveType,
	UINT MinVertexIndex,
	UINT NumVertexIndices,
	UINT PrimitiveCount,
	CONST void* pIndexData,
	D3DFORMAT IndexDataFormat,
	CONST void* pVertexStreamZeroData,
	UINT VertexStreamZeroStride)
{
	if (!m_pD3D10Device)
		return E_FAIL;

	if (!pIndexData || !pVertexStreamZeroData)
		return E_INVALIDARG;

	if (VertexStreamZeroStride == 0 || NumVertexIndices == 0)
		return E_INVALIDARG;

	D3D10_PRIMITIVE_TOPOLOGY topology =
		ConvertPrimitiveTopology(
			PrimitiveType
		);

	if (topology == D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED)
	{
		TraceError(
			"CStateManager::DrawIndexedPrimitiveUP - "
			"Unsupported primitive type: %d",
			PrimitiveType
		);

		return E_FAIL;
	}

	const UINT indexCount =
		GetPrimitiveElementCount(
			PrimitiveType,
			PrimitiveCount
		);

	if (indexCount == 0)
		return E_FAIL;

	UINT indexStride = 0;
	DXGI_FORMAT dxgiIndexFormat =
		DXGI_FORMAT_UNKNOWN;

	switch (IndexDataFormat)
	{
	case D3DFMT_INDEX16:
		indexStride = sizeof(WORD);
		dxgiIndexFormat =
			DXGI_FORMAT_R16_UINT;
		break;

	case D3DFMT_INDEX32:
		indexStride = sizeof(DWORD);
		dxgiIndexFormat =
			DXGI_FORMAT_R32_UINT;
		break;

	default:
		TraceError(
			"CStateManager::DrawIndexedPrimitiveUP - "
			"Unsupported index format: %d",
			IndexDataFormat
		);

		return E_FAIL;
	}

	const UINT vertexBufferSize =
		NumVertexIndices *
		VertexStreamZeroStride;

	const UINT indexBufferSize =
		indexCount *
		indexStride;

	ID3D10Buffer* pVertexBuffer = NULL;
	ID3D10Buffer* pIndexBuffer = NULL;

	D3D10_BUFFER_DESC vertexDesc;
	ZeroMemory(
		&vertexDesc,
		sizeof(vertexDesc)
	);

	vertexDesc.ByteWidth =
		vertexBufferSize;

	vertexDesc.Usage =
		D3D10_USAGE_DYNAMIC;

	vertexDesc.BindFlags =
		D3D10_BIND_VERTEX_BUFFER;

	vertexDesc.CPUAccessFlags =
		D3D10_CPU_ACCESS_WRITE;

	HRESULT hr =
		m_pD3D10Device->CreateBuffer(
			&vertexDesc,
			NULL,
			&pVertexBuffer
		);

	if (FAILED(hr))
	{
		TraceError(
			"CStateManager::DrawIndexedPrimitiveUP - "
			"Vertex CreateBuffer failed: 0x%08X",
			hr
		);

		return hr;
	}

	void* pMappedVertices = NULL;

	hr = pVertexBuffer->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		&pMappedVertices
	);

	if (FAILED(hr))
	{
		pVertexBuffer->Release();
		pVertexBuffer = NULL;

		return hr;
	}

	memcpy(
		pMappedVertices,
		pVertexStreamZeroData,
		vertexBufferSize
	);

	pVertexBuffer->Unmap();

	D3D10_BUFFER_DESC indexDesc;
	ZeroMemory(
		&indexDesc,
		sizeof(indexDesc)
	);

	indexDesc.ByteWidth =
		indexBufferSize;

	indexDesc.Usage =
		D3D10_USAGE_DYNAMIC;

	indexDesc.BindFlags =
		D3D10_BIND_INDEX_BUFFER;

	indexDesc.CPUAccessFlags =
		D3D10_CPU_ACCESS_WRITE;

	hr = m_pD3D10Device->CreateBuffer(
		&indexDesc,
		NULL,
		&pIndexBuffer
	);

	if (FAILED(hr))
	{
		pVertexBuffer->Release();
		pVertexBuffer = NULL;

		TraceError(
			"CStateManager::DrawIndexedPrimitiveUP - "
			"Index CreateBuffer failed: 0x%08X",
			hr
		);

		return hr;
	}

	void* pMappedIndices = NULL;

	hr = pIndexBuffer->Map(
		D3D10_MAP_WRITE_DISCARD,
		0,
		&pMappedIndices
	);

	if (FAILED(hr))
	{
		pIndexBuffer->Release();
		pIndexBuffer = NULL;

		pVertexBuffer->Release();
		pVertexBuffer = NULL;

		return hr;
	}

	memcpy(
		pMappedIndices,
		pIndexData,
		indexBufferSize
	);

	pIndexBuffer->Unmap();

	UINT vertexOffset = 0;

	m_pD3D10Device->IASetVertexBuffers(
		0,
		1,
		&pVertexBuffer,
		&VertexStreamZeroStride,
		&vertexOffset
	);

	m_pD3D10Device->IASetIndexBuffer(
		pIndexBuffer,
		dxgiIndexFormat,
		0
	);

	m_pD3D10Device->IASetPrimitiveTopology(
		topology
	);

	m_pD3D10Device->DrawIndexed(
		indexCount,
		0,
		-static_cast<INT>(
			MinVertexIndex
			)
	);

	ID3D10Buffer* pNullBuffer = NULL;
	UINT nullStride = 0;
	UINT nullOffset = 0;

	m_pD3D10Device->IASetVertexBuffers(
		0,
		1,
		&pNullBuffer,
		&nullStride,
		&nullOffset
	);

	m_pD3D10Device->IASetIndexBuffer(
		NULL,
		DXGI_FORMAT_R16_UINT,
		0
	);

	pIndexBuffer->Release();
	pIndexBuffer = NULL;

	pVertexBuffer->Release();
	pVertexBuffer = NULL;

	m_CurrentState.m_IndexData =
		CIndexData();

	m_CurrentState.m_StreamData[0] =
		CStreamData();

	return S_OK;
}
