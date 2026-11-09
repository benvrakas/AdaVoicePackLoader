#include "AdaGameEvents.h"
#include "AdaVoicePackLoaderModule.h"
#include "AdaVoicePlayer.h"
#include "Buildables/FGBuildable.h"
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
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterPlayer::OnTakeDamage, GetMutableDefault<AFGCharacterPlayer>(), [](AFGCharacterPlayer* Self, AActor*, float, const UDamageType* DamageType, AController*, AActor*)
	{
		NotifyPlayerDamaged(Self, DamageType);
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

void UAdaGameEvents::NotifyPlayerDamaged(AFGCharacterPlayer* Player, const UDamageType* DamageType)
{
	if (!IsLocalPlayer(Player))
	{
		return;
	}
	UAdaGameEvents* Events = Get();
	if (DamageType && DamageType->GetClass()->GetName().Contains(TEXT("Fall")))
	{
		Events->LastFallDamage = FPlatformTime::Seconds();
	}
	Fire(TEXT("damage"));
}

void UAdaGameEvents::NotifyPlayerDied(AFGCharacterPlayer* Player)
{
	if (!IsLocalPlayer(Player))
	{
		return;
	}
	const bool bFall = FPlatformTime::Seconds() - Get()->LastFallDamage < FallDeathWindow;
	UE_LOG(LogAdaVoicePacks, Log, TEXT("The local player died (%s)"), bFall ? TEXT("fall") : TEXT("other"));
	if (!bFall || !UAdaVoicePlayer::Get()->Fire(TEXT("fall_death")))
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
		Fire(TEXT("research_complete"));
		break;
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
	Fire(TEXT("building_placed"));
}

void UAdaGameEvents::BindDelegates(UWorld* World)
{
	if (BoundWorld.Get() != World)
	{
		BoundWorld = World;
		bSchematicsBound = false;
		bBuildablesBound = false;
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

void UAdaGameEvents::PollPlayer(UWorld* World, AFGCharacterPlayer* Player)
{
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

	// Fuses are only known on the machine that runs the factory (single-player or the host).
	if (World->GetNetMode() != NM_Client)
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
				else
				{
					TriggeredFuses.Remove(Id);
				}
			}
		}
	}
}
