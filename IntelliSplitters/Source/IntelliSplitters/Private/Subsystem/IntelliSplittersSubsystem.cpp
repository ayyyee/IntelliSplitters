#include "Subsystem/IntelliSplittersSubsystem.h"
#include "ModLoading/ModLoadingLibrary.h"
#include "IntelliSplittersLog.h"


AIntelliSplittersSubsystem* AIntelliSplittersSubsystem::CachedSubsystem = nullptr;

const FVersion AIntelliSplittersSubsystem::NewSession
	= FVersion(INT32_MAX, INT32_MAX, INT32_MAX);

const FVersion AIntelliSplittersSubsystem::ModVersionLegacy = FVersion(0, 0, 0);


AIntelliSplittersSubsystem::AIntelliSplittersSubsystem()
	: LoadedModVersion(NewSession)
	, SerializationVersion(EIntelliSplittersSerializationVersion::Legacy)
	, bIsNewSession(false)
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
}


AIntelliSplittersSubsystem* AIntelliSplittersSubsystem::Get(
	UObject* WorldContext, bool bFailIfMissing)
{
	if (CachedSubsystem)
	{
		return CachedSubsystem;
	}

	return FindAndGet(WorldContext, bFailIfMissing);
}


FVersion AIntelliSplittersSubsystem::GetLoadedModVersion() const
{
	return LoadedModVersion;
}


FVersion AIntelliSplittersSubsystem::GetRunningModVersion() const
{
	return RunningModVersion;
}


EIntelliSplittersSerializationVersion
	AIntelliSplittersSubsystem::GetSerializationVersion() const
{
	return SerializationVersion.GetValue();
}


const FIntelliSplittersConfig& AIntelliSplittersSubsystem::GetConfig() const
{
	return ModConfig;
}


bool AIntelliSplittersSubsystem::IsModOlderThanSaveGame() const
{
	return GetLoadedModVersion().Compare(NewSession) != 0
		&& GetLoadedModVersion().Compare(GetRunningModVersion()) > 0;
}


bool AIntelliSplittersSubsystem::IsNewSession() const
{
	return bIsNewSession;
}


void AIntelliSplittersSubsystem::ReloadConfig()
{
	ModConfig = FIntelliSplittersConfig::GetActiveConfig();
}


void AIntelliSplittersSubsystem::NotifyChat(ESeverity Severity, FString Message) const
{
	auto ChatManager = AFGChatManager::Get(GetWorld());

	FChatMessageStruct ChatMessage;
	ChatMessage.MessageString = FString::Printf(TEXT("IntelliSplitters: %s"), *Message);
	ChatMessage.MessageType = EFGChatMessageType::CMT_SystemMessage;
	ChatMessage.ServerTimeStamp = GetWorld()->TimeSeconds;

	switch (Severity)
	{

	case ESeverity::Debug:
	case ESeverity::Info:

		ChatMessage.CachedColor = FLinearColor(0.92, 0.92, 0.92);
		break;

	case ESeverity::Notice:

		ChatMessage.CachedColor = FLinearColor(0.0, 0.667, 0);
		break;

	case ESeverity::Warning:

		ChatMessage.CachedColor = FLinearColor(0.949, 0.667, 0);
		break;

	case ESeverity::Error:

		ChatMessage.CachedColor = FLinearColor(0.9, 0, 0);
		break;

	}

	ChatManager->AddChatMessageToReceived(ChatMessage);
}


void AIntelliSplittersSubsystem::PreSaveGame_Implementation(
	int32 saveVersion, int32 gameVersion)
{
	LoadedModVersion = RunningModVersion;
	SerializationVersion = EIntelliSplittersSerializationVersion::Latest;
	VersionMajor = LoadedModVersion.Major;
	VersionMinor = LoadedModVersion.Minor;
	VersionPatch = LoadedModVersion.Patch;
}


void AIntelliSplittersSubsystem::PostLoadGame_Implementation(
	int32 saveVersion, int32 gameVersion)
{
	/**
	 * If serialized properties are missing, they get zeroed out and
	 * we get our legacy version marker by copying them into LoadedModVersion.
	 */

	LoadedModVersion = FVersion(VersionMajor, VersionMinor, VersionPatch);
}


bool AIntelliSplittersSubsystem::NeedTransform_Implementation()
{
	return false;
}


bool AIntelliSplittersSubsystem::ShouldSave_Implementation() const
{
	return true;
}


AIntelliSplittersSubsystem* AIntelliSplittersSubsystem::FindAndGet(
	UObject* WorldContext, bool bFailIfMissing)
{
	const auto World = WorldContext->GetWorld();
	auto SubsystemActorManager = World->GetSubsystem<USubsystemActorManager>();

	CachedSubsystem = SubsystemActorManager->
		GetSubsystemActor<AIntelliSplittersSubsystem>();

	if (bFailIfMissing)
	{
		check(CachedSubsystem);
	}

	return CachedSubsystem;
}


void AIntelliSplittersSubsystem::Init()
{
	Super::Init();

	FModInfo ModInfo;
	GEngine->GetEngineSubsystem<UModLoadingLibrary>()->
		GetLoadedModInfo("IntelliSplitters", ModInfo);
	RunningModVersion = ModInfo.Version;

	// Preload the configuration
	ReloadConfig();

	UE_LOG(LogIntelliSplitters, Display,
		TEXT("AIntelliSplittersSubsystem initialized: IntelliSplitters %s"),
		*GetRunningModVersion().ToString());

	// Check if this is a loaded save file or a new session
	if (GetLoadedModVersion().Compare(NewSession) == 0)
	{
		if (FIntelliSplittersModule::Get()->HasLoadedSplitters())
		{
			// Subsystem was not loaded from save, need to fix manually
			LoadedModVersion = ModVersionLegacy;
			bIsNewSession = false;
		}
		else
		{
			bIsNewSession = true;
		}
	}
	else
	{
		bIsNewSession = false;
	}

	if (!IsNewSession())
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Savegame was created with IntelliSplitters %s."),
			GetLoadedModVersion().Compare(ModVersionLegacy) == 0
				? TEXT("/ [AutoSplitters] legacy version < 0.3.9")
				: *GetLoadedModVersion().ToString()
		);
	}

	if (IsModOlderThanSaveGame())
	{
		UE_LOG(LogIntelliSplitters, Warning,
			TEXT("Savegame was created with a newer mod version, "\
				"there might be bugs or incompatibilities."));
	}

	UE_LOG(LogIntelliSplitters, Display,
		TEXT("IntelliSplitters serialization version: %d (%s)."),
		GetSerializationVersion(),
		*UEnum::GetValueAsString(GetSerializationVersion()));

	if (GetSerializationVersion() > EIntelliSplittersSerializationVersion::Latest)
	{
		UE_LOG(LogIntelliSplitters, Error,
			TEXT("Serialization version not supported by this version "\
				"of the mod, all splitters will be removed during loading."));
	}
}


void AIntelliSplittersSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (this == CachedSubsystem)
	{
		CachedSubsystem = nullptr;
	}
}




