#include "RoseCharSelectCamPath.h"
#include "Components/SplineComponent.h"

ARoseCharSelectCamPath::ARoseCharSelectCamPath()
{
	PrimaryActorTick.bCanEverTick = false;
	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
	Spline->SetClosedLoop(true);
}
