#include "IntelliSplittersModule.h"

#include "Patching/NativeHookManager.h"

#include "CoreMinimal.h"
#include "UI/FGPopupWidget.h"
#include "FGWorldSettings.h"
#include "FGPlayerController.h"
#include "FGBlueprintFunctionLibrary.h"
#include "FGBuildableSubsystem.h"
#include "Buildables/MFGBuildableIntelliSplitter.h"
#include "Hologram/MFGIntelliSplitterHologram.h"
#include "Engine/RendererSettings.h"
#include "Subsystem/IntelliSplittersSubsystem.h"
#include "Registry/ModContentRegistry.h"
#include "Resources/FGBuildingDescriptor.h"
#include "ModLoading/PluginModuleLoader.h"


#define LOCTEXT_NAMESPACE "IntelliSplitters"

// Fixes a linker issue
DEFINE_LOG_CATEGORY(LogGame)

DEFINE_LOG_CATEGORY(LogIntelliSplitters)


void FIntelliSplittersModule::StartupModule()
{
#if UE_BUILD_SHIPPING

	auto UpgradeHook = [](auto& Call, UObject* Self, AActor* NewActor)
	{
		if (!NewActor->HasAuthority())
		{
			return;
		}

		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Entered hook for IFGDismantleInterface::Execute_Upgrade()."));

		AMFGBuildableIntelliSplitter* Target = Cast<AMFGBuildableIntelliSplitter>(NewActor);

		if (!Target)
		{
			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Target is not an AMFGBuildableIntelliSplitter, bailing out."));
			return;
		}

		AFGBuildableAttachmentSplitter* Source
			= Cast<AFGBuildableAttachmentSplitter>(Self);

		if (!Source)
		{
			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Self is not an AFGBuildableAttachmentSplitter, bailing out."));
			return;
		}

		UE_LOG(LogIntelliSplitters, Display, TEXT("Cancelling original call."));

		Call.Cancel();
	};

	SUBSCRIBE_METHOD(IFGDismantleInterface::Execute_Upgrade, UpgradeHook);

	auto NotifyBeginPlayHook = [&](AFGWorldSettings* WorldSettings)
	{
		if (!WorldSettings->HasAuthority())
		{
			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Not running on server, skipping."));
			return;
		}

		auto World = WorldSettings->GetWorld();

		if (FPluginModuleLoader::IsMainMenuWorld(World)
			|| !FPluginModuleLoader::ShouldLoadModulesForWorld(World))
		{
			UE_LOG(LogIntelliSplitters, Display, TEXT("Ignoring main menu world."));
			return;
		}

		auto IntelliSplittersSubsystem = AIntelliSplittersSubsystem::Get(WorldSettings);

		if (IntelliSplittersSubsystem->IsNewSession())
		{
			UE_LOG(LogIntelliSplitters, Display,
				TEXT("Newly created game session detected, no compatibility issues expected."));
		}

		if (PreComponentFixSplitters.Num() > 0)
		{
			ReplacePreComponentFixSplitters(World, IntelliSplittersSubsystem);
		}

		if (DoomedSplitters.Num() > 0)
		{
			UE_LOG(LogIntelliSplitters, Warning,
				TEXT("Removing %d splitters with an incompatible serialization version."),
				DoomedSplitters.Num());

			for (auto Splitter : DoomedSplitters)
			{
				IFGDismantleInterface::Execute_Dismantle(Splitter);
			}

			IntelliSplittersSubsystem->NotifyChat(
				EAIntelliSplittersSubsystemSeverity::Warning, FString::Printf(
					TEXT("Savegame created with version %s that uses unknown "\
						"serialization version %d, removed %d incompatible IntelliSplitters."),
					*IntelliSplittersSubsystem->GetLoadedModVersion().ToString(),
					IntelliSplittersSubsystem->GetSerializationVersion(),
					DoomedSplitters.Num()));
		}
		else if (IntelliSplittersSubsystem->IsModOlderThanSaveGame())
		{
			IntelliSplittersSubsystem->NotifyChat(
				EAIntelliSplittersSubsystemSeverity::Warning, FString::Printf(
					TEXT("Running %s, but the savegame was created with %s."),
					*IntelliSplittersSubsystem->GetRunningModVersion().ToString(),
					*IntelliSplittersSubsystem->GetLoadedModVersion().ToString()));
		}
		else if (IntelliSplittersSubsystem->GetSerializationVersion()
			< EIntelliSplittersSerializationVersion::Latest)
		{
			IntelliSplittersSubsystem->NotifyChat(
				EAIntelliSplittersSubsystemSeverity::Notice, FString::Printf(
					TEXT("Now running %s, downgrade to previous version %s will not be possible."),
					*IntelliSplittersSubsystem->GetRunningModVersion().ToString(),
					*IntelliSplittersSubsystem->GetLoadedModVersion().ToString()));
		}
		else if (IntelliSplittersSubsystem->GetLoadedModVersion()
			.Compare(IntelliSplittersSubsystem->GetRunningModVersion()) < 0)
		{
			IntelliSplittersSubsystem->NotifyChat(
				EAIntelliSplittersSubsystemSeverity::Info, FString::Printf(
					TEXT("Upgraded from %s to %s."),
					*IntelliSplittersSubsystem->GetLoadedModVersion().ToString(),
					*IntelliSplittersSubsystem->GetRunningModVersion().ToString()));
		}

		if (bIsAlphaVersion)
		{
			if (IntelliSplittersSubsystem->GetConfig().Preferences.ShowAlphaWarning)
			{
				AFGPlayerController* LocalController
					= UFGBlueprintFunctionLibrary::GetLocalPlayerController(World);

				FPopupClosed CloseDelegate;

				UFGBlueprintFunctionLibrary::AddPopupWithCloseDelegate(
					LocalController, FText::FromString("IntelliSplitters Alpha Version"),
					FText::FromString("You are running an alpha version of IntelliSplitters."\
						"There will most likely be bugs - please keep your old savegames around!"\
						"\nPlease report any bugs you encounter on the Modding Discord."\
						"\n\nYou can disable this message in the mod settings."),
					CloseDelegate);
			}
			else
			{
				IntelliSplittersSubsystem->NotifyChat(
					EAIntelliSplittersSubsystemSeverity::Warning, FString::Printf(
						TEXT("Alpha version %s. Please report any bugs you encounter on the Modding Discord."),
						*IntelliSplittersSubsystem->GetRunningModVersion().ToString()));
			}
		}

		LoadedSplitterCount = 0;
		PreComponentFixSplitters.Empty();
		DoomedSplitters.Empty();

	};

	void* SampleInstance = GetMutableDefault<AFGWorldSettings>();

	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGWorldSettings::NotifyBeginPlay, SampleInstance, NotifyBeginPlayHook);

#endif
}


FIntelliSplittersModule* FIntelliSplittersModule::Get()
{
	return FModuleManager::GetModulePtr<FIntelliSplittersModule>(ModReference);
}


void FIntelliSplittersModule::OnSplitterLoadedFromSaveGame(AMFGBuildableIntelliSplitter* Splitter)
{
	++LoadedSplitterCount;
}


void FIntelliSplittersModule::ScheduleDismantle(AMFGBuildableIntelliSplitter* Splitter)
{
	if (LoadedSplitterCount == 0)
	{
		UE_LOG(LogIntelliSplitters, Warning, TEXT("No splitters loaded."));
		return;
	}

	DoomedSplitters.Add(Splitter);
}


bool FIntelliSplittersModule::HasLoadedSplitters() const
{
	return LoadedSplitterCount > 0;
}


void FIntelliSplittersModule::ReplacePreComponentFixSplitters(
	UWorld* World, AIntelliSplittersSubsystem* IntelliSplittersSubsystem)
{
	const auto& Config = IntelliSplittersSubsystem->ModConfig;

	UE_LOG(LogIntelliSplitters, Display,
		TEXT("Found %d pre-upgrade IntelliSplitters while loading savegame."),
		PreComponentFixSplitters.Num());

	if (Config.Upgrade.RemoveAllConveyors)
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Nuclear upgrade option chosen: Removing all "\
				"conveyors attached to IntelliSplitters."));
	}

	auto ModContentRegistry = AModContentRegistry::Get(World);
	auto BuildableSubSystem = AFGBuildableSubsystem::Get(World);

	UFGRecipe* IntelliSplitterRecipe = nullptr;

	for (auto& RecipeInfo : ModContentRegistry->GetRegisteredRecipes())
	{
		if (RecipeInfo.OwnedByModReference != FName("IntelliSplitters")
			|| RecipeInfo.OwnedByModReference != FName("AutoSplitters"))
		{
			continue;
		}

		auto Recipe = Cast<UFGRecipe>(RecipeInfo.RegisteredObject->GetDefaultObject());

		if (Recipe->GetProducts().Num() != 1)
		{
			continue;
		}

		auto BuildingDescriptor = Recipe->GetProducts()[0].ItemClass->GetDefaultObject<UFGBuildingDescriptor>();

		if (!BuildingDescriptor)
		{
			continue;
		}

		UE_LOG(LogIntelliSplitters, Display, TEXT("Found building descriptor: %s"),
			*BuildingDescriptor->GetClass()->GetName());

		if (UFGBuildingDescriptor::GetBuildableClass(
			BuildingDescriptor->GetClass())->IsChildOf(AMFGBuildableIntelliSplitter::StaticClass()))
		{
			UE_LOG(LogIntelliSplitters, Display, TEXT("Found IntelliSplitter recipe to use for rebuilt splitters."));
			IntelliSplitterRecipe = Recipe;
			break;
		}
	}

	if (!IntelliSplitterRecipe)
	{
		UE_LOG(LogIntelliSplitters, Error,
			TEXT("Could not find IntelliSplitter recipe, unable to upgrade old IntelliSplitters."));
	}

	TSet<AFGBuildableConveyorBase*> Conveyors;

	for (auto& [Splitter, PreUpgradeComponents, ConveyorConnections] : PreComponentFixSplitters)
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Replacing IntelliSplitter %s."), *Splitter->GetName());

		auto Location = Splitter->GetActorLocation();
		auto Transform = Splitter->GetTransform();
		IFGDismantleInterface::Execute_Dismantle(Splitter);

		for (auto Component : PreUpgradeComponents)
		{
			Component->DestroyComponent();
		}

		UE_LOG(LogIntelliSplitters, Display, TEXT("Creating and setting up hologram."));

		auto Hologram = Cast<AMFGIntelliSplitterHologram>(
			AFGHologram::SpawnHologramFromRecipe(IntelliSplitterRecipe->GetClass(),
				World->GetFirstPlayerController(), Location));

		Hologram->SetActorTransform(Transform);

		if (Config.Upgrade.RemoveAllConveyors)
		{
			for (auto Connection : ConveyorConnections)
			{
				auto Conveyor = Cast<AFGBuildableConveyorBase>(Connection->GetOuterBuildable());

				if (!Conveyor)
				{
					UE_LOG(LogIntelliSplitters, Warning,
						TEXT("Found something connected to a splitter that is not a conveyor: %s."),
						*Connection->GetOuterBuildable()->GetClass()->GetName());
					break;
				}

				Conveyors.Add(Conveyor);
			}
		}
		else
		{
			Hologram->PreUpgradeConnections = ConveyorConnections;
		}

		UE_LOG(LogIntelliSplitters, Display, TEXT("Spawning Splitter through hologram."));

		TArray<AActor*> Children;
		auto Actor = Hologram->Construct(
			Children, BuildableSubSystem->GetNewNetConstructionID());

		UE_LOG(LogIntelliSplitters, Display, TEXT("Destroying Hologram."));

		Hologram->Destroy();
	}

	if (Config.Upgrade.RemoveAllConveyors)
	{
		UE_LOG(LogIntelliSplitters, Display,
			TEXT("Dismantling %d attached conveyors."), Conveyors.Num());

		for (auto Conveyor : Conveyors)
		{
			IFGDismantleInterface::Execute_Dismantle(Conveyor);
		}
	}

	if (Config.Upgrade.ShowWarningMessages)
	{
		FString Str;

		if (Config.Upgrade.RemoveAllConveyors)
		{
			Str = FString::Printf(TEXT(
				"Your savegame contained %d IntelliSplitters created with "\
				"a mod version older than 0.3.0, which connect to the attached "\
				"conveyors the wrong way. All conveyors attached to IntelliSplitters "\
				"have been dismantled. All replaced splitters have been reset to "\
				"full automatic mode. A total of %d conveyors have been removed."),
				PreComponentFixSplitters.Num(), Conveyors.Num());
		}
		else
		{
			Str = FString::Printf(TEXT(
				"Your savegame contained %d IntelliSplitters created with "\
				"a mod version older than 0.3.0, which connect to the attached "\
				"conveyors the wrong way. The mod has replaced these IntelliSplitters "\
				"with new ones. All replaced splitters have been reset to "\
				"full automatic mode."), PreComponentFixSplitters.Num());
		}

		AFGPlayerController* LocalController
			= UFGBlueprintFunctionLibrary::GetLocalPlayerController(World);

		FPopupClosed CloseDelegate;

		UFGBlueprintFunctionLibrary::AddPopupWithCloseDelegate(LocalController,
			FText::FromString("Savegame upgraded from legacy version < 0.3.0"),
			FText::FromString(Str), CloseDelegate);
	}
}


const FName FIntelliSplittersModule::ModReference("IntelliSplitters");


IMPLEMENT_GAME_MODULE(FIntelliSplittersModule, IntelliSplitters);




