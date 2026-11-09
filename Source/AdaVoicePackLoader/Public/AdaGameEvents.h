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

	// Binds the managers' delegates once they exist in World.
	void BindDelegates(UWorld* World);

	void PollPlayer(UWorld* World, class AFGCharacterPlayer* Player);
	void PollWorld(UWorld* World, class AFGCharacterPlayer* Player);

	static bool IsLocalPlayer(const AActor* Actor);

	TWeakObjectPtr<UWorld> BoundWorld;
	bool bSchematicsBound = false;
	bool bBuildablesBound = false;

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
};
