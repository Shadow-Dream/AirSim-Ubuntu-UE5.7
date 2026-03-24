// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameUserSettings.h"
#include "AirSimGameMode.generated.h"

/**
 * 
 */
UCLASS()
class AIRSIM_API AAirSimGameMode : public AGameMode
{
public:
    GENERATED_BODY()

    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void StartPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    AAirSimGameMode(const FObjectInitializer& ObjectInitializer);

private:
    void RegisterProblemActorGuard();
    void UnregisterProblemActorGuard();
    void SweepProblemActors();
    void HandleActorSpawned(AActor* SpawnedActor);
    bool ShouldDisableProblemActor(const AActor* Actor) const;
    void DisableProblemActor(AActor* Actor) const;

    FDelegateHandle ProblemActorSpawnHandle;

    //UGameUserSettings* GetGameUserSettings();
};

