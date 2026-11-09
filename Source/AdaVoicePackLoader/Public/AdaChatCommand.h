#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "Module/GameWorldModule.h"
#include "AdaChatCommand.generated.h"

// "/adapack list" lists the installed packs and their lines, "/adapack play <pack> <line>" plays one now, "/adapack
// trigger <name> [argument]" fires a trigger as if it happened. Chat commands run on the server, so this works in
// single-player and for the host of a session, on the host's own game.
UCLASS()
class ADAVOICEPACKLOADER_API AAdaChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	AAdaChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;
};

// The mod's root game world module; registers the chat command with SML.
UCLASS()
class ADAVOICEPACKLOADER_API UAdaGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UAdaGameWorldModule();
};
