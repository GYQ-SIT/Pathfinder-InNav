#pragma once

#include "CoreMinimal.h"

// In-memory, lossless storage only. Project files and the number of undo steps are unchanged.
class INNAVI_API FHistorySnapshotText
{
public:
	void Set(FString Text);
	bool Read(FString& OutText) const;
	bool Equals(const FString& Text) const;
	uint64 GetStoredBytes() const { return Plain.GetAllocatedSize() + Compressed.GetAllocatedSize(); }
	uint64 GetUncompressedBytes() const { return UncompressedBytes; }
	bool IsCompressed() const { return !Compressed.IsEmpty(); }
private:
	FString Plain;
	TArray<uint8> Compressed;
	int64 UncompressedBytes = sizeof(TCHAR);
};
