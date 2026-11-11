#include "HistorySnapshotText.h"

#include "Misc/Compression.h"

void FHistorySnapshotText::Set(FString Text)
{
	Plain = MoveTemp(Text);
	Compressed.Empty();
	UncompressedBytes = (static_cast<int64>(Plain.Len()) + 1) * sizeof(TCHAR);
	if (UncompressedBytes < 4096) return;
	int64 Capacity = FCompression::CompressMemoryBound(NAME_Zlib, UncompressedBytes);
	if (Capacity <= 0 || Capacity > MAX_int32) return;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(static_cast<int32>(Capacity));
	if (FCompression::CompressMemory(NAME_Zlib, Buffer.GetData(), Capacity, *Plain, UncompressedBytes, COMPRESS_BiasSpeed)
		&& Capacity < UncompressedBytes * 0.8)
	{
		Buffer.SetNum(static_cast<int32>(Capacity));
		Buffer.Shrink();
		Compressed = MoveTemp(Buffer);
		Plain.Empty();
	}
}

bool FHistorySnapshotText::Read(FString& OutText) const
{
	if (Compressed.IsEmpty())
	{
		OutText = Plain;
		return true;
	}
	OutText.GetCharArray().SetNumUninitialized(static_cast<int32>(UncompressedBytes / sizeof(TCHAR)));
	if (!FCompression::UncompressMemory(NAME_Zlib, OutText.GetCharArray().GetData(), UncompressedBytes,
		Compressed.GetData(), Compressed.Num()) || OutText.GetCharArray().Last() != 0)
	{
		OutText.Reset();
		return false;
	}
	return true;
}

bool FHistorySnapshotText::Equals(const FString& Text) const
{
	if (static_cast<uint64>(Text.Len() + 1) * sizeof(TCHAR) != UncompressedBytes) return false;
	FString Stored;
	return Read(Stored) && Stored == Text;
}
