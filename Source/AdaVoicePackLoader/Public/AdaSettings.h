#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Module/GameInstanceModule.h"
#include "AdaSettings.generated.h"

// The mod's page in the game's Mods settings: one switch turning every pack's lines on or off. Built in code (the mod
// has no assets) from SML's own config property Blueprints, which carry the menu widgets. Saved by SML in
// Configs/AdaVoicePackLoader.cfg.
UCLASS()
class ADAVOICEPACKLOADER_API UAdaVoicePackConfiguration : public UModConfiguration
{
	GENERATED_BODY()

public:
	UAdaVoicePackConfiguration();

	// Fills the class default object's RootSection; SML copies it when the configuration is registered.
	static void BuildRootSection();

	// Key of the on/off switch in the root section and the saved file.
	static const TCHAR* EnabledKey;
};

// The mod's root game instance module; registers the configuration with SML.
UCLASS()
class ADAVOICEPACKLOADER_API UAdaGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UAdaGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

namespace AdaSettings
{
	// The switch's current value for World's game instance; true if the configuration isn't registered.
	bool IsEnabled(const UWorld* World);
}
