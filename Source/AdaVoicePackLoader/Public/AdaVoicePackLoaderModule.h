#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAdaVoicePacks, Log, All);

// Module entry point: installs the message box hook and ticks the voice player.
class FAdaVoicePackLoaderModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	bool Tick(float DeltaTime);

	FTSTicker::FDelegateHandle TickHandle;
};
