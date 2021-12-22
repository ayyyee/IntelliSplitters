#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "Buildables/MFGBuildableIntelliSplitter.h"
#include "IntelliSplittersConfig.h"
#include "Subsystem/SubsystemActorManager.h"
#include "Util/SemVersion.h"
#include "IntelliSplittersSerializationVersion.h"

#include "IntelliSplittersSubsystem.generated.h"


UENUM()
enum class EAIntelliSplittersSubsystemSeverity : uint8
{
	/** Debug level */
	Debug = 1,

	/** Info level */
	Info = 2,

	/** Notice level */
	Notice = 3,

	/** Warning level */
	Warning = 4,

	/** Error level */
	Error = 5
};

using ESeverity = EAIntelliSplittersSubsystemSeverity;


UCLASS(BlueprintType, NotBlueprintable)
class INTELLISPLITTERS_API AIntelliSplittersSubsystem
	: public AModSubsystem, public IFGSaveInterface
{

	GENERATED_BODY()

	friend class FIntelliSplittersModule;

public:

	/**
	 * Default constructor.
	 */
	AIntelliSplittersSubsystem();

	/**
	 * Get a pointer to the subsystem.
	 * 
	 * @param WorldContext The world context
	 * @param bFailIfMissing Flag indicating whether it should fail when missing
	 */
	static AIntelliSplittersSubsystem* Get(
		UObject* WorldContext, bool bFailIfMissing = true);

	/**
	 * Get the loaded mod version.
	 */
	FVersion GetLoadedModVersion() const;

	/**
	 * Get the running mod version.
	 */
	FVersion GetRunningModVersion() const;

	/**
	 * Get the serialization version.
	 */
	EIntelliSplittersSerializationVersion
		GetSerializationVersion() const;

	/**
	 * Get the mod config.
	 */
	const FIntelliSplittersConfig& GetConfig() const;

	/**
	 * Check whether the mod is older than the savegame.
	 */
	bool IsModOlderThanSaveGame() const;

	/**
	 * Check whether this is a new session.
	 */
	bool IsNewSession() const;

	/**
	 * Reload the mod config.
	 */
	void ReloadConfig();

	/**
	 * Notify the chat.
	 * 
	 * @param Severity The message severity
	 * @param Message The message
	 */
	void NotifyChat(ESeverity Severity, FString Message) const;

public:

	/**
	 * Set mod versions with PreSaveGame blueprint hook.
	 *
	 * @param saveVersion The save version
	 * @param gameVersion The game version
	 */
	virtual void PreSaveGame_Implementation(
		int32 saveVersion, int32 gameVersion) override;

	/**
	 * Parse loaded mod version with PostLoadGame blueprint hook.
	 *
	 * @param saveVersion The save version
	 * @param gameVersion The game version
	 */
	virtual void PostLoadGame_Implementation(
		int32 saveVersion, int32 gameVersion) override;

	/**
	 * NeedTransform blueprint hook.
	 */
	virtual bool NeedTransform_Implementation() override;

	/**
	 * ShouldSave blueprint hook.
	 */
	virtual bool ShouldSave_Implementation() const override;

private:

	/**
	 * Find and get the subsystem.
	 * 
	 * @param WorldContext The world context
	 * @param bFailIfMissing Whether it should fail if missing
	 */
	static AIntelliSplittersSubsystem* FindAndGet(
		UObject* WorldContext, bool bFailIfMissing);

protected:

	/**
	 * Initiate the subsystem and check mod versions.
	 */
	virtual void Init() override;

	/**
	 * Stop the subsystem.
	 * 
	 * @param EndPlayReason The reason
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

	/** Holds the cached subsystem. */
	static AIntelliSplittersSubsystem* CachedSubsystem;

	/** Holds the major version number. */
	UPROPERTY(SaveGame)
	int64 VersionMajor;

	/** Holds the minor version number. */
	UPROPERTY(SaveGame)
	int64 VersionMinor;

	/** Holds the patch version number. */
	UPROPERTY(SaveGame)
	int64 VersionPatch;

	/** Holds a flag indicating whether this is a new session. */
	bool bIsNewSession;

protected:

	/** Holds the serialization version. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TEnumAsByte<EIntelliSplittersSerializationVersion> SerializationVersion;

	/**
	 * FVersion cannot be serialized, so we store the data
	 * into three separate properties in the save file.
	 */

	/** Holds the loaded mod version. */
	UPROPERTY(Transient, BlueprintReadOnly)
	FVersion LoadedModVersion;

	/** Holds the running mod version. */
	UPROPERTY(Transient, BlueprintReadOnly)
	FVersion RunningModVersion;

	/** Holds the mod config. */
	UPROPERTY(Transient, BlueprintReadOnly)
	FIntelliSplittersConfig ModConfig;

public:
	
	static const FVersion NewSession;
	
	static const FVersion ModVersionLegacy;

};




