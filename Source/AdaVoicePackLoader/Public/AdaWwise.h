#pragma once

#include "CoreMinimal.h"

// The game's own Wwise sound engine, reached through the functions its DLL exports.
namespace AdaWwise
{
	struct FCalls
	{
		int32 (*LoadBankMemoryCopy)(const void*, uint32, uint32&, uint32&) = nullptr;
		uint32 (*PostEvent)(uint32, uint64, uint32, void (*)(int32, void*, void*, void*), void*, uint32, void*, uint32) = nullptr;
		int32 (*RegisterGameObj)(uint64, const char*) = nullptr;
		void (*StopPlayingID)(uint32, int32, int32) = nullptr;

		bool IsComplete() const { return LoadBankMemoryCopy && PostEvent && RegisterGameObj && StopPlayingID; }
	};

	// The functions, looked up once; members are null where the game doesn't export them (or off Windows).
	const FCalls& GetCalls();

	// Wwise's id for a name: FNV-1 of the lowercase name.
	uint32 IdFromName(const FString& Name);

	// Loads a bank from memory. Logs the result under Label; returns true if it loaded.
	bool LoadBank(const TArray<uint8>& Bytes, const TCHAR* Label);
}
