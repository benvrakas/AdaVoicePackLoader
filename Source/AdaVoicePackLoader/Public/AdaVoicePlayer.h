#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AdaVoicePlayer.generated.h"

struct FAdaLine;
class UFGAudioMessage;
class UFGGameUI;
class UFGMessage;
class UFGMessageSender;

// Plays pack lines for the local player. A line is queued in the game's message box as a runtime UFGMessage from ADA's
// sender (her box and jingle); when the box starts it, the line's Wwise event is posted on the box's emitter, so ADA's
// own voice bus mixes it. Triggers (named events) pick a line of any installed pack; see Docs/Triggers.md.
UCLASS()
class ADAVOICEPACKLOADER_API UAdaVoicePlayer : public UObject
{
	GENERATED_BODY()

public:
	// The player object, created and kept alive on first use.
	static UAdaVoicePlayer* Get();

	void Tick(float DeltaSeconds);

	// Called from a hook whenever the game's message box starts playing a message.
	static void NotifyAudioMessageStarted(UFGAudioMessage* AudioMessage);

	// A trigger named Name fired (Arg selects the lines written "Name:Arg"; 0 selects every line of that name). Picks one
	// eligible line of any pack and plays it. Returns true if a line was queued.
	bool Fire(const FString& Name, float Arg = 0.f);

	// Plays a specific line now, ignoring the gaps, chance, cooldown and once-per-session rule (the /adapack command).
	bool PlayNow(const FString& PackId, const FString& LineId, FString& OutReason);

private:
	// Queues Line in the message box (or posts it directly without one). Returns false, with the reason logged, if a
	// line or a game message is already playing, the game is in the background or the line's pack has no bank.
	bool Queue(FAdaLine& Line, bool bForce);

	void StartVoice(bool bBoxShown);
	void StopLine();
	bool PostVoice();

	UFGGameUI* GetGameUI() const;
	UWorld* GetWorld() const override;
	TSubclassOf<UFGMessageSender> GetAdaSender();

	// Takes the mod's messages out of the game's saved lists of played messages.
	void RemoveFromPlayedMessages();

	// The idle, random and world-loaded triggers.
	void PollTriggers(float DeltaSeconds);

	bool IsSpeaking() const { return WwisePlayingId != 0; }

	UPROPERTY()
	TObjectPtr<UFGMessage> PendingMessage;

	UPROPERTY()
	TSubclassOf<UFGMessageSender> AdaSender;

	TWeakObjectPtr<UFGAudioMessage> ShownBox;
	double ShownBoxSince = 0.0;

	// The line queued or playing.
	FAdaLine* CurrentLine = nullptr;
	int32 CurrentPriority = 0;
	TMap<FString, double> EventLastFired;
	double PendingSince = 0.0;
	float LineTimeLeft = 0.f;
	uint32 WwisePlayingId = 0;

	// FPlatformTime::Seconds() when the last line was queued.
	double LastLineTime = 0.0;
	// Gap rule of the pack that played last.
	float LastGapSeconds = 0.f;

	FString LastRefusal;
	float PlayedCleanupTimer = 0.f;

	// World-loaded and idle trigger state.
	TWeakObjectPtr<UWorld> TrackedWorld;
	double WorldSeenSince = 0.0;
	bool bWorldLoadedFired = false;
	FVector IdleLocation = FVector::ZeroVector;
	FRotator IdleRotation = FRotator::ZeroRotator;
	float IdleSeconds = 0.f;
	TArray<float> IdleFired;

	// Seconds until the next firing of each "random:<seconds>" trigger, by argument.
	TMap<float, float> RandomCountdowns;

	bool bBanksTried = false;
};
