#include "AirSimPlayerController.h"

AAirSimPlayerController::AAirSimPlayerController()
{
    bShowMouseCursor = false;
    bEnableStreamingSource = true;
}

void AAirSimPlayerController::BeginPlay()
{
    Super::BeginPlay();

    FInputModeGameOnly InputMode;
    SetInputMode(InputMode);
}
