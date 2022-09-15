#include "DirectXCommon.h"
#include <vector>
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using namespace Microsoft::WRL;

void DirectXCommon::Initialize(WinApp* winApp)
{
	// nullptrチェック
	assert(winApp);

	this->winApp = winApp;

	//DXGIデバイス初期化
	if (!InitializeDevice()) {
		assert(0);
	}

	//コマンド関連初期化
	if (!InitializeCommand()) {
		assert(0);
	}

	//スワップチェーンの生成
	if (!CreateSwapChain()) {
		assert(0);
	}

	//レンダーターゲット生成
	if (!InitializeRenderTargetView()) {
		assert(0);
	}

	//深度バッファ生成
	if (!InitializeDepthBuffer()) {
		assert(0);
	}

	//フェンス生成
	if (!CreateFence()) {
		assert(0);
	}
}

void DirectXCommon::PreDraw()
{
	//#pragma regin グラフィックスコマンド
	UINT bbIndex = swapchain->GetCurrentBackBufferIndex();
	//実行
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[bbIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT));
	//描画先指定
	auto rtvH = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeaps->GetCPUDescriptorHandleForHeapStart(), bbIndex, dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

	//深度ステンシルビュー用でスクリプタヒープ
	D3D12_CPU_DESCRIPTOR_HANDLE dsvH = dsvHeap->GetCPUDescriptorHandleForHeapStart();
	cmdList->OMSetRenderTargets(1, &rtvH, false, &dsvH);

	//画面クリア
	float clearColor[] = { 1.0f,1.0f, 1.0f,1.0f }; // 青っぽい色
	cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
	cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	//描画コマンド

	cmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, WinApp::window_width, WinApp::window_height));

	cmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, WinApp::window_width, WinApp::window_height));
}
void DirectXCommon::ClearDepthBuffer() {
	// 深度ステンシルビュー用デスクリプタヒープのハンドルを取得
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvH = CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvHeap->GetCPUDescriptorHandleForHeapStart());
	// 深度バッファのクリア
	cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}
void DirectXCommon::PostDraw()
{
	//#pragma regin グラフィックスコマンド
	UINT bbIndex = swapchain->GetCurrentBackBufferIndex();
	//リソースバリアを戻す
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backBuffers[bbIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT));

	cmdList->Close();

	ID3D12CommandList* cmdLists[] = { cmdList.Get() };
	cmdQueue->ExecuteCommandLists(1, cmdLists);
	cmdQueue->Signal(fence.Get(), ++fenceVal);
	if (fence->GetCompletedValue() != fenceVal) {
		HANDLE event = CreateEvent(nullptr, false, false, nullptr);
		fence->SetEventOnCompletion(fenceVal, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
	cmdAllocator->Reset();
	cmdList->Reset(cmdAllocator.Get(), nullptr);
	swapchain->Present(1, 0);
}

bool DirectXCommon::InitializeDevice()
{
	HRESULT result = S_FALSE;
	ComPtr<ID3D12Debug> debugController;
	//デバッグレイヤーをオンに	
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
	}

	//dxgiファクトリーの生成
	result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

	//アダプタの列挙用
	std::vector<ComPtr<IDXGIAdapter1>>adapters;

	//ここに特定の名前を持つアダプターオブジェクトが入る
	ComPtr<IDXGIAdapter1>tmpAdapter;
	for (int i = 0; dxgiFactory->EnumAdapters1(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
		adapters.push_back(tmpAdapter);
	}
	//グラフィックボードのアダプタを列挙2
	for (int i = 0; i < adapters.size(); i++) {
		DXGI_ADAPTER_DESC1 adesc;
		adapters[i]->GetDesc1(&adesc);
		if (adesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}
		std::wstring strDesc = adesc.Description;
		if (strDesc.find(L"Intel") == std::wstring::npos) {
			tmpAdapter = adapters[i];
			break;
		}
	}


	//デバイスの生成
	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	D3D_FEATURE_LEVEL featureLevel;
	result = S_FALSE;
	for (int i = 0; i < _countof(levels); i++) {
		result = D3D12CreateDevice(tmpAdapter.Get(), levels[i], IID_PPV_ARGS(&dev));
		if (result == S_OK) {
			featureLevel = levels[i];
			break;
		}
	}

	if (FAILED(result)) {
		assert(0);
		return false;
	}

	return true;
}

bool DirectXCommon::CreateSwapChain()
{
	HRESULT result = S_FALSE;
	//スワップチェーンの生成
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc{};
	swapchainDesc.Width = 1200;
	swapchainDesc.Height = 600;
	swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
	swapchainDesc.BufferCount = 2;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ComPtr<IDXGISwapChain1> swapchain1;
	HWND hwnd = winApp->GetHwnd();
	dxgiFactory->CreateSwapChainForHwnd(cmdQueue.Get(), winApp->GetHwnd(), &swapchainDesc, nullptr, nullptr, &swapchain1);
	if (FAILED(result)) {
		assert(0);
		return result;
	}

	swapchain1.As(&swapchain);

	return true;
}

bool DirectXCommon::InitializeCommand()
{
	HRESULT result = S_FALSE;

	result = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&cmdAllocator));
	if (FAILED(result)) {
		assert(0);
		return result;
	}

	//コマンドリストの生成
	result = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocator.Get(), nullptr, IID_PPV_ARGS(&cmdList));
	if (FAILED(result)) {
		assert(0);
		return result;
	}
	//コマンドキューの生成
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc{};
	dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdQueue));
	if (FAILED(result)) {
		assert(0);
		return result;
	}

	return true;
}

bool DirectXCommon::InitializeRenderTargetView()
{
	HRESULT result = S_FALSE;

	//でスクリプタヒープの生成
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NumDescriptors = 2;
	dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));
	if (FAILED(result)) {
		assert(0);
		return result;
	}
	//レンダーターゲットviewの生成
	backBuffers.resize(2);
	for (int i = 0; i < 2; i++) {
		result = swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
		if (FAILED(result)) {
			assert(0);
			return result;
		}

		//ディスクリプタヒープのハンドルを取得
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeaps->GetCPUDescriptorHandleForHeapStart(),
			i, dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
		//D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
		//handle.ptr += i * dev->GetDescriptorHandleIncrementSize(heapDesc.Type);
		dev->CreateRenderTargetView(backBuffers[i].Get(), nullptr, handle);
	}
	return true;
}

//深度バッファ
bool DirectXCommon::InitializeDepthBuffer()
{
	HRESULT result = S_FALSE;

	CD3DX12_RESOURCE_DESC depthResDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_D32_FLOAT, WinApp::window_width, WinApp::window_height, 1, 0, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);
	//リソース生成
	result = dev->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
		&depthResDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0), IID_PPV_ARGS(&depthBuffer));

	if (FAILED(result)) {
		assert(0);
		return result;
	}

	//深度view用でスクリプタヒープ作成
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	result = dev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
	if (FAILED(result)) {
		assert(0);
		return result;
	}

	//深度ビュー作成
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dev->CreateDepthStencilView(depthBuffer.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());

	return true;
}

bool DirectXCommon::CreateFence()
{
	HRESULT result = S_FALSE;
	result = dev->CreateFence(fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(result)) {
		assert(0);
		return false;
	}
	return true;
}
