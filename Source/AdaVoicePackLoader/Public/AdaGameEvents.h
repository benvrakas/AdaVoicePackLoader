#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AdaGameEvents.generated.h"

class AFGBuildable;
class AFGCharacterPlayer;
class AFGCreature;
class UFGSchematic;

// Detects what happens in the game and fires the matching event name at UAdaVoicePlayer (see Docs/Triggers.md): game
// hooks (death, damage, kills, dismantling), delegates on the game's managers (schematics, construction) and a slow poll
// of the local player and the world (health, radiation, inventory, night, enemies, power fuses).
UCLASS()
class ADAVOICEPACKLOADER_API UAdaGameEvents : public UObject
{
	GENERATED_BODY()

public:
	static UAdaGameEvents* Get();

	// Installs the game hooks. Call once at module startup.
	static void RegisterHooks();

	// Called every frame with the game's local world.
	void Tick(UWorld* World, float DeltaSeconds);

	static void NotifyPlayerDamaged(AFGCharacterPlayer* Player, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);
	static void NotifyPlayerDied(AFGCharacterPlayer* Player);
	static void NotifyCreatureDamaged(AActor* Damaged, AController* InstigatedBy);
	static void NotifyCreatureDied(AFGCreature* Creature);
	static void NotifyDismantled(AFGBuildable* Buildable);

private:
	UFUNCTION()
	void OnSchematicPurchased(TSubclassOf<UFGSchematic> Schematic);

	UFUNCTION()
	void OnBuildableConstructed(AFGBuildable* Buildable);

	// A buildable left the world; on a client this is how a dismantle near the player is seen.
	UFUNCTION()
	void OnBuildableRemoved(AFGBuildable* Buildable);

	// The local player died: fires the event for Cause (null for a plain death) unless this death was already handled.
	void OnPlayerDeath(const TCHAR* Cause);

	// Works out damage and deaths from replicated state (health, velocity, radiation, nearby creatures), which is how a
	// client sees them when the game's damage and death functions only run on the server.
	void WatchHealth(UWorld* World, AFGCharacterPlayer* Player);
	void WatchCreatureDeaths(UWorld* World, AFGCharacterPlayer* Player);

	// Binds the managers' delegates once they exist in World.
	void BindDelegates(UWorld* World);

	void PollPlayer(UWorld* World, class AFGCharacterPlayer* Player);
	void PollWorld(UWorld* World, class AFGCharacterPlayer* Player);

	static bool IsLocalPlayer(const AActor* Actor);

	TWeakObjectPtr<UWorld> BoundWorld;
	bool bSchematicsBound = false;
	bool bBuildablesBound = false;
	bool bRemovalsBound = false;

	float PollTimer = 0.f;
	float WorldPollTimer = 0.f;

	bool bLowHealth = false;
	bool bRadiation = false;
	bool bInventoryFull = false;
	bool bTrashFilled = false;
	bool bHostileNear = false;
	TOptional<bool> bWasDay;
	TSet<int32> TriggeredFuses;
	TArray<double> DismantleTimes;

	// Creatures the local player damaged recently, and when.
	TMap<TWeakObjectPtr<AActor>, double> DamagedByPlayer;

	// When the local player last took damage of each kind, to tell how they died.
	double LastFallDamage = 0.0;
	double LastCreatureDamage = 0.0;
	double LastHazardDamage = 0.0;

	// Health as last polled, whether the local player was dead, when a death was last handled, and when the player was
	// last falling fast (cm/s downwards).
	float LastHealth = -1.f;
	bool bWasDead = false;
	double LastDeathHandled = 0.0;
	double LastFastFall = 0.0;

	// Creatures already counted as killed (client only).
	TSet<TWeakObjectPtr<AActor>> CountedKills;

	// The pawn that died, until a different one exists (the respawn).
	TWeakObjectPtr<APawn> DeadPawn;
	bool bPendingRespawn = false;

	// When the game window went to the background (0 while it is in front).
	double BackgroundSince = 0.0;

	// MAM researches collected recently, for the overload.
	TArray<double> ResearchTimes;

	// Production stall: when factories were last counted, and how many counts in a row found most of them stalled.
	double LastStallPoll = 0.0;
	int32 StallPolls = 0;
	bool bStalled = false;
	int32 StallPollCount = 0;
};
