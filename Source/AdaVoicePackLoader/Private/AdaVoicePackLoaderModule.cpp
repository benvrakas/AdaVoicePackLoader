#include "AdaVoicePackLoaderModule.h"
#include "AdaChatCommand.h"
#include "AdaGameEvents.h"
#include "AdaPackRegistry.h"
#include "AdaVoicePlayer.h"
#include "FGPlayerController.h"
#include "Patching/NativeHookManager.h"
#include "UI/Message/FGAudioMessage.h"

DEFINE_LOG_CATEGORY(LogAdaVoicePacks);

void FAdaVoicePackLoaderModule::StartupModule()
{
	if (WITH_EDITOR)
	{
		return;
	}
	FAdaPackRegistry::Get().Discover();

	// The game's message box starting a message: a pack line waiting for it starts its voice.
	SUBSCRIBE_METHOD_AFTER(UFGAudioMessage::StartPlayback, [](UFGAudioMessage* Self)
	{
		UAdaVoicePlayer::NotifyAudioMessageStarted(Self);
	});

	// A chat line typed on a client: /adapack runs on this game instead of going to the server.
	SUBSCRIBE_METHOD(AFGPlayerController::EnterChatMessage, [](auto& Scope, AFGPlayerController* Self, const FString& Message)
	{
		if (AAdaChatCommand::RunOnClient(Self, Message))
		{
			Scope.Cancel();
		}
	});

	UAdaGameEvents::RegisterHooks();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAdaVoicePackLoaderModule::Tick));
	UE_LOG(LogAdaVoicePacks, Log, TEXT("Started."));
}

void FAdaVoicePackLoaderModule::ShutdownModule()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
}

bool FAdaVoicePackLoaderModule::Tick(float DeltaTime)
{
	if (FAdaPackRegistry::Get().GetPacks().Num() > 0)
	{
		UAdaVoicePlayer::Get()->Tick(DeltaTime);
	}
	return true;
}

IMPLEMENT_MODULE(FAdaVoicePackLoaderModule, AdaVoicePackLoader)
