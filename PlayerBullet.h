#pragma once

#include <Windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <d3dx12.h>
#include "Player.h"


/// <summary>
/// 3D�I�u�W�F�N�g
/// </summary>
class PlayerBullet {
private: // �G�C���A�X
	// Microsoft::WRL::���ȗ�
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	// DirectX::���ȗ�
	using XMFLOAT2 = DirectX::XMFLOAT2;
	using XMFLOAT3 = DirectX::XMFLOAT3;
	using XMFLOAT4 = DirectX::XMFLOAT4;
	using XMMATRIX = DirectX::XMMATRIX;

public: // �T�u�N���X
	// ���_�f�[�^�\����
	struct VertexPosNormalUv {
		XMFLOAT3 pos; // xyz���W
		XMFLOAT3 normal; // �@���x�N�g��
		XMFLOAT2 uv;  // uv���W
	};

	// �萔�o�b�t�@�p�f�[�^�\����
	struct ConstBufferData {
		XMFLOAT4 color;	// �F (RGBA)
		XMMATRIX mat;	// �R�c�ϊ��s��
	};

private: // �萔
	static const int division = 50;					// ������
	static const float radius;				// ��ʂ̔��a
	static const float prizmHeight;			// ���̍���
	static const int planeCount = division * 2 + division * 2;		// �ʂ̐�
	static const int vertexCount = planeCount * 3;		// ���_��

public: // �ÓI�����o�֐�
	/// <summary>
	/// �ÓI������
	/// </summary>
	/// <param name="device">�f�o�C�X</param>
	/// <param name="window_width">��ʕ�</param>
	/// <param name="window_height">��ʍ���</param>
	/// <returns>����</returns>
	static bool StaticInitialize(ID3D12Device* device, int window_width, int window_height);

	/// <summary>
	/// �`��O����
	/// </summary>
	/// <param name="cmdList">�`��R�}���h���X�g</param>
	static void PreDraw(ID3D12GraphicsCommandList* cmdList);

	/// <summary>
	/// �`��㏈��
	/// </summary>
	static void PostDraw();

	/// <summary>
	/// 3D�I�u�W�F�N�g����
	/// </summary>
	/// <returns></returns>
	static PlayerBullet* Create();

	/// <summary>
	/// ���_���W�̎擾
	/// </summary>
	/// <returns>���W</returns>
	static const XMFLOAT3& GetEye() { return eye; }

	/// <summary>
	/// ���_���W�̐ݒ�
	/// </summary>
	/// <param name="position">���W</param>
	static void SetEye(XMFLOAT3 eye);

	/// <summary>
	/// �����_���W�̎擾
	/// </summary>
	/// <returns>���W</returns>
	static const XMFLOAT3& GetTarget() { return target; }

	/// <summary>
	/// �����_���W�̐ݒ�
	/// </summary>
	/// <param name="position">���W</param>
	static void SetTarget(XMFLOAT3 target);

	/// <summary>
	/// �x�N�g���ɂ��ړ�
	/// </summary>
	/// <param name="move">�ړ���</param>
	static void CameraMoveVector(XMFLOAT3 move);

private: // �ÓI�����o�ϐ�
	// �f�o�C�X
	static ID3D12Device* device;
	// �f�X�N���v�^�T�C�Y
	static UINT descriptorHandleIncrementSize;
	// �R�}���h���X�g
	static ID3D12GraphicsCommandList* cmdList;
	// ���[�g�V�O�l�`��
	static ComPtr<ID3D12RootSignature> rootsignature;
	// �p�C�v���C���X�e�[�g�I�u�W�F�N�g
	static ComPtr<ID3D12PipelineState> pipelinestate;
	// �f�X�N���v�^�q�[�v
	static ComPtr<ID3D12DescriptorHeap> descHeap;
	// ���_�o�b�t�@
	static ComPtr<ID3D12Resource> vertBuff;
	// �C���f�b�N�X�o�b�t�@
	static ComPtr<ID3D12Resource> indexBuff;
	// �e�N�X�`���o�b�t�@
	static ComPtr<ID3D12Resource> texbuff;
	// �V�F�[�_���\�[�X�r���[�̃n���h��(CPU)
	static CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescHandleSRV;
	// �V�F�[�_���\�[�X�r���[�̃n���h��(CPU)
	static CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescHandleSRV;
	// �r���[�s��
	static XMMATRIX matView;
	// �ˉe�s��
	static XMMATRIX matProjection;
	// ���_���W
	static XMFLOAT3 eye;
	// �����_���W
	static XMFLOAT3 target;
	// ������x�N�g��
	static XMFLOAT3 up;
	// ���_�o�b�t�@�r���[
	static D3D12_VERTEX_BUFFER_VIEW vbView;
	// �C���f�b�N�X�o�b�t�@�r���[
	static D3D12_INDEX_BUFFER_VIEW ibView;
	// ���_�f�[�^�z��
	//static VertexPosNormalUv vertices[vertexCount];
	static std::vector<VertexPosNormalUv> vertices;
	// ���_�C���f�b�N�X�z��
	//static unsigned short indices[planeCount * 3];
	static std::vector<unsigned short> indices;
private:// �ÓI�����o�֐�
	/// <summary>
	/// �f�X�N���v�^�q�[�v�̏�����
	/// </summary>
	/// <returns></returns>
	static bool InitializeDescriptorHeap();

	/// <summary>
	/// �J����������
	/// </summary>
	/// <param name="window_width">��ʉ���</param>
	/// <param name="window_height">��ʏc��</param>
	static void InitializeCamera(int window_width, int window_height);

	/// <summary>
	/// �O���t�B�b�N�p�C�v���C������
	/// </summary>
	/// <returns>����</returns>
	static bool InitializeGraphicsPipeline();

	/// <summary>
	/// �e�N�X�`���ǂݍ���
	/// </summary>
	/// <returns>����</returns>
	static bool LoadTexture();

	/// <summary>
	/// ���f���쐬
	/// </summary>
	static void CreateModel();

	/// <summary>
	/// �r���[�s����X�V
	/// </summary>
	static void UpdateViewMatrix();

public: // �����o�֐�
	bool Initialize();
	/// <summary>
	/// ���t���[������
	/// </summary>
	void Update(XMMATRIX& matView,int awakeingNumber);
	void Move(int awakeingNumber);
	void Shot(XMFLOAT3 position,int lane, float speed);
	bool collide(Player* player,int bulletNum);
	/// <summary>
	/// �`��
	/// </summary>
	void Draw();
	/// <summary>
	/// ���W�̎擾
	/// </summary>
	/// <returns>���W</returns>
	const XMFLOAT3& GetPosition() { return position; }
	const XMFLOAT3& GetRotaition() { return rotation; }
	const XMFLOAT4& GetColor() { return color; }
	const int GetIsAlive() { return isAlive; }
	const float GetSpeed() { return speed; }
	const int GetLane() { return lane; }
	const int GetAreaNumber() { return AreaNumber; }
	const int GetInsideCount() { return InsideCount; }
	const int GetCenterCount() { return CenterCount; }
	const int GetOutsideCount() { return OutsideCount; }
	/// <summary>
	/// ���W�̐ݒ�
	/// </summary>
	/// <param name="position">���W</param>
	void SetConvertPos(XMFLOAT3 position, int lane, float speed);
	void SetPosition(XMFLOAT3 position) { this->position = position; }
	void SetPosition2Enemy(XMFLOAT3 position) { this->position2 = position2; }
	void SetRotaition(XMFLOAT3 rotaition) { this->rotation = rotaition; }
	void GetColor(XMFLOAT4 color) { this->color= color; }
	void SetIsAlive(int isAlive) { this->isAlive = isAlive;}
	void SetSpeed(float speed) { this->speed = speed; }
	void Setlane(int lane) { this->lane = lane; }
private: // �����o�ϐ�
	ComPtr<ID3D12Resource> constBuff; // �萔�o�b�t�@
	float vel = 1.5f;
	int HitFlag = 0;
	int Rock = 0;
	// �F
	XMFLOAT4 color = { 1.0f,1.0f,1.0f,1.0f };
	// ���[�J���X�P�[��
	XMFLOAT3 scale = { 1.5f,1.5f,1.5f };
	// X,Y,Z�����̃��[�J����]�p
	XMFLOAT3 rotation = { 0,0,0 };
	// ���[�J�����W
	XMFLOAT3 position = { 0,0,-100.f };
	// �G�̃��[�J�����W
	XMFLOAT3 position2 = { 0,0,0 };
	// ���[�J�����[���h�ϊ��s��
	XMMATRIX matWorld;
	// �e�I�u�W�F�N�g
	PlayerBullet* parent = nullptr;
	//���֘A
	int isAlive = 0;
	float aX2bX = 0;
	float aZ2bZ = 0;
	float aR2bR = 0;
	float speed = 0;
	const float PI = 3.14f;
	float CirRadius=0;
	float Scale = 0;
	int invalid = 0;
	int lane = 0;
	int AreaNumber = 0;
	int InsideCount = 0;
	int CenterCount = 0;
	int OutsideCount = 0;
	int awekeningNumber = 0;
};