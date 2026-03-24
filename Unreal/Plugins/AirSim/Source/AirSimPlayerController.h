#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AirSimPlayerController.generated.h"

UCLASS()
class AIRSIM_API AAirSimPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AAirSimPlayerController();

protected:
    virtual void BeginPlay() override;
};
