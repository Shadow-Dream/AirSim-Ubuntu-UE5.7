#include "AirSimGameMode.h"
#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"
#include "SimHUD/SimHUD.h"
#include "common/Common.hpp"
#include "AirBlueprintLib.h"
#include "AirSimPlayerController.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

class AUnrealLog : public msr::airlib::Utils::Logger
{
public:
    virtual void log(int level, const std::string& message) override
    {
        size_t tab_pos;
        static const std::string delim = ":\t";
        if ((tab_pos = message.find(delim)) != std::string::npos) {
            UAirBlueprintLib::LogMessageString(message.substr(0, tab_pos),
                                               message.substr(tab_pos + delim.size(), std::string::npos),
                                               LogDebugLevel::Informational);

            return; //display only
        }

        if (level == msr::airlib::Utils::kLogLevelError) {
            UE_LOG(LogTemp, Error, TEXT("%s"), *FString(message.c_str()));
        }
        else if (level == msr::airlib::Utils::kLogLevelWarn) {
            UE_LOG(LogTemp, Warning, TEXT("%s"), *FString(message.c_str()));
        }
        else {
            UE_LOG(LogTemp, Log, TEXT("%s"), *FString(message.c_str()));
        }

        //#ifdef _MSC_VER
        //        //print to VS output window
        //        OutputDebugString(std::wstring(message.begin(), message.end()).c_str());
        //#endif

        //also do default logging
        msr::airlib::Utils::Logger::log(level, message);
    }
};

static AUnrealLog GlobalASimLog;

AAirSimGameMode::AAirSimGameMode(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DefaultPawnClass = nullptr;
    PlayerControllerClass = AAirSimPlayerController::StaticClass();
    HUDClass = ASimHUD::StaticClass();

    common_utils::Utils::getSetLogger(&GlobalASimLog);

    //module loading is not allowed outside of the main thread, so we load the ImageWrapper module ahead of time.
    static IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
}

//UGameUserSettings* AAirSimGameMode::GetGameUserSettings()
//{
//    if (GEngine != nullptr)
//    {
//        return GEngine->GameUserSettings;
//    }
//    return nullptr;
//}

void AAirSimGameMode::StartPlay()
{
    Super::StartPlay();

    SweepProblemActors();

    //UGameUserSettings* game_settings = GetGameUserSettings();
    //game_settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
    //game_settings->ApplySettings(true);
}

void AAirSimGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    RegisterProblemActorGuard();
    SweepProblemActors();
}

void AAirSimGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterProblemActorGuard();
    Super::EndPlay(EndPlayReason);
}

void AAirSimGameMode::RegisterProblemActorGuard()
{
    UWorld* World = GetWorld();
    if (World == nullptr || ProblemActorSpawnHandle.IsValid()) {
        return;
    }

    ProblemActorSpawnHandle = World->AddOnActorSpawnedHandler(
        FOnActorSpawned::FDelegate::CreateUObject(this, &AAirSimGameMode::HandleActorSpawned));

    UE_LOG(LogTemp, Warning, TEXT("[AirSimInitVehicleGuard] registered spawn guard for %s"), *GetNameSafe(World));
}

void AAirSimGameMode::UnregisterProblemActorGuard()
{
    UWorld* World = GetWorld();
    if (World != nullptr && ProblemActorSpawnHandle.IsValid()) {
        World->RemoveOnActorSpawnedHandler(ProblemActorSpawnHandle);
    }
    ProblemActorSpawnHandle.Reset();
}

void AAirSimGameMode::SweepProblemActors()
{
    UWorld* World = GetWorld();
    if (World == nullptr) {
        return;
    }

    int32 DisabledCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It) {
        AActor* Actor = *It;
        if (ShouldDisableProblemActor(Actor)) {
            DisableProblemActor(Actor);
            ++DisabledCount;
        }
    }

    if (DisabledCount > 0) {
        UE_LOG(LogTemp, Warning, TEXT("[AirSimInitVehicleGuard] disabled %d pre-existing InitializeVehicles actor(s)"), DisabledCount);
    }
}

void AAirSimGameMode::HandleActorSpawned(AActor* SpawnedActor)
{
    if (ShouldDisableProblemActor(SpawnedActor)) {
        DisableProblemActor(SpawnedActor);
    }
}

bool AAirSimGameMode::ShouldDisableProblemActor(const AActor* Actor) const
{
    if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) {
        return false;
    }

    const UClass* ActorClass = Actor->GetClass();
    const FString ActorName = Actor->GetName();
    const FString ActorPath = Actor->GetPathName();
    const FString ClassName = ActorClass ? ActorClass->GetName() : FString();
    const FString ClassPath = ActorClass ? ActorClass->GetPathName() : FString();

    return ActorName.Contains(TEXT("InitializeVehicles"))
        || ClassName.Contains(TEXT("InitializeVehicles"))
        || ActorPath.Contains(TEXT("InitializeVehicles"))
        || ClassPath.Contains(TEXT("/Game/Effect/Vehicle/Blueprint/InitializeVehicles"));
}

void AAirSimGameMode::DisableProblemActor(AActor* Actor) const
{
    if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) {
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[AirSimInitVehicleGuard] neutralizing problematic actor %s class=%s"),
        *Actor->GetPathName(),
        *GetNameSafe(Actor->GetClass()));

    Actor->SetActorTickEnabled(false);
    Actor->SetActorEnableCollision(false);
    Actor->SetActorHiddenInGame(true);
    Actor->SetCanBeDamaged(false);
    Actor->Tags.AddUnique(TEXT("Codex_DisabledInitializeVehicles"));

    TInlineComponentArray<UActorComponent*> Components(Actor);
    for (UActorComponent* Component : Components) {
        if (!IsValid(Component)) {
            continue;
        }

        Component->SetComponentTickEnabled(false);
        Component->Deactivate();

        if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component)) {
            SceneComponent->SetVisibility(false, true);
            SceneComponent->SetHiddenInGame(true, true);
        }
    }
}

