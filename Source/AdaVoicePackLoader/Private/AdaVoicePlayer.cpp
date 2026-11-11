#include "AdaVoicePlayer.h"
#include "AdaGameEvents.h"
#include "AdaPackRegistry.h"
#include "AdaSettings.h"
#include "AdaVoicePackLoaderModule.h"
#include "AdaWwise.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FGHUD.h"
#include "FGPlayerState.h"
#include "FGStorySubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Narrative/FGMessage.h"
#include "UI/FGGameUI.h"
#include "UI/Message/FGAudioMessage.h"
#include "UI/Message/FGMessageSender.h"
#include "UObject/UnrealType.h"

namespace
{
	// A line queued in the game's message box that hasn't started by then plays without the box.
	constexpr double BoxTimeoutSeconds = 8.0;

	// Extra time the message box keeps the text up after the clip ends.
	constexpr float BoxExtraSeconds = 1.f;

	// The box closing with more than this left of the clip means the player skipped the message: the voice stops too.
	constexpr float SkipDetectSeconds = 2.f;

	const TCHAR* AdaSenderPath = TEXT("/Game/FactoryGame/Interface/UI/Message/Sender/Sender_ADA.Sender_ADA_C");

	// The Wwise game object lines play on when the message box's own emitter isn't available.
	constexpr uint64 VoiceGameObject = 0x4144415041434b53ull;

	// Seconds between removals of the mod's messages from the game's saved lists of played messages.
	constexpr float PlayedCleanupInterval = 5.f;

	// Seconds after a world's first tick before the "world_loaded" trigger fires.
	constexpr double WorldLoadedDelaySeconds = 20.0;

	// Moving or turning less than this still counts as idle.
	constexpr float IdleMoveTolerance = 100.f;
	constexpr float IdleTurnToleranceDegrees = 5.f;

	// A "random:<seconds>" trigger fires after a random time between these fractions of its argument.
	constexpr float RandomMinFraction = 0.5f;
	constexpr float RandomMaxFraction = 1.5f;

	UWorld* FindGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::Game && World && World->GetNetMode() != NM_DedicatedServer && UGameplayStatics::GetPlayerController(World, 0))
			{
				return World;
			}
		}
		return nullptr;
	}

	// Removes messages made by the player object from Owner's array property PropertyName; returns how many.
	int32 RemoveOwnMessages(UObject* Owner, const TCHAR* PropertyName)
	{
		FArrayProperty* Property = Owner ? FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName) : nullptr;
		FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
		if (!Inner)
		{
			return 0;
		}
		FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Owner));
		int32 Removed = 0;
		for (int32 i = Array.Num() - 1; i >= 0; --i)
		{
			const UObject* Message = Inner->GetObjectPropertyValue(Array.GetRawPtr(i));
			if (Message && Message->GetTypedOuter<UAdaVoicePlayer>())
			{
				Array.RemoveValues(i, 1);
				++Removed;
			}
		}
		return Removed;
	}
}

UAdaVoicePlayer* UAdaVoicePlayer::Get()
{
	static TWeakObjectPtr<UAdaVoicePlayer> Instance;
	if (!Instance.IsValid())
	{
		UAdaVoicePlayer* Created = NewObject<UAdaVoicePlayer>(GetTransientPackage());
		Created->AddToRoot();
		Instance = Created;
	}
	return Instance.Get();
}

UWorld* UAdaVoicePlayer::GetWorld() const
{
	return FindGameWorld();
}

UFGGameUI* UAdaVoicePlayer::GetGameUI() const
{
	const UWorld* World = FindGameWorld();
	const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	const AFGHUD* Hud = PlayerController ? Cast<AFGHUD>(PlayerController->GetHUD()) : nullptr;
	return Hud ? Hud->GetGameUI() : nullptr;
}

TSubclassOf<UFGMessageSender> UAdaVoicePlayer::GetAdaSender()
{
	if (!AdaSender)
	{
		AdaSender = TSoftClassPtr<UFGMessageSender>(FSoftObjectPath(AdaSenderPath)).LoadSynchronous();
		if (!AdaSender)
		{
			UE_LOG(LogAdaVoicePacks, Warning, TEXT("Can't load ADA's message sender '%s'"), AdaSenderPath);
		}
	}
	return AdaSender;
}

void UAdaVoicePlayer::Tick(float DeltaSeconds)
{
	UWorld* World = FindGameWorld();
	if (!World)
	{
		TrackedWorld = nullptr;
		return;
	}
	if (!bBanksTried)
	{
		bBanksTried = true;
		FAdaPackRegistry::Get().LoadBanks();
		if (AdaWwise::GetCalls().IsComplete())
		{
			AdaWwise::GetCalls().RegisterGameObj(VoiceGameObject, "AdaVoicePacks");
		}
	}

	const bool bEnabledNow = AdaSettings::IsEnabled(World);
	if (bEnabledNow != bEnabled)
	{
		bEnabled = bEnabledNow;
		UE_LOG(LogAdaVoicePacks, Log, TEXT("Voice lines turned %s in the mod's settings"), bEnabled ? TEXT("on") : TEXT("off"));
		if (!bEnabled && (IsSpeaking() || PendingMessage))
		{
			if (ShownBox.IsValid())
			{
				ShownBox->CancelPlayback();
			}
			StopLine();
		}
	}

	const double Now = FPlatformTime::Seconds();
	if (PendingMessage && Now - PendingSince > BoxTimeoutSeconds)
	{
		UE_LOG(LogAdaVoicePacks, Warning, TEXT("The message box didn't start '%s' within %.0f s; playing it without the box"), CurrentLine ? *CurrentLine->EventName : TEXT("?"), BoxTimeoutSeconds);
		AbandonedMessages.Add(PendingMessage.Get());
		StartVoice(false);
	}
	if (ShownBox.IsValid() || ShownBoxSince > 0.0)
	{
		UFGGameUI* GameUI = GetGameUI();
		if (!ShownBox.IsValid() || !GameUI || GameUI->GetActiveAudioMessage() != ShownBox.Get())
		{
			UE_LOG(LogAdaVoicePacks, Log, TEXT("Message box closed after %.1f s (voice clip %.1f s)"), Now - ShownBoxSince, CurrentLine ? CurrentLine->Seconds : 0.f);
			ShownBox = nullptr;
			ShownBoxSince = 0.0;
			if (IsSpeaking() && LineTimeLeft > SkipDetectSeconds)
			{
				UE_LOG(LogAdaVoicePacks, Log, TEXT("Message skipped; stopping '%s'"), CurrentLine ? *CurrentLine->EventName : TEXT("?"));
				StopLine();
			}
		}
	}
	if (IsSpeaking())
	{
		LineTimeLeft -= DeltaSeconds;
		if (LineTimeLeft <= 0.f)
		{
			WwisePlayingId = 0;
			CurrentLine = nullptr;
		}
	}

	PlayedCleanupTimer -= DeltaSeconds;
	if (PlayedCleanupTimer <= 0.f)
	{
		PlayedCleanupTimer = PlayedCleanupInterval;
		RemoveFromPlayedMessages();
	}

	if (TrackedWorld.Get() != World)
	{
		TrackedWorld = World;
		WorldSeenSince = Now;
		bWorldLoadedFired = false;
		IdleSeconds = 0.f;
		IdleFired.Reset();
	}
	PollTriggers(DeltaSeconds);
	UAdaGameEvents::Get()->Tick(World, DeltaSeconds);
}

void UAdaVoicePlayer::PollTriggers(float DeltaSeconds)
{
	FAdaPackRegistry& Registry = FAdaPackRegistry::Get();
	UWorld* World = FindGameWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	if (!bWorldLoadedFired && FPlatformTime::Seconds() - WorldSeenSince >= WorldLoadedDelaySeconds)
	{
		bWorldLoadedFired = true;
		Fire(TEXT("world_loaded"));
	}

	// Menus show the mouse cursor; time spent in one isn't idling.
	const FRotator Rotation = PlayerController->GetControlRotation();
	const bool bMoved = FVector::Dist(Pawn->GetActorLocation(), IdleLocation) > IdleMoveTolerance || !Rotation.Equals(IdleRotation, IdleTurnToleranceDegrees);
	if (bMoved || PlayerController->ShouldShowMouseCursor())
	{
		IdleSeconds = 0.f;
		IdleFired.Reset();
		IdleLocation = Pawn->GetActorLocation();
		IdleRotation = Rotation;
	}
	else
	{
		IdleSeconds += DeltaSeconds;
		for (const float Threshold : Registry.GetTriggerArgs(TEXT("idle")))
		{
			if (Threshold > 0.f && IdleSeconds >= Threshold && !IdleFired.Contains(Threshold))
			{
				IdleFired.Add(Threshold);
				Fire(TEXT("idle"), Threshold);
			}
		}
	}

	// Events with their own timer, when some pack has a line for them.
	for (const TPair<FString, float>& Timed : Registry.GetTimedEvents())
	{
		if (Registry.FindLines(Timed.Key, 0.f).Num() == 0)
		{
			continue;
		}
		float* Countdown = EventCountdowns.Find(Timed.Key);
		if (!Countdown)
		{
			Countdown = &EventCountdowns.Add(Timed.Key, Timed.Value * FMath::FRandRange(RandomMinFraction, RandomMaxFraction));
		}
		*Countdown -= DeltaSeconds;
		if (*Countdown <= 0.f)
		{
			*Countdown = Timed.Value * FMath::FRandRange(RandomMinFraction, RandomMaxFraction);
			Fire(Timed.Key);
		}
	}

	for (const float Interval : Registry.GetTriggerArgs(TEXT("random")))
	{
		if (Interval <= 0.f)
		{
			continue;
		}
		float* Countdown = RandomCountdowns.Find(Interval);
		if (!Countdown)
		{
			Countdown = &RandomCountdowns.Add(Interval, Interval * FMath::FRandRange(RandomMinFraction, RandomMaxFraction));
		}
		*Countdown -= DeltaSeconds;
		if (*Countdown <= 0.f)
		{
			*Countdown = Interval * FMath::FRandRange(RandomMinFraction, RandomMaxFraction);
			Fire(TEXT("random"), Interval);
		}
	}
}

bool UAdaVoicePlayer::Fire(const FString& Name, float Arg)
{
	if (!bEnabled)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	FAdaPackRegistry& Registry = FAdaPackRegistry::Get();
	const FAdaEventRule Rule = Registry.GetEventRule(Name);
	const double* LastFired = EventLastFired.Find(Name);
	if (LastFired && Now - *LastFired < Rule.Cooldown)
	{
		return false;
	}
	TArray<FAdaLine*> Candidates;
	int32 TotalWeight = 0;
	for (FAdaLine* Line : Registry.FindLines(Name, Arg))
	{
		const float Cooldown = FMath::Max(Line->Cooldown, Line->bRare ? FAdaRareRule::Cooldown : 0.f);
		const bool bCoolingDown = Line->LastPlayed > 0.0 && Now - Line->LastPlayed < Cooldown;
		const bool bUsedUp = Line->bOncePerSession && Line->PlayCount > 0;
		if (!bCoolingDown && !bUsedUp)
		{
			Candidates.Add(Line);
			TotalWeight += Line->Weight;
		}
	}
	if (Candidates.Num() == 0)
	{
		return false;
	}
	int32 Pick = FMath::RandHelper(TotalWeight);
	FAdaLine* Chosen = Candidates.Last();
	for (FAdaLine* Line : Candidates)
	{
		Pick -= Line->Weight;
		if (Pick < 0)
		{
			Chosen = Line;
			break;
		}
	}
	const float Chance = Rule.Chance * Chosen->Chance * (Chosen->bRare ? FAdaRareRule::Chance : 1.f);
	if (FMath::FRand() > Chance)
	{
		UE_LOG(LogAdaVoicePacks, Verbose, TEXT("Event '%s': '%s' lost its %.0f%% chance roll"), *Name, *Chosen->EventName, Chance * 100.f);
		return false;
	}
	if ((IsSpeaking() || PendingMessage) && Rule.Priority >= AdaInterruptPriority && CurrentLine && CurrentPriority < Rule.Priority)
	{
		UE_LOG(LogAdaVoicePacks, Log, TEXT("Event '%s' (priority %d) interrupts '%s'"), *Name, Rule.Priority, *CurrentLine->EventName);
		StopLine();
	}
	UE_LOG(LogAdaVoicePacks, Log, TEXT("Event '%s' fired; %d eligible line(s), chose '%s'"), *Name, Candidates.Num(), *Chosen->EventName);
	CurrentPriority = Rule.Priority;
	if (!Queue(*Chosen, false))
	{
		return false;
	}
	EventLastFired.Add(Name, Now);
	return true;
}

bool UAdaVoicePlayer::PlayNow(const FString& PackId, const FString& LineId, FString& OutReason)
{
	FAdaLine* Line = FAdaPackRegistry::Get().FindLine(PackId, LineId);
	if (!Line)
	{
		OutReason = TEXT("no such line");
		return false;
	}
	if (!bEnabled)
	{
		OutReason = TEXT("voice lines are turned off in the mod's settings (Mods menu)");
		return false;
	}
	if (!Queue(*Line, true))
	{
		OutReason = LastRefusal;
		return false;
	}
	return true;
}

bool UAdaVoicePlayer::Queue(FAdaLine& Line, bool bForce)
{
	const double Now = FPlatformTime::Seconds();
	const FAdaPack* Pack = FAdaPackRegistry::Get().FindPack(Line);
	UFGGameUI* GameUI = GetGameUI();
	const TCHAR* Refusal = !Pack || !Pack->bBankLoaded ? TEXT("the pack's Wwise bank isn't loaded")
		: (IsSpeaking() || PendingMessage) ? TEXT("another line is playing")
		: !FPlatformApplicationMisc::IsThisApplicationForeground() ? TEXT("the game is in the background")
		: (!bForce && LastLineTime > 0.0 && Now - LastLineTime < FMath::Max(Pack->MinGapSeconds, LastGapSeconds)) ? TEXT("too soon after the last line")
		: (!bForce && GameUI && GameUI->GetActiveAudioMessage()) ? TEXT("one of the game's own messages is playing")
		: nullptr;
	if (Refusal)
	{
		const FString Reason = Line.EventName + TEXT(": ") + Refusal;
		if (Reason != LastRefusal)
		{
			LastRefusal = Reason;
			UE_LOG(LogAdaVoicePacks, Log, TEXT("'%s' triggered but not played (%s)"), *Line.EventName, Refusal);
		}
		return false;
	}
	LastRefusal.Reset();
	CurrentLine = &Line;
	Line.LastPlayed = Now;
	++Line.PlayCount;
	LastLineTime = Now;
	LastGapSeconds = Pack->MinGapSeconds;

	const TSubclassOf<UFGMessageSender> Sender = GetAdaSender();
	if (!GameUI || !Sender)
	{
		UE_LOG(LogAdaVoicePacks, Log, TEXT("No game message box; playing '%s' without it"), *Line.EventName);
		StartVoice(false);
		return true;
	}

	FString Text = Line.Subtitle;
	const UWorld* World = FindGameWorld();
	const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	const APlayerState* PlayerState = PlayerController ? PlayerController->PlayerState : nullptr;
	Text.ReplaceInline(TEXT("{name}"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("Pioneer"));

	// A legacy-style message: its dialogue has no Wwise audio, and the subtitle stays up for the clip's length.
	const FText Subtitle = FText::FromString(Text);
	FMessageSubtitle MessageSubtitle(Subtitle, Sender, 0.f, nullptr);
	MessageSubtitle.TimeOnScreen = Line.Seconds + BoxExtraSeconds;
	FMessageDialogue Dialogue;
	Dialogue.MessageSubtitles = { MessageSubtitle };
	UFGMessage* Message = NewObject<UFGMessage>(this);
	Message->mTitle = FText::FromString(TEXT("ADA"));
	Message->mMessage = Subtitle;
	Message->mSubtitles = { MessageSubtitle };
	Message->mMessageSharing = EMessageSharing::EMS_NeverShare;
	Message->mCanBeQueued = true;
	Message->mCanBeRepeated = true;
	Message->mCanInterrupt = false;
	Message->mPlayInterruptMessage = false;
	Message->mBlockedByCooldown = false;
	Message->mIsLegacyMessage = true;
	Message->mSenderClass = Sender.Get();
	Message->mDialogueData = { Dialogue };
	Message->mSubtitleTimeMultiplier = (Line.Seconds + BoxExtraSeconds) / FMath::Max(1, Text.Len());
	PendingMessage = Message;
	PendingSince = Now;
	GameUI->AddPendingMessages({ Message });
	UE_LOG(LogAdaVoicePacks, Log, TEXT("Queued '%s' (%.1f s) in the game's message box"), *Line.EventName, Line.Seconds);
	return true;
}

void UAdaVoicePlayer::NotifyAudioMessageStarted(UFGAudioMessage* AudioMessage)
{
	const UFGMessage* Message = AudioMessage ? AudioMessage->GetMessage() : nullptr;
	UAdaVoicePlayer* Player = Get();
	if (Message && Player && Player->PendingMessage == Message)
	{
		UE_LOG(LogAdaVoicePacks, Log, TEXT("Message box started '%s' %.1f s after queuing; starting the voice"), Player->CurrentLine ? *Player->CurrentLine->EventName : TEXT("?"), FPlatformTime::Seconds() - Player->PendingSince);
		Player->ShownBox = AudioMessage;
		Player->ShownBoxSince = FPlatformTime::Seconds();
		Player->StartVoice(true);
	}
	else if (Message && Player && Player->AbandonedMessages.Remove(const_cast<UFGMessage*>(Message)) > 0)
	{
		UE_LOG(LogAdaVoicePacks, Log, TEXT("Message box started a line that was stopped or played without it while queued; closing it"));
		AudioMessage->CancelPlayback();
	}
}

bool UAdaVoicePlayer::PostVoice()
{
	if (!CurrentLine || !AdaWwise::GetCalls().IsComplete())
	{
		return false;
	}
	// On the message box's own emitter, as the game plays ADA's lines, so her voice gets the same treatment.
	static FObjectPropertyBase* BoxEmitterProperty = FindFProperty<FObjectPropertyBase>(UFGAudioMessage::StaticClass(), TEXT("mAkAudioComponent"));
	UObject* BoxEmitter = BoxEmitterProperty && ShownBox.IsValid() ? BoxEmitterProperty->GetObjectPropertyValue_InContainer(ShownBox.Get()) : nullptr;
	const uint64 GameObject = BoxEmitter ? reinterpret_cast<uint64>(BoxEmitter) : VoiceGameObject;
	const uint32 EventId = AdaWwise::IdFromName(CurrentLine->EventName);
	uint32 PlayingId = AdaWwise::GetCalls().PostEvent(EventId, GameObject, 0, nullptr, nullptr, 0, nullptr, 0);
	if (PlayingId == 0 && BoxEmitter)
	{
		PlayingId = AdaWwise::GetCalls().PostEvent(EventId, VoiceGameObject, 0, nullptr, nullptr, 0, nullptr, 0);
	}
	if (PlayingId == 0)
	{
		UE_LOG(LogAdaVoicePacks, Warning, TEXT("Wwise wouldn't play '%s'"), *CurrentLine->EventName);
		return false;
	}
	WwisePlayingId = PlayingId;
	return true;
}

void UAdaVoicePlayer::StartVoice(bool bBoxShown)
{
	PendingMessage = nullptr;
	if (!PostVoice())
	{
		CurrentLine = nullptr;
		return;
	}
	LineTimeLeft = CurrentLine->Seconds + 0.5f;
	UE_LOG(LogAdaVoicePacks, Log, TEXT("Playing '%s' on ADA's Wwise voice bus (%.1f s%s)"), *CurrentLine->EventName, CurrentLine->Seconds, bBoxShown ? TEXT("") : TEXT(", no message box"));
}

void UAdaVoicePlayer::StopLine()
{
	if (PendingMessage)
	{
		AbandonedMessages.Add(PendingMessage.Get());
	}
	PendingMessage = nullptr;
	if (WwisePlayingId != 0)
	{
		// AkCurveInterpolation_Linear is 4.
		AdaWwise::GetCalls().StopPlayingID(WwisePlayingId, 200, 4);
		WwisePlayingId = 0;
	}
	CurrentLine = nullptr;
	LineTimeLeft = 0.f;
}

void UAdaVoicePlayer::RemoveFromPlayedMessages()
{
	UWorld* World = FindGameWorld();
	const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	int32 Removed = RemoveOwnMessages(PlayerController ? PlayerController->GetPlayerState<AFGPlayerState>() : nullptr, TEXT("mPlayedMessages"));
	if (World && World->GetNetMode() != NM_Client)
	{
		Removed += RemoveOwnMessages(AFGStorySubsystem::Get(World), TEXT("mAllPlayedMessages"));
	}
	if (Removed > 0)
	{
		UE_LOG(LogAdaVoicePacks, Log, TEXT("Took %d voice line message(s) out of the game's saved played messages"), Removed);
	}
}
