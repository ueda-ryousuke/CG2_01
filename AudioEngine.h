#pragma once

#include <xaudio2.h>
#include <wrl/client.h> // Microsoft::WRL::ComPtr
#include <fstream>
#include <cassert>

#pragma comment(lib, "xaudio2.lib")

using Microsoft::WRL::ComPtr;

// --- 各種構造体定義 ---

// チャンクヘッダー
struct ChunkHeader
{
    char id[4];      // チャンクID ("RIFF", "fmt ", "JUNK", "data" 等)
    int32_t size;    // チャンクサイズ
};

// RIFFヘッダー
struct RiffHeader
{
    ChunkHeader chunk; // "RIFF"
    char type[4];      // "WAVE"
};

// フォーマットチャンク
struct FormatChunk
{
    ChunkHeader chunk; // "fmt "
    WAVEFORMATEX fmt;  // 波形フォーマット
};

// 音声データ構造体
struct SoundData
{
    WAVEFORMATEX wfex = {};      // 波形フォーマット
    BYTE* pBuffer = nullptr;     // 波形データのバッファ
    unsigned int bufferSize = 0; // バッファサイズ
};

// --- オーディオ管理クラス ---

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    // 初期化処理
    bool Initialize();

    // 後始末処理（XAudio2インスタンスの解放）
    void Finalize();

    // 音声データの読み込み (.wav)
    SoundData LoadWave(const char* filename);

    // 音声データの解放
    void Unload(SoundData* soundData);

    // 音声データの再生
    IXAudio2SourceVoice* PlayWave(const SoundData& soundData, bool loop = false);

private:
    ComPtr<IXAudio2> m_xAudio2;
    IXAudio2MasteringVoice* m_masterVoice = nullptr;
};