#pragma once

#include "CoreMinimal.h"

// One voice line of a pack, from the pack's pack.json.
struct FAdaLine
{
	FString PackId;
	FString Id;
	// Text shown in ADA's message box; "{name}" is replaced by the player's name.
	FString Subtitle;
	// Length of the voice clip.
	float Seconds = 0.f;
	// Events that can play this line: "name" or "name:argument" ("idle:300" is name "idle", argument 300).
	TArray<FString> Triggers;
	// Relative chance of being picked among the eligible lines of an event.
	int32 Weight = 1;
	// True for the pack's rare pool: long cooldown and a lower chance (FAdaRareRule).
	bool bRare = false;
	// Chance (0..1) that a firing trigger actually plays this line.
	float Chance = 1.f;
	// Seconds before this line may play again; 0 for none.
	float Cooldown = 0.f;
	// True if the line plays at most once per game session.
	bool bOncePerSession = false;
	FString EventName;

	// FPlatformTime::Seconds() when the line last played; 0 if it hasn't this session.
	double LastPlayed = 0.0;
	int32 PlayCount = 0;
};

// How an event behaves, from a pack's "events" (defaults for events a pack doesn't list: priority 50, no cooldown).
struct FAdaEventRule
{
	// An event with priority at least AdaInterruptPriority cuts off a lower-priority line that is playing.
	int32 Priority = 50;
	float Cooldown = 0.f;
	// Chance (0..1) that a firing event plays anything.
	float Chance = 1.f;
	// If above 0 the loader fires the event by itself at random times about this many seconds apart (0.5x to 1.5x),
	// as long as an installed pack has a line for it.
	float Interval = 0.f;
};

// Applies to every rare-pool line.
namespace FAdaRareRule
{
	constexpr float Cooldown = 1800.f;
	constexpr float Chance = 0.25f;
}

constexpr int32 AdaInterruptPriority = 90;

// One content pack: a mod whose Resources/AdaPack holds pack.json and the Wwise bank with its lines.
struct FAdaPack
{
	FString Id;
	FString Name;
	FString BankPath;
	// Shortest time between two lines of this pack.
	float MinGapSeconds = 60.f;
	bool bBankLoaded = false;
	TArray<FAdaLine> Lines;
};

// The installed packs, found by scanning every enabled plugin for Resources/AdaPack/pack.json.
class FAdaPackRegistry
{
public:
	static FAdaPackRegistry& Get();

	// Reads every pack.json once. Does nothing on later calls.
	void Discover();

	// Loads each pack's bank into Wwise once. Call when the game's sound engine is running.
	void LoadBanks();

	TArray<FAdaPack>& GetPacks() { return Packs; }

	// The rule of event Name (a default one if no pack lists it).
	FAdaEventRule GetEventRule(const FString& Name) const;

	// Events that fire on their own timer, with their average interval.
	TArray<TPair<FString, float>> GetTimedEvents() const;

	// All lines with a trigger Name (and whose argument is Arg, unless Arg is 0).
	TArray<FAdaLine*> FindLines(const FString& Name, float Arg);

	// The line PackId/LineId, or null.
	FAdaLine* FindLine(const FString& PackId, const FString& LineId);

	// The pack that owns Line, or null.
	const FAdaPack* FindPack(const FAdaLine& Line) const;

	// Distinct arguments of the lines with trigger Name.
	TArray<float> GetTriggerArgs(const FString& Name) const;

private:
	TArray<FAdaPack> Packs;
	TMap<FString, FAdaEventRule> Events;
	bool bDiscovered = false;
	bool bBanksLoaded = false;
};
