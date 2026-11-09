#include "AdaPackRegistry.h"
#include "AdaVoicePackLoaderModule.h"
#include "AdaWwise.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 SupportedFormat = 1;

	void ParseTrigger(const FString& Trigger, FString& OutName, float& OutArg)
	{
		FString Arg;
		if (!Trigger.Split(TEXT(":"), &OutName, &Arg))
		{
			OutName = Trigger;
			OutArg = 0.f;
			return;
		}
		OutArg = FCString::Atof(*Arg);
	}

	bool MatchesTrigger(const FAdaLine& Line, const FString& Name, float Arg)
	{
		for (const FString& Trigger : Line.Triggers)
		{
			FString TriggerName;
			float TriggerArg = 0.f;
			ParseTrigger(Trigger, TriggerName, TriggerArg);
			if (TriggerName.Equals(Name, ESearchCase::IgnoreCase) && (Arg == 0.f || FMath::IsNearlyEqual(TriggerArg, Arg)))
			{
				return true;
			}
		}
		return false;
	}

	bool ReadPack(const FString& Path, FAdaPack& OutPack)
	{
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Json, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
		{
			UE_LOG(LogAdaVoicePacks, Warning, TEXT("Pack: can't read '%s'"), *Path);
			return false;
		}
		int32 Format = 0;
		Root->TryGetNumberField(TEXT("format"), Format);
		FString BankName;
		FString BankFile;
		if (Format != SupportedFormat || !Root->TryGetStringField(TEXT("id"), OutPack.Id) || !Root->TryGetStringField(TEXT("bankName"), BankName)
			|| !Root->TryGetStringField(TEXT("bank"), BankFile))
		{
			UE_LOG(LogAdaVoicePacks, Warning, TEXT("Pack: '%s' has format %d (this loader reads %d) or lacks id/bankName/bank"), *Path, Format, SupportedFormat);
			return false;
		}
		Root->TryGetStringField(TEXT("name"), OutPack.Name);
		double Gap = OutPack.MinGapSeconds;
		Root->TryGetNumberField(TEXT("minGapSeconds"), Gap);
		OutPack.MinGapSeconds = static_cast<float>(Gap);
		OutPack.BankPath = FPaths::GetPath(Path) / BankFile;

		const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
		Root->TryGetArrayField(TEXT("lines"), Lines);
		for (const TSharedPtr<FJsonValue>& Value : Lines ? *Lines : TArray<TSharedPtr<FJsonValue>>())
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			FAdaLine Line;
			if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Line.Id))
			{
				continue;
			}
			Line.PackId = OutPack.Id;
			(*Object)->TryGetStringField(TEXT("subtitle"), Line.Subtitle);
			double Number = 0.0;
			if ((*Object)->TryGetNumberField(TEXT("seconds"), Number)) { Line.Seconds = static_cast<float>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("chance"), Number)) { Line.Chance = static_cast<float>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("cooldown"), Number)) { Line.Cooldown = static_cast<float>(Number); }
			(*Object)->TryGetBoolField(TEXT("once"), Line.bOncePerSession);
			FString Trigger;
			if ((*Object)->TryGetStringField(TEXT("trigger"), Trigger) && !Trigger.IsEmpty())
			{
				Line.Triggers.Add(Trigger);
			}
			const TArray<TSharedPtr<FJsonValue>>* Triggers = nullptr;
			if ((*Object)->TryGetArrayField(TEXT("triggers"), Triggers))
			{
				for (const TSharedPtr<FJsonValue>& Item : *Triggers)
				{
					Line.Triggers.Add(Item->AsString());
				}
			}
			int32 Weight = 1;
			if ((*Object)->TryGetNumberField(TEXT("weight"), Weight))
			{
				Line.Weight = FMath::Max(1, Weight);
			}
			FString Pool;
			Line.bRare = (*Object)->TryGetStringField(TEXT("pool"), Pool) && Pool == TEXT("rare");
			Line.EventName = BankName + TEXT("_") + Line.Id;
			OutPack.Lines.Add(MoveTemp(Line));
		}
		return true;
	}

	void ReadEvents(const FString& Path, TMap<FString, FAdaEventRule>& OutEvents)
	{
		FString Json;
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Events = nullptr;
		if (!FFileHelper::LoadFileToString(Json, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root || !Root->TryGetObjectField(TEXT("events"), Events))
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Events)->Values)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Pair.Value->TryGetObject(Object))
			{
				continue;
			}
			FAdaEventRule Rule;
			double Number = 0.0;
			if ((*Object)->TryGetNumberField(TEXT("priority"), Number)) { Rule.Priority = static_cast<int32>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("cooldown"), Number)) { Rule.Cooldown = static_cast<float>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("chance"), Number)) { Rule.Chance = static_cast<float>(Number); }
			if ((*Object)->TryGetNumberField(TEXT("interval"), Number)) { Rule.Interval = static_cast<float>(Number); }
			OutEvents.Add(Pair.Key, Rule);
		}
	}
}

FAdaPackRegistry& FAdaPackRegistry::Get()
{
	static FAdaPackRegistry Registry;
	return Registry;
}

void FAdaPackRegistry::Discover()
{
	if (bDiscovered)
	{
		return;
	}
	bDiscovered = true;
	// The loader's own event rules are the defaults; a pack's "events" override them.
	if (const TSharedPtr<IPlugin> Loader = IPluginManager::Get().FindPlugin(TEXT("AdaVoicePackLoader")))
	{
		const FString RulesPath = FPaths::ConvertRelativePathToFull(Loader->GetBaseDir() / TEXT("Resources") / TEXT("event_rules.json"));
		ReadEvents(RulesPath, Events);
		UE_LOG(LogAdaVoicePacks, Log, TEXT("%d event rule(s) from '%s'"), Events.Num(), *RulesPath);
	}
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		const FString Path = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("AdaPack") / TEXT("pack.json"));
		if (!FPaths::FileExists(Path))
		{
			continue;
		}
		FAdaPack Pack;
		if (!ReadPack(Path, Pack))
		{
			continue;
		}
		if (Packs.ContainsByPredicate([&Pack](const FAdaPack& Other) { return Other.Id == Pack.Id; }))
		{
			UE_LOG(LogAdaVoicePacks, Warning, TEXT("Pack '%s' (%s) has the same id as another pack; ignored"), *Pack.Id, *Plugin->GetName());
			continue;
		}
		ReadEvents(Path, Events);
		UE_LOG(LogAdaVoicePacks, Log, TEXT("Pack '%s' from %s: %d line(s)"), *Pack.Id, *Plugin->GetName(), Pack.Lines.Num());
		Packs.Add(MoveTemp(Pack));
	}
	UE_LOG(LogAdaVoicePacks, Log, TEXT("%d voice pack(s) installed"), Packs.Num());
}

void FAdaPackRegistry::LoadBanks()
{
	if (bBanksLoaded)
	{
		return;
	}
	bBanksLoaded = true;
	for (FAdaPack& Pack : Packs)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Pack.BankPath))
		{
			UE_LOG(LogAdaVoicePacks, Warning, TEXT("Pack '%s': no bank at '%s'"), *Pack.Id, *Pack.BankPath);
			continue;
		}
		Pack.bBankLoaded = AdaWwise::LoadBank(Bytes, *Pack.Id);
	}
}

TArray<FAdaLine*> FAdaPackRegistry::FindLines(const FString& Name, float Arg)
{
	TArray<FAdaLine*> Found;
	for (FAdaPack& Pack : Packs)
	{
		for (FAdaLine& Line : Pack.Lines)
		{
			if (MatchesTrigger(Line, Name, Arg))
			{
				Found.Add(&Line);
			}
		}
	}
	return Found;
}

FAdaLine* FAdaPackRegistry::FindLine(const FString& PackId, const FString& LineId)
{
	for (FAdaPack& Pack : Packs)
	{
		if (Pack.Id.Equals(PackId, ESearchCase::IgnoreCase))
		{
			return Pack.Lines.FindByPredicate([&LineId](const FAdaLine& Line) { return Line.Id.Equals(LineId, ESearchCase::IgnoreCase); });
		}
	}
	return nullptr;
}

const FAdaPack* FAdaPackRegistry::FindPack(const FAdaLine& Line) const
{
	return Packs.FindByPredicate([&Line](const FAdaPack& Pack) { return Pack.Id == Line.PackId; });
}

TArray<float> FAdaPackRegistry::GetTriggerArgs(const FString& Name) const
{
	TArray<float> Args;
	for (const FAdaPack& Pack : Packs)
	{
		for (const FAdaLine& Line : Pack.Lines)
		{
			for (const FString& Trigger : Line.Triggers)
			{
				FString TriggerName;
				float TriggerArg = 0.f;
				ParseTrigger(Trigger, TriggerName, TriggerArg);
				if (TriggerName.Equals(Name, ESearchCase::IgnoreCase))
				{
					Args.AddUnique(TriggerArg);
				}
			}
		}
	}
	return Args;
}

FAdaEventRule FAdaPackRegistry::GetEventRule(const FString& Name) const
{
	const FAdaEventRule* Rule = Events.Find(Name);
	return Rule ? *Rule : FAdaEventRule();
}

TArray<TPair<FString, float>> FAdaPackRegistry::GetTimedEvents() const
{
	TArray<TPair<FString, float>> Timed;
	for (const TPair<FString, FAdaEventRule>& Event : Events)
	{
		if (Event.Value.Interval > 0.f)
		{
			Timed.Emplace(Event.Key, Event.Value.Interval);
		}
	}
	return Timed;
}
