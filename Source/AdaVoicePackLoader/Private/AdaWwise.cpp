#include "AdaWwise.h"
#include "AdaVoicePackLoaderModule.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	// AK_Success.
	constexpr int32 WwiseSuccess = 1;

#if PLATFORM_WINDOWS
	void* GetSoundEngineModule()
	{
		for (const TCHAR* Dll : { TEXT("FactoryGameSteam-WwiseSoundEngine-Win64-Shipping.dll"), TEXT("FactoryGameEGS-WwiseSoundEngine-Win64-Shipping.dll") })
		{
			if (void* Module = GetModuleHandleW(Dll))
			{
				return Module;
			}
		}
		return nullptr;
	}
#endif
}

const AdaWwise::FCalls& AdaWwise::GetCalls()
{
	static const FCalls Calls = []
	{
		FCalls Found;
#if PLATFORM_WINDOWS
		void* Module = GetSoundEngineModule();
		if (!Module)
		{
			UE_LOG(LogAdaVoicePacks, Warning, TEXT("Wwise: the game's sound engine DLL isn't loaded"));
			return Found;
		}
		Found.LoadBankMemoryCopy = reinterpret_cast<decltype(Found.LoadBankMemoryCopy)>(FPlatformProcess::GetDllExport(Module, TEXT("?LoadBankMemoryCopy@SoundEngine@AK@@YA?AW4AKRESULT@@PEBXIAEAI1@Z")));
		Found.PostEvent = reinterpret_cast<decltype(Found.PostEvent)>(FPlatformProcess::GetDllExport(Module, TEXT("?PostEvent@SoundEngine@AK@@YAII_KIP6AXW4AkCallbackType@@PEAUAkEventCallbackInfo@@PEAX3@Z3IPEAUAkExternalSourceInfo@@I@Z")));
		Found.RegisterGameObj = reinterpret_cast<decltype(Found.RegisterGameObj)>(FPlatformProcess::GetDllExport(Module, TEXT("?RegisterGameObj@SoundEngine@AK@@YA?AW4AKRESULT@@_KPEBD@Z")));
		Found.StopPlayingID = reinterpret_cast<decltype(Found.StopPlayingID)>(FPlatformProcess::GetDllExport(Module, TEXT("?StopPlayingID@SoundEngine@AK@@YAXIHW4AkCurveInterpolation@@@Z")));
		UE_LOG(LogAdaVoicePacks, Log, TEXT("Wwise: game sound engine functions %s"), Found.IsComplete() ? TEXT("found") : TEXT("INCOMPLETE; no voice lines will play"));
#endif
		return Found;
	}();
	return Calls;
}

uint32 AdaWwise::IdFromName(const FString& Name)
{
	uint32 Hash = 2166136261u;
	for (const TCHAR Char : Name.ToLower())
	{
		Hash = (Hash * 16777619u) ^ static_cast<uint8>(Char);
	}
	return Hash;
}

bool AdaWwise::LoadBank(const TArray<uint8>& Bytes, const TCHAR* Label)
{
	const FCalls& Calls = GetCalls();
	if (!Calls.IsComplete())
	{
		return false;
	}
	uint32 BankId = 0;
	uint32 BankType = 0;
	const int32 Result = Calls.LoadBankMemoryCopy(Bytes.GetData(), Bytes.Num(), BankId, BankType);
	UE_LOG(LogAdaVoicePacks, Log, TEXT("%s: Wwise bank (%d bytes) %s (result %d)"), Label, Bytes.Num(), Result == WwiseSuccess ? TEXT("loaded") : TEXT("FAILED to load"), Result);
	return Result == WwiseSuccess;
}
