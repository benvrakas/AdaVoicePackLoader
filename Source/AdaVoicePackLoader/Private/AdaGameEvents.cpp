#include "AdaGameEvents.h"
#include "AdaVoicePackLoaderModule.h"
#include "AdaVoicePlayer.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Creature/Enemy/FGEnemy.h"
#include "Creature/FGCreature.h"
#include "EngineUtils.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGCircuitSubsystem.h"
#include "FGHealthComponent.h"
#include "FGInventoryComponent.h"
#include "FGPowerCircuit.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "FGTimeSubsystem.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Patching/NativeHookManager.h"

namespace
{
	constexpr float PollSeconds = 0.5f;
	constexpr float WorldPollSeconds = 2.f;

	// Health below this fraction is "low health"; it has to climb back above the second one to count again.
	constexpr float LowHealthFraction = 0.25f;
	constexpr float RecoveredHealthFraction = 0.5f;

	// An enemy this close (cm) to the local player is a hostile zone.
	constexpr float HostileDistance = 6000.f;

	// Dismantling counts once this many buildables go within the window (seconds): a demolition, not one removal.
	constexpr int32 DemolitionCount = 4;
	constexpr double DemolitionWindow = 10.0;

	// A creature the player damaged this recently (seconds) was killed by them.
	constexpr double KillCredit = 30.0;

	// Fall damage this recently (seconds) before a death makes it a fall death.
	constexpr double FallDeathWindow = 1.5;

	// The cause of a death is the kind of damage the player took within this many seconds of it.
	constexpr double DeathCauseWindow = 2.0;

	// Away from the game at least this long (seconds) makes coming back a session resume.
	constexpr double ResumeAwaySeconds = 120.0;

	// Two MAM researches collected within this many seconds are an overload.
	constexpr double ResearchOverloadWindow = 60.0;

	// Production stall: factories are counted this often (seconds); at least MinFactories of them, with at least this
	// percentage stalled for StallPollsNeeded counts in a row, is a stall that ends once the percentage falls below
	// the second one.
	constexpr double StallPollSeconds = 15.0;
	constexpr int32 MinFactories = 10;
	constexpr int32 StallPercent = 60;
	constexpr int32 RecoveredPercent = 30;
	constexpr int32 StallPollsNeeded = 4;

	// Falling faster than this (cm/s) counts as a fall: a death or damage within FallDeathWindow after it is a fall's.
	constexpr float FastFallSpeed = 1500.f;

	// A creature this close (cm) to the player when they die or lose health is what hurt them.
	constexpr float CreatureThreatDistance = 1500.f;

	// A creature that dies this close (cm) to the local player counts as a kill on a client.
	constexpr float KillDistance = 4000.f;

	// On a client, a buildable removed this close (cm) to the player is a dismantle (farther ones are streaming out).
	constexpr float ClientDismantleDistance = 3000.f;

	// A second death within this many seconds of the last is the same death.
	constexpr double SameDeathSeconds = 5.0;

	// Damage types (by name) that are an environmental hazard.
	bool IsHazardDamage(const FString& Name)
	{
		for (const TCHAR* Word : { TEXT("Radiation"), TEXT("Gas"), TEXT("Fire"), TEXT("Burn"), TEXT("Drown"), TEXT("Lava"), TEXT("Poison"), TEXT("Fume") })
		{
			if (Name.Contains(Word))
			{
				return true;
			}
		}
		return false;
	}

	// Power circuit ids searched for tripped fuses.
	constexpr int32 MaxCircuitId = 512;

	void Fire(const TCHAR* Name)
	{
		UAdaVoicePlayer::Get()->Fire(Name);
	}
}

UAdaGameEvents* UAdaGameEvents::Get()
{
	static TWeakObjectPtr<UAdaGameEvents> Instance;
	if (!Instance.IsValid())
	{
		UAdaGameEvents* Created = NewObject<UAdaGameEvents>(GetTransientPackage());
		Created->AddToRoot();
		Instance = Created;
	}
	return Instance.Get();
}

bool UAdaGameEvents::IsLocalPlayer(const AActor* Actor)
{
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	return World && Actor == UGameplayStatics::GetPlayerPawn(World, 0);
}

void UAdaGameEvents::RegisterHooks()
{
	// These are virtual functions, so each hook resolves the implementation through the class's default object.
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterPlayer::OnTakeDamage, GetMutableDefault<AFGCharacterPlayer>(), [](AFGCharacterPlayer* Self, AActor*, float, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
	{
		NotifyPlayerDamaged(Self, DamageType, InstigatedBy, DamageCauser);
	});
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterPlayer::Died, GetMutableDefault<AFGCharacterPlayer>(), [](AFGCharacterPlayer* Self, AActor*)
	{
		NotifyPlayerDied(Self);
	});
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterBase::OnTakeDamage, GetMutableDefault<AFGCharacterBase>(), [](AFGCharacterBase* Self, AActor*, float, const UDamageType*, AController* InstigatedBy, AActor*)
	{
		NotifyCreatureDamaged(Self, InstigatedBy);
	});
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCreature::Died, GetMutableDefault<AFGCreature>(), [](AFGCreature* Self, AActor*)
	{
		NotifyCreatureDied(Self);
	});
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGBuildable::Dismantle_Implementation, GetMutableDefault<AFGBuildable>(), [](AFGBuildable* Self)
	{
		NotifyDismantled(Self);
	});
	UE_LOG(LogAdaVoicePacks, Log, TEXT("Game hooks installed: player damage and death, creature damage and death, dismantle"));
}

void UAdaGameEvents::NotifyPlayerDamaged(AFGCharacterPlayer* Player, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	if (!IsLocalPlayer(Player))
	{
		return;
	}
	UAdaGameEvents* Events = Get();
	const double Now = FPlatformTime::Seconds();
	const FString TypeName = DamageType ? DamageType->GetClass()->GetName() : FString();
	if (TypeName.Contains(TEXT("Fall")))
	{
		Events->LastFallDamage = Now;
	}
	else if (Cast<AFGCreature>(DamageCauser) || (InstigatedBy && Cast<AFGCreature>(InstigatedBy->GetPawn())))
	{
		Events->LastCreatureDamage = Now;
	}
	else if (IsHazardDamage(TypeName))
	{
		Events->LastHazardDamage = Now;
	}
	Fire(TEXT("damage"));
}

void UAdaGameEvents::NotifyPlayerDied(AFGCharacterPlayer* Player)
{
	if (!IsLocalPlayer(Player))
	{
		return;
	}
	UAdaGameEvents* Events = Get();
	const double Now = FPlatformTime::Seconds();
	Events->DeadPawn = Player;
	Events->OnPlayerDeath(Now - Events->LastFallDamage < DeathCauseWindow ? TEXT("fall_death")
		: Now - Events->LastCreatureDamage < DeathCauseWindow ? TEXT("creature_death")
		: Now - Events->LastHazardDamage < DeathCauseWindow ? TEXT("hazard_death")
		: nullptr);
}

void UAdaGameEvents::OnPlayerDeath(const TCHAR* Cause)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastDeathHandled < SameDeathSeconds)
	{
		return;
	}
	LastDeathHandled = Now;
	bPendingRespawn = true;
	UE_LOG(LogAdaVoicePacks, Log, TEXT("The local player died (%s)"), Cause ? Cause : TEXT("other"));
	// A pack without a line for the specific cause still gets its plain death lines.
	if (!Cause || !UAdaVoicePlayer::Get()->Fire(Cause))
	{
		Fire(TEXT("death"));
	}
}

void UAdaGameEvents::NotifyCreatureDamaged(AActor* Damaged, AController* InstigatedBy)
{
	const UWorld* World = Damaged ? Damaged->GetWorld() : nullptr;
	if (!Cast<AFGCreature>(Damaged) || !World || InstigatedBy != UGameplayStatics::GetPlayerController(World, 0))
	{
		return;
	}
	Get()->DamagedByPlayer.Add(Damaged, FPlatformTime::Seconds());
}

void UAdaGameEvents::NotifyCreatureDied(AFGCreature* Creature)
{
	UAdaGameEvents* Events = Get();
	const double* Hit = Events->DamagedByPlayer.Find(Creature);
	if (!Hit || FPlatformTime::Seconds() - *Hit > KillCredit)
	{
		return;
	}
	Events->DamagedByPlayer.Remove(Creature);
	Fire(Creature->GetClass()->GetName().Contains(TEXT("Doggo")) ? TEXT("doggo_kill") : TEXT("creature_kill"));
}

void UAdaGameEvents::NotifyDismantled(AFGBuildable* Buildable)
{
	UAdaGameEvents* Events = Get();
	const double Now = FPlatformTime::Seconds();
	Events->DismantleTimes.Add(Now);
	Events->DismantleTimes.RemoveAll([Now](double Time) { return Now - Time > DemolitionWindow; });
	if (Events->DismantleTimes.Num() >= DemolitionCount)
	{
		Events->DismantleTimes.Reset();
		Fire(TEXT("building_dismantled"));
	}
}

void UAdaGameEvents::OnSchematicPurchased(TSubclassOf<UFGSchematic> Schematic)
{
	if (!Schematic)
	{
		return;
	}
	const ESchematicType Type = UFGSchematic::GetType(Schematic);
	const FString Name = Schematic->GetName();
	UE_LOG(LogAdaVoicePacks, Log, TEXT("Schematic purchased: %s (type %d)"), *Name, static_cast<int32>(Type));
	switch (Type)
	{
	case ESchematicType::EST_MAM:
	{
		const double Now = FPlatformTime::Seconds();
		ResearchTimes.RemoveAll([Now](double Time) { return Now - Time > ResearchOverloadWindow; });
		ResearchTimes.Add(Now);
		Fire(ResearchTimes.Num() >= 2 ? TEXT("research_skip") : TEXT("research_complete"));
		break;
	}
	case ESchematicType::EST_Milestone:
		Fire(Name.Contains(TEXT("Elevator")) || Name.Contains(TEXT("GamePhase")) ? TEXT("space_elevator") : TEXT("milestone"));
		break;
	case ESchematicType::EST_ResourceSink:
		Fire(TEXT("coupon"));
		break;
	case ESchematicType::EST_HardDrive:
		Fire(TEXT("hard_drive"));
		break;
	default:
		break;
	}
}

void UAdaGameEvents::OnBuildableConstructed(AFGBuildable* Buildable)
{
	// A pack without a line for production buildings still gets its plain building lines.
	if (Cast<AFGBuildableFactory>(Buildable) && UAdaVoicePlayer::Get()->Fire(TEXT("production_building_placed")))
	{
		return;
	}
	Fire(TEXT("building_placed"));
}

void UAdaGameEvents::OnBuildableRemoved(AFGBuildable* Buildable)
{
	const UWorld* World = Buildable ? Buildable->GetWorld() : nullptr;
	const APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	if (Pawn && !World->bIsTearingDown && FVector::Dist(Buildable->GetActorLocation(), Pawn->GetActorLocation()) < ClientDismantleDistance)
	{
		NotifyDismantled(Buildable);
	}
}

void UAdaGameEvents::BindDelegates(UWorld* World)
{
	if (BoundWorld.Get() != World)
	{
		BoundWorld = World;
		bSchematicsBound = false;
		bBuildablesBound = false;
		bRemovalsBound = false;
		CountedKills.Reset();
		LastHealth = -1.f;
		bWasDead = false;
		bWasDay.Reset();
		TriggeredFuses.Reset();
		DamagedByPlayer.Reset();
	}
	if (!bSchematicsBound)
	{
		if (AFGSchematicManager* Schematics = AFGSchematicManager::Get(World))
		{
			Schematics->PurchasedSchematicDelegate.AddDynamic(this, &UAdaGameEvents::OnSchematicPurchased);
			bSchematicsBound = true;
			UE_LOG(LogAdaVoicePacks, Log, TEXT("Bound to the schematic manager"));
		}
	}
	if (!bRemovalsBound && World->GetNetMode() == NM_Client)
	{
		if (AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World))
		{
			Buildables->mBuildableRemovedDelegate.AddDynamic(this, &UAdaGameEvents::OnBuildableRemoved);
			bRemovalsBound = true;
			UE_LOG(LogAdaVoicePacks, Log, TEXT("Client: watching removed buildables for dismantles"));
		}
	}
	if (!bBuildablesBound)
	{
		if (AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World))
		{
			Buildables->BuildableConstructedGlobalDelegate.AddDynamic(this, &UAdaGameEvents::OnBuildableConstructed);
			bBuildablesBound = true;
			UE_LOG(LogAdaVoicePacks, Log, TEXT("Bound to the buildable subsystem"));
		}
	}
}

void UAdaGameEvents::Tick(UWorld* World, float DeltaSeconds)
{
	BindDelegates(World);
	const double Now = FPlatformTime::Seconds();
	if (!FPlatformApplicationMisc::IsThisApplicationForeground())
	{
		if (BackgroundSince == 0.0)
		{
			BackgroundSince = Now;
		}
	}
	else if (BackgroundSince > 0.0)
	{
		const double Away = Now - BackgroundSince;
		BackgroundSince = 0.0;
		if (Away >= ResumeAwaySeconds)
		{
			UE_LOG(LogAdaVoicePacks, Log, TEXT("Back in the game after %.0f s"), Away);
			Fire(TEXT("session_resume"));
		}
	}
	AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(UGameplayStatics::GetPlayerPawn(World, 0));
	if (!Player)
	{
		return;
	}
	PollTimer -= DeltaSeconds;
	if (PollTimer <= 0.f)
	{
		PollTimer = PollSeconds;
		PollPlayer(World, Player);
	}
	WorldPollTimer -= DeltaSeconds;
	if (WorldPollTimer <= 0.f)
	{
		WorldPollTimer = WorldPollSeconds;
		PollWorld(World, Player);
	}
}

void UAdaGameEvents::WatchHealth(UWorld* World, AFGCharacterPlayer* Player)
{
	const UFGHealthComponent* Health = Player->GetHealthComponent();
	if (!Health)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Player->GetVelocity().Z < -FastFallSpeed)
	{
		LastFastFall = Now;
	}
	static const FFloatProperty* RadiationProperty = FindFProperty<FFloatProperty>(AFGCharacterPlayer::StaticClass(), TEXT("mRadiationIntensity"));
	const bool bInRadiation = RadiationProperty && RadiationProperty->GetPropertyValue_InContainer(Player) > KINDA_SMALL_NUMBER;
	bool bCreatureNear = false;
	const bool bNeedCreatures = (LastHealth >= 0.f && Health->GetCurrentHealth() < LastHealth) || Health->IsDead();
	if (bNeedCreatures)
	{
		for (TActorIterator<AFGCreature> It(World); It && !bCreatureNear; ++It)
		{
			const UFGHealthComponent* Other = It->GetHealthComponent();
			bCreatureNear = !(Other && Other->IsDead()) && FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) < CreatureThreatDistance;
		}
	}
	const float Current = Health->GetCurrentHealth();
	if (LastHealth >= 0.f && Current < LastHealth - KINDA_SMALL_NUMBER)
	{
		// Health lost since the last poll: the same kinds of damage the game's damage function reports.
		if (Now - LastFastFall < FallDeathWindow)
		{
			LastFallDamage = Now;
		}
		else if (bCreatureNear)
		{
			LastCreatureDamage = Now;
		}
		else if (bInRadiation)
		{
			LastHazardDamage = Now;
		}
		if (!Health->IsDead())
		{
			Fire(TEXT("damage"));
		}
	}
	LastHealth = Current;
	const bool bDead = Health->IsDead();
	if (bDead && !bWasDead)
	{
		DeadPawn = Player;
		OnPlayerDeath(Now - LastFallDamage < DeathCauseWindow || Now - LastFastFall < FallDeathWindow ? TEXT("fall_death")
			: Now - LastCreatureDamage < DeathCauseWindow || bCreatureNear ? TEXT("creature_death")
			: Now - LastHazardDamage < DeathCauseWindow || bInRadiation ? TEXT("hazard_death")
			: nullptr);
	}
	bWasDead = bDead;
}

void UAdaGameEvents::WatchCreatureDeaths(UWorld* World, AFGCharacterPlayer* Player)
{
	for (auto It = CountedKills.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (TActorIterator<AFGCreature> It(World); It; ++It)
	{
		const UFGHealthComponent* Health = It->GetHealthComponent();
		if (Health && Health->IsDead() && !CountedKills.Contains(*It) && FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) < KillDistance)
		{
			CountedKills.Add(*It);
			Fire(It->GetClass()->GetName().Contains(TEXT("Doggo")) ? TEXT("doggo_kill") : TEXT("creature_kill"));
		}
	}
}

void UAdaGameEvents::PollPlayer(UWorld* World, AFGCharacterPlayer* Player)
{
	WatchHealth(World, Player);
	if (bPendingRespawn && Player != DeadPawn.Get())
	{
		bPendingRespawn = false;
		DeadPawn = nullptr;
		Fire(TEXT("respawn"));
	}

	if (const UFGHealthComponent* Health = Player->GetHealthComponent())
	{
		const float Fraction = Health->GetMaxHealth() > 0.f ? Health->GetCurrentHealth() / Health->GetMaxHealth() : 1.f;
		if (!bLowHealth && Fraction > 0.f && Fraction < LowHealthFraction)
		{
			bLowHealth = true;
			Fire(TEXT("low_health"));
		}
		else if (bLowHealth && Fraction > RecoveredHealthFraction)
		{
			bLowHealth = false;
		}
	}

	// The player's radiation level is a protected property, read by name.
	static const FFloatProperty* RadiationProperty = FindFProperty<FFloatProperty>(AFGCharacterPlayer::StaticClass(), TEXT("mRadiationIntensity"));
	const bool bInRadiation = RadiationProperty && RadiationProperty->GetPropertyValue_InContainer(Player) > KINDA_SMALL_NUMBER;
	if (bInRadiation && !bRadiation)
	{
		Fire(TEXT("radiation"));
	}
	bRadiation = bInRadiation;

	const UFGInventoryComponent* Inventory = Player->GetInventory();
	const bool bFull = Inventory && Inventory->FindEmptyIndex() == INDEX_NONE;
	if (bFull && !bInventoryFull)
	{
		Fire(TEXT("inventory_full"));
	}
	bInventoryFull = bFull;

	const UFGInventoryComponent* Trash = Player->GetTrashSlot();
	const bool bFilled = Trash && !Trash->IsEmpty();
	if (bFilled && !bTrashFilled)
	{
		Fire(TEXT("item_discard"));
	}
	bTrashFilled = bFilled;
}

void UAdaGameEvents::PollWorld(UWorld* World, AFGCharacterPlayer* Player)
{
	if (AFGTimeOfDaySubsystem* Time = AFGTimeOfDaySubsystem::Get(World))
	{
		const bool bDay = Time->IsDay();
		if (bWasDay.IsSet() && bWasDay.GetValue() && !bDay)
		{
			Fire(TEXT("nightfall"));
		}
		bWasDay = bDay;
	}

	// On a client the game's creature death function doesn't run, so deaths near the player are counted from the
	// replicated health instead.
	if (World->GetNetMode() == NM_Client)
	{
		WatchCreatureDeaths(World, Player);
	}

	bool bHostile = false;
	for (TActorIterator<AFGEnemy> It(World); It && !bHostile; ++It)
	{
		bHostile = FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) < HostileDistance;
	}
	if (bHostile && !bHostileNear)
	{
		Fire(TEXT("hostile_zone"));
	}
	bHostileNear = bHostile;

	// Circuits and their fuse state are replicated, so a client sees trips too.
	{
		if (const AFGCircuitSubsystem* Circuits = AFGCircuitSubsystem::GetCircuitSubsystem(World))
		{
			for (int32 Id = 0; Id < MaxCircuitId; ++Id)
			{
				UFGPowerCircuit* Circuit = Cast<UFGPowerCircuit>(Circuits->FindCircuit(Id));
				if (!Circuit)
				{
					continue;
				}
				if (Circuit->IsFuseTriggered())
				{
					if (!TriggeredFuses.Contains(Id))
					{
						TriggeredFuses.Add(Id);
						Fire(TEXT("power_failure"));
					}
				}
				else if (TriggeredFuses.Remove(Id) > 0)
				{
					Fire(TEXT("power_restored"));
				}
			}
		}
	}

	// A client only has the factories near it, so its count is of those.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastStallPoll >= StallPollSeconds)
	{
		LastStallPoll = Now;
		int32 Total = 0;
		int32 Stalled = 0;
		for (TActorIterator<AFGBuildableManufacturer> It(World); It; ++It)
		{
			const EProductionStatus Status = It->GetProductionIndicatorStatus();
			if (Status == EProductionStatus::IS_NONE)
			{
				continue;
			}
			++Total;
			Stalled += (Status == EProductionStatus::IS_STANDBY || Status == EProductionStatus::IS_ERROR) ? 1 : 0;
		}
		if (++StallPollCount % 20 == 1)
		{
			UE_LOG(LogAdaVoicePacks, Log, TEXT("Factory count: %d factories, %d idle or blocked (%s)"), Total, Stalled, World->GetNetMode() == NM_Client ? TEXT("client, nearby only") : TEXT("all"));
		}
		if (Total >= MinFactories && Stalled * 100 >= Total * StallPercent)
		{
			if (++StallPolls >= StallPollsNeeded && !bStalled)
			{
				bStalled = true;
				UE_LOG(LogAdaVoicePacks, Log, TEXT("Production stall: %d of %d factories stalled"), Stalled, Total);
				Fire(TEXT("production_stall"));
			}
		}
		else if (Total < MinFactories || Stalled * 100 < Total * RecoveredPercent)
		{
			StallPolls = 0;
			bStalled = false;
		}
	}
}
