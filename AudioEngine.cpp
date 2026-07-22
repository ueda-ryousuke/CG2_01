#include "AudioEngine.h"
#include <cstring>

AudioEngine::AudioEngine()
    : m_masterVoice(nullptr)
{
}

AudioEngine::~AudioEngine()
{
    Finalize();
}

bool AudioEngine::Initialize()
{
    HRESULT hr;

    // XAudio2エンジンのインスタンス作成
    hr = XAudio2Create(&m_xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) return false;

    // マスターボイスの作成
    hr = m_xAudio2->CreateMasteringVoice(&m_masterVoice);
    if (FAILED(hr)) return false;

    return true;
}

// 追加：XAudio2インスタンスの解放（xAudio2.Reset() に相当）
void AudioEngine::Finalize()
{
    // MasteringVoiceはDestroyVoiceで破棄
    if (m_masterVoice)
    {
        m_masterVoice->DestroyVoice();
        m_masterVoice = nullptr;
    }

    // XAudio2本体の参照を解放 ( ComPtr::Reset() に相当 )
    if (m_xAudio2)
    {
        m_xAudio2.Reset();
    }
}

SoundData AudioEngine::LoadWave(const char* filename)
{
    // ① ファイルオープン
    std::ifstream file;
    file.open(filename, std::ios_base::binary);
    assert(file.is_open());

    // ② .wavデータ読み込み
    RiffHeader riff;
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

    assert(strncmp(riff.chunk.id, "RIFF", 4) == 0);
    assert(strncmp(riff.type, "WAVE", 4) == 0);

    FormatChunk format = {};
    file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
    assert(strncmp(format.chunk.id, "fmt ", 4) == 0);

    assert(format.chunk.size <= sizeof(format.fmt));
    file.read(reinterpret_cast<char*>(&format.fmt), format.chunk.size);

    ChunkHeader data;
    file.read(reinterpret_cast<char*>(&data), sizeof(data));

    if (strncmp(data.id, "JUNK", 4) == 0)
    {
        file.seekg(data.size, std::ios_base::cur);
        file.read(reinterpret_cast<char*>(&data), sizeof(data));
    }

    assert(strncmp(data.id, "data", 4) == 0);

    char* pBuffer = new char[data.size];
    file.read(pBuffer, data.size);

    // ③ ファイルを閉じる
    file.close();

    // ④ 読み込んだ音声データをreturn
    SoundData soundData = {};
    soundData.wfex = format.fmt;
    soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
    soundData.bufferSize = data.size;

    return soundData;
}

void AudioEngine::Unload(SoundData* soundData)
{
    if (!soundData || !soundData->pBuffer) return;

    // バッファのメモリを解放
    delete[] soundData->pBuffer;

    soundData->pBuffer = nullptr;
    soundData->bufferSize = 0;
    soundData->wfex = {};
}

IXAudio2SourceVoice* AudioEngine::PlayWave(const SoundData& soundData, bool loop)
{
    if (!m_xAudio2 || !soundData.pBuffer) return nullptr;

    HRESULT hr;
    IXAudio2SourceVoice* pSourceVoice = nullptr;

    hr = m_xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
    assert(SUCCEEDED(hr));

    XAUDIO2_BUFFER buf = {};
    buf.pAudioData = soundData.pBuffer;
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;
    buf.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    hr = pSourceVoice->SubmitSourceBuffer(&buf);
    assert(SUCCEEDED(hr));

    hr = pSourceVoice->Start();
    assert(SUCCEEDED(hr));

    return pSourceVoice;
}