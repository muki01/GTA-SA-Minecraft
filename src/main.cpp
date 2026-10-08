// GTA San Andreas - Minecraft mod (MinecraftSA.asi)
#include "ModCommon.h"

#include "GameVersion.h"

#include "Combat.h"
#include "Config.h"
#include "Game.h"
#include "GeoCut.h"
#include "Gui.h"
#include "McMenu.h"
#include "Movement.h"
#include "PedSkins.h"
#include "Player3D.h"
#include "Sound.h"
#include "Render3D.h"
#include "Textures.h"

using namespace plugin;

class MinecraftSA {
public:
    MinecraftSA() {
        mc::Log("MinecraftSA 0.13 loading (game version: %s)", GetGameVersionName());
        if (!IsGameVersion10us()) {
            mc::Log("ERROR: only GTA SA 1.0 US is supported, mod disabled");
            MessageBoxA(nullptr, "MinecraftSA: sadece GTA San Andreas 1.0 US surumu destekleniyor.", "MinecraftSA",
                        MB_ICONWARNING);
            return;
        }
        mc::LoadConfig();

        Events::initRwEvent += [] {
            mc::LoadTextures();
            mc::InstallCombatHooks();
            mc::InstallMovementHooks();
            mc::InstallMenuHooks();
            mc::InstallGeoCutHooks();
            mc::Render3DInit();
        };
        Events::initGameEvent += [] { mc::GameInit(); };
        Events::reInitGameEvent += [] { mc::GameOnNewSession(); };
        Events::restartGameEvent += [] { mc::GameOnNewSession(); };
        Events::processScriptsEvent += [] { mc::GameProcess(); };
        Events::gameProcessEvent += [] { mc::GameAfterProcess(); };
        Events::renderSceneEvent += [] {
            if (!mc::Render3DHooked())
                mc::Render3D();
        };
        Events::vehicleRenderEvent += [](CVehicle* vehicle) { mc::RenderVehicleOccupants(vehicle); };
        Events::drawingEvent += [] {
            mc::RenderFirstPersonHand();
            mc::GuiDrawHud();
        };
        Events::onPauseAllSounds += [] { mc::PauseAllSfx(true); };
        Events::onResumeAllSounds += [] { mc::PauseAllSfx(false); };
        Events::shutdownRwEvent += [] {
            mc::GameShutdown();
            mc::UnloadTextures();
        };
    }
} gMinecraftSA;
