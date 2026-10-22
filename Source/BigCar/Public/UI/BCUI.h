// Copyright BigCar Team. All Rights Reserved.
//
// Factory functions for every Slate screen. Screens are code-only (no UMG assets) and read all text through the
// localization tables (ui.* keys in Content/BigCar/Data/Localization/<lang>/ui.json).
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"

class SWidget;
class ABCPlayerController;

namespace BCUI
{
BIGCAR_API TSharedRef<SWidget> MakeMainMenu(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeCampaign(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeQuickPlay(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeOnline(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeGarage(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeStore(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeSettings(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakePause(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeResults(ABCPlayerController* PC);
BIGCAR_API TSharedRef<SWidget> MakeStory(ABCPlayerController* PC);
// In-game HUD (speed, boost, damage silhouette, timer, scores, feed, popups, radar, scoreboard while Tab is held).
BIGCAR_API TSharedRef<SWidget> MakeHud(ABCPlayerController* PC);
} // namespace BCUI
