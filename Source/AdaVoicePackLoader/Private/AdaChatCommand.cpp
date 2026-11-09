#include "AdaChatCommand.h"
#include "AdaPackRegistry.h"
#include "AdaVoicePackLoaderModule.h"
#include "AdaVoicePlayer.h"
#include "Command/ChatCommandLibrary.h"
#include "FGChatManager.h"
#include "FGPlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

namespace
{
	// True if the chat line Message calls this mod's command (by its name or its mod-qualified name).
	bool IsAdaCommand(const FString& Message)
	{
		const FString Line = Message.TrimStartAndEnd();
		if (!Line.StartsWith(TEXT("/")))
		{
			return false;
		}
		FString Name = Line.RightChop(1);
		Name.Split(TEXT(" "), &Name, nullptr);
		return Name.Equals(TEXT("adapack"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("AdaVoicePackLoader:adapack"), ESearchCase::IgnoreCase);
	}
}

AAdaChatCommand::AAdaChatCommand()
{
	CommandName = TEXT("adapack");
	MinNumberOfArguments = 1;
	bOnlyUsableByPlayer = true;
	Usage = NSLOCTEXT("AdaVoicePacks", "AdaPackUsage", "/adapack list | /adapack play <pack> <line> | /adapack trigger <name> [argument]");
}

EExecutionStatus AAdaChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	const AFGPlayerController* Player = Sender->GetPlayer();
	if (Player && !Player->IsLocalController())
	{
		Sender->SendChatMessage(TEXT("Voice packs play on each player's own game; this command reached the host instead of yours."), FLinearColor::Yellow);
		return EExecutionStatus::UNCOMPLETED;
	}
	const FString& What = Arguments[0];
	FAdaPackRegistry& Registry = FAdaPackRegistry::Get();
	if (What.Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		if (Registry.GetPacks().Num() == 0)
		{
			Sender->SendChatMessage(TEXT("No ADA voice packs are installed."), FLinearColor::Yellow);
		}
		for (const FAdaPack& Pack : Registry.GetPacks())
		{
			TArray<FString> Ids;
			for (const FAdaLine& Line : Pack.Lines)
			{
				Ids.Add(Line.Id);
			}
			Sender->SendChatMessage(FString::Printf(TEXT("%s (%s): %s"), *Pack.Id, *Pack.Name, *FString::Join(Ids, TEXT(", "))), FLinearColor::White);
		}
		return EExecutionStatus::COMPLETED;
	}
	if (What.Equals(TEXT("play"), ESearchCase::IgnoreCase) && Arguments.Num() >= 3)
	{
		FString Reason;
		if (!UAdaVoicePlayer::Get()->PlayNow(Arguments[1], Arguments[2], Reason))
		{
			Sender->SendChatMessage(TEXT("Can't play that: ") + Reason, FLinearColor::Yellow);
			return EExecutionStatus::UNCOMPLETED;
		}
		return EExecutionStatus::COMPLETED;
	}
	if (What.Equals(TEXT("trigger"), ESearchCase::IgnoreCase) && Arguments.Num() >= 2)
	{
		const float Arg = Arguments.Num() >= 3 ? FCString::Atof(*Arguments[2]) : 0.f;
		if (!UAdaVoicePlayer::Get()->Fire(Arguments[1], Arg))
		{
			Sender->SendChatMessage(TEXT("Nothing played for that trigger (no line, a chance roll, a cooldown, or a line is playing)."), FLinearColor::Yellow);
			return EExecutionStatus::UNCOMPLETED;
		}
		return EExecutionStatus::COMPLETED;
	}
	Sender->SendChatMessage(Usage.ToString(), FLinearColor::Red);
	return EExecutionStatus::BAD_ARGUMENTS;
}

bool AAdaChatCommand::RunOnClient(AFGPlayerController* Controller, const FString& Message)
{
	if (!Controller || Controller->GetNetMode() != NM_Client || !Controller->IsLocalController() || !IsAdaCommand(Message))
	{
		return false;
	}
	UAdaLocalCommandSender* Sender = NewObject<UAdaLocalCommandSender>(Controller);
	AChatCommandSubsystem* Commands = AChatCommandSubsystem::Get(Controller);
	if (!Commands || !Commands->FindCommandByName(TEXT("adapack")))
	{
		UE_LOG(LogAdaVoicePacks, Warning, TEXT("'%s' typed on a client, but SML's chat commands aren't set up on this game yet"), *Message);
		Sender->SendChatMessage(TEXT("/adapack isn't ready yet; try again in a moment."), FLinearColor::Yellow);
		return true;
	}
	UE_LOG(LogAdaVoicePacks, Log, TEXT("Running '%s' on this game (client) instead of sending it to the server"), *Message.TrimStartAndEnd());
	Commands->RunChatCommand(Message.TrimStartAndEnd().RightChop(1), Sender);
	return true;
}

FString UAdaLocalCommandSender::GetSenderName() const
{
	const AFGPlayerController* Controller = GetPlayer();
	return Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : TEXT("Player");
}

AFGPlayerController* UAdaLocalCommandSender::GetPlayer() const
{
	return Cast<AFGPlayerController>(GetOuter());
}

void UAdaLocalCommandSender::SendChatMessage(const FString& Message, const FLinearColor PrefixColor)
{
	UWorld* World = GetPlayer() ? GetPlayer()->GetWorld() : nullptr;
	AFGChatManager* ChatManager = World ? AFGChatManager::Get(World) : nullptr;
	if (!ChatManager)
	{
		UE_LOG(LogAdaVoicePacks, Warning, TEXT("No chat box for the reply: %s"), *Message);
		return;
	}
	FChatMessageStruct Chat;
	Chat.MessageText = FText::FromString(Message);
	Chat.MessageType = EFGChatMessageType::CMT_CustomMessage;
	Chat.ServerTimeStamp = World->GetGameState() ? World->GetGameState()->GetServerWorldTimeSeconds() : 0.f;
	Chat.MessageSenderColor = PrefixColor;
	Chat.MessageSender = FText::FromString(TEXT("SYSTEM"));
	ChatManager->AddChatMessageToReceived(Chat);
}

UAdaGameWorldModule::UAdaGameWorldModule()
{
	bRootModule = true;
	mChatCommands.Add(AAdaChatCommand::StaticClass());
}
