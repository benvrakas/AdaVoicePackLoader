#include "AdaSettings.h"
#include "AdaVoicePackLoaderModule.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "AdaVoicePacks"

namespace
{
	const TCHAR* ModReference = TEXT("AdaVoicePackLoader");
	const TCHAR* SectionClassPath = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C");
	const TCHAR* BoolClassPath = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C");

	// SML's Blueprint subclass of T at Path (it has the menu widget), or T itself if it can't be loaded.
	template <typename T>
	UClass* LoadPropertyClass(const TCHAR* Path)
	{
		UClass* Class = LoadClass<T>(nullptr, Path);
		if (!Class)
		{
			UE_LOG(LogAdaVoicePacks, Error, TEXT("Can't load SML's config property class '%s'; the setting won't show in the Mods menu"), Path);
			return T::StaticClass();
		}
		return Class;
	}

	FConfigId GetConfigId()
	{
		FConfigId Id;
		Id.ModReference = ModReference;
		return Id;
	}
}

const TCHAR* UAdaVoicePackConfiguration::EnabledKey = TEXT("Enabled");

UAdaVoicePackConfiguration::UAdaVoicePackConfiguration()
{
	ConfigId = GetConfigId();
	DisplayName = LOCTEXT("ConfigName", "ADA Voice Pack Loader");
	Description = LOCTEXT("ConfigDescription", "Settings for the ADA voice packs.");
}

void UAdaVoicePackConfiguration::BuildRootSection()
{
	UAdaVoicePackConfiguration* Defaults = GetMutableDefault<UAdaVoicePackConfiguration>();
	if (Defaults->RootSection)
	{
		return;
	}
	UConfigPropertySection* Section = NewObject<UConfigPropertySection>(Defaults, LoadPropertyClass<UConfigPropertySection>(SectionClassPath), TEXT("RootSection"));

	UConfigPropertyBool* Enabled = NewObject<UConfigPropertyBool>(Section, LoadPropertyClass<UConfigPropertyBool>(BoolClassPath), EnabledKey);
	Enabled->DisplayName = LOCTEXT("EnabledName", "Play ADA voice lines");
	Enabled->Tooltip = LOCTEXT("EnabledTooltip", "Off: no voice pack line plays, and a line already playing stops. The packs stay installed.");
	Enabled->DefaultValue = true;
	Enabled->Value = true;
	Section->SectionProperties.Add(EnabledKey, Enabled);

	Defaults->RootSection = Section;
}

UAdaGameInstanceModule::UAdaGameInstanceModule()
{
	bRootModule = true;
	ModConfigurations.Add(UAdaVoicePackConfiguration::StaticClass());
}

void UAdaGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	// SML registers ModConfigurations at initialization and copies the root section then.
	if (Phase == ELifecyclePhase::INITIALIZATION)
	{
		UAdaVoicePackConfiguration::BuildRootSection();
	}
	Super::DispatchLifecycleEvent(Phase);
}

bool AdaSettings::IsEnabled(const UWorld* World)
{
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UConfigManager* ConfigManager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	const UConfigPropertySection* Root = ConfigManager ? ConfigManager->GetConfigurationRootSection(GetConfigId()) : nullptr;
	const UConfigPropertyBool* Enabled = Root ? Cast<UConfigPropertyBool>(Root->SectionProperties.FindRef(UAdaVoicePackConfiguration::EnabledKey)) : nullptr;
	return !Enabled || Enabled->Value;
}

#undef LOCTEXT_NAMESPACE
