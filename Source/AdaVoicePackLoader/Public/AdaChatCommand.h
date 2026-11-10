#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "Command/CommandSender.h"
#include "Module/GameWorldModule.h"
#include "AdaChatCommand.generated.h"

class AFGPlayerController;

// "/adapack list" lists the installed packs and their lines, "/adapack play <pack> <line>" plays one now, "/adapack
// trigger <name> [argument]" fires a trigger as if it happened. In single-player and on a host SML runs it; on a client
// RunOnClient runs it on the player's own game, since the server (often without this mod) would only answer
// "Unknown command" and SML's command list exists only on the server.
UCLASS()
class ADAVOICEPACKLOADER_API AAdaChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	AAdaChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;

	// Runs Message on this machine if it is an /adapack command typed on a client. Returns true if it was handled, and
	// then it must not be sent to the server.
	static bool RunOnClient(AFGPlayerController* Controller, const FString& Message);

private:
	// The command itself, for SML's call and for RunOnClient; Arguments are the words after "/adapack".
	static EExecutionStatus Execute(UCommandSender* Sender, const TArray<FString>& Arguments);
};

// The local player as a command sender: replies go straight into this game's chat box.
UCLASS()
class ADAVOICEPACKLOADER_API UAdaLocalCommandSender : public UCommandSender
{
	GENERATED_BODY()

public:
	virtual FString GetSenderName() const override;
	virtual bool IsPlayerSender() const override { return true; }
	virtual AFGPlayerController* GetPlayer() const override;
	virtual void SendChatMessage(const FString& Message, const FLinearColor PrefixColor) override;
};

// The mod's root game world module; registers the chat command with SML.
UCLASS()
class ADAVOICEPACKLOADER_API UAdaGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UAdaGameWorldModule();
};
