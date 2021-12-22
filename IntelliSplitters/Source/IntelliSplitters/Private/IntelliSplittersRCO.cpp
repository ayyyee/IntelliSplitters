#include "IntelliSplittersRCO.h"

#include "IntelliSplittersLog.h"
#include "Buildables/MFGBuildableIntelliSplitter.h"


void UIntelliSplittersRCO::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UIntelliSplittersRCO, Dummy);
}


UIntelliSplittersRCO* UIntelliSplittersRCO::Get(UWorld* World)
{
    return Cast<UIntelliSplittersRCO>(
        Cast<AFGPlayerController>(World->GetFirstPlayerController())->
        GetRemoteCallObjectOfClass(UIntelliSplittersRCO::StaticClass()));
}


void UIntelliSplittersRCO::EnableReplication_Implementation(
    AMFGBuildableIntelliSplitter* Splitter, float Duration) const
{
    UE_LOG(LogIntelliSplitters, Display, TEXT("Client RPC: EnableReplication"));
    Splitter->ServerEnableReplication(Duration);
}


void UIntelliSplittersRCO::SetTargetRateAutomatic_Implementation(
    AMFGBuildableIntelliSplitter* Splitter, bool bAutomatic) const
{
    UE_LOG(LogIntelliSplitters, Display, TEXT("Client RPC: SetTargetRateAutomatic"));
    Splitter->ServerSetTargetRateAutomatic(bAutomatic);
}


void UIntelliSplittersRCO::SetTargetInputRate_Implementation(
    AMFGBuildableIntelliSplitter* Splitter, float Rate) const
{
    UE_LOG(LogIntelliSplitters, Display, TEXT("Client RPC: SetTargetInputRate"));
    Splitter->ServerSetTargetInputRate(Rate);
}


void UIntelliSplittersRCO::SetOutputRate_Implementation(
    AMFGBuildableIntelliSplitter* Splitter, int32 Output, float Rate) const
{
    UE_LOG(LogIntelliSplitters, Display, TEXT("Client RPC: SetOutputRate"));
    Splitter->ServerSetOutputRate(Output, Rate);
}


void UIntelliSplittersRCO::SetOutputAutomatic_Implementation(
    AMFGBuildableIntelliSplitter* Splitter, int32 Output, bool bAutomatic) const
{
    UE_LOG(LogIntelliSplitters, Display, TEXT("Client RPC: SetOutputAutomatic"));
    Splitter->ServerSetOutputAutomatic(Output, bAutomatic);
}


void UIntelliSplittersRCO::BalanceNetwork_Implementation(
    AMFGBuildableIntelliSplitter* Splitter, bool bRootOnly) const
{
    UE_LOG(LogIntelliSplitters, Display, TEXT("Client RPC: BalanceNetwork"));
    Splitter->ServerBalanceNetwork(Splitter, bRootOnly);
}




