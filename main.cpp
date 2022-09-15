#include<Windows.h>
#include<d3d12.h>
#include<dxgi1_6.h>
#include<vector>
#include<string>
#include<DirectXMath.h>
#include<d3dcompiler.h>
#define DIRECTINPUT_VERSION 0x0800//DirectInputのバージョン指定
#include<dinput.h>
#include<DirectXTex.h>
#include<wrl.h>
#include<d3dx12.h>
#include <time.h>
#include "Input.h"
#include "Player.h"
#include "PlayerBullet.h"
#include "Enemy.h"
#include "EnemyBullet.h"
#include "WinApp.h"
#include "Sprite.h"
#include "DirectXCommon.h"
#include "Zone.h"
#include "BlackBoard.h"
#include "Audio.h"
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")

using namespace DirectX;
using namespace Microsoft::WRL;
const int spriteSRVCount = 512;

#pragma region //構造体
//定数バッファ用データ構造体
struct ConstBufferData {
	XMFLOAT4 color;//色
	XMMATRIX mat;//3D変換行列
};

//頂点データ構造体
struct Vertex {
	XMFLOAT3 pos;//xyz座標
	XMFLOAT3 normal;//法線ベクトル
	XMFLOAT2 uv;//uv座標
};

struct PipelineSet {
	ComPtr<ID3D12PipelineState>pipelinestate;

	ComPtr<ID3D12RootSignature>rootsignature;
};

struct Object3d {
	//定数バッファ
	ID3D12Resource* constBUff;
	//定数バッファビューのハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandleCBV;
	//定数バッファビューのハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleCBV;

	ComPtr<ID3D12Resource> texBuff;

	ComPtr<ID3D12DescriptorHeap> descHeap;

	UINT descriptorHandleIncrementSize = 0;

	XMFLOAT3 scale = { 1,1,1 };
	XMFLOAT3 rotation = { 0,0,0 };
	XMFLOAT3 position = { 0,0,0 };
	XMMATRIX matWorld;
	Object3d* parent = nullptr;
};
#pragma endregion
#pragma region 関数


void InitializeObject3d(Object3d* object, int index, ComPtr<ID3D12Device> dev, ComPtr<ID3D12DescriptorHeap> descHeap) {
	HRESULT result;
	//定数バッファのヒープ設定
	// 頂点バッファの設定
	D3D12_HEAP_PROPERTIES heapprop{};   // ヒープ設定
	heapprop.Type = D3D12_HEAP_TYPE_UPLOAD; // GPUへの転送用

	D3D12_RESOURCE_DESC resdesc{};  // リソース設定
	resdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resdesc.Width = (sizeof(ConstBufferData) + 0xff) & ~0xff; // 頂点データ全体のサイズ
	resdesc.Height = 1;
	resdesc.DepthOrArraySize = 1;
	resdesc.MipLevels = 1;
	resdesc.SampleDesc.Count = 1;
	resdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	result = dev->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&object->constBUff)
	);

	UINT descHandleIncrementSize =
		dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	object->cpuDescHandleCBV = descHeap->GetCPUDescriptorHandleForHeapStart();
	object->cpuDescHandleCBV.ptr += index * descHandleIncrementSize;

	object->gpuDescHandleCBV = descHeap->GetGPUDescriptorHandleForHeapStart();
	object->gpuDescHandleCBV.ptr += index * descHandleIncrementSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
	cbvDesc.BufferLocation = object->constBUff->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (UINT)object->constBUff->GetDesc().Width;
	dev->CreateConstantBufferView(&cbvDesc, object->cpuDescHandleCBV);
}
void UpdateObject3d(Object3d* object, XMMATRIX& matview, XMMATRIX& matProjection, XMFLOAT4& color) {
	XMMATRIX matScale, matRot, matTrans;

	//スケール、回転、平行移動行列の計算
	matScale = XMMatrixScaling(object->scale.x, object->scale.y, object->scale.z);
	matRot = XMMatrixIdentity();
	matRot *= XMMatrixRotationZ(XMConvertToRadians(object->rotation.z));
	matRot *= XMMatrixRotationX(XMConvertToRadians(object->rotation.x));
	matRot *= XMMatrixRotationY(XMConvertToRadians(object->rotation.y));
	matTrans = XMMatrixTranslation(object->position.x, object->position.y, object->position.z);
	//ワールド行列の合成
	object->matWorld = XMMatrixIdentity();//変形をリセット
	object->matWorld *= matScale;//ワールド行列にスケーリングを反映
	object->matWorld *= matRot;//ワールド行列に回転を反映
	object->matWorld *= matTrans;//ワールド行列に平行移動を反映
	//親オブジェクトがあれば
	if (object->parent != nullptr) {
		//親オブジェクトのワールド行列を掛ける
		object->matWorld = object->parent->matWorld;
	}
	//定数バッファへデータ転送
	ConstBufferData* constMap = nullptr;
	if (SUCCEEDED(object->constBUff->Map(0, nullptr, (void**)&constMap))) {
		constMap->color = color;
		constMap->mat = object->matWorld * matview * matProjection;
		object->constBUff->Unmap(0, nullptr);
	}
}

PipelineSet object3dCreateGrphicsPipeline(ID3D12Device* dev) {
#pragma region 

	HRESULT result;
	ComPtr<ID3DBlob> vsBlob = nullptr; // 頂点シェーダオブジェクト
	ComPtr<ID3DBlob> psBlob = nullptr; // ピクセルシェーダオブジェクト
	ComPtr<ID3DBlob> errorBlob = nullptr; // エラーオブジェクト

	// 頂点シェーダの読み込みとコンパイル
	result = D3DCompileFromFile(
		L"Resources/Shaders/BasicVS.hlsl",  // シェーダファイル名
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE, // インクルード可能にする
		"main", "vs_5_0", // エントリーポイント名、シェーダーモデル指定
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, // デバッグ用設定
		0,
		&vsBlob, &errorBlob);

	if (FAILED(result)) {
		// errorBlobからエラー内容をstring型にコピー
		std::string errstr;
		errstr.resize(errorBlob->GetBufferSize());

		std::copy_n((char*)errorBlob->GetBufferPointer(),
			errorBlob->GetBufferSize(),
			errstr.begin());
		errstr += "\n";
		// エラー内容を出力ウィンドウに表示
		OutputDebugStringA(errstr.c_str());
		exit(1);
	}

	// ピクセルシェーダの読み込みとコンパイル
	result = D3DCompileFromFile(
		L"Resources/Shaders/BasicPS.hlsl",   // シェーダファイル名
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE, // インクルード可能にする
		"main", "ps_5_0", // エントリーポイント名、シェーダーモデル指定
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, // デバッグ用設定
		0,
		&psBlob, &errorBlob);

	if (FAILED(result)) {
		// errorBlobからエラー内容をstring型にコピー
		std::string errstr;
		errstr.resize(errorBlob->GetBufferSize());

		std::copy_n((char*)errorBlob->GetBufferPointer(),
			errorBlob->GetBufferSize(),
			errstr.begin());
		errstr += "\n";
		// エラー内容を出力ウィンドウに表示
		OutputDebugStringA(errstr.c_str());
		exit(1);
	}


	//頂点レイアウト
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{//xyz座標
			"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0
		},
		{//法線ベクトル
			"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0
		},
		{//uv座標
			"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0
		},
	};
#pragma endregion
#pragma region パイプライン
	//グラフィックスパイプライン設定
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline{};

	gpipeline.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
	gpipeline.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());

	gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;//標準設定

	gpipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	//デプステンシルステートの設定
	gpipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_NEVER;
	//gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	//レンダーターゲットのブレンド設定
	D3D12_RENDER_TARGET_BLEND_DESC& blenddesc = gpipeline.BlendState.RenderTarget[0];
	blenddesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;	// RBGA全てのチャンネルを描画
	blenddesc.BlendEnable = true;
	blenddesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blenddesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	blenddesc.DestBlendAlpha = D3D12_BLEND_ZERO;


	blenddesc.BlendOp = D3D12_BLEND_OP_ADD;
	//blenddesc.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;//反転
	//blenddesc.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;//減算
	//blenddesc.SrcBlend = D3D12_BLEND_ZERO;//加算

	//blenddesc.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;//反転
	blenddesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;//半透明
	blenddesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;//半透明

	gpipeline.InputLayout.pInputElementDescs = inputLayout;
	gpipeline.InputLayout.NumElements = _countof(inputLayout);

	gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	gpipeline.NumRenderTargets = 1;//描画対象は1つ
	gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;//0～255指定のRGBA
	gpipeline.SampleDesc.Count = 1;//1ぴくせるにつき1回サンプリング


	PipelineSet pipelineSet;

	CD3DX12_DESCRIPTOR_RANGE descRangeCBV, descRangeSRV;
	descRangeCBV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	descRangeSRV.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER rootparm[2];
	rootparm[0].InitAsConstantBufferView(0);
	rootparm[1].InitAsDescriptorTable(1, &descRangeSRV);

#pragma endregion
#pragma region シグネチャー
	CD3DX12_STATIC_SAMPLER_DESC samplerDesc = CD3DX12_STATIC_SAMPLER_DESC(0);

	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;//繰り返し
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;//繰り返し
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;//奥行繰り返し
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;//ボーダーの時は黒
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;//補完しない
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;//ミニマップ最大値
	samplerDesc.MinLOD = 0.0f;//ミニマップ最小値
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;//ピクセルシェーダからのみ可視

	ComPtr<ID3D12RootSignature> rootsignature;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_0(_countof(rootparm), rootparm, 1, &samplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob>rootSigBlob;
	result = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &errorBlob);
	result = dev->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&pipelineSet.rootsignature));

	// パイプラインにルートシグネチャをセット
	gpipeline.pRootSignature = pipelineSet.rootsignature.Get();
	ComPtr<ID3D12PipelineState> pipelinestate = nullptr;
	result = dev->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&pipelineSet.pipelinestate));
	return pipelineSet;
}
#pragma endregion
#pragma region //描画
void DrawObject3d(Object3d* object, ComPtr<ID3D12GraphicsCommandList> cmdList,
	ComPtr<ID3D12DescriptorHeap> descHeap, D3D12_VERTEX_BUFFER_VIEW& vbView,
	D3D12_INDEX_BUFFER_VIEW& ibView, D3D12_GPU_DESCRIPTOR_HANDLE
	gpuDescHandleSRV, UINT numIndices) {
	//頂点バッファの設定
	cmdList->IASetVertexBuffers(0, 1, &vbView);
	//インデックスバッファの設定
	cmdList->IASetIndexBuffer(&ibView);
	//デスクリプタヒープの配列
	ID3D12DescriptorHeap* ppHeaps[] = { descHeap.Get() };
	cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	//定数バッファビューをセット
	cmdList->SetGraphicsRootConstantBufferView(0, object->constBUff->GetGPUVirtualAddress());
	//シェーダリソースビューをセット
	cmdList->SetGraphicsRootDescriptorTable(1, gpuDescHandleSRV);
	//描画コマンド
	cmdList->DrawIndexedInstanced(numIndices, 2, 0, 0, 0);
}

#pragma endregion
#pragma endregion


float easeInSine(float x){
	return x * x * x;
}
float easeOutBack(float x){
	return x ==  1 ? 1 : 1 - powf(2, -10 * x);
}
#pragma region //Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	WinApp* winApp = nullptr;
	winApp = new WinApp();
	winApp->Initialize();
	DirectXCommon* dxCommon = nullptr;
	dxCommon = new DirectXCommon();
	dxCommon->Initialize(winApp);

	MSG msg{};//メッセージ

#ifdef _DEBUG

#endif // DEBUG

	HRESULT result;

	//描画初期化処理
#pragma region 読み込み
	//WICテクスチャのロード
	TexMetadata metadata{};
	ScratchImage scratchImg{};

	result = LoadFromWICFile(
		L"Resources/Circle.png",
		WIC_FLAGS_NONE,
		&metadata, scratchImg
	);

	const Image* img = scratchImg.GetImage(0, 0, 0);

	//リソース設定
	CD3DX12_RESOURCE_DESC texresDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata.format,
		metadata.width,
		(UINT)metadata.height,
		(UINT16)metadata.arraySize,
		(UINT16)metadata.mipLevels
	);
	//テクスチャ用のバッファの生成
	ComPtr<ID3D12Resource>texbuff = nullptr;
	result = dxCommon->GetDev()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0),
		D3D12_HEAP_FLAG_NONE,
		&texresDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&texbuff)
	);

	//テクスチャバッファにデータ転送
	result = texbuff->WriteToSubresource(
		0,
		nullptr,//全領域へコピー
		img->pixels,//元データアドレス
		(UINT)img->rowPitch,//1ラインサイズ
		(UINT)img->slicePitch//全サイズ
	);
#pragma endregion
#pragma region 立方体

	const float topHeight = 5.0f;
	const int DIV = 4;
	const float radius = 3;
	const int  constantBufferNum = 128;

	//頂点データ
	Vertex vertices[] = {
		{{-110.0f,-110.0f,0.0f},{0,0,0}, {0.0f,1.0f}},
		{{-110.0f,+110.0f,0.0f },{0,0,0},{0.0f,0.0f}},
		{{+110.0f,-110.0f,0.0f},{0,0,0},{1.0f,1.0f}},
		{{+110.0f,+110.0f,0.0f},{0,0,0},{1.0f,0.0f}},
	};

	//パーティクル用
	Vertex squarevertices[] = {
		//前
	{{-5.0f,-5.0f, 5.0f},{},{0.0f,1.0f}},//左下
	{{ 5.0f,-5.0f, 5.0f},{},{0.0f,0.0f}},//左上
	{{-5.0f, 5.0f, 5.0f},{},{1.0f,1.0f}},//右下
	{{ 5.0f, 5.0f, 5.0f},{},{1.0f,0.0f}},//右上

	//後ろ
	{{-5.0f, 5.0f,-5.0f},{},{0.0f,1.0f}},//左下
	{{ 5.0f, 5.0f,-5.0f},{},{0.0f,0.0f}},//左上
	{{-5.0f,-5.0f,-5.0f},{},{1.0f,1.0f}},//右下
	{{ 5.0f,-5.0f,-5.0f},{},{1.0f,0.0f}},//右上

	//左
	{{-5.0f, 5.0f, 5.0f},{},{0.0f,1.0f}},//左下
	{{-5.0f, 5.0f,-5.0f},{},{0.0f,0.0f}},//左上
	{{-5.0f,-5.0f, 5.0f},{},{1.0f,1.0f}},//右下
	{{-5.0f,-5.0f,-5.0f},{},{1.0f,0.0f}},//右上

	//右
	{{ 5.0f, -5.0f, 5.0f},{},{0.0f,1.0f}},//左下
	{{ 5.0f, -5.0f,-5.0f},{},{0.0f,0.0f}},//左上
	{{ 5.0f,  5.0f, 5.0f},{},{1.0f,1.0f}},//右下
	{{ 5.0f,  5.0f,-5.0f},{},{1.0f,0.0f}},//右上

	//下
	{{ 5.0f,5.0f,-5.0f},{},{0.0f,1.0f}},//左下
	{{-5.0f,5.0f,-5.0f},{},{0.0f,0.0f}},//左上
	{{ 5.0f,5.0f, 5.0f},{},{1.0f,1.0f}},//右下
	{{-5.0f,5.0f, 5.0f},{},{1.0f,0.0f}},//右上

	//上
	{{-5.0f,-5.0f,-5.0f},{},{0.0f,1.0f}},//左下
	{{ 5.0f,-5.0f,-5.0f},{},{0.0f,0.0f}},//左上
	{{-5.0f,-5.0f, 5.0f},{},{1.0f,1.0f}},//右下
	{{ 5.0f,-5.0f, 5.0f},{},{1.0f,0.0f}},//右上
	};

	unsigned short indices[] = {
		0,1,2,
		2,1,3,
	};

	//パーティクル用
	unsigned short squareindices[] = {
		0,1,2,
		2,1,3,

		4,5,6,
		6,5,7,

		8,9,10,
		10,9,11,

		12,13,14,
		14,13,15,

		16,17,18,
		18,17,19,

		20,21,22,
		22,21,23,
	};

#pragma endregion
#pragma region//背景用
#pragma region 頂点データ	
	//頂点データ全体のサイズ=頂点データ一つ文のサイズ*頂点データの要素数
	UINT sizeVB = static_cast<UINT>(sizeof(Vertex) * _countof(vertices));

	ComPtr<ID3D12Resource>vertBuff;
	result = dxCommon->GetDev()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeVB),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vertBuff)
	);

	// GPU上のバッファに対応した仮想メモリを取得
	Vertex* vertMap = nullptr;
	result = vertBuff->Map(0, nullptr, (void**)&vertMap);

	// 全頂点に対して
	for (int i = 0; i < _countof(vertices); i++) {
		vertMap[i] = vertices[i];   // 座標をコピー
	}

	// マップを解除
	vertBuff->Unmap(0, nullptr);
#pragma endregion
#pragma region インデックスデータ
	//インデックスデータ全体のサイズ
	UINT sizeIB = static_cast<UINT>(sizeof(unsigned short) * _countof(indices));
	//インデックスバッファの設定
	//インデックスバッファの生成
	ComPtr<ID3D12Resource>indexBuff;
	result = dxCommon->GetDev()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeIB),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuff)
	);

	//GPU上のバッファに対応した仮想メモリの取得
	unsigned short* indexMap = nullptr;
	result = indexBuff->Map(0, nullptr, (void**)&indexMap);

	//全インデックスに対して
	for (int i = 0; i < _countof(indices); i++) {
		indexMap[i] = indices[i];//インデックスをコピー
	}
#pragma endregion
#pragma region ビュー関係
	D3D12_INDEX_BUFFER_VIEW ibView{};
	ibView.BufferLocation = indexBuff->GetGPUVirtualAddress();
	ibView.Format = DXGI_FORMAT_R16_UINT;
	ibView.SizeInBytes = sizeIB;

	// 頂点バッファビューの作成
	D3D12_VERTEX_BUFFER_VIEW vbView{};

	vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
	vbView.SizeInBytes = sizeVB;
	vbView.StrideInBytes = sizeof(Vertex);
#pragma endregion
#pragma endregion
#pragma region //パーティクル用
	//頂点データ全体のサイズ=頂点データ一つ文のサイズ*頂点データの要素数
	UINT squaresizeVB = static_cast<UINT>(sizeof(Vertex) * _countof(squarevertices));

	ComPtr<ID3D12Resource>squarevertBuff;
	result = dxCommon->GetDev()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(squaresizeVB),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&squarevertBuff)
	);

	// GPU上のバッファに対応した仮想メモリを取得
	Vertex* squarevertMap = nullptr;
	result = squarevertBuff->Map(0, nullptr, (void**)&squarevertMap);

	// 全頂点に対して
	for (int i = 0; i < _countof(squarevertices); i++) {
		squarevertMap[i] = squarevertices[i];   // 座標をコピー
	}

	// マップを解除
	squarevertBuff->Unmap(0, nullptr);
#pragma region インデックスデータ
	//インデックスデータ全体のサイズ
	UINT squaresizeIB = static_cast<UINT>(sizeof(unsigned short) * _countof(squareindices));
	//インデックスバッファの設定
	//インデックスバッファの生成
	ComPtr<ID3D12Resource>squareindexBuff;
	result = dxCommon->GetDev()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(squaresizeIB),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&squareindexBuff)
	);

	//GPU上のバッファに対応した仮想メモリの取得
	unsigned short* squareindexMap = nullptr;
	result = squareindexBuff->Map(0, nullptr, (void**)&squareindexMap);

	//全インデックスに対して
	for (int i = 0; i < _countof(squareindices); i++) {
		squareindexMap[i] = squareindices[i];//インデックスをコピー
	}
#pragma endregion
#pragma region ビュー関係
	D3D12_INDEX_BUFFER_VIEW squareibView{};
	squareibView.BufferLocation = squareindexBuff->GetGPUVirtualAddress();
	squareibView.Format = DXGI_FORMAT_R16_UINT;
	squareibView.SizeInBytes = squaresizeIB;

	// 頂点バッファビューの作成
	D3D12_VERTEX_BUFFER_VIEW squarevbView{};

	squarevbView.BufferLocation = squarevertBuff->GetGPUVirtualAddress();
	squarevbView.SizeInBytes = squaresizeVB;
	squarevbView.StrideInBytes = sizeof(Vertex);
#pragma endregion
#pragma endregion
#pragma region //ヒープ設定
	D3D12_HEAP_PROPERTIES cbheapprop{};//ヒープ設定
	cbheapprop.Type = D3D12_HEAP_TYPE_UPLOAD;//GPUへの転送用
	//リソース設定
	D3D12_RESOURCE_DESC cbresdesc{};//リソース設定
	cbresdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbresdesc.Width = (sizeof(ConstBufferData) + 0xff) & ~0xff;//256バイトアラインメント
	cbresdesc.Height = 1;
	cbresdesc.DepthOrArraySize = 1;
	cbresdesc.MipLevels = 1;
	cbresdesc.SampleDesc.Count = 1;
	cbresdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
#pragma endregion
#pragma region//ビュー変換行列
	XMMATRIX matProjection = XMMatrixPerspectiveFovLH(
		XMConvertToRadians(60.0f),
		(float)WinApp::window_width / WinApp::window_height,
		0.1f, 1000.0f
	);

	XMMATRIX matView;
	XMFLOAT3 eye(0.0f, 0.0f, -10.0f);//視点座標
	XMFLOAT3 target(0.0f, 0.0f, 0.0f);//注視点座標
	XMFLOAT3 up(0.0f, 0.3f, 0.0f);//上方向ベクトル

	//matView = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));
#pragma endregion
#pragma region// 入力の初期化
	Input* input = nullptr;
	input = new Input();
	if (!input->Initialize(winApp)) {
		assert(0);
		return 1;
	}
#pragma endregion
#pragma region //オブジェクト
	//背景
	Object3d backScreen;
	//パーティクル
	const int Particle_Max = 80;
	Object3d particle[Particle_Max];

	XMFLOAT4 ParticleColor = { 1.0f,0.3f,0.3f,0.1f };

#pragma endregion
#pragma region 定数バッファ
#pragma region //定数バッファ用のデスクリプタヒープ
	ComPtr<ID3D12DescriptorHeap> basicDescHeap = nullptr;
#pragma endregion

#pragma region //設定構造体
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;//シェーダーから見える
	descHeapDesc.NumDescriptors = constantBufferNum + 1;//定数バッファの数
	//デスクリプタヒープの生成
	result = dxCommon->GetDev()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&basicDescHeap));
#pragma endregion
#pragma endregion
#pragma region //テクスチャ関係初期化
	InitializeObject3d(&backScreen, 1, dxCommon->GetDev(), basicDescHeap.Get());
	backScreen.scale = { 1,1,1 };
	backScreen.position = { 0,0,0 };
	backScreen.rotation = { 90,0,0 };

	for (int i = 0; i < _countof(particle); i++) {
		InitializeObject3d(&particle[i], 1, dxCommon->GetDev(), basicDescHeap.Get());
		particle[i].scale = { 0.0,0.0,0.0 };
		particle[i].position = { 0,0,0 };
		particle[i].rotation = { 0,0,0 };
	}
#pragma endregion
#pragma region ハンドル取得
	//デスクリプタヒープの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandleSRV =
		basicDescHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleSRV =
		basicDescHeap->GetGPUDescriptorHandleForHeapStart();
	//ハンドルアドレスを進める
	cpuDescHandleSRV.ptr += constantBufferNum * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	gpuDescHandleSRV.ptr += constantBufferNum * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#pragma endregion
#pragma region ビュー設定
	//シェーダリソースビュー設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};//設定構造体
	srvDesc.Format = metadata.format;//RGBA
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2Dテクスチャ
	srvDesc.Texture2D.MipLevels = 1;

	//デスクリプタの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE basicHeaphandle = basicDescHeap->GetCPUDescriptorHandleForHeapStart();
	//ハンドルのアドレスを進める
	basicHeaphandle.ptr += 2 * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//ヒープの2番目にシェーダリソースビュー作成
	dxCommon->GetDev()->CreateShaderResourceView(texbuff.Get(),//ビューと関連付けるバッファ
		&srvDesc,//テクスチャ設定情報
		cpuDescHandleSRV
	);
#pragma endregion
#pragma region 読み込み2
	TexMetadata metadata1{};
	ScratchImage scratchImg1{};

	result = LoadFromWICFile(
		L"Resources/Circle5if.png",
		WIC_FLAGS_NONE,
		&metadata1, scratchImg1
	);
	if (FAILED(result)) {
		return result;
	}
	const Image* img1 = scratchImg1.GetImage(0, 0, 0);

	//リソース設定
	CD3DX12_RESOURCE_DESC texresDesc1 = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata1.format,
		metadata1.width,
		(UINT)metadata1.height,
		(UINT16)metadata1.arraySize,
		(UINT16)metadata1.mipLevels
	);
	//テクスチャ用のバッファの生成
	ComPtr<ID3D12Resource>texbuff1 = nullptr;
	result = dxCommon->GetDev()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0),
		D3D12_HEAP_FLAG_NONE,
		&texresDesc1,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&texbuff1)
	);

	//テクスチャバッファにデータ転送
	result = texbuff1->WriteToSubresource(
		0,
		nullptr,//全領域へコピー
		img1->pixels,//元データアドレス
		(UINT)img1->rowPitch,//1ラインサイズ
		(UINT)img1->slicePitch//全サイズ
	);


	ComPtr<ID3D12DescriptorHeap> basicDescHeap1 = nullptr;

	//設定構造体
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc1 = {};
	descHeapDesc1.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descHeapDesc1.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;//シェーダーから見える
	descHeapDesc1.NumDescriptors = constantBufferNum + 1;//定数バッファの数
	//デスクリプタヒープの生成
	result = dxCommon->GetDev()->CreateDescriptorHeap(&descHeapDesc1, IID_PPV_ARGS(&basicDescHeap1));
#pragma endregion
#pragma region ハンドル取得2
	//デスクリプタヒープの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandleSRV1 =
		basicDescHeap1->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleSRV1 =
		basicDescHeap1->GetGPUDescriptorHandleForHeapStart();
	//ハンドルアドレスを進める
	cpuDescHandleSRV1.ptr += constantBufferNum * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	gpuDescHandleSRV1.ptr += constantBufferNum * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#pragma endregion
#pragma region ビュー設定2
	//シェーダリソースビュー設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1{};//設定構造体
	srvDesc1.Format = metadata1.format;//RGBA
	srvDesc1.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2Dテクスチャ
	srvDesc1.Texture2D.MipLevels = 1;

	//デスクリプタの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE basicHeaphandle1 = basicDescHeap1->GetCPUDescriptorHandleForHeapStart();
	//ハンドルのアドレスを進める
	basicHeaphandle1.ptr += 2 * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//ヒープの2番目にシェーダリソースビュー作成
	dxCommon->GetDev()->CreateShaderResourceView(texbuff1.Get(),//ビューと関連付けるバッファ
		&srvDesc1,//テクスチャ設定情報
		cpuDescHandleSRV1
	);
#pragma endregion
#pragma region 読み込み
	TexMetadata metadata2{};
	ScratchImage scratchImg2{};

	result = LoadFromWICFile(
		L"Resources/PlayCircle.png",
		WIC_FLAGS_NONE,
		&metadata2, scratchImg2
	);
	if (FAILED(result)) {
		return result;
	}
	const Image* img2 = scratchImg2.GetImage(0, 0, 0);

	//リソース設定
	CD3DX12_RESOURCE_DESC texresDesc2 = CD3DX12_RESOURCE_DESC::Tex2D(
		metadata2.format,
		metadata2.width,
		(UINT)metadata2.height,
		(UINT16)metadata2.arraySize,
		(UINT16)metadata2.mipLevels
	);
	//テクスチャ用のバッファの生成
	ComPtr<ID3D12Resource>texbuff2 = nullptr;
	result = dxCommon->GetDev()->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0),
		D3D12_HEAP_FLAG_NONE,
		&texresDesc2,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&texbuff2)
	);

	//テクスチャバッファにデータ転送
	result = texbuff2->WriteToSubresource(
		0,
		nullptr,//全領域へコピー
		img2->pixels,//元データアドレス
		(UINT)img2->rowPitch,//1ラインサイズ
		(UINT)img2->slicePitch//全サイズ
	);
#pragma endregion
	ComPtr<ID3D12DescriptorHeap> basicDescHeap2 = nullptr;
	//設定構造体
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc2 = {};
	descHeapDesc2.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descHeapDesc2.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;//シェーダーから見える
	descHeapDesc2.NumDescriptors = constantBufferNum + 1;//定数バッファの数
	//デスクリプタヒープの生成
	result = dxCommon->GetDev()->CreateDescriptorHeap(&descHeapDesc2, IID_PPV_ARGS(&basicDescHeap2));
#pragma region ハンドル取得
	//デスクリプタヒープの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandleSRV2 =
		basicDescHeap2->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleSRV2 =
		basicDescHeap2->GetGPUDescriptorHandleForHeapStart();
	//ハンドルアドレスを進める
	cpuDescHandleSRV2.ptr += constantBufferNum * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	gpuDescHandleSRV2.ptr += constantBufferNum * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#pragma endregion
#pragma region ビュー設定
	//シェーダリソースビュー設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};//設定構造体
	srvDesc2.Format = metadata2.format;//RGBA
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;//2Dテクスチャ
	srvDesc2.Texture2D.MipLevels = 1;

	//デスクリプタの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE basicHeaphandle2 = basicDescHeap2->GetCPUDescriptorHandleForHeapStart();
	//ハンドルのアドレスを進める
	basicHeaphandle2.ptr += 2 * dxCommon->GetDev()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//ヒープの2番目にシェーダリソースビュー作成
	dxCommon->GetDev()->CreateShaderResourceView(texbuff2.Get(),//ビューと関連付けるバッファ
		&srvDesc2,//テクスチャ設定情報
		cpuDescHandleSRV2
	);
#pragma endregion
#pragma region パイプラインのセット
	PipelineSet object3dPipelineSet = object3dCreateGrphicsPipeline(dxCommon->GetDev());

	XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };//スケーリング
	XMFLOAT3 rotation = { 0.0f,0.0f,0.0f };//回転
	XMFLOAT3 position = { 0.0f,0.0f,0.0f };//座標
	XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
#pragma endregion
#pragma region//スプライト関係
	// スプライト静的初期化
	if (!Sprite::StaticInitialize(dxCommon->GetDev(), WinApp::window_width, WinApp::window_height)) {
		assert(0);
		return 1;
	}
	const int SpriteMax = 100;
	Sprite* sprite[SpriteMax] = { nullptr };
	// テクスチャ読み込み
	Sprite::LoadTexture(0, L"Resources/TitleBack.png");
	Sprite::LoadTexture(1, L"Resources/waku.png");
	Sprite::LoadTexture(2, L"Resources/Space01.png");
	Sprite::LoadTexture(3, L"Resources/start03.png");
	Sprite::LoadTexture(4, L"Resources/HP/HP_1.png");
	Sprite::LoadTexture(5, L"Resources/HP/HP_2.png");
	Sprite::LoadTexture(6, L"Resources/HP/HP_3.png");
	Sprite::LoadTexture(7, L"Resources/HP/HP_4.png");
	Sprite::LoadTexture(8, L"Resources/HP/HP_5.png");
	Sprite::LoadTexture(9, L"Resources/HP/HP_0.png");
	Sprite::LoadTexture(10, L"Resources/HP/zannki.png");
	Sprite::LoadTexture(11, L"Resources/Stock/stock_10.png");
	Sprite::LoadTexture(12, L"Resources/Stock/stock_9.png");
	Sprite::LoadTexture(13, L"Resources/Stock/stock_8.png");
	Sprite::LoadTexture(14, L"Resources/Stock/stock_7.png");
	Sprite::LoadTexture(15, L"Resources/Stock/stock_6.png");
	Sprite::LoadTexture(16, L"Resources/Stock/stock_5.png");
	Sprite::LoadTexture(17, L"Resources/Stock/stock_4.png");
	Sprite::LoadTexture(18, L"Resources/Stock/stock_3.png");
	Sprite::LoadTexture(19, L"Resources/Stock/stock_2.png");
	Sprite::LoadTexture(20, L"Resources/Stock/stock_1.png");
	Sprite::LoadTexture(21, L"Resources/Stock/stock_0.png");
	Sprite::LoadTexture(22, L"Resources/Stock/Makibisi.png");
	Sprite::LoadTexture(23, L"Resources/EnemyHP.png");
	Sprite::LoadTexture(24, L"Resources/DoorLeft.png");
	Sprite::LoadTexture(25, L"Resources/DoorLight.png");
	Sprite::LoadTexture(26, L"Resources/PlayBack.png");
	Sprite::LoadTexture(27, L"Resources/GameOver.png");
	Sprite::LoadTexture(28, L"Resources/EnemyHPGauge.png");
	Sprite::LoadTexture(29, L"Resources/Clear.png");
	Sprite::LoadTexture(30, L"Resources/RetryUI.png");
	Sprite::LoadTexture(31, L"Resources/Skip.png");
	Sprite::LoadTexture(32, L"Resources/ClearUI.png");
	Sprite::LoadTexture(33, L"Resources/Arrow.png");


	// 背景スプライト生成
	sprite[0] = Sprite::Create(0, { 0.0f,0.0f });
	sprite[1] = Sprite::Create(1, { 0.0f,0.0f });
	sprite[2] = Sprite::Create(2, { 180.0f,200.0f });
	sprite[3] = Sprite::Create(3, { 30.0f, 97.5f });
	sprite[4] = Sprite::Create(4, { 120.0f,506.0f });
	sprite[5] = Sprite::Create(5, { 120.0f,506.0f });
	sprite[6] = Sprite::Create(6, { 120.0f,506.0f });
	sprite[7] = Sprite::Create(7, { 120.0f,506.0f });
	sprite[8] = Sprite::Create(8, { 120.0f,506.0f });
	sprite[9] = Sprite::Create(9, { 120.0f,506.0f });
	sprite[10] = Sprite::Create(10, { 40.0f,506.0f });
	sprite[11] = Sprite::Create(11, { 1100.0f,20.0f });
	sprite[12] = Sprite::Create(12, { 1100.0f,20.0f });
	sprite[13] = Sprite::Create(13, { 1100.0f,20.0f });
	sprite[14] = Sprite::Create(14, { 1100.0f,20.0f });
	sprite[15] = Sprite::Create(15, { 1100.0f,20.0f });
	sprite[16] = Sprite::Create(16, { 1100.0f,20.0f });
	sprite[17] = Sprite::Create(17, { 1100.0f,20.0f });
	sprite[18] = Sprite::Create(18, { 1100.0f,20.0f });
	sprite[19] = Sprite::Create(19, { 1100.0f,20.0f });
	sprite[20] = Sprite::Create(20, { 1100.0f,20.0f });
	sprite[21] = Sprite::Create(21, { 1100.0f,20.0f });
	sprite[22] = Sprite::Create(22, { 1030.0f,20.0f });
	sprite[23] = Sprite::Create(23, { 104.0f,31.0f });
	sprite[24] = Sprite::Create(24, { -600.0f,0.0f });
	sprite[25] = Sprite::Create(25, { 1200.0f,0.0f });
	sprite[26] = Sprite::Create(26, { 0.0f,0.0f });
	sprite[27] = Sprite::Create(27, { 0.0f,0.0f });
	sprite[28] = Sprite::Create(28, { 50.0f,17.0f });
	sprite[29] = Sprite::Create(29, { 0.0f,45.0f });
	sprite[30] = Sprite::Create(30, { 344.0f,506.0f });
	sprite[31] = Sprite::Create(31, { 944.0f,536.0f });
	sprite[32] = Sprite::Create(32, { 311.0f,536.0f });
	sprite[33] = Sprite::Create(33, { 0.0f,0.0f });

#pragma endregion
#pragma region//オーディオ関連
	const int AudioMax = 3;
	Audio* audio = new Audio;
	if (!audio->Initialize()) {
		assert(0);
		return 1;
	}
	audio->LoadSound(0, "Resources/Sound/BGM/TitleBGM.wav");
	audio->LoadSound(1, "Resources/Sound/BGM/InGameBGM.wav");
	audio->LoadSound(2, "Resources/Sound/BGM/PlayBGM.wav");
	audio->LoopWave2(0, 0.5f);
#pragma endregion
#pragma region//ゲーム変数
	const int ZoneMax = 3;
	Zone* zone;
	zone = new Zone();
	if (!zone->StaticInitialize(dxCommon->GetDev(), WinApp::window_width, WinApp::window_height)) {
		assert(0);
		return 1;
	}
	zone = Zone::Create();
	int arrowFlag = 1;
	BlackBoard* bb;
	if (!bb->StaticInitialize(dxCommon->GetDev(), WinApp::window_width, WinApp::window_height)) {
		assert(0);
		return 1;
	}
	bb = BlackBoard::Create();
	bb->Update(matView);
	const int BulletMax = 10;
	const int BulletAreaMax = 4;
#pragma endregion
#pragma region//プレイヤーの変数
	Player* player;
	player = new Player();
	if (!player->StaticInitialize(dxCommon->GetDev(), WinApp::window_width, WinApp::window_height)) {
		assert(0);
		return 1;
	}
	player = Player::Create();
	player->Update(matView);

	XMFLOAT3 PlayerRotation = player->GetRotaition();
	XMFLOAT3 PlayerPosition = player->GetPosition();
	int PlayerAreaNumber = 0;
#pragma endregion
#pragma region//敵の変数
	Enemy* enemy;
	enemy = new Enemy();
	if (!enemy->StaticInitialize(dxCommon->GetDev(), WinApp::window_width, WinApp::window_height)) {
		assert(0);
		return 1;
	}
	enemy = Enemy::Create();
	XMFLOAT3 EnemyRotation = enemy->GetRotaition();
	XMFLOAT3 EnemyPosition = enemy->GetPosition();

	//敵の行動決める
	int EnemyAIRand = 0;
	int EnemyAINumber = 0;
	int MaxRotationCount = 0;
	int EnemyMoveTimer = 0;
#pragma endregion
#pragma region//プレイヤーの弾の変数
	PlayerBullet* playerBullet[BulletMax]{};
	for (int i = 0; i < BulletMax; i++) {
		playerBullet[i] = new PlayerBullet();
		if (!playerBullet[i]->StaticInitialize(dxCommon->GetDev(), WinApp::window_width, WinApp::window_height)) {
			assert(0);
			return 1;
		}
		playerBullet[i] = PlayerBullet::Create();
		playerBullet[i]->Update(matView, enemy->GetAwakeingNumber());
	}
	int bulletNum = 0;
	int bulletCount = 2;//毎フレームの弾情報取得
	for (int i = 0; i < BulletMax; i++) {
		if (playerBullet[i]->GetIsAlive() == 0) {
			bulletNum++;
		}
	}
#pragma endregion
#pragma region//敵の弾の変数
	const int EnemyBulletMax = 20;
	EnemyBullet* enemyBullet[EnemyBulletMax]{};
	for (int i = 0; i < EnemyBulletMax;i++) {
		enemyBullet[i] = new EnemyBullet();
		if (!enemyBullet[i]->StaticInitialize(dxCommon->GetDev(), WinApp::window_width, WinApp::window_height)) {
			assert(0);
			return 1;
		}
		enemyBullet[i] = EnemyBullet::Create();
		enemyBullet[i]->Update(matView);
	}
	int FlameSetNumber = 0;
	int FlameTimer = 0; 
#pragma endregion
#pragma region//カメラ
	float angle = 0.0f;//カメラの回転角度
	XMVECTOR v0 = { 0,0,-200,0 };
	XMMATRIX rotM;
	XMVECTOR eye2;
	XMVECTOR target2 = { 0, 0, 0,0 };
	XMVECTOR up2 = { 0.0f, 0.3f, 0.0f,0.0f };
	matView = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));
#pragma endregion
#pragma region//レーン関係の変数
	int RemoveLane = 0;
	int LaneNum = 2;
	int InsideCount = 0;
	int CenterCount = 0;
	int OutsideCount = 0;
	//レーン移動の処理
	float frame = 0.0f;
	float frameMax = 17.0f;
	float initScale = 0.0f;
	int MoveNumber = 0;
	XMFLOAT3 BasePosition = { 0.0f,0.0f,0.0f };
#pragma endregion
#pragma region//敵の突進処理
	int AttackTimer = 0;
	int AttackFlag = 0;
	int AttackNumber = 0;
	float AttackSpeed = 0.0f;
	XMFLOAT3 AttackPosition = { 0.0f,0.0f,0.0f };
	float angleX = 0.0f;
	float angleZ = 0.0f;
	double	angleR = 0;
	int DistanceCount = 0;
	int ReturnFlag = 0;
	int AttackCount = 0;
#pragma endregion
#pragma region//突進するまでの挙動
	int SlopeFlag = 0;
	int SlopeNumber = 0;
	int SlopeCount = 0;
	int TargetSlopeCount = 0;
	float SlopeG = 0.0f;
#pragma endregion
#pragma region//爆弾除去処理
	int BulletLane[BulletMax]{};
	int BulletAreaCount[BulletAreaMax] = { 0 };
	int RemoveAreaNumber = 0;
#pragma endregion
#pragma region//パーティクルの円運動
	int RemoveParticleStartflag = 0;
	float Particleradius[Particle_Max] = { 0.0f };
	float ParticleSpeed[Particle_Max] = { 0.0f };
	float Particlescale[Particle_Max] = { 0.0f };
	float ParticleCircleX[Particle_Max] = { 0.0f };
	float ParticleCircleZ[Particle_Max] = { 0.0f };
	float ParticleScaleRand[Particle_Max] = { 0.0f };
	int ParticleFlag[Particle_Max] = { 0 };
#pragma endregion
#pragma region//円運動のための変数
	float PI = 3.14f;
	float Playerradius = 0.0f;
	float PlayerSpeed = 0.0f;
	float Playerscale = 75.0f;// LaneNumと一緒に変えること
	float PlayerCircleX = 0.0f;
	float PlayerCircleZ = 0.0f;
#pragma endregion
#pragma region//カメラ関係
	int CameraSetNumber = 0;
	float Cameraangle = 0.0f;
	float Cameraradius = 0.0f;
	float CameraSpeed = 0.0f;
	float Camerascale = 0.0f;
	float CameraCircleX = 0.0f;
	float CameraCircleZ = 0.0f;
	XMFLOAT3 CameraPosition(0.0f, 5.0f, 0.0f);
	Cameraradius = PlayerSpeed * PI / 180.0f;
	CameraCircleX = cosf(Cameraradius) * Camerascale;
	CameraCircleZ = sinf(Cameraradius) * Camerascale;
	CameraPosition.x = CameraCircleX + EnemyPosition.x;
	CameraPosition.z = CameraCircleZ + EnemyPosition.z;
	int ClearCameraTimer = 0;
	float AppearanceCameraTargetY = 0.0f;

	XMFLOAT3 playertarget = XMFLOAT3(BasePosition.x - PlayerPosition.x, BasePosition.y - PlayerPosition.y, BasePosition.y - PlayerPosition.z);

	float length = sqrtf(powf(playertarget.x, 2.0f) + powf(playertarget.y, 2.0f) + powf(playertarget.z, 2.0f));

	XMFLOAT3 enemytarget = XMFLOAT3(playertarget.x / length, playertarget.y / length, playertarget.z / length);

#pragma endregion
#pragma region	//シーン
	int Scene = 0;
	XMFLOAT3 BbRotation = bb->GetRotation();
	enum Scene {
		title,
		appearance,
		gamePlay,
		awakening,
		gameOver,
		gameClear
	};
	int OverFlag = 0;
	int OverCount = 0;
	int SceneChange = 0;
	float doorX = -600;
	float doorX2 = 1200;
	int AwakeningSceneNumber = 0;
	int AwakeningSceneTimer = 0;
#pragma endregion
#pragma region//ループ全体
	while (true)//ゲームループ
	{
#pragma region//すべての更新処理
#pragma region//ループ開始時の処理
		input->Update();
		//DirectX毎フレーム処理 ここから
		PlayerPosition = player->GetPosition();
		EnemyPosition = enemy->GetPosition();
		PlayerRotation = player->GetRotaition();
		EnemyRotation = enemy->GetRotaition();

		//円運動の計算
		Cameraradius = CameraSpeed * PI / 180.0f;
		CameraCircleX = cosf(Cameraradius) * Camerascale;
		CameraCircleZ = sinf(Cameraradius) * Camerascale;
		CameraPosition.x = CameraCircleX + BasePosition.x;
		CameraPosition.z = CameraCircleZ + BasePosition.z;
		//プレイヤー
		Playerradius = PlayerSpeed * PI / 180.0f;
		PlayerCircleX = cosf(Playerradius) * Playerscale;
		PlayerCircleZ = sinf(Playerradius) * Playerscale;
		PlayerPosition.x = PlayerCircleX + BasePosition.x;
		PlayerPosition.z = PlayerCircleZ + BasePosition.z;
		//弾を毎フレームの取得
		if (bulletCount == 2) {
			bulletNum = 0;
			for (int i = 0; i < BulletMax; i++) {
				if (playerBullet[i]->GetIsAlive() == 0) {
					bulletNum++;
				}
			}
			bulletCount = 0;
		}
		bulletCount++;
		//扉の開封
		if (SceneChange == 1) {
			doorX += 20.0f;
			doorX2 -= 20.0f;
			sprite[24]->SetPosition({ doorX ,0 });
			sprite[25]->SetPosition({ doorX2 ,0 });
			if (doorX == 0.0f) {
				SceneChange = 2;
			}
		} else if (SceneChange == 2) {
			doorX -= 20.0f;
			doorX2 += 20.0f;
			sprite[24]->SetPosition({ doorX ,0 });
			sprite[25]->SetPosition({ doorX2 ,0 });
			if (doorX == -600.0f) {
				SceneChange = 0;
			}
		}

#pragma endregion
#pragma region//移動処理とカメラ共通
		if (Scene != appearance) {
			//乱数の設定
			srand((unsigned int)time(NULL));
			if (SceneChange == 0) {
				if (player->GetDamageCount() == 1) {
					audio->PlayWave("Resources/Sound/Player_Damage01.wav", 0.7f);
				}
				if (OverFlag != 1) {
					if ( (input->TriggerKey(DIK_UP) || input->TriggerKey(DIK_W))||
						 (input->TriggerKey(DIK_DOWN) || input->TriggerKey(DIK_S))||
						 (input->PushKey(DIK_LEFT) || input->PushKey(DIK_A))||
						 (input->PushKey(DIK_RIGHT) || input->PushKey(DIK_D))        ) {
						if (arrowFlag == 1) {
							arrowFlag = 0;
						}
						if (MoveNumber == 0) {
							//プレイヤーのレーン移り変わり
							if (LaneNum > 0 && (input->TriggerKey(DIK_UP) || input->TriggerKey(DIK_W))) {
								LaneNum--;
								frame = 0;
								initScale = Playerscale;
								MoveNumber = 1;
							}

							if (LaneNum < 2 && (input->TriggerKey(DIK_DOWN) || input->TriggerKey(DIK_S))) {
								LaneNum++;
								frame = 0;
								initScale = Playerscale;
								MoveNumber = 2;
							}
						}
						//プレイヤーの左右移動
						if ((input->PushKey(DIK_LEFT) || input->PushKey(DIK_A))) {
							if (LaneNum == 0) {
								PlayerSpeed -= 1.25;
								PlayerRotation.y += 1.25f;
								BbRotation.y += 1.25f;
							} else if (LaneNum == 1) {
								PlayerSpeed -= 1.0f;
								PlayerRotation.y += 1.0f;
								BbRotation.y += 1.0f;
							} else {
								PlayerSpeed -= 0.60f;
								PlayerRotation.y += 0.60f;
								BbRotation.y += 0.60f;
							}
						} else if ((input->PushKey(DIK_RIGHT) || input->PushKey(DIK_D))) {
							if (LaneNum == 0) {
								PlayerSpeed += 1.25;
								PlayerRotation.y -= 1.25f;
								BbRotation.y -= 1.25f;
							} else if (LaneNum == 1) {
								PlayerSpeed += 1.0f;
								PlayerRotation.y -= 1.0f;
								BbRotation.y -= 1.0f;
							} else {
								PlayerSpeed += 0.60f;
								PlayerRotation.y -= 0.60f;
								BbRotation.y -= 0.60f;
							}
							/*if (PlayerRotation.y >= 360 || PlayerRotation.y <= -360) {
								PlayerRotation.y = 0;
							}*/
						}
					}

				}
			}
#pragma region//中心からの距離

#pragma endregion
#pragma endregion
#pragma region//レーン移動
			if (PlayerSpeed >= 360 || PlayerSpeed <= -360) {
				PlayerSpeed = 0;
			}
			//レーン移動の速度の計算
			if (MoveNumber == 1) {
				Playerscale = initScale - 25.0f * easeInSine(frame / frameMax);
				if (frame != frameMax) {
					frame = frame + 1;
				} else {
					MoveNumber = 0;
				}
			}
			if (MoveNumber == 2) {
				Playerscale = initScale + 25.0f * easeOutBack(frame / frameMax);
				if (frame != frameMax) {
					frame = frame + 1;
				} else {
					MoveNumber = 0;
				}
			}

			//プレイヤーのいるエリアを決める
			if (enemy->GetAwakeingNumber() == 0) {
				if ((PlayerSpeed >= 0 && PlayerSpeed <= 90) || (PlayerSpeed <= -270 && PlayerSpeed >= -360)) {
					PlayerAreaNumber = 1;
				}

				else if ((PlayerSpeed >= 91 && PlayerSpeed <= 180) || (PlayerSpeed <= -180 && PlayerSpeed >= -269)) {
					PlayerAreaNumber = 2;
				}

				else if ((PlayerSpeed >= 181 && PlayerSpeed <= 270) || (PlayerSpeed <= -90 && PlayerSpeed >= -179)) {
					PlayerAreaNumber = 3;
				}

				else if ((PlayerSpeed >= 271 && PlayerSpeed <= 359) || (PlayerSpeed <= -1 && PlayerSpeed >= -89)) {
					PlayerAreaNumber = 4;
				}
			}

			else {
				if ((PlayerSpeed >= 0 && PlayerSpeed <= 180) || (PlayerSpeed <= -180 && PlayerSpeed >= -360)) {
					PlayerAreaNumber = 1;
				}

				else if ((PlayerSpeed >= 181 && PlayerSpeed <= 360) || (PlayerSpeed <= -1 && PlayerSpeed >= -179)) {
					PlayerAreaNumber = 2;
				}

			}
#pragma endregion
#pragma region//カメラ処理
			//プレイヤーから中心へのベクトル
			playertarget = XMFLOAT3(BasePosition.x - PlayerPosition.x, BasePosition.y - PlayerPosition.y, BasePosition.y - PlayerPosition.z);		
			length = sqrtf(powf(playertarget.x, 2.0f) + powf(playertarget.y, 2.0f) + powf(playertarget.z, 2.0f));
			enemytarget = XMFLOAT3(playertarget.x / length, playertarget.y / length, playertarget.z / length);
			eye2.m128_f32[0] = PlayerPosition.x - enemytarget.x * 15;
			eye2.m128_f32[1] = 5.0f;
			eye2.m128_f32[2] = PlayerPosition.z - enemytarget.z * 15;
			//eye2 = target2 + v;
			target2.m128_f32[0] = BasePosition.x + enemytarget.x;
			target2.m128_f32[1] = BasePosition.y + enemytarget.y + 8.0f;
			target2.m128_f32[2] = BasePosition.z + enemytarget.z;
		}
#pragma endregion
#pragma region//タイトル
		if (Scene == title) {
			zone->Update(matView, 1, player, playerBullet[0], audio);
			if (SceneChange == 0) {
				if (input->TriggerKey(DIK_SPACE) && MoveNumber == 0) {
					if (zone->GetStartFlag() == 0) {
						audio->PlayWave("Resources/Sound/Makibisi_Roll.wav", 0.7f);
						playerBullet[0]->Shot(PlayerPosition, LaneNum, PlayerSpeed);//Laneナンバーで定義
					} else {
						audio->StopWave(0);
						audio->PlayWave("Resources/Sound/Decision.wav", 0.7f);
						audio->LoopWave2(1, 0.5f);
						Scene = appearance;
						PlayerPosition = { 25,0,0 };
						PlayerRotation = { 0,-90,0 };
						LaneNum = 0;
						PlayerSpeed = 0.0f;
						Playerscale = 25.0f;
						for (int i = 0; i < BulletMax; i++) {
							if (playerBullet[i]->GetIsAlive() == 1) {
								playerBullet[i]->SetIsAlive(0);
							}
						}
					}
				}
			}
		}
#pragma endregion
#pragma region//最初の演出
		if (Scene == appearance) {
			player->SetHp(5);
			EnemyRotation.y = 90;
			Camerascale = 40.0f;
			if (CameraSetNumber == 0) {
				EnemyPosition.y = 100;
				if (AppearanceCameraTargetY <= 30.0f) {
					AppearanceCameraTargetY += 0.075f;
				}

				else {
					CameraSetNumber = 1;
				}
			}
#pragma endregion
#pragma region//ドラゴンの動きに合わせる
			else if (CameraSetNumber == 1) {
				//ドラゴンが降りてくる
				if (EnemyPosition.y >= 2.0f) {
					//カメラがついてくる
					if (EnemyPosition.y <= AppearanceCameraTargetY) {
						AppearanceCameraTargetY -= 0.2f;
					}
					EnemyPosition.y += SlopeG;
					SlopeG -= 0.02f;
					if (SlopeG <= -1.0) {
						SlopeG = 0.2f;
						audio->PlayWave("Resources/Sound/Dragon_Fly.wav", 0.3f);
					}
				}

				else {
					if (EnemyRotation.x >= -5.0f) {
						EnemyRotation.x -= 0.1f;
					} else {
						audio->PlayWave("Resources/Sound/DragonVoice_2.wav", 0.3f);
						CameraSetNumber = 2;
					}
				}
			}

			else {
				if (Camerascale >= 5) {
					Camerascale--;
				}
				/*	if (CameraPosition.y <= 13) {
						CameraPosition.y += 0.2f;
					}*/
				if (SceneChange == 0) {
					if (EnemyRotation.x <= 15.0f) {
						EnemyRotation.x += 0.2f;
					} else {
						SceneChange = 1;
					}
				}
			}
			if (SceneChange == 2) {
				EnemyPosition.y = 0.0f;
				EnemyRotation.x = 0.0f;
				Playerscale = 75.0f;
				LaneNum = 2;
				Scene = gamePlay;
				audio->StopWave(1);
				audio->LoopWave2(2, 0.3f);
			}
			if (input->TriggerKey(DIK_S)) {
				EnemyPosition.y = 0.0f;
				EnemyRotation.x = 0.0f;
				Playerscale = 75.0f;
				LaneNum = 2;
				Scene = gamePlay;
				audio->StopWave(1);
				audio->LoopWave2(2, 0.3f);
			}
			target2.m128_f32[0] = BasePosition.x;
			target2.m128_f32[1] = AppearanceCameraTargetY;
			target2.m128_f32[2] = BasePosition.z;
			eye2.m128_f32[0] = CameraPosition.x;
			eye2.m128_f32[1] = CameraPosition.y;
			eye2.m128_f32[2] = CameraPosition.z;
		}
#pragma endregion
#pragma region//プレイ中
#pragma region//ゲームオーバーやUI
		if (Scene == gamePlay) {
			//マキビシの設置
			if (OverFlag != 1) {
				//ゲームオーバー処理
				if (player->GetHp() <= 0) {
					audio->PlayWave("Resources/Sound/GameOver.wav", 0.4f);
					audio->StopWave(2);
					OverFlag = 1;
				}
				if (SceneChange == 0) {
					if (input->TriggerKey(DIK_SPACE) && MoveNumber == 0) {
						for (int i = 0; i < BulletMax; i++) {
							if (playerBullet[i]->GetIsAlive() == 0) {
								audio->PlayWave("Resources/Sound/Makibisi_Roll.wav", 0.7f);
								playerBullet[i]->Shot(PlayerPosition, LaneNum, PlayerSpeed);//Laneナンバーで定義
								BulletLane[i] = LaneNum;
								break;
							}
						}
					}
				}
			} else {
				OverCount++;
			}
#pragma endregion
#pragma region//敵のAI
			//敵覚醒
			//敵のHPによって攻撃パターン
			if (enemy->GetHp() >= 17) {
				EnemyMoveTimer = 40;
			}

			else if (enemy->GetHp() >= 12 && enemy->GetHp() <= 16) {
				EnemyMoveTimer = 90;
			}

			else if (enemy->GetHp() >= 7 && enemy->GetHp() <= 11) {
				EnemyMoveTimer = 85;
			}

			else if (enemy->GetHp() <= 6) {
				EnemyMoveTimer = 90;
			}

			//攻撃パターン決定
			if (AttackFlag == 0 && MoveNumber == 0) {
				AttackTimer++;
			}
			//攻撃準備
			if (AttackTimer == EnemyMoveTimer) {
				SlopeFlag = 1;
				SlopeG = 1.0f;
				//敵の攻撃AI
				//敵のHPMAX
				if (enemy->GetHp() >= 17) {
					EnemyAINumber = 1;
				}
				//敵のHP3/4
				else if (enemy->GetHp() >= 12 && enemy->GetHp() <= 16) {

					EnemyAIRand = rand() % 100;
					if (EnemyAIRand <= 30) {
						EnemyAINumber = 1;
					}
					else if ((EnemyAIRand > 30) && (EnemyAIRand <= 65)) {
						EnemyAINumber = 3;
					}
					else {
						EnemyAINumber = 2;
					} 

				}
				//敵のHP2/4
				else if (enemy->GetHp() >= 7 && enemy->GetHp() <= 11 ) {
					EnemyAIRand = rand() % 100;
					if (EnemyAIRand <= 35) {
						EnemyAINumber = 1;
					} else if ((EnemyAIRand > 35) && (EnemyAIRand <= 70)) {
						EnemyAINumber = 3;
					} else {
						EnemyAINumber = 2;
					}
				}
				//敵のHP1/4
				else {
					EnemyAIRand = rand() % 100;
					if (EnemyAIRand <= 45) {
						EnemyAINumber = 1;
					} else if ((EnemyAIRand > 45) && (EnemyAIRand <= 60)) {
						EnemyAINumber = 3;
					} else {
						EnemyAINumber = 2;
					}
				}

				if (LaneNum == 0&&RemoveParticleStartflag==0) {
					EnemyAINumber = 4;
				}

				if ((DistanceCount != 0) && DistanceCount != 3) {
					EnemyAINumber = 4;
				}
			}
#pragma endregion
#pragma region//突進攻撃
			if (EnemyAINumber == 1) {
				//ロックオン
				if (SlopeFlag == 1 && SlopeCount != 6) {
					EnemyPosition.y += SlopeG;
					SlopeG -= 0.02f;
					if (SlopeG <= 0.1) {
						SlopeG = 1.0f;
						SlopeCount++;
						audio->PlayWave("Resources/Sound/Dragon_Fly.wav", 0.3f);
					}

					if (SlopeCount == 5) {
						EnemyPosition.y = 100;
						EnemyPosition.x = -5 * PlayerPosition.x;
						EnemyPosition.z = -5 * PlayerPosition.z;
						AttackPosition.x = cosf(Playerradius) * (Playerscale + ((3 - LaneNum) * 40));
						AttackPosition.z = sinf(Playerradius) * (Playerscale + ((3 - LaneNum) * 40));
						EnemyRotation.y = PlayerRotation.y + 180;
						EnemyRotation.x = 150.0f;
						AttackFlag = 1;
						SlopeCount = 0;
						SlopeFlag = 0;
						AttackTimer = 0;
					}
				}

				//攻撃前の傾き	
				if (AttackFlag == 1 && EnemyPosition.y >= 3.0f && AttackNumber == 0) {
					EnemyPosition.y -= 1.0f;
					if (EnemyPosition.y <= 53 && EnemyRotation.x >= 70.0f) {
						EnemyRotation.x -= 2.0f;
					}
				}

				//攻撃
				if (AttackFlag == 1 && AttackNumber == 0 && EnemyRotation.x <= 90) {
					AttackCount++;
					AttackSpeed += 0.2f;
					angleX = (AttackPosition.x - EnemyPosition.x);
					angleZ = (AttackPosition.z - EnemyPosition.z);
					angleR = sqrt((AttackPosition.x - EnemyPosition.x) * (AttackPosition.x - EnemyPosition.x)
						+ (AttackPosition.z - EnemyPosition.z) * (AttackPosition.z - EnemyPosition.z));
					if (AttackCount == 50) {
						audio->PlayWave("Resources/Sound/Dragon_Attack.wav", 0.3f);
					}
					if (AttackSpeed <= 20.0f) {
						EnemyPosition.x += (float)(angleX / angleR) * AttackSpeed;
						EnemyPosition.z += (float)(angleZ / angleR) * AttackSpeed;
					} else {
						AttackNumber = 1;
					}
				}

				if (AttackNumber == 1) {
					if (AttackSpeed >= 0.5f) {
						AttackSpeed -= 0.01f;
					}
					/*	EnemyPosition.x += AttackSpeed;
						EnemyPosition.z += AttackSpeed;*/
					EnemyPosition.y += SlopeG;
					SlopeG -= 0.02f;
					if (SlopeG <= 0.1) {
						SlopeG = 1.0f;
						SlopeCount++;
						audio->PlayWave("Resources/Sound/Dragon_Fly.wav", 0.3f);
					}

					if (SlopeCount == 2) {
						AttackNumber = 2;
					}
				}
			}
#pragma endregion
#pragma region//爆弾除去攻撃
			if (EnemyAINumber == 2) {
				//ロックオン
				if (SlopeFlag == 1 && SlopeCount != 3) {
					EnemyRotation.y = PlayerRotation.y + 180;
					if (SlopeNumber == 0) {
						EnemyRotation.z += 1.0f;
					}

					else if (SlopeNumber == 1) {
						EnemyRotation.z -= 1.0f;
					}

					if (EnemyRotation.z <= -20) {
						SlopeNumber = 0;
						SlopeCount++;
					}

					else if (EnemyRotation.z >= 20) {
						SlopeNumber = 1;
						SlopeCount++;
					}

					EnemyPosition.y += SlopeG;
					SlopeG -= 0.2f;
					if (EnemyPosition.y <= 0.0) {
						SlopeG = 2.0f;
					}

					if (SlopeCount == 0) {
						AttackPosition.x = PlayerPosition.x;
						AttackPosition.z = PlayerPosition.z;
						for (int i = 0; i < BulletMax; i++) {
							if (playerBullet[i]->GetIsAlive() == 1) {
								if (BulletLane[i] == 0) {
									InsideCount++;
								}

								else if (BulletLane[i] == 1) {
									CenterCount++;
								}

								else {
									OutsideCount++;
								}

								//除去するレーンを決める
								if (bulletNum != 10) {
									if (InsideCount > 0) {
										RemoveLane = 0;
									}

									else if (InsideCount == 0 && 0 < CenterCount) {
										RemoveLane = 1;
									}

									else {
										RemoveLane = 2;
									}
								} else {
									RemoveLane = LaneNum;
								}
								if ((BulletLane[i] == RemoveLane) && (playerBullet[i]->GetAreaNumber() == 1)) {
									BulletAreaCount[0]++;
								}

								if ((BulletLane[i] == RemoveLane) && (playerBullet[i]->GetAreaNumber() == 2)) {
									BulletAreaCount[1]++;
								}

								if ((BulletLane[i] == RemoveLane) && (playerBullet[i]->GetAreaNumber() == 3)) {
									BulletAreaCount[2]++;
								}

								if ((BulletLane[i] == RemoveLane) && (playerBullet[i]->GetAreaNumber() == 4)) {
									BulletAreaCount[3]++;
								}
							}
							//マキビシが置かれていなかった場合のエリアぎめ(プレイヤー依存)
							if ((bulletNum != 10) && (enemy->GetAwakeingNumber() == 0)) {
								if ((BulletAreaCount[0] >= BulletAreaCount[1]) && (BulletAreaCount[0] >= BulletAreaCount[2]) && (BulletAreaCount[0] >= BulletAreaCount[3])) {
									RemoveAreaNumber = 1;
								}

								else if ((BulletAreaCount[1] >= BulletAreaCount[0]) && (BulletAreaCount[1] >= BulletAreaCount[2]) && (BulletAreaCount[1] >= BulletAreaCount[3])) {
									RemoveAreaNumber = 2;
								}

								else if ((BulletAreaCount[2] >= BulletAreaCount[0]) && (BulletAreaCount[2] >= BulletAreaCount[1]) && (BulletAreaCount[2] >= BulletAreaCount[3])) {
									RemoveAreaNumber = 3;
								}

								else if ((BulletAreaCount[3] >= BulletAreaCount[0]) && (BulletAreaCount[3] >= BulletAreaCount[1]) && (BulletAreaCount[3] >= BulletAreaCount[2])) {
									RemoveAreaNumber = 4;
								}
							} else {
								RemoveAreaNumber = PlayerAreaNumber;
								RemoveLane = LaneNum;
							}
						}
						for (int i = 0; i < _countof(particle); i++) {
							ParticleFlag[i] = 0;
							Particleradius[i] = ParticleSpeed[i] * PI / 180.0f;
							ParticleCircleX[i] = cosf(Particleradius[i]) * Particlescale[i];
							ParticleCircleZ[i] = sinf(Particleradius[i]) * Particlescale[i];
						}
						RemoveParticleStartflag = 1;
					}
				}
				else if (SlopeCount == 3) {
					AttackFlag = 1;
					EnemyRotation.z = 0;
					EnemyPosition.y = 0.0f;
				}
#pragma region//爆弾除去
				if (RemoveParticleStartflag == 1) {
					for (int i = 0; i < _countof(particle); i++) {
						Particleradius[i] = ParticleSpeed[i] * PI / 180.0f;
						ParticleCircleX[i] = cosf(Particleradius[i]) * Particlescale[i];
						ParticleCircleZ[i] = sinf(Particleradius[i]) * Particlescale[i];
						if (ParticleFlag[i] == 0) {
							ParticleScaleRand[i] = (float)(rand() % 3 + 1) / 10.0f;
							if (RemoveLane == 0) {
								particle[i].scale = { 0.1f,0.1f,0.1f };
							} 					else if (RemoveLane == 1) {
								particle[i].scale = { 0.3f,0.3f,0.3f };
							} 					else if (RemoveLane == 2) {
								particle[i].scale = { 0.5f,0.5f,0.5f };
							}
							if (RemoveLane == 0) {
								Particlescale[i] = (float)(rand() % 25);
							}

							else if (RemoveLane == 1) {
								Particlescale[i] = (float)(rand() % 50);
							}

							else if (RemoveLane == 2) {
								Particlescale[i] = (float)(rand() % 75);
							}
							if (enemy->GetAwakeingNumber() == 0) {
								if (RemoveAreaNumber == 1) {
									ParticleSpeed[i] = (float)(rand() % 90);
								} if (RemoveAreaNumber == 2) {
									ParticleSpeed[i] = (float)(rand() % 90 + 90);
								} if (RemoveAreaNumber == 3) {
									ParticleSpeed[i] = (float)(rand() % 90 + 180);
								} if (RemoveAreaNumber == 4) {
									ParticleSpeed[i] = (float)(rand() % 90 + 270);
								}
							} else {
								if (RemoveAreaNumber == 1) {
									ParticleSpeed[i] = (float)(rand() % 180);
								} else if (RemoveAreaNumber == 2) {
									ParticleSpeed[i] = (float)(rand() % 180 + 180);
								}
							}

						}
						ParticleFlag[i] = 1;

						if (ParticleFlag[i] == 1) {
							particle[i].position.y = 0.0f;
							particle[i].position.x = ParticleCircleX[i] + BasePosition.x;
							particle[i].position.z = ParticleCircleZ[i] + BasePosition.z;
							particle[i].rotation.y += 3.0f;
							particle[i].rotation.z += 3.0f;
						}
					}
				}
#pragma endregion
				//攻撃前の傾き
				if (AttackFlag == 1 && EnemyRotation.x >= -30.0f) {
					EnemyRotation.x -= 1.0;
				}

				if (EnemyRotation.x <= -30) {
					for (int i = 0; i < BulletMax; i++) {
						if (enemy->GetAwakeingNumber() == 0) {
							if ((RemoveLane >= BulletLane[i]) && (RemoveAreaNumber == playerBullet[i]->GetAreaNumber())) {
								playerBullet[i]->SetIsAlive(0);
							}
						}
						else {
							if (RemoveLane >= BulletLane[i]) {
								if ((RemoveAreaNumber == 1) && (playerBullet[i]->GetAreaNumber() == 1 || playerBullet[i]->GetAreaNumber() == 2)) {
									playerBullet[i]->SetIsAlive(0);
								}

								else if ((RemoveAreaNumber == 2) && (playerBullet[i]->GetAreaNumber() == 3 || playerBullet[i]->GetAreaNumber() == 4)) {
									playerBullet[i]->SetIsAlive(0);
								}
							}
						}
					}
					
					if ((RemoveLane >= LaneNum) && (RemoveAreaNumber == PlayerAreaNumber)) {
						player->SetHp(player->GetHp() - 1);
						player->SetHitFlag(1);
						audio->PlayWave("Resources/Sound/Player_Damage01.wav", 0.7f);
					}
					RemoveParticleStartflag = 2;
					AttackNumber = 2;
				}
			}
			//パーティクル飛ぶ
			if (RemoveParticleStartflag == 2) {
				for (int i = 0; i < _countof(particle); i++) {
					particle[i].position.y += ParticleScaleRand[i] * 5;
					if (particle[i].position.y >= 10) {
						ParticleFlag[i] = 0;
						ParticleSpeed[i] = 0.0f;
						Particlescale[i] = 0.0f;
						Particleradius[i] = ParticleSpeed[i] * PI / 180.0f;
						ParticleCircleX[i] = cosf(Particleradius[i]) * Particlescale[i];
						ParticleCircleZ[i] = sinf(Particleradius[i]) * Particlescale[i];

					}
				}
			}

#pragma endregion
#pragma region//火炎放射攻撃
			if (EnemyAINumber == 3) {
				//敵が後ろに反りきるまでにプレイヤーの位置に炎を打つ準備をします
				EnemyRotation.y = PlayerRotation.y + 180;
				if (FlameSetNumber == 0) {
					if (EnemyRotation.x >= -20.0f) {
						EnemyRotation.x -= 2.0f;
					}
					else if (EnemyRotation.x <= -10.0f) {
						FlameSetNumber = 1;
					}
				}
				//敵が前に倒れて炎を出します
					if (FlameSetNumber == 1) {
						if (EnemyRotation.x <= 10.0f) {
							EnemyRotation.x += 2.0f;
						}
						else if (EnemyRotation.x >= 5.0f) {
							for (int i = 0; i < EnemyBulletMax;i++) {
								if (enemyBullet[i]->GetIsAlive() == 0&& FlameTimer%20==0) {
									enemyBullet[i]->Shot(PlayerSpeed, LaneNum);
									audio->PlayWave("Resources/Sound/Fire.wav",0.2f);
									break;
								}
							}
							FlameTimer++;
							if (enemy->GetAwakeingNumber()==0) {
								if (FlameTimer >= 20) {
									FlameTimer = 0;
									AttackNumber = 2;
								}
							} else {
								if (FlameTimer >= 20 * 5) {
									FlameTimer = 0;
									AttackNumber = 2;
								}
							}
						}
					}
			}
#pragma endregion
#pragma region//近距離攻撃
			if (LaneNum != 0) {
				DistanceCount = 0;
			}
			if (EnemyAINumber == 4) {
				//2回目まで突進
				if (DistanceCount != 2) {
					if (AttackSpeed >= -0.45f && AttackNumber == 0) {
						EnemyRotation.y = PlayerRotation.y + 180;
						AttackPosition.x = PlayerPosition.x;
						AttackPosition.z = PlayerPosition.z;
						AttackSpeed -= 0.05f;
					}

					else if (AttackSpeed <= -0.1f) {
						AttackNumber = 1;
					}

					if (ReturnFlag == 0) {
						if ((AttackNumber == 1 && AttackSpeed <= 1.5f)) {
							AttackSpeed += 0.05f;
						}

						if (AttackSpeed >= 1.5f) {
							DistanceCount++;
							AttackSpeed = 0.0f;
							EnemyPosition.x = 0.0f;
							EnemyPosition.z = 0.0f;
							AttackNumber = 2;
						}
					}
					angleX = (AttackPosition.x - EnemyPosition.x);
					angleZ = (AttackPosition.z - EnemyPosition.z);
					angleR = sqrt((AttackPosition.x - EnemyPosition.x) * (AttackPosition.x - EnemyPosition.x)
						+ (AttackPosition.z - EnemyPosition.z) * (AttackPosition.z - EnemyPosition.z));
					EnemyPosition.x += (float)(angleX / angleR) * AttackSpeed;
					EnemyPosition.z += (float)(angleZ / angleR) * AttackSpeed;
				}

				else if (DistanceCount == 2) {
					MaxRotationCount += 6;
					if (MaxRotationCount <= 359) {
						EnemyRotation.y += 6;
					}

					//ダメージを食らったかどうか
					if ((EnemyRotation.y >= PlayerRotation.y + 357) && (EnemyRotation.y <= PlayerRotation.y + 363) && (LaneNum == 0)) {
						player->SetHp(player->GetHp() - 1);
						audio->PlayWave("Resources/Sound/Player_Damage01.wav", 0.7f);
					}

					if (MaxRotationCount >= 360) {
						DistanceCount++;
						//EnemyRotation.y = 0.0f;
						AttackNumber = 2;
					}
				}
			}
#pragma endregion
#pragma region//初期の位置に降り立つ
			if (AttackNumber == 2) {
				//諸々の初期化じゃね
				if (EnemyPosition.y != 0.0f) {
					EnemyPosition.y += SlopeG;
					SlopeG -= 0.02f;
					if (SlopeG <= -1.0) {
						SlopeG = 0.2f;
						audio->PlayWave("Resources/Sound/Dragon_Fly.wav", 0.3f);
					}
				}
				EnemyPosition.x = 0.0f;
				EnemyPosition.z = 0.0f;
				EnemyRotation.x = 0.0f;
				EnemyAINumber = 0;
				EnemyAIRand = 0;
				MaxRotationCount = 0;
				ReturnFlag = 0;
				SlopeCount = 0;
				SlopeFlag = 0;
				if (DistanceCount == 0 || DistanceCount == 3) {
					AttackTimer = 0;
					DistanceCount = 0;
				} else {
					AttackTimer = EnemyMoveTimer - 50;
				}
				AttackTimer = 0;
				RemoveAreaNumber = 0;
				RemoveLane = 3;
				InsideCount = 0;
				CenterCount = 0;
				OutsideCount = 0;
				FlameSetNumber = 0;

				if (EnemyPosition.y <= 0.0f) {
					AttackNumber = 0;
					AttackFlag = 0;
					AttackSpeed = 0.0f;
					AttackCount = 0;
					EnemyPosition.y = 0.0f;
					for (int i = 0; i < BulletAreaMax; i++) {
						BulletAreaCount[i] = 0;
					}
				}
			}
			if (enemy->GetHp() <= 0) {
				Scene = gameClear;
				audio->StopWave(2);
			}

			if (enemy->GetAwakeingNumber() == 1 && AwakeningSceneNumber == 0) {
				Scene = awakening;
			}
		}
#pragma endregion
#pragma region//パーティクル発生
#pragma endregion
#pragma endregion
#pragma region//覚醒中
		if (Scene == awakening) {
			if (AwakeningSceneNumber == 0) {
				enemy->SetHp(6);
				EnemyPosition.y = 0.0f;
				EnemyPosition.x = 0.0f;
				EnemyPosition.z = 0.0f;
				AttackSpeed = 0.0f;
				PlayerRotation.y = -90.0f;
				EnemyRotation.x = 0.0f;
				EnemyRotation.y = 90.0f;
				Playerscale = 75.0f;
				PlayerSpeed = 0.0f;
				bulletNum = 10;
				for (int i = 0; i < BulletMax; i++) {
					playerBullet[i]->SetIsAlive(0);
				}
				LaneNum = 2;
				Camerascale = 40;
				CameraSpeed = 0;
				AwakeningSceneTimer++;
				if (AwakeningSceneTimer >= 100) {
					AwakeningSceneNumber = 1;
				}
			} else if (AwakeningSceneNumber == 1) {
				if (EnemyRotation.x >= -20.0f) {
					EnemyRotation.x -= 0.5f;
				} else {
					AwakeningSceneNumber = 2;
				}
			} else {
				if (SceneChange == 0) {
					if (EnemyRotation.x <= 10.0f) {
						EnemyRotation.x += 1.0f;
					} else {
						SceneChange = 1;
					}
				}
				if (SceneChange == 2) {
					Scene = gamePlay;
					EnemyRotation.x = 0.0;
					AttackNumber = 2;
				}
			}

			eye2.m128_f32[0] = CameraPosition.x;
			eye2.m128_f32[1] = CameraPosition.y;
			eye2.m128_f32[2] = CameraPosition.z;
			target2.m128_f32[0] = 0;
			target2.m128_f32[1] = 10;
			target2.m128_f32[2] = 0;
		}
#pragma endregion
#pragma region//ゲームクリア
		if (Scene == gameClear) {
			PlayerPosition.x = 0.0f;
			PlayerPosition.z = 0.0f;
			PlayerRotation.y = -90.0f;
			EnemyRotation.y = 90.0f;
			ClearCameraTimer++;
			if (ClearCameraTimer < 100) {
				EnemyPosition.x = 50.0f;
				EnemyPosition.z = 0.0f;
				CameraPosition.y = 12.0f;
				AppearanceCameraTargetY = 15;
				CameraSpeed = 20;
				EnemyRotation.x = 0.0f;
				Camerascale = 80;
				EnemyPosition.y = 0.0f;
				SlopeG = 0.4f;
				target2.m128_f32[0] = EnemyPosition.x;
			}
			//カメラを三回変える
			else if ((ClearCameraTimer >= 100) && (ClearCameraTimer <= 200)) {
				CameraSpeed = 340;
				target2.m128_f32[0] = EnemyPosition.x;
			}

			else if ((ClearCameraTimer > 200) && (ClearCameraTimer <= 250)) {
				EnemyPosition.x = 25.0f;
				Camerascale = 10.0f;
				CameraSpeed = 180;
				CameraPosition.y = 2;
				target2.m128_f32[0] = PlayerPosition.x;
				AppearanceCameraTargetY = 5;
			} else {
				if (EnemyPosition.y <= 6.0f) {
					EnemyPosition.y += 0.05f;
				}

				if (EnemyRotation.z >= -90) {
					EnemyRotation.z -= 0.5f;
				} else {
					if (SlopeNumber == 0) {
						PlayerRotation.z += 0.5f;
					}

					else if (SlopeNumber == 1) {
						PlayerRotation.z -= 0.5f;
					}

					if (PlayerRotation.z <= -20) {
						SlopeNumber = 0;
						SlopeCount++;
					}

					else if (PlayerRotation.z >= 20) {
						SlopeNumber = 1;
						SlopeCount++;
					}

					PlayerPosition.y += SlopeG;
					SlopeG -= 0.05f;
					if (PlayerPosition.y <= 0.0) {
						SlopeG = 0.4f;
					}
				}
			}
			//サウンドの設定
			if ((ClearCameraTimer == 10) || (ClearCameraTimer == 110) || (ClearCameraTimer == 210)) {
				audio->PlayWave("Resources/Sound/knockDown.wav", 0.3f);
			}
			if (ClearCameraTimer == 500) {
				audio->PlayWave("Resources/Sound/Clear.wav", 0.3f);
				OverCount = 61;
			}
			//カメラのセット
			eye2.m128_f32[0] = CameraPosition.x;
			eye2.m128_f32[1] = CameraPosition.y;
			eye2.m128_f32[2] = CameraPosition.z;
			target2.m128_f32[1] = AppearanceCameraTargetY;
		}
#pragma endregion
#pragma region//更新処理
		//多分初期処理
		if (input->TriggerKey(DIK_SPACE) && OverCount > 60) {
			audio->PlayWave("Resources/Sound/Decision.wav", 0.7f);
			//これはプレイヤー
			PlayerSpeed = 0.0f;
			Playerscale = 75.0f;
			player->Player::Create();
			player->Update(matView);
			PlayerRotation = {0,-90,0};
			PlayerPosition = { 0,0,0 };
			Playerradius = PlayerSpeed * PI / 180.0f;
			PlayerCircleX = cosf(Playerradius) * Playerscale;
			PlayerCircleZ = sinf(Playerradius) * Playerscale;
			PlayerPosition.x = PlayerCircleX + BasePosition.x;
			PlayerPosition.z = PlayerCircleZ + BasePosition.z;
			PlayerAreaNumber = 0;
			arrowFlag = 1;
			//これがエネミー
			enemy = Enemy::Create();
			EnemyRotation = { 0,90,0 };
			EnemyPosition = enemy->GetPosition();
			//敵の行動決めろ
			EnemyAIRand = 0;
			EnemyAINumber = 0;
			MaxRotationCount = 0;
			EnemyMoveTimer = 0;
			//パーティクル関連
			RemoveParticleStartflag = 0;
			for (int i = 0; i < Particle_Max;i++) {
				Particleradius[i] = { 0.0f };
				ParticleSpeed[i] = { 0.0f };
				Particlescale[i] = { 0.0f };
				ParticleCircleX[i] = { 0.0f };
				ParticleCircleZ[i] = { 0.0f };
				ParticleScaleRand[i] = { 0.0f };
				ParticleFlag[i] = { 0 };
			}
			AttackNumber = 2;
			//覚醒シーン変数//
			AwakeningSceneNumber = 0;
			AwakeningSceneTimer = 0;
			//プレイヤー弾
			for (int i = 0; i < BulletMax; i++) {
				playerBullet[i] = PlayerBullet::Create();
			}
			//エネミー弾
			for (int i = 0; i < EnemyBulletMax; i++) {
				enemyBullet[i] = EnemyBullet::Create();
			}
			//Lane関連
			RemoveLane = 0;
			LaneNum = 2;
			frame = 0;
			InsideCount = 0;
			CenterCount = 0;
			OutsideCount = 0;
			MoveNumber = 0;
			//カメラ
			CameraSetNumber = 0;
			Cameraangle = 0.0f;
			Cameraradius = 0.0f;
			CameraSpeed = 0.0f;
			Camerascale = 0.0f;
			CameraCircleX = 0.0f;
			CameraCircleZ = 0.0f;
			CameraPosition = { 0.0f, 5.0f, 0.0f };
			Cameraradius = PlayerSpeed * PI / 180.0f;
			AppearanceCameraTargetY = 0.0f;
			playertarget = XMFLOAT3(BasePosition.x - PlayerPosition.x, BasePosition.y - PlayerPosition.y, BasePosition.y - PlayerPosition.z);
			length = sqrtf(powf(playertarget.x, 2.0f) + powf(playertarget.y, 2.0f) + powf(playertarget.z, 2.0f));
			enemytarget = XMFLOAT3(playertarget.x / length, playertarget.y / length, playertarget.z / length);
			eye2.m128_f32[0] = PlayerPosition.x - enemytarget.x * 15;
			eye2.m128_f32[1] = 5.0f;
			eye2.m128_f32[2] = PlayerPosition.z - enemytarget.z * 15;
			//eye2 = target2 + v;
			target2.m128_f32[0] = BasePosition.x + enemytarget.x;
			target2.m128_f32[1] = BasePosition.y + enemytarget.y + 8.0f;
			target2.m128_f32[2] = BasePosition.z + enemytarget.z;
			//バレット
			for (int i = 0; i < BulletMax; i++) {
				playerBullet[i]->Create();
			}
			//ここに初期化処理入れないと進めない
			if (OverFlag == 1) {
				audio->StopWave(0);
				audio->StopWave(2);
				audio->LoopWave2(1, 0.5f);
				Scene = appearance;
			}             else {
				audio->LoopWave2(0, 0.5f);
				Scene = title;
				ClearCameraTimer = 0;
			}
			RemoveParticleStartflag = 0;
			OverFlag = 0;
			OverCount = 0;
			SceneChange = 0;
			//3Dオブジェクトなどのアップデート
			matView = XMMatrixLookAtLH((eye2), (target2), (up2));
			zone = Zone::Create();
			zone->Update(matView, 1, player, playerBullet[0], audio);
			bb = BlackBoard::Create();
			BbRotation = { 0,-90,0 };
			bb->Update(matView);
			UpdateObject3d(&backScreen, matView, matProjection, color);
		}

		player->SetPosition(PlayerPosition);
		enemy->SetPosition(EnemyPosition);
		player->SetRotaition(PlayerRotation);
		enemy->SetRotaition(EnemyRotation);
		bb->SetRotation(BbRotation);
		//カメラ追従
		rotM = XMMatrixRotationY(XMConvertToRadians(angle));
		XMVECTOR v;
		v = XMVector3TransformNormal(v0, rotM);

		matView = XMMatrixLookAtLH((eye2), (target2), (up2));

		UpdateObject3d(&backScreen, matView, matProjection, color);
		for (int i = 0; i < _countof(particle); i++) {
			UpdateObject3d(&particle[i], matView, matProjection, ParticleColor);
		}
		player->Update(matView);
		enemy->Update(matView,player);
		for (int i = 0; i < BulletMax; i++) {
			playerBullet[i]->Update(matView, enemy->GetAwakeingNumber());
		}
		if (EnemyAINumber == 1) {
			for (int i = 0; i < BulletMax; i++) {
				enemy->collide(playerBullet[i], bulletNum);
			}
		}
		for (int i = 0; i < BulletMax; i++) {
			playerBullet[i]->collide(player, bulletNum);
		}

		for (int i = 0; i < EnemyBulletMax; i++) {
			enemyBullet[i]->Update(matView);
		}
		for (int i = 0; i < EnemyBulletMax; i++) {
			enemyBullet[i]->collide(player);
		}
		bb->Update(matView);
#pragma endregion
#pragma endregion
#pragma region //ゲーム外処理
		//メッセージがある?
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);//キー入力メッセージの処理
			DispatchMessage(&msg);//プロシージャにメッセージを送る
		}
		//xボタンで終了メッセージが来たらゲームループを抜ける
		if (msg.message == WM_QUIT) {
			break;
		}
		dxCommon->PreDraw();
		////4.描画コマンドここから
		dxCommon->GetCmdList()->SetPipelineState(object3dPipelineSet.pipelinestate.Get());
		dxCommon->GetCmdList()->SetGraphicsRootSignature(object3dPipelineSet.rootsignature.Get());
		dxCommon->GetCmdList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		//4.描画コマンドここまで
#pragma endregion
#pragma region//描画
		//背景スプライト描画前処理
		Sprite::PreDraw(dxCommon->GetCmdList());
		if (Scene == title) {
			sprite[0]->Draw();
		} else {
			sprite[26]->Draw();
		}
		dxCommon->ClearDepthBuffer();
		BlackBoard::PreDraw(dxCommon->GetCmdList());
		// 3Dオブジェクト描画前処理
		PlayerBullet::PreDraw(dxCommon->GetCmdList());
		// 3Dオブジェクト描画前処理
		EnemyBullet::PreDraw(dxCommon->GetCmdList());
		// 3Dオブジェクト描画前処理
		Enemy::PreDraw(dxCommon->GetCmdList());
		// 3Dオブジェクト描画前処理
		Zone::PreDraw(dxCommon->GetCmdList());
		// 3Dオブジェクト描画前処理
		Player::PreDraw(dxCommon->GetCmdList());
		//背景
		player->Draw();

		if (Scene != awakening && Scene != gameClear) {
			for (int i = 0; i < BulletMax; i++) {
				playerBullet[i]->Draw();
			}
		}

		//敵の弾
		for (int i = 0; i < EnemyBulletMax; i++) {
			enemyBullet[i]->Draw();
		}
		if (Scene == title) {
			zone->Draw();
			bb->Draw();
		}

		if ((Scene == appearance) || (Scene == awakening) || (Scene == gameClear)) {
			enemy->Draw();
		}

		if (Scene == gamePlay) {
			for (int i = 0; i < _countof(particle); i++) {
				if ((RemoveParticleStartflag == 1) || (RemoveParticleStartflag == 2)) {
					DrawObject3d(&particle[i], dxCommon->GetCmdList(), basicDescHeap1.Get(), squarevbView, squareibView, gpuDescHandleSRV2, _countof(squareindices));
				}
			}
			//敵
			enemy->Draw();
		}

		Sprite::PostDraw();
		// 3Dオブジェクト描画後処理
		PlayerBullet::PostDraw();
		// 3Dオブジェクト描画後処理
		Player::PostDraw();
		// 3Dオブジェクト描画後処理
		EnemyBullet::PostDraw();
		// 3Dオブジェクト描画後処理
		Enemy::PostDraw();
		Zone::PostDraw();
		BlackBoard::PostDraw();
		if (Scene == title) {
			DrawObject3d(&backScreen, dxCommon->GetCmdList(), basicDescHeap.Get(), vbView, ibView, gpuDescHandleSRV, _countof(indices));
		} else {
			DrawObject3d(&backScreen, dxCommon->GetCmdList(), basicDescHeap2.Get(), vbView, ibView, gpuDescHandleSRV, _countof(indices));
		}
		// 3Dオブジェクト描画後処理
		PlayerBullet::PostDraw();
		// 3Dオブジェクト描画後処理
		Player::PostDraw();
		// 3Dオブジェクト描画後処理
		EnemyBullet::PostDraw();
		// 3Dオブジェクト描画後処理
		Enemy::PostDraw();
		Zone::PostDraw();
		BlackBoard::PostDraw();
		Sprite::PreDraw(dxCommon->GetCmdList());
		if (Scene == title) {
			if (zone->GetSpace() == 1) {
				sprite[2]->Draw();
			}
			if (zone->GetStartFlag() == 1) {
				sprite[3]->Draw();
				sprite[2]->SetPosition({ 180.0f, 200.0f });
				sprite[2]->Draw();
			} else {
				sprite[2]->SetPosition({ 30.0f, 200.0f });
			}
			if (arrowFlag == 1) {
				sprite[33]->Draw();
			}
			sprite[24]->Draw();
			sprite[25]->Draw();
		}
		if (Scene == appearance || Scene == awakening || Scene == gameClear) {
			sprite[1]->Draw();
			sprite[24]->Draw();
			sprite[25]->Draw();
			if (ClearCameraTimer >= 500) {
				sprite[29]->Draw();
				sprite[32]->Draw();
			}
		}
		if (Scene == appearance) {
			sprite[31]->Draw();
		}
		if (Scene == gamePlay) {
			if (LaneNum == 0) {
				for (int i = 4; i < 24; i++) {
					sprite[i]->SetColor({ 1,1,1,0 });
				}
				sprite[28]->SetColor({ 1,1,1,0 });
			}
			if (LaneNum == 1) {
				for (int i = 4; i < 24; i++) {
					sprite[i]->SetColor({ 1,1,1,0.3f });
				}
				sprite[23]->SetColor({ 1,1,1,0.1f });
				sprite[28]->SetColor({ 1,1,1,0.2f });

			}
			if (LaneNum == 2) {
				for (int i = 4; i < 24; i++) {
					sprite[i]->SetColor({ 1,1,1,1 });
				}
				sprite[28]->SetColor({ 1,1,1,1 });
			}

			sprite[23]->Draw();
			sprite[23]->SetSize({ (float)(enemy->GetHp() * 20),16.0f });
			sprite[22]->Draw();
			if (bulletNum == 10) { sprite[11]->Draw(); }
			if (bulletNum == 9) { sprite[12]->Draw(); }
			if (bulletNum == 8) { sprite[13]->Draw(); }
			if (bulletNum == 7) { sprite[14]->Draw(); }
			if (bulletNum == 6) { sprite[15]->Draw(); }
			if (bulletNum == 5) { sprite[16]->Draw(); }
			if (bulletNum == 4) { sprite[17]->Draw(); }
			if (bulletNum == 3) { sprite[18]->Draw(); }
			if (bulletNum == 2) { sprite[19]->Draw(); }
			if (bulletNum == 1) { sprite[20]->Draw(); }
			if (bulletNum == 0) { sprite[21]->Draw(); }
			sprite[10]->Draw();
			if (player->GetHp() == 5) { sprite[8]->Draw(); }
			if (player->GetHp() == 4) { sprite[7]->Draw(); }
			if (player->GetHp() == 3) { sprite[6]->Draw(); }
			if (player->GetHp() == 2) { sprite[5]->Draw(); }
			if (player->GetHp() == 1) { sprite[4]->Draw(); }
			if (player->GetHp() == 0) { sprite[9]->Draw(); }
			sprite[28]->Draw();
			sprite[23]->Draw();
			sprite[23]->SetSize({ (float)(enemy->GetHp() * 18.8),24.0f });
		}

		if (OverFlag == 1) {
			sprite[27]->Draw();
			if (OverCount>=60) {
				sprite[30]->Draw();
			}
		}
		sprite[24]->Draw();
		sprite[25]->Draw();
		Sprite::PostDraw();
		wchar_t str[256];
		
		swprintf_s(str, L" particleSpeed[0]:%f\n", ParticleSpeed[0]);
		OutputDebugString(str);
		swprintf_s(str, L"RemoveArea:%d\n", RemoveAreaNumber);
		OutputDebugString(str);
#pragma endregion
#pragma region//ゲーム外処理２
		dxCommon->PostDraw();
	}
#pragma endregion
	delete(input);
	//ウィンドウ表示
	ShowWindow(winApp->GetHwnd(), SW_SHOW);
	winApp->Finalize();
	delete player;
	for (int i = 0; i < BulletMax; i++) {
		delete playerBullet[i];
	}
	delete enemy;
	for (int i = 0; i < EnemyBulletMax; i++) {
		delete enemyBullet[i];
	}
	for (int i = 0; i < SpriteMax; i++) {
		delete sprite[i];
	}
	delete audio;
	delete zone;
	delete bb;
	delete winApp;
	delete dxCommon;
	return 0;
}
#pragma endregion
#pragma endregion
//スズキノア