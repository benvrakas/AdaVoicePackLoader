#include "AdaChatCommand.h"
#include "AdaPackRegistry.h"
#include "AdaVoicePackLoaderModule.h"
#include "AdaVoicePlayer.h"
#include "Command/CommandSender.h"
#include "FGPlayerController.h"

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
		Sender->SendChatMessage(TEXT("Voice packs play on the machine that runs the command; ask the host."), FLinearColor::Yellow);
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

UAdaGameWorldModule::UAdaGameWorldModule()
{
	bRootModule = true;
	mChatCommands.Add(AAdaChatCommand::StaticClass());
}
