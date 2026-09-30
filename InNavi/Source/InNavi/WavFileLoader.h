// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class USoundWaveProcedural;

// Reads a PCM .wav file from disk and builds a ready-to-play USoundWaveProcedural from it - the
// project's entire runtime audio pipeline (see ANavPlayerController::PlayWavFile) after the
// vendored RuntimeAudioImporter plugin (originally added for mp3 support) was removed once the
// professor confirmed wav-only playback is sufficient. WAV needs no real decompression (it's
// already raw PCM behind a small header), so unlike that plugin this reads and converts
// synchronously on the calling thread - no async decode, no background-thread handling to get
// wrong.
//
// Supports the common cases: 8/16/24/32-bit integer PCM and 32-bit float PCM, mono or stereo, any
// sample rate, including files whose "fmt " chunk uses WAVE_FORMAT_EXTENSIBLE (some export tools
// write this even for plain PCM) - skips other chunks it doesn't need (e.g. "LIST"/"fact"). Returns
// nullptr (and logs the reason) if the file can't be read or isn't a WAV this understands (e.g. an
// actually-compressed format inside a WAV container, which this deliberately doesn't decode).
//
// Assumes a little-endian platform (Windows), matching the rest of this project - WAV's own byte
// order is always little-endian regardless of platform, so this only matters for how the raw bytes
// here are reassembled into integers, not for the file format itself.
USoundWaveProcedural* LoadWavFileAsSoundWave(const FString& FilePath, UObject* Outer);
