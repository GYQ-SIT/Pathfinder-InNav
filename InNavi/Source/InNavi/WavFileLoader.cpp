#include "WavFileLoader.h"

#include "HAL/FileManager.h"
#include "Serialization/Archive.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	constexpr uint16 WaveFormatPcm = 1;
	constexpr uint16 WaveFormatIeeeFloat = 3;
	constexpr uint16 WaveFormatExtensible = 0xFFFE;
	constexpr int64 MaxWaveFileBytes = 64 * 1024 * 1024;

	uint32 ReadUInt32LE(const uint8* Data)
	{
		return Data[0] | (uint32(Data[1]) << 8) | (uint32(Data[2]) << 16) | (uint32(Data[3]) << 24);
	}

	uint16 ReadUInt16LE(const uint8* Data)
	{
		return uint16(Data[0] | (Data[1] << 8));
	}

	// Converts one sample (however it's stored in the file) to a signed 16-bit PCM sample -
	// USoundWaveProcedural::QueueAudio expects 16-bit PCM regardless of the source file's own depth.
	int16 ConvertSampleToInt16(const uint8* SampleBytes, uint16 BitsPerSample, uint16 FormatTag)
	{
		if (FormatTag == WaveFormatIeeeFloat && BitsPerSample == 32)
		{
			float Value;
			FMemory::Memcpy(&Value, SampleBytes, sizeof(float));
			return (int16)FMath::Clamp(Value * 32767.f, -32768.f, 32767.f);
		}
		switch (BitsPerSample)
		{
		case 8:
			// WAV's 8-bit PCM is unsigned (0..255, silence at 128) - shift to signed range first.
			return (int16)((int32(SampleBytes[0]) - 128) * 256);
		case 16:
			return (int16)ReadUInt16LE(SampleBytes);
		case 24:
			{
				// Drop the least-significant byte rather than dithering/rounding - these are short UI
				// sounds and background loops, the precision loss going from 24-bit to 16-bit is
				// inaudible here.
				const int32 Value24 = int32(SampleBytes[0]) | (int32(SampleBytes[1]) << 8) | (int32(SampleBytes[2]) << 16);
				const int32 Signed24 = (Value24 & 0x800000) ? (Value24 | 0xFF000000) : Value24;
				return (int16)(Signed24 >> 8);
			}
		case 32:
			return (int16)(int32(ReadUInt32LE(SampleBytes)) >> 16);
		default:
			return 0;
		}
	}
}

USoundWaveProcedural* LoadWavFileAsSoundWave(const FString& FilePath, UObject* Outer)
{
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath));
	if (!Reader)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: could not read '%s'"), *FilePath);
		return nullptr;
	}
	const int64 Size = Reader->TotalSize();
	if (Size < 12 || Size > MaxWaveFileBytes)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: '%s' must be a WAV file no larger than 64 MiB"), *FilePath);
		return nullptr;
	}
	TArray<uint8> FileBytes;
	FileBytes.SetNumUninitialized(static_cast<int32>(Size));
	Reader->Serialize(FileBytes.GetData(), Size);
	if (Reader->IsError())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: could not read '%s'"), *FilePath);
		return nullptr;
	}
	Reader.Reset();

	// RIFF header: "RIFF" (4) + chunk size (4) + "WAVE" (4) = 12 bytes minimum before any sub-chunk.
	if (FileBytes[0] != 'R' || FileBytes[1] != 'I' || FileBytes[2] != 'F' || FileBytes[3] != 'F' ||
		FileBytes[8] != 'W' || FileBytes[9] != 'A' || FileBytes[10] != 'V' || FileBytes[11] != 'E')
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: '%s' isn't a RIFF/WAVE file"), *FilePath);
		return nullptr;
	}

	uint16 FormatTag = 0;
	uint16 NumChannels = 0;
	uint32 SampleRate = 0;
	uint16 BitsPerSample = 0;
	const uint8* DataChunk = nullptr;
	uint32 DataChunkSize = 0;

	// Walks every sub-chunk rather than assuming "fmt " is always immediately followed by "data" -
	// some export tools insert others ("LIST", "fact", ...) in between, or after.
	int32 Offset = 12;
	while (Offset <= FileBytes.Num() - 8)
	{
		const uint8* ChunkId = &FileBytes[Offset];
		const uint32 ChunkSize = ReadUInt32LE(&FileBytes[Offset + 4]);
		const int32 ChunkDataStart = Offset + 8;
		if (ChunkDataStart + (int64)ChunkSize > FileBytes.Num())
		{
			UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: truncated chunk in '%s'"), *FilePath);
			return nullptr;
		}

		if (ChunkId[0] == 'f' && ChunkId[1] == 'm' && ChunkId[2] == 't' && ChunkId[3] == ' ' && ChunkSize >= 16)
		{
			const uint8* Fmt = &FileBytes[ChunkDataStart];
			FormatTag = ReadUInt16LE(Fmt);
			NumChannels = ReadUInt16LE(Fmt + 2);
			SampleRate = ReadUInt32LE(Fmt + 4);
			BitsPerSample = ReadUInt16LE(Fmt + 14);

			// WAVE_FORMAT_EXTENSIBLE stores the real format tag as the first two bytes of the
			// sub-format GUID: base "fmt " fields are 16 bytes, +2 for cbSize, +22 for the extension
			// itself, and the sub-format GUID is the last 16 bytes of that extension - so its first
			// two bytes sit at offset 24 from the start of the chunk. Some export tools (including
			// some Mac ones) write this even for plain PCM.
			if (FormatTag == WaveFormatExtensible && ChunkSize >= 40)
			{
				FormatTag = ReadUInt16LE(Fmt + 24);
			}
		}
		else if (ChunkId[0] == 'd' && ChunkId[1] == 'a' && ChunkId[2] == 't' && ChunkId[3] == 'a')
		{
			DataChunk = FileBytes.GetData() + ChunkDataStart;
			DataChunkSize = ChunkSize;
		}

		// Chunks are word-aligned - a chunk with an odd size is followed by one pad byte.
		Offset = ChunkDataStart + static_cast<int32>(ChunkSize) + (ChunkSize & 1);
	}

	if (!DataChunk || NumChannels == 0 || SampleRate == 0 || SampleRate > MAX_int32 || BitsPerSample == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: '%s' is missing a usable 'fmt '/'data' chunk"), *FilePath);
		return nullptr;
	}
	if (FormatTag != WaveFormatPcm && FormatTag != WaveFormatIeeeFloat)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: '%s' uses an unsupported compressed format (tag %d) - only PCM/float WAV is supported"), *FilePath, FormatTag);
		return nullptr;
	}
	if (BitsPerSample != 8 && BitsPerSample != 16 && BitsPerSample != 24 && BitsPerSample != 32)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: '%s' has an unsupported bit depth (%d)"), *FilePath, BitsPerSample);
		return nullptr;
	}

	const uint32 BytesPerSample = BitsPerSample / 8;
	const uint32 NumSamples = DataChunkSize / BytesPerSample;
	if (NumSamples == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("LoadWavFileAsSoundWave: '%s' has no audio data"), *FilePath);
		return nullptr;
	}

	TArray<uint8> Pcm16Data;
	Pcm16Data.SetNumUninitialized(NumSamples * sizeof(int16));
	int16* Out = (int16*)Pcm16Data.GetData();
	for (uint32 i = 0; i < NumSamples; i++)
	{
		Out[i] = ConvertSampleToInt16(DataChunk + i * BytesPerSample, BitsPerSample, FormatTag);
	}

	USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>(Outer);
	SoundWave->SetSampleRate(SampleRate);
	SoundWave->NumChannels = NumChannels;
	SoundWave->Duration = (float)(NumSamples / NumChannels) / (float)SampleRate;
	SoundWave->bLooping = false;
	SoundWave->QueueAudio(Pcm16Data.GetData(), Pcm16Data.Num());

	return SoundWave;
}
