/* The native PE linker resolves references even in unrelated parts of the
 * complete main/menu translation units. These excluded-system entry points
 * must never execute in the lifecycle fixture. No arguments are inspected or
 * return values supplied: every unexpected call aborts. */
#include <stdio.h>
#include <stdlib.h>

#define UNUSED(name) _Noreturn void name(void) { fputs("Unexpected excluded call: " #name "\n", stderr); abort(); }

UNUSED(AdjustRubberBandAI) UNUSED(AdvanceGrandPrixTrack) UNUSED(AdvancePlayerAnimation)
UNUSED(AnimateTrackObjects) UNUSED(ApplyViewportGeometry) UNUSED(AutoSelectDemo)
UNUSED(BuildCameraView) UNUSED(CalculateChampionshipPoints) UNUSED(CharacterSelectScreen)
UNUSED(CheckRaceCompletion) UNUSED(ComputeRacePositions) UNUSED(CourseSelectScreen)
UNUSED(FindNearestWaypoint) UNUSED(FindSonicRCD) UNUSED(GetLogicalCDTrack) UNUSED(getopt_long)
UNUSED(InitCD) UNUSED(InitCDPlayer) UNUSED(InitDefaultTimeTables) UNUSED(InitDirectInput)
UNUSED(InitDirectSound) UNUSED(InitInputMappings) UNUSED(InitLevel) UNUSED(InitOptionStuff)
UNUSED(InitPadTypes) UNUSED(InitRaceStart) UNUSED(InitRandomTables) UNUSED(InitSaveData)
UNUSED(InitSineTable) UNUSED(InitSonicR_A) UNUSED(InitTitleScreen) UNUSED(InitWeather)
UNUSED(LoadAllGhostTimes) UNUSED(LoadCharacterGouraudTables) UNUSED(LoadGameSettings)
UNUSED(LoadGameState) UNUSED(LoadGhostData) UNUSED(LoadKeyMappings) UNUSED(LoadTextureSubRect)
UNUSED(LoadTitleTextureD3D) UNUSED(LoadTrackSinglePlayer) UNUSED(MainMenuScreen)
UNUSED(MatchmakerCreateSession) UNUSED(MatchmakerGenerateQR) UNUSED(MatchmakerGetCode)
UNUSED(MatchmakerGetToken) UNUSED(MatchmakerLoadToken) UNUSED(MatchmakerPollToken)
UNUSED(MatchmakerRefreshToken) UNUSED(MatchmakerRequestCode) UNUSED(MatchmakerSetFallbackUsername)
UNUSED(MultiPlayerModeSelect) UNUSED(MultiPlayerScreen) UNUSED(Music_SetVolume)
UNUSED(platform_base_path) UNUSED(platform_init) UNUSED(PlayerPhysicsMain) UNUSED(qrcodegen_getModule)
UNUSED(R_ClearNoColorKey) UNUSED(R_SetNoColorKey) UNUSED(R_ThawTexture) UNUSED(Random)
UNUSED(RelocateModelVertices) UNUSED(RemapCharacterTpages) UNUSED(RenderBalloonModelForResultsScreen)
UNUSED(RenderCharacterCredits) UNUSED(RenderEnvMappedModel3D) UNUSED(RenderHUD) UNUSED(RenderLogoQuads)
UNUSED(ReplayVoice_Start) UNUSED(ReplayVoice_Tick) UNUSED(ResetInputState)
UNUSED(ResolvePlayerCollision) UNUSED(SaveGameSettings) UNUSED(SaveGhostData)
UNUSED(SaveKeyMappings) UNUSED(SavePadTypesImpl) UNUSED(ScanInputForAction) UNUSED(SegaLogoScreen)
UNUSED(SelectCDTrack) UNUSED(SetAllSoundVolumes) UNUSED(SetScreenDimensions) UNUSED(SetTitleTextureFile)
UNUSED(SetupD3DTexturesBegin) UNUSED(SetupReplayData) UNUSED(SetupSpecialRace)
UNUSED(SetViewportClipRect) UNUSED(SetViewportFromConfig) UNUSED(SFX_DuckTick)
UNUSED(ShowEmeraldUnlockScreen) UNUSED(Shutdown) UNUSED(SpawnFootShadows) UNUSED(SpawnOtherParticle)
UNUSED(SpawnPlayerParticleEffects) UNUSED(StopAmbientSounds) UNUSED(StopCD) UNUSED(SweepPlayerCollision)
UNUSED(TickPlayerAnimation) UNUSED(TimeAttackModeSelect) UNUSED(TintBackgroundTPage)
UNUSED(TintCharacterGouraudTables) UNUSED(TitleScreen) UNUSED(TrackSurfaceAI) UNUSED(TravellersTalesLogoScreen)
UNUSED(Update5PlayerRacePhysics) UNUSED(UpdateCameraConfig) UNUSED(UpdateCityAnimations)
UNUSED(UpdateFactoryAnimations) UNUSED(UpdateFlyoverCamera) UNUSED(UpdateGameLogic)
UNUSED(UpdateHumanPlayerPhysics) UNUSED(UpdateIslandAnimations) UNUSED(UpdateLapCounter)
UNUSED(UpdateMissiles) UNUSED(UpdatePerPlayerInput) UNUSED(UpdatePlayerAnimation)
UNUSED(UpdatePlayerLapSector) UNUSED(UpdatePlayerMovement) UNUSED(UpdatePlayerPhysicsA)
UNUSED(UpdatePlayerPhysicsB) UNUSED(UpdatePlayfieldGridPositions) UNUSED(UpdateRaceObjects)
UNUSED(UpdateRaceRings) UNUSED(UpdateRuinAnimations) UNUSED(UpdateTrackObjects)
UNUSED(UpdateTrackWorld) UNUSED(UpdateWeather) UNUSED(UpdateWeatherCounters) UNUSED(UpdateWeatherEffects)
UNUSED(UpnpOpenPort)

int g_glowPrevChar5, g_glowPrevChar7, g_glowPrevChar8, g_screenHeightBase;
unsigned char s_keystate[256];
char *optarg;
int optind;
