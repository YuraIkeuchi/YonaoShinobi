#pragma once

#include <Windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <d3dx12.h>
#include "Player.h"

/// <summary>
/// 3Dオブジェクト
/// </summary>
class EnemyBullet {
private: // エイリアス
	// Microsoft::WRL::を省略
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	// DirectX::を省略
	using XMFLOAT2 = DirectX::XMFLOAT2;
	using XMFLOAT3 = DirectX::XMFLOAT3;
	using XMFLOAT4 = DirectX::XMFLOAT4;
	using XMMATRIX = DirectX::XMMATRIX;

public: // サブクラス
	// 頂点データ構造体
	struct VertexPosNormalUv {
		XMFLOAT3 pos; // xyz座標
		XMFLOAT3 normal; // 法線ベクトル
		XMFLOAT2 uv;  // uv座標
	};

	// 定数バッファ用データ構造体
	struct ConstBufferData {
		XMFLOAT4 color;	// 色 (RGBA)
		XMMATRIX mat;	// ３Ｄ変換行列
	};

private: // 定数
	static const int division = 50;					// 分割数
	static const float radius;				// 底面の半径
	static const float prizmHeight;			// 柱の高さ
	static const int planeCount = division * 2 + division * 2;		// 面の数
	static const int vertexCount = planeCount * 3;		// 頂点数

public: // 静的メンバ関数
	/// <summary>
	/// 静的初期化
	/// </summary>
	/// <param name="device">デバイス</param>
	/// <param name="window_width">画面幅</param>
	/// <param name="window_height">画面高さ</param>
	/// <returns>成否</returns>
	static bool StaticInitialize(ID3D12Device* device, int window_width, int window_height);

	/// <summary>
	/// 描画前処理
	/// </summary>
	/// <param name="cmdList">描画コマンドリスト</param>
	static void PreDraw(ID3D12GraphicsCommandList* cmdList);

	/// <summary>
	/// 描画後処理
	/// </summary>
	static void PostDraw();

	/// <summary>
	/// 3Dオブジェクト生成
	/// </summary>
	/// <returns></returns>
	static EnemyBullet* Create();

	/// <summary>
	/// 視点座標の取得
	/// </summary>
	/// <returns>座標</returns>
	static const XMFLOAT3& GetEye() { return eye; }

	/// <summary>
	/// 視点座標の設定
	/// </summary>
	/// <param name="position">座標</param>
	static void SetEye(XMFLOAT3 eye);

	/// <summary>
	/// 注視点座標の取得
	/// </summary>
	/// <returns>座標</returns>
	static const XMFLOAT3& GetTarget() { return target; }

	/// <summary>
	/// 注視点座標の設定
	/// </summary>
	/// <param name="position">座標</param>
	static void SetTarget(XMFLOAT3 target);

	/// <summary>
	/// ベクトルによる移動
	/// </summary>
	/// <param name="move">移動量</param>
	static void CameraMoveVector(XMFLOAT3 move);

private: // 静的メンバ変数
	// デバイス
	static ID3D12Device* device;
	// デスクリプタサイズ
	static UINT descriptorHandleIncrementSize;
	// コマンドリスト
	static ID3D12GraphicsCommandList* cmdList;
	// ルートシグネチャ
	static ComPtr<ID3D12RootSignature> rootsignature;
	// パイプラインステートオブジェクト
	static ComPtr<ID3D12PipelineState> pipelinestate;
	// デスクリプタヒープ
	static ComPtr<ID3D12DescriptorHeap> descHeap;
	// 頂点バッファ
	static ComPtr<ID3D12Resource> vertBuff;
	// インデックスバッファ
	static ComPtr<ID3D12Resource> indexBuff;
	// テクスチャバッファ
	static ComPtr<ID3D12Resource> texbuff;
	// シェーダリソースビューのハンドル(CPU)
	static CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescHandleSRV;
	// シェーダリソースビューのハンドル(CPU)
	static CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescHandleSRV;
	// ビュー行列
	static XMMATRIX matView;
	// 射影行列
	static XMMATRIX matProjection;
	// 視点座標
	static XMFLOAT3 eye;
	// 注視点座標
	static XMFLOAT3 target;
	// 上方向ベクトル
	static XMFLOAT3 up;
	// 頂点バッファビュー
	static D3D12_VERTEX_BUFFER_VIEW vbView;
	// インデックスバッファビュー
	static D3D12_INDEX_BUFFER_VIEW ibView;
	// 頂点データ配列
	//static VertexPosNormalUv vertices[vertexCount];
	static std::vector<VertexPosNormalUv> vertices;
	// 頂点インデックス配列
	//static unsigned short indices[planeCount * 3];
	static std::vector<unsigned short> indices;
private:// 静的メンバ関数
	/// <summary>
	/// デスクリプタヒープの初期化
	/// </summary>
	/// <returns></returns>
	static bool InitializeDescriptorHeap();

	/// <summary>
	/// カメラ初期化
	/// </summary>
	/// <param name="window_width">画面横幅</param>
	/// <param name="window_height">画面縦幅</param>
	static void InitializeCamera(int window_width, int window_height);

	/// <summary>
	/// グラフィックパイプライン生成
	/// </summary>
	/// <returns>成否</returns>
	static bool InitializeGraphicsPipeline();

	/// <summary>
	/// テクスチャ読み込み
	/// </summary>
	/// <returns>成否</returns>
	static bool LoadTexture();

	/// <summary>
	/// モデル作成
	/// </summary>
	static void CreateModel();

	/// <summary>
	/// ビュー行列を更新
	/// </summary>
	static void UpdateViewMatrix();

public: // メンバ関数
	bool Initialize();
	/// <summary>
	/// 毎フレーム処理
	/// </summary>
	void Update(XMMATRIX& matView);
	void Move();
	void Shot(float speed, int lane);
	bool collide(Player* player);
	/// <summary>
	/// 描画
	/// </summary>
	void Draw();
	/// <summary>
	/// 座標の取得
	/// </summary>
	/// <returns>座標</returns>
	const XMFLOAT3& GetPosition() { return position; }
	const XMFLOAT3& GetRotaition() { return rotation; }
	const XMFLOAT4& GetColor() { return color; }
	const int GetIsAlive() { return isAlive; }
	const float GetSpeed() { return speed; }
	const int GetLane() { return lane; }
	const int GetAreaNumber() { return AreaNumber; }
	/// <summary>
	/// 座標の設定
	/// </summary>
	/// <param name="position">座標</param>
	void SetConvertPos(XMFLOAT3 position);
	void SetPosition(XMFLOAT3 position) { this->position = position; }
	void SetPosition2Enemy(XMFLOAT3 position) { this->position2 = position2; }
	void SetRotaition(XMFLOAT3 rotaition) { this->rotation = rotaition; }
	void GetColor(XMFLOAT4 color) { this->color = color; }
	void SetIsAlive(int isAlive) { this->isAlive = isAlive; }
	void SetSpeed(float speed) { this->speed = speed; }
	void Setlane(int lane) { this->lane = lane; }
private: // メンバ変数
	ComPtr<ID3D12Resource> constBuff; // 定数バッファ
	float vel = 1.5f;
	int HitFlag = 0;
	int Rock = 0;
	// 色
	XMFLOAT4 color = { 1.0f,1.0f,1.0f,1.0f };
	// ローカルスケール
	XMFLOAT3 scale = { 1.5f,1.5f,1.5f };
	// X,Y,Z軸回りのローカル回転角
	XMFLOAT3 rotation = { 0,0,0 };
	// ローカル座標
	XMFLOAT3 position = { 0,0,0};
	// 敵のローカル座標
	XMFLOAT3 position2 = { 0,0,0 };
	// ローカルワールド変換行列
	XMMATRIX matWorld;
	// 親オブジェクト
	EnemyBullet* parent = nullptr;
	//球関連
	int isAlive = 0;
	float FirstSpeed = 0;
	float speed = 0;
	const float PI = 3.14f;
	float CirRadius = 0;
	float Scale = 0;
	int lane = 0;
	int AreaNumber = 0;
	int Heating = 0;
	int awekeningNumber = 0;//覚醒

};