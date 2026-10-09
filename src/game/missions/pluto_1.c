// MISSIONS\PLUTO\1\MISSIONR.DLL - faithful C port.
//
// The final mission. The lander (mob 0x11) starts on Pluto's surface towing the first cortex
// (pod 0x10c0027). Four beacon switches 0xa001c..1f light four emitters; with all four lit the
// surface guards wake and trigger plane 0x1a0045 becomes the way underground: everything is
// teleported (SetupOrientationTeleport between two trigger planes / markers) into the interior
// (spinning spikes 0xf0020..2b, gates on switches 0xa003c..3f, the transport torus between planes
// 0x1a004a/4b). Trigger 0x40070 leads into the central core (an inertial frame): placing the
// cortex in the socket (trigger 0x40009 + stage) lights its stage and the lander is sent to the next
// area to fetch the next cortex: area 2 (pod 0x10c0026, guards, lifts on switches), area 3
// (0x10c0025, an airlock on switch 0xa005e), area 4 (0x10c0024). Each area's exit plane takes the
// lander (and the cortex if hooked) back to the core. After the fourth socket: end film OUTRO,
// MissionScore_End(1). Every area change sets gravity / air / thrust for the place.
//
// Addresses are the DLL's (image base 0x10000000). DLL exports: LevelLoadLevel 0x100029a0,
// LevelStartLevel_Lander 0x100029b0 (-> 0x10003950), LevelStartLevel_Flycam 0x100029c0
// (-> 0x10003a40), LevelStopLevel 0x100029d0 (-> empty 0x10003a60), LevelGetVersion 0x100029e0.
// The score library (0x10003120-0x100036cf, Init 0x100035f0, End 0x10003470) and the destroyed-
// sound handler (0x10001dc0) are the common code in mission_lib.c. Note: the objdump listing
// misses the function at 0x100019c0 (it follows a jump table); it is the mission's end.
#include <string.h>
#include "mission_lib.h"
#include "missions.h"

// ---- objects ------------------------------------------------------------------------------------
#define PL_LANDER_ID     MOB_ID(OT_LANDER, 0x11)           // 0x01010011
#define PL_CAMERA_ID     MOB_ID(OT_CAMERA, 2)              // 0x01060002
#define PL_MOB_LANDER    0x11
#define PL_MOB_FLYCAM    0xc
#define PL_ANIM(n)       OBJ_ID(OT_ANIMOBJECT, (n))        // 0x000300nn
#define PL_TRIGGER(n)    OBJ_ID(OT_TRIGGERBOX, (n))        // 0x000400nn
#define PL_SWITCH(n)     OBJ_ID(OT_SWITCH, (n))            // 0x000a00nn
#define PL_DESTR(n)      OBJ_ID(OT_DESTRUCTIBLE, (n))      // 0x000800nn
#define PL_ANIMDESTR(n)  OBJ_ID(OT_ANIMDESTRUCTIBLE, (n))  // 0x000f00nn
#define PL_EMITTER(n)    OBJ_ID(OT_PARTICLEEMITTER, (n))   // 0x000d00nn
#define PL_PLANE(n)      OBJ_ID(OT_TRIGGERPLANE, (n))      // 0x001a00nn
#define PL_ANIMTURRET(n) MOB_ID(OT_ANIMTURRET, (n))        // 0x010e00nn (mobs)
#define PL_FRAME(n)      MOB_ID(OT_INERTIALFRAME, (n))     // 0x011400nn (mobs)
#define PL_MARKER(n)     MOB_ID(OT_MARKER, (n))            // 0x011900nn (mobs)
#define PL_PICKUP(n)     OBJ_ID(OT_PICKUP, (n))
#define PL_FUEL_FULL     20000000                          // 0x1312d00

// ---- static data (.data) --------------------------------------------------------------------------
static const char pl_movie[] = "OUTRO";              // 0x10009030
static const int32_t pl_airlockBusyInit = 1;         // 0x10009038
static const TurretType pl_turretType7 = {           // 0x10009040 -> gTurretTypes[7]
    { -19, 0, 793, 0 }, 0x00, 0, 3, 7, 60, 0x30, 1024, 12000, 0
};
static const TurretType pl_turretType3 = {           // 0x10009060 -> gTurretTypes[3]
    { 0, 18, 158, 0 }, 0x05, 0, 8, 5, 80, 0x20, 1024, 12000, 0
};
// 0x10009080..0x100090a7: EV_TRIGGER_ENTER filters {object: any, trigger}
static const intptr_t pl_filter67[2] = { -1, (intptr_t)PL_TRIGGER(0x67) };
static const intptr_t pl_filter68[2] = { -1, (intptr_t)PL_TRIGGER(0x68) };
static const intptr_t pl_filter69[2] = { -1, (intptr_t)PL_TRIGGER(0x69) };
static const intptr_t pl_filter6A[2] = { -1, (intptr_t)PL_TRIGGER(0x6a) };
static const intptr_t pl_filter6B[2] = { -1, (intptr_t)PL_TRIGGER(0x6b) };
static const int32_t  pl_nbWaypoints = 1;            // 0x100090a8
static const Waypoint pl_waypoints[1] = {            // 0x100090b0
    { -108800, -6400 },
};
static const TurretType pl_turretType21 = {          // 0x100090b8 -> gTurretTypes[21]
    { 384, 4736, 0, 0 }, 0x00, 0, 6, 2, 32, 0x30, 800, 12000, 0
};
// 0x100090d8: beacon emitter positions (0xd0059..5c), then 0x10009118 for 0xd005d
static const int32_t pl_beaconPos[4][4] = {
    { -105138, -87678, -2195, 0 }, { -97111, -87678, -2202, 0 },
    { -97111, -87678, 2203, 0 }, { -105125, -87678, 2210, 0 },
};
static const int32_t pl_beaconCentrePos[4] = { -101120, -82682, 17, 0 };
static const int32_t pl_gateTurnInit = 2;            // 0x1000963c
// 0x10009268: [language][9]
static const char *pl_text[3][9];

// ---- DLL globals --------------------------------------------------------------------------------
static struct {
    int32_t      airlockBusy;   // 0x10009038 (.data 1)
    int32_t      gateTurn;      // 0x1000963c (.data 2) which gate switch 0xa003f closes next
    int32_t      area4Hooked;   // 0x1000bd58 area 4's cortex is hooked
    int32_t      area3Hooked;   // 0x1000bd5c
    int32_t      area2Hooked;   // 0x1000bd60
    int32_t      guard67, guard68, guard69, guard6A, guard6B;   // 0x1000bd68..78 handler registered
    int32_t      beaconsLit;    // 0x1000bd7c
    Window      *win;           // 0x1000bda4 objective line
    const char *const *text;    // 0x1000c1e8 = pl_text[gLanguage]
    ObjId        lander;        // 0x1000c1ec
    ObjId        camera;        // 0x1000c1f0
    ObjId        cortex1;       // 0x1000c24c pod 0x10c0027 (towed from the start)
    ObjId        cortex2;       // 0x1000c250 pod 0x10c0026 (area 2)
    ObjId        cortex3;       // 0x1000c254 pod 0x10c0025 (area 3)
    ObjId        socketPlane;   // 0x1000c258 the core's exit plane for this stage
    int32_t      coreWithCortex;// 0x1000c25c (written, never read)
    ObjId        socket;        // 0x1000c260 the socket trigger of this stage
    int32_t      stage;         // 0x1000c264 1..4 sockets, 5 done
    ObjId        stageCortex;   // 0x1000c268
    ObjId        coreLeave;     // 0x1000c26c trigger that arms the exit plane when left
    ObjId        cortex4;       // 0x1000c270 pod 0x10c0024 (area 4)
} st;

// 0x100036d0  move lander / pod (orientation) and camera (position) from one plane to another
static void PL_Teleport(ObjId lander, ObjId pod, ObjId camera, ObjId from, ObjId to)
{
    SetupOrientationTeleport(from, to);
    if (lander != OBJ_NONE)
        OrientationTeleport(lander);
    if (pod != OBJ_NONE)
        OrientationTeleport(pod);
    if (camera != OBJ_NONE)
        PositionTeleport(camera);
}

static void PL_EnterCore(int32_t withCortex);
static void PL_ReturnFromCore(void);

// ================================================================================================
// Area 4 (fourth cortex)
// ================================================================================================

// 0x10001000  EV_POD_HOOKED: area 4's cortex is hooked; its guards wake
static void PL_Area4PodHooked(const LanderEvent *ev)
{
    (void)ev;
    WindowSetText(st.win, st.text[4]);           // the socket objective
    Object_SetProperty(PL_DESTR(0x35), PROP_HITPOINTS, 0);
    Object_SetProperty(PL_DESTR(0x36), PROP_HITPOINTS, 0);
    Object_SetProperty(PL_DESTR(0x37), PROP_HITPOINTS, 0);
    st.area4Hooked = 1;
    MobObj_Wake(0xd);
    MobObj_Wake(0xe);
    MobObj_Wake(0xf);
    for (int32_t id = (int32_t)PL_ANIMTURRET(0xd); id <= (int32_t)PL_ANIMTURRET(0xf); id++)
        Sound_PlayLoop((ObjId)id, 0x39a, -1);
    AnimPlay(PL_ANIMTURRET(0xd), 0x1d, 0x61);
    AnimPlay(PL_ANIMTURRET(0xe), 0x1e, 0x61);
    AnimPlay(PL_ANIMTURRET(0xf), 0x1f, 0x61);
}

static void PL_Area4Exit(void);

// 0x100010d0  EV_TRIGGER_PLANE 0x1a0042: back to the core
static void PL_Area4ExitPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_PLANE(0x42))
        return;
    Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
    PL_Area4Exit();
}

// 0x10001100  EV_DESTROYED in area 4
static void PL_Area4Destroyed(const LanderEvent *ev)
{
    Sound_PlayOnce((ObjId)ev->arg[0], 0x39b);
}

// 0x10001120  EV_TRIGGER_LEAVE 0x4006e: the exit plane is armed
static void PL_Area4LeaveTrigger(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_TRIGGER(0x6e))
        return;
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_Area4ExitPlane);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, PL_Area4LeaveTrigger);
}

// 0x10001150
static void PL_Area4Exit(void)
{
    EventType_RemoveHandler(EV_POD_HOOKED, PL_Area4PodHooked);
    EventType_RemoveHandler(EV_TRIGGER_PLANE, PL_Area4ExitPlane);
    EventType_RemoveHandler(EV_DESTROYED, PL_Area4Destroyed);
    if (st.area4Hooked == 0)
        MobObj_Sleep((int32_t)(st.cortex4 & 0xffff));
    MobObj_Sleep(0x30);
    MobObj_Sleep(0x28);
    MobObj_Sleep(1);
    Lander_SetHeadLight(PL_LANDER_ID, 0, 0);
    if (st.area4Hooked != 0) {
        PL_Teleport(st.lander, st.cortex4, st.camera, PL_PLANE(0x42), PL_PLANE(0x49));
    } else {
        MobObj_Sleep((int32_t)(st.cortex4 & 0xffff));   // sic: slept twice (the engine ignores it)
        PL_Teleport(st.lander, OBJ_NONE, st.camera, PL_PLANE(0x42), PL_PLANE(0x49));
    }
    PL_EnterCore(st.area4Hooked != 0);
}

// 0x10001230
static void PL_Area4Setup(void)
{
    for (int32_t id = (int32_t)PL_ANIMTURRET(0xd); id <= (int32_t)PL_ANIMTURRET(0x10); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    SetGravity(0xb);
    gAirResistance = 8;
    gMainThrottlePower = 0x19;
    MobObj_Wake(1);
    Object_SetProperty(PL_FRAME(1), PROP_TARGET, st.lander);
    Environment_SetInteriorAmbient(0xf, 0xc, 0x18);
    Lander_SetHeadLight(PL_LANDER_ID, 0x31c, 0xc00);
    MobObj_Wake((int32_t)(st.cortex4 & 0xffff));
    MobObj_Wake(0x30);
    MobObj_Wake(0x28);
    EventType_AddHandler(EV_POD_HOOKED, NULL, PL_Area4PodHooked);
    EventType_AddHandler(EV_DESTROYED, NULL, PL_Area4Destroyed);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, PL_Area4LeaveTrigger);
}

// ================================================================================================
// The central core and its sockets
// ================================================================================================

// 0x10001320  EV_TRIGGER_PLANE: the lander crossed this stage's exit plane
static void PL_CoreExitPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != st.lander || (ObjId)ev->arg[1] != st.socketPlane)
        return;
    Object_SetProperty(st.lander, PROP_HITPOINTS, 0xff);
    PL_ReturnFromCore();
    Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
    EventType_RemoveHandler(EV_TRIGGER_PLANE, PL_CoreExitPlane);
}

static void PL_SetStage(int32_t stage);
static void PL_SocketFxOff(void);

// 0x10001380  a cortex is in its socket: next stage
static void PL_SocketFilled(void)
{
    const char *msg;
    MobObj_Sleep((int32_t)(st.stageCortex & 0xffff));
    switch (st.stage) {
    case 1:
        MobObj_Wake(8);
        Sound_PlayLoop(MOB_ID(OT_POD, 8), 0x3a8, -1);
        Object_SetDead(PL_DESTR(0x82));
        Object_SetAlive(PL_DESTR(0x83));
        PL_SocketFxOff();
        PL_SetStage(2);
        msg = st.text[5];                        // the second cortex
        break;
    case 2:
        MobObj_Wake(9);
        Sound_PlayLoop(MOB_ID(OT_POD, 9), 0x3a8, -1);
        PL_SocketFxOff();
        Object_SetDead(PL_DESTR(0x83));
        Object_SetAlive(PL_DESTR(0x84));
        PL_SetStage(3);
        msg = st.text[6];                        // the third cortex
        break;
    case 3:
        MobObj_Wake(0xa);
        Sound_PlayLoop(MOB_ID(OT_POD, 0xa), 0x3a8, -1);
        PL_SocketFxOff();
        Object_SetDead(PL_DESTR(0x84));
        Object_SetAlive(PL_DESTR(0x85));
        PL_SetStage(4);
        msg = st.text[7];                        // the last cortex
        break;
    case 4:
        MobObj_Wake(0xb);
        Sound_PlayLoop(MOB_ID(OT_POD, 0xb), 0x3a8, -1);
        PL_SocketFxOff();
        Object_SetDead(PL_DESTR(0x85));
        st.stage = 5;
        PL_ReturnFromCore();
        EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_CoreExitPlane);
        return;
    default:
        EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_CoreExitPlane);
        return;
    }
    WindowSetText(st.win, msg);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_CoreExitPlane);
}

// 0x10001530  EV_TRIGGER_ENTER: the cortex reached the socket trigger
static void PL_OnSocketTrigger(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != st.socket)
        return;
    Object_SetProperty(st.stageCortex, PROP_TARGET, -1);
    Object_SetProperty(st.socket, PROP_TARGET, -1);
    Object_SetDead(st.socket);
    PL_SocketFilled();
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_OnSocketTrigger);
}

// 0x10001590  EV_TRIGGER_LEAVE: came without the cortex: leaving the arrival trigger arms the
// exit plane
static void PL_OnCoreLeave(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != st.coreLeave)
        return;
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_CoreExitPlane);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, PL_OnCoreLeave);
}

// 0x100015d0  in the core (its inertial frame mob 7 follows the lander)
static void PL_EnterCore(int32_t withCortex)
{
    MobObj_Wake(7);
    Object_SetProperty(PL_FRAME(7), PROP_TARGET, st.lander);
    Environment_SetInteriorAmbient(0x99, 0x99, 0x99);
    Object_SetProperty(st.lander, PROP_HITPOINTS, 0xff);
    st.coreWithCortex = withCortex;
    if (withCortex != 0)
        EventType_AddHandler(EV_TRIGGER_ENTER, NULL, PL_OnSocketTrigger);
    else
        EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, PL_OnCoreLeave);
}

static void PL_Area2Setup(void);
static void PL_Area3Setup(void);
static void PL_TheEnd(void);

// 0x10001660  leave the core for the area of the current stage (or the end)
static void PL_ReturnFromCore(void)
{
    MobObj_Sleep(7);
    if (st.stage == 3) {
        Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
        PL_Teleport(st.lander, OBJ_NONE, st.camera, PL_PLANE(0x48), PL_PLANE(0x41));
        PL_Area3Setup();
        return;
    }
    if (st.stage == 2) {
        Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
        PL_Teleport(st.lander, OBJ_NONE, st.camera, PL_PLANE(0x47), PL_PLANE(0x40));
        PL_Area2Setup();
        return;
    }
    if (st.stage == 4) {
        Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
        PL_Teleport(st.lander, OBJ_NONE, st.camera, PL_PLANE(0x49), PL_PLANE(0x42));
        PL_Area4Setup();
        return;
    }
    PL_TheEnd();
}

// 0x10001730  stage n: its socket, cortex, exit plane (jump table 0x100018a4)
static void PL_SetStage(int32_t stage)
{
    st.stage = stage;
    switch (stage) {
    case 1:                                                     // 0x1000174b
        st.coreLeave = PL_TRIGGER(0x72);
        st.stageCortex = st.cortex1;
        st.socketPlane = PL_PLANE(0x46);
        st.socket = PL_TRIGGER(0x09);
        Object_SetProperty(PL_TRIGGER(0x09), PROP_TARGET, st.cortex1);
        Object_AdvanceDestroyLevel(PL_DESTR(0x7e), -1);
        break;
    case 2:                                                     // 0x10001796
        st.coreLeave = PL_TRIGGER(0x73);
        st.stageCortex = st.cortex2;
        st.socketPlane = PL_PLANE(0x47);
        st.socket = PL_TRIGGER(0x0a);
        Object_SetProperty(PL_TRIGGER(0x0a), PROP_TARGET, st.cortex2);
        Object_AdvanceDestroyLevel(PL_DESTR(0x78), -1);
        Object_AdvanceDestroyLevel(PL_DESTR(0x7f), -1);
        break;
    case 3:                                                     // 0x100017ef
        st.coreLeave = PL_TRIGGER(0x74);
        st.stageCortex = st.cortex3;
        st.socketPlane = PL_PLANE(0x48);
        st.socket = PL_TRIGGER(0x0b);
        Object_SetProperty(PL_TRIGGER(0x0b), PROP_TARGET, st.cortex3);
        Object_AdvanceDestroyLevel(PL_DESTR(0x80), -1);
        Object_AdvanceDestroyLevel(PL_DESTR(0x79), -1);
        break;
    case 4:                                                     // 0x10001848
        st.coreLeave = PL_TRIGGER(0x75);
        st.stageCortex = st.cortex4;
        st.socketPlane = PL_PLANE(0x49);
        st.socket = PL_TRIGGER(0x0c);
        Object_SetProperty(PL_TRIGGER(0x0c), PROP_TARGET, st.cortex4);
        Object_AdvanceDestroyLevel(PL_DESTR(0x81), -1);
        Object_AdvanceDestroyLevel(PL_DESTR(0x7a), -1);
        break;
    }
}

// 0x100018c0  the filled socket's markers go back to their first model, their sounds stop
// (jump table 0x100019ac)
static void PL_SocketFxOff(void)
{
    switch (st.stage) {
    case 1:
        Object_AdvanceDestroyLevel(PL_DESTR(0x7e), 1);
        Sound_StopObject(PL_DESTR(0x7e));
        break;
    case 2:
        Object_AdvanceDestroyLevel(PL_DESTR(0x7f), 1);
        Sound_StopObject(PL_DESTR(0x7f));
        Sound_StopObject(PL_DESTR(0x78));
        Object_AdvanceDestroyLevel(PL_DESTR(0x78), 1);
        break;
    case 3:
        Object_AdvanceDestroyLevel(PL_DESTR(0x80), 1);
        Sound_StopObject(PL_DESTR(0x80));
        Sound_StopObject(PL_DESTR(0x79));
        Object_AdvanceDestroyLevel(PL_DESTR(0x79), 1);
        break;
    case 4:
        Object_AdvanceDestroyLevel(PL_DESTR(0x81), 1);
        Object_AdvanceDestroyLevel(PL_DESTR(0x7a), 1);
        Sound_StopObject(PL_DESTR(0x81));
        Sound_StopObject(PL_DESTR(0x7a));
        break;
    }
}

// 0x100019c0  all four cortexes placed
static void PL_TheEnd(void)
{
    SetLevelEndMovie(pl_movie);
    MissionScore_End(1);                                        // 0x10003470
}

// 0x100019e0  first arrival in the core (from the interior, with the first cortex)
static void PL_EnterCoreFirst(void)
{
    SetGravity(0xb);
    gAirResistance = 0x10;
    gMainThrottlePower = 0x19;
    PL_SetStage(1);
    PL_EnterCore(1);
    WindowSetText(st.win, st.text[4]);           // the socket objective
}

// ================================================================================================
// Area 3 (third cortex): the airlock
// ================================================================================================

static void PL_AirlockSwitch(const LanderEvent *ev);
static void PL_AirlockStep1(const LanderEvent *ev);
static void PL_AirlockStep2(const LanderEvent *ev);

// 0x10001a40  EV_SWITCH while the airlock cycles: any switch shot is undone
static void PL_AirlockRevert(const LanderEvent *ev)
{
    Object_SetProperty((ObjId)ev->arg[0], PROP_STATE, 1 - (int32_t)ev->arg[1]);
}

// 0x10001a60  EV_ANIM_FINISHED 0x30039: the cycle is complete
static void PL_AirlockStep3(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_ANIM(0x39))
        return;
    AnimPlay(PL_ANIM(0x38), 0, 4);
    AnimSetUpdateInterval(PL_ANIM(0x38), 0x14);
    Object_SetProperty(PL_SWITCH(0x5e), PROP_STATE, 1 - (int32_t)Object_GetProperty(PL_SWITCH(0x5e), PROP_STATE));
    EventType_AddHandler(EV_SWITCH, NULL, PL_AirlockSwitch);
    st.airlockBusy = 1;
    EventType_RemoveHandler(EV_SWITCH, PL_AirlockRevert);
    EventType_RemoveHandler(EV_ANIM_FINISHED, PL_AirlockStep3);
}

// 0x10001b00  EV_ANIM_FINISHED 0x30038
static void PL_AirlockStep2(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_ANIM(0x38))
        return;
    AnimPlay(PL_ANIM(0x39), 0, 4);
    AnimSetUpdateInterval(PL_ANIM(0x39), 0x28);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, PL_AirlockStep3);
    EventType_RemoveHandler(EV_ANIM_FINISHED, PL_AirlockStep2);
}

// 0x10001b60  EV_ANIM_FINISHED 0x30039
static void PL_AirlockStep1(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_ANIM(0x39))
        return;
    AnimPlay(PL_ANIM(0x38), 0, 4);
    AnimSetUpdateInterval(PL_ANIM(0x38), 0x14);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, PL_AirlockStep2);
    EventType_RemoveHandler(EV_ANIM_FINISHED, PL_AirlockStep1);
}

// 0x10001bc0  EV_SWITCH 0xa005e: start the airlock cycle
static void PL_AirlockSwitch(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_SWITCH(0x5e))
        return;
    Sound_PlayOnce(PL_SWITCH(0x5e), 0x398);
    st.airlockBusy = 0;
    AnimPlay(PL_ANIM(0x39), 0, 0);
    AnimSetUpdateInterval(PL_ANIM(0x39), 0x14);
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, PL_AirlockStep1);
    EventType_AddHandler(EV_SWITCH, NULL, PL_AirlockRevert);
    EventType_RemoveHandler(EV_SWITCH, PL_AirlockSwitch);
}

// 0x10001c50  EV_TRIGGER_ENTER/LEAVE 0x4003b: door 0x3003a
static void PL_Door3A(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_TRIGGER(0x3b))
        return;
    if (ev->type == EV_TRIGGER_ENTER) {
        AnimPlay(PL_ANIM(0x3a), 0, 0);
        return;
    }
    AnimPlay(PL_ANIM(0x3a), 0, 4);
}

// 0x10001c90  EV_DESTROYED 0x8004c: machine 0x30058 starts
static void PL_OnDestroyed4C(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_DESTR(0x4c))
        return;
    AnimPlay(PL_ANIM(0x58), 2, 0);
    Sound_PlayLoop(PL_ANIM(0x58), 0x3a9, -1);
}

// 0x10001cd0  EV_TICK_100MS after the cortex is hooked: once the airlock is idle, open it for good
static void PL_AirlockForceOpen(const LanderEvent *ev)
{
    (void)ev;
    if (st.airlockBusy == 0)
        return;
    EventType_RemoveHandler(EV_SWITCH, PL_AirlockSwitch);
    AnimPlay(PL_ANIM(0x39), 0, 0);
    AnimSetUpdateInterval(PL_ANIM(0x39), 0x14);
    EventType_RemoveHandler(EV_TICK_100MS, PL_AirlockForceOpen);
}

// 0x10001d20  EV_POD_HOOKED in area 3
static void PL_Area3PodHooked(const LanderEvent *ev)
{
    (void)ev;
    EventType_AddHandler(EV_TICK_100MS, NULL, PL_AirlockForceOpen);
    WindowSetText(st.win, st.text[4]);           // the socket objective
    st.area3Hooked = 1;
}

static void PL_Area3Exit(void);

// 0x10001d60  EV_TRIGGER_PLANE 0x1a0041: back to the core
static void PL_Area3ExitPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_PLANE(0x41))
        return;
    Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
    PL_Area3Exit();
}

// 0x10001d90  EV_TRIGGER_LEAVE 0x4006d: the exit plane is armed
static void PL_Area3LeaveTrigger(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_TRIGGER(0x6d))
        return;
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_Area3ExitPlane);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, PL_Area3LeaveTrigger);
}

// 0x10001e50  EV_ANIM_FINISHED 0x30058
static void PL_OnMachine58Done(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_ANIM(0x58))
        return;
    Sound_StopObject(PL_ANIM(0x58));
    Sound_PlayOnce(PL_ANIM(0x58), 0xf7);
}

// 0x10001e80
static void PL_Area3Exit(void)
{
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Door3A);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, PL_Door3A);
    EventType_RemoveHandler(EV_DESTROYED, PL_OnDestroyed4C);
    EventType_RemoveHandler(EV_POD_HOOKED, PL_Area3PodHooked);
    EventType_RemoveHandler(EV_TRIGGER_PLANE, PL_Area3ExitPlane);
    EventType_RemoveHandler(EV_DESTROYED, MissionLib_OnDestroyedSound);
    EventType_RemoveHandler(EV_ANIM_FINISHED, PL_OnMachine58Done);
    Lander_SetHeadLight(PL_LANDER_ID, 0, 0);
    if (st.area3Hooked != 0) {
        PL_Teleport(st.lander, st.cortex3, st.camera, PL_PLANE(0x41), PL_PLANE(0x48));
    } else {
        MobObj_Sleep((int32_t)(st.cortex3 & 0xffff));
        PL_Teleport(st.lander, OBJ_NONE, st.camera, PL_PLANE(0x41), PL_PLANE(0x48));
    }
    PL_EnterCore(st.area3Hooked != 0);
}

// 0x10001f60
static void PL_Area3Setup(void)
{
    for (int32_t id = (int32_t)PL_ANIMTURRET(0x29); id <= (int32_t)PL_ANIMTURRET(0x2f); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    SetGravity(0x13);
    gAirResistance = 0x23;
    gMainThrottlePower = 0x25;
    AnimStop(PL_ANIM(0x38), 0);
    Environment_SetInteriorAmbient(0xf, 0xc, 0x18);
    Lander_SetHeadLight(PL_LANDER_ID, 0x31c, 0xc00);
    MobObj_Wake((int32_t)(st.cortex3 & 0xffff));
    MobObj_Wake(0x29);
    MobObj_Wake(0x2a);
    MobObj_Wake(0x2b);
    MobObj_Wake(0x2c);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x29), 0x14);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x2a), 0x14);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x2b), 0x14);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x2c), 0x14);
    for (int32_t id = (int32_t)PL_ANIMTURRET(0x29); id <= (int32_t)PL_ANIMTURRET(0x2c); id++)
        Sound_PlayLoop((ObjId)id, 0x39a, -1);
    AnimPlay(PL_ANIMTURRET(0x29), 0x15, 0x61);
    AnimPlay(PL_ANIMTURRET(0x2a), 0x16, 0x61);
    AnimPlay(PL_ANIMTURRET(0x2b), 0x17, 0x61);
    AnimPlay(PL_ANIMTURRET(0x2c), 0x18, 0x61);
    EventType_AddHandler(EV_SWITCH, NULL, PL_AirlockSwitch);
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, PL_Door3A);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, PL_Door3A);
    EventType_AddHandler(EV_DESTROYED, NULL, PL_OnDestroyed4C);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10001dc0
    EventType_AddHandler(EV_ANIM_FINISHED, NULL, PL_OnMachine58Done);
    EventType_AddHandler(EV_POD_HOOKED, NULL, PL_Area3PodHooked);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, PL_Area3LeaveTrigger);
}

// ================================================================================================
// Area 2 (second cortex)
// ================================================================================================

// 0x10002130  EV_TRIGGER_ENTER (filter 0x40067): guards 0x14..16 attack
static void PL_Guards67(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(PL_ANIMTURRET(0x14), 5, 0x60);
    AnimPlay(PL_ANIMTURRET(0x15), 6, 0x60);
    AnimPlay(PL_ANIMTURRET(0x16), 7, 0x60);
    st.guard67 = 0;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards67);
}

// 0x10002180  (filter 0x40068): guards 0x17/0x18
static void PL_Guards68(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(PL_ANIMTURRET(0x17), 8, 0x60);
    AnimPlay(PL_ANIMTURRET(0x18), 9, 0x60);
    st.guard68 = 0;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards68);
}

// 0x100021c0  (filter 0x40069): guards 0x19/0x1a
static void PL_Guards69(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(PL_ANIMTURRET(0x19), 0xa, 0x60);
    AnimPlay(PL_ANIMTURRET(0x1a), 0xb, 0x60);
    st.guard69 = 0;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards69);
}

// 0x10002200  (filter 0x4006a): guards 0x1b/0x1c, only once the cortex is hooked
static void PL_Guards6A(const LanderEvent *ev)
{
    (void)ev;
    if (st.area2Hooked == 0)
        return;
    MobObj_Wake(0x1b);
    MobObj_Wake(0x1c);
    AnimPlay(PL_ANIMTURRET(0x1b), 0xc, 0x60);
    AnimPlay(PL_ANIMTURRET(0x1c), 0xd, 0x60);
    st.guard6A = 0;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards6A);
}

// 0x10002260  (filter 0x4006b): guards 0x1d..0x23
static void PL_Guards6B(const LanderEvent *ev)
{
    (void)ev;
    AnimPlay(PL_ANIMTURRET(0x1d), 0xe, 0x60);
    AnimPlay(PL_ANIMTURRET(0x1e), 0xf, 0x60);
    AnimPlay(PL_ANIMTURRET(0x1f), 0x10, 0x60);
    AnimPlay(PL_ANIMTURRET(0x20), 0x11, 0x60);
    AnimPlay(PL_ANIMTURRET(0x21), 0x12, 0x60);
    AnimPlay(PL_ANIMTURRET(0x22), 0x13, 0x60);
    AnimPlay(PL_ANIMTURRET(0x23), 0x14, 0x60);
    st.guard6B = 0;
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards6B);
}

// 0x100022f0  EV_DESTROYED 0x8001b: the generator: three more blow up
static void PL_OnDestroyed1B(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_DESTR(0x1b))
        return;
    Sound_StopObject(PL_DESTR(0x1b));
    Sound_StopObject(PL_DESTR(0x00));
    Sound_StopObject(PL_DESTR(0x56));
    Sound_StopObject(PL_DESTR(0x57));
    Object_SetProperty(PL_DESTR(0x00), PROP_HITPOINTS, 0);
    Object_SetProperty(PL_DESTR(0x56), PROP_HITPOINTS, 0);
    Object_SetProperty(PL_DESTR(0x57), PROP_HITPOINTS, 0);
}

// 0x10002360  EV_SWITCH (any switch beeps): 0xa0063 / 0xa0064 move lift 0x3000f
static void PL_OnSwitchLift0F(const LanderEvent *ev)
{
    Sound_PlayOnce((ObjId)ev->arg[0], 0xeb);
    if ((ObjId)ev->arg[0] != PL_SWITCH(0x63) && (ObjId)ev->arg[0] != PL_SWITCH(0x64))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(PL_ANIM(0x0f), 3, 0);
        AnimSetUpdateInterval(PL_ANIM(0x0f), 0xa);
        return;
    }
    AnimPlay(PL_ANIM(0x0f), 3, 4);
    AnimSetUpdateInterval(PL_ANIM(0x0f), 0x14);
}

// 0x100023e0  EV_SWITCH 0xa0065: 0x30010
static void PL_OnSwitch10(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_SWITCH(0x65))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(PL_ANIM(0x10), 3, 0);
        return;
    }
    AnimPlay(PL_ANIM(0x10), 3, 4);
}

// 0x10002420  EV_SWITCH 0xa0066: 0x30011
static void PL_OnSwitch11(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != PL_SWITCH(0x66))
        return;
    if (ev->arg[1] == 1) {
        AnimPlay(PL_ANIM(0x11), 3, 0);
        return;
    }
    AnimPlay(PL_ANIM(0x11), 3, 4);
}

// 0x10002460  EV_POD_HOOKED in area 2
static void PL_Area2PodHooked(const LanderEvent *ev)
{
    (void)ev;
    WindowSetText(st.win, st.text[4]);           // the socket objective
    st.area2Hooked = 1;
}

static void PL_Area2Exit(void);

// 0x10002490  EV_TRIGGER_PLANE 0x1a0040: back to the core
static void PL_Area2ExitPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_PLANE(0x40))
        return;
    Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
    PL_Area2Exit();
}

// 0x100024c0  EV_TRIGGER_LEAVE 0x4006c: the exit plane is armed
static void PL_Area2LeaveTrigger(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_TRIGGER(0x6c))
        return;
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_Area2ExitPlane);
    EventType_RemoveHandler(EV_TRIGGER_LEAVE, PL_Area2LeaveTrigger);
}

// 0x100024f0
static void PL_Area2Exit(void)
{
    EventType_RemoveHandler(EV_SWITCH, PL_OnSwitchLift0F);
    EventType_RemoveHandler(EV_SWITCH, PL_OnSwitch10);
    EventType_RemoveHandler(EV_SWITCH, PL_OnSwitch11);
    EventType_RemoveHandler(EV_DESTROYED, MissionLib_OnDestroyedSound);
    EventType_RemoveHandler(EV_DESTROYED, PL_OnDestroyed1B);
    EventType_RemoveHandler(EV_POD_HOOKED, PL_Area2PodHooked);
    EventType_RemoveHandler(EV_TRIGGER_PLANE, PL_Area2ExitPlane);
    if (st.guard67 != 0)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards67);
    if (st.guard68 != 0)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards68);
    if (st.guard69 != 0)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards69);
    if (st.guard6A != 0)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards6A);
    if (st.guard6B != 0)
        EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_Guards6B);
    for (int32_t mob = 0x14; mob <= 0x1a; mob++)
        MobObj_Sleep(mob);
    for (int32_t mob = 0x1d; mob <= 0x23; mob++)
        MobObj_Sleep(mob);
    if (st.area2Hooked != 0) {
        PL_Teleport(st.lander, st.cortex2, st.camera, PL_PLANE(0x40), PL_PLANE(0x47));
    } else {
        MobObj_Sleep((int32_t)(st.cortex2 & 0xffff));
        PL_Teleport(st.lander, OBJ_NONE, st.camera, PL_PLANE(0x40), PL_PLANE(0x47));
    }
    PL_EnterCore(st.area2Hooked != 0);
}

// 0x10002690
static void PL_Area2Setup(void)
{
    Environment_SetInteriorAmbient(0xf, 0xc, 0x18);
    MobObj_Wake(0x26);
    for (int32_t id = (int32_t)PL_ANIMTURRET(0x14); id <= (int32_t)PL_ANIMTURRET(0x23); id++)
        Sound_PlayLoop((ObjId)id, 0x90, -1);
    for (int32_t mob = 0x14; mob <= 0x1a; mob++)
        MobObj_Wake(mob);
    for (int32_t mob = 0x1d; mob <= 0x23; mob++)
        MobObj_Wake(mob);
    for (int32_t id = (int32_t)PL_ANIMTURRET(0x14); id <= (int32_t)PL_ANIMTURRET(0x23); id++)
        AnimSetUpdateInterval((ObjId)id, 0x1e);
    EventType_AddHandler(EV_TRIGGER_ENTER, pl_filter67, PL_Guards67);
    EventType_AddHandler(EV_TRIGGER_ENTER, pl_filter68, PL_Guards68);
    EventType_AddHandler(EV_TRIGGER_ENTER, pl_filter69, PL_Guards69);
    EventType_AddHandler(EV_TRIGGER_ENTER, pl_filter6A, PL_Guards6A);
    EventType_AddHandler(EV_TRIGGER_ENTER, pl_filter6B, PL_Guards6B);
    st.guard67 = 1;
    st.guard68 = 1;
    st.guard69 = 1;
    st.guard6A = 1;
    st.guard6B = 1;
    gTurretTypes[7] = &pl_turretType7;
    gTurretTypes[3] = &pl_turretType3;
    SetGravity(0xd);
    gAirResistance = 0xe;
    gMainThrottlePower = 0x1a;
    for (int32_t id = (int32_t)PL_ANIMTURRET(0x14); id <= (int32_t)PL_ANIMTURRET(0x23); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x46);
    }
    for (int32_t id = 0x52; id <= 0x53; id++)                  // turrets
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x28);
    Sound_PlayLoop(PL_DESTR(0x1b), 0xd1, -1);
    Sound_PlayLoop(PL_DESTR(0x00), 0xc5, -1);
    Sound_PlayLoop(PL_DESTR(0x56), 0xc5, -1);
    Sound_PlayLoop(PL_DESTR(0x57), 0xc5, -1);
    EventType_AddHandler(EV_DESTROYED, NULL, PL_OnDestroyed1B);
    EventType_AddHandler(EV_DESTROYED, NULL, MissionLib_OnDestroyedSound);   // 0x10001dc0
    EventType_AddHandler(EV_SWITCH, NULL, PL_OnSwitchLift0F);
    EventType_AddHandler(EV_SWITCH, NULL, PL_OnSwitch10);
    EventType_AddHandler(EV_SWITCH, NULL, PL_OnSwitch11);
    EventType_AddHandler(EV_POD_HOOKED, NULL, PL_Area2PodHooked);
    EventType_AddHandler(EV_TRIGGER_LEAVE, NULL, PL_Area2LeaveTrigger);
}

// ================================================================================================
// The surface
// ================================================================================================

// 0x100029f0  EV_SWITCH after all beacons are lit: the beacon switches cannot be changed any more
static void PL_LockBeacons(const LanderEvent *ev)
{
    int32_t sw = (int32_t)ev->arg[0];
    if (sw < (int32_t)PL_SWITCH(0x1c) || sw > (int32_t)PL_SWITCH(0x1f))
        return;
    Object_SetProperty((ObjId)sw, PROP_STATE, 1 - (int32_t)ev->arg[1]);
}

static void PL_BeaconsDone(void);

// 0x10002a20  EV_SWITCH 0xa001c..1f: beacon emitter 0xd0059 + k on (switch 0) / off (switch 1);
// all four lit: the surface guards wake
static void PL_OnBeaconSwitch(const LanderEvent *ev)
{
    int32_t sw = (int32_t)ev->arg[0];
    if (sw < (int32_t)PL_SWITCH(0x1c) || sw > (int32_t)PL_SWITCH(0x1f))
        return;
    ObjId emitter = (ObjId)sw + 0x3003d;                         // 0xd0059 + k
    Sound_PlayOnce((ObjId)sw, 0x398);
    if (Object_GetProperty((ObjId)sw, PROP_STATE) != 0) {
        Object_SetDead(emitter);
        Sound_StopObject(emitter);
        st.beaconsLit--;
    } else {
        Object_SetAlive(emitter);
        Sound_PlayLoop(emitter, 0xfa, -1);
        Object_SetProperty(emitter, PROP_POSITION, (intptr_t)pl_beaconPos[emitter - PL_EMITTER(0x59)]);
        st.beaconsLit++;
    }
    if (st.beaconsLit != 4)
        return;
    Object_SetAlive(PL_EMITTER(0x5d));
    Object_SetProperty(PL_EMITTER(0x5d), PROP_POSITION, (intptr_t)pl_beaconCentrePos);
    PL_BeaconsDone();
    Sound_PlayLoop(PL_DESTR(0x1b), 0x399, -1);
    EventType_AddHandler(EV_SWITCH, NULL, PL_LockBeacons);
    EventType_RemoveHandler(EV_SWITCH, PL_OnBeaconSwitch);
}

static void PL_SurfaceExitPlane(const LanderEvent *ev);

// 0x10002b30  the beacons are lit: the way down opens, guards 0x31..34 wake
static void PL_BeaconsDone(void)
{
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_SurfaceExitPlane);
    gTurretTypes[21] = &pl_turretType21;
    MobObj_Wake(0x31);
    MobObj_Wake(0x32);
    MobObj_Wake(0x33);
    MobObj_Wake(0x34);
    AnimPlay(PL_ANIMTURRET(0x31), 0x33, 0x61);
    Sound_PlayLoop(PL_ANIMTURRET(0x31), 0x39a, -1);
    AnimPlay(PL_ANIMTURRET(0x32), 0x34, 0x61);
    Sound_PlayLoop(PL_ANIMTURRET(0x32), 0x39a, -1);
    AnimPlay(PL_ANIMTURRET(0x33), 0x35, 0x61);
    Sound_PlayLoop(PL_ANIMTURRET(0x33), 0x39a, -1);
    AnimPlay(PL_ANIMTURRET(0x34), 0x36, 0x61);
    Sound_PlayLoop(PL_ANIMTURRET(0x34), 0x39a, -1);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x31), 0x28);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x32), 0x28);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x33), 0x28);
    AnimSetUpdateInterval(PL_ANIMTURRET(0x34), 0x28);
    Object_SetAlive(PL_DESTR(0x76));
    Object_SetAlive(PL_DESTR(0x77));
    Object_AdvanceDestroyLevel(PL_DESTR(0x7d), -1);
}

// 0x10002c60  EV_ANIM_TURNED: a falling icicle hit the ground
static void PL_OnIcicleFallen(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)PL_ANIMDESTR(0x2f) && id <= (int32_t)PL_ANIMDESTR(0x33)) {
        AnimStop((ObjId)id, 0);
        Object_SetProperty((ObjId)ev->arg[0], PROP_HITPOINTS, 0);
        Sound_PlayOnce((ObjId)ev->arg[0], 0x70);
        return;
    }
    if ((ObjId)id == PL_ANIMDESTR(0x01)) {
        AnimStop((ObjId)id, 0);
        Object_SetProperty((ObjId)ev->arg[0], PROP_HITPOINTS, 0);
        Sound_PlayOnce((ObjId)ev->arg[0], 0x6e);
        Object_SetProperty(PL_DESTR(0x34), PROP_HITPOINTS, 0);
        Object_SetDead(PL_DESTR(0x34));
    }
}

// 0x10002d00  EV_PROJECTILE_HIT: a shot icicle falls (its own anim, 0x2d for the big one)
static void PL_OnIcicleHit(const LanderEvent *ev)
{
    int32_t id = (int32_t)ev->arg[0];
    if (id >= (int32_t)PL_ANIMDESTR(0x2f) && id <= (int32_t)PL_ANIMDESTR(0x33)) {
        Sound_PlayOnce((ObjId)id, 0x72);
        AnimStop((ObjId)ev->arg[0], 0);
        InTime3(100, AnimPlay, ev->arg[0], ev->arg[0] - (intptr_t)PL_ANIMDESTR(0x01), 0);
        return;
    }
    if ((ObjId)id == PL_ANIMDESTR(0x01)) {
        Sound_PlayOnce((ObjId)id, 0x70);
        AnimStop((ObjId)ev->arg[0], 0);
        InTime3(100, AnimPlay, ev->arg[0], 0x2d, 0);
    }
}

static void PL_SurfaceExit(void);
static void PL_InteriorSetup(void);

// 0x10002d80  EV_TRIGGER_PLANE 0x1a0045: down into the complex
static void PL_SurfaceExitPlane(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_PLANE(0x45))
        return;
    PL_SurfaceExit();
}

// 0x10002da0
static void PL_SurfaceExit(void)
{
    EventType_RemoveHandler(EV_PROJECTILE_HIT, PL_OnIcicleHit);
    EventType_RemoveHandler(EV_ANIM_TURNED, PL_OnIcicleFallen);
    EventType_RemoveHandler(EV_TRIGGER_PLANE, PL_SurfaceExitPlane);
    MobObj_Sleep(0x31);
    MobObj_Sleep(0x32);
    MobObj_Sleep(0x33);
    MobObj_Sleep(0x34);
    Object_SetProperty(st.lander, PROP_HITPOINTS, 0xff);
    PL_Teleport(st.lander, st.cortex1, st.camera, PL_PLANE(0x45), PL_PLANE(0x43));
    Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
    WindowSetText(st.win, st.text[3]);           // the torus to the core
    MobObj_Sleep(6);
    PL_InteriorSetup();
}

// 0x10002e70  EV_TRIGGER_PLANE {player, plane, side}: objective texts at the entrance; the
// entrance guards (mobs 3, 4, 5, 0x30) sleep inside and wake outside
static void PL_OnSurfacePlanes(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == PL_PLANE(0x15)) {
        void (*mobFn)(int32_t);
        if (ev->arg[2] == 1) {
            WindowSetText(st.win, st.text[1]);   // the entry objective
            mobFn = MobObj_Sleep;
        } else {
            WindowSetText(st.win, st.text[0]);
            mobFn = MobObj_Wake;
        }
        mobFn(3);
        mobFn(4);
        mobFn(5);
        mobFn(0x30);
    }
    if ((ObjId)ev->arg[1] == PL_PLANE(0x14)) {
        if (ev->arg[2] == 1) {
            WindowSetText(st.win, st.text[2]);   // the torus to the vestibule
            return;
        }
        WindowSetText(st.win, st.text[1]);
    }
}

// 0x10002f20
static void PL_SurfaceSetup(void)
{
    for (int32_t id = (int32_t)PL_ANIMTURRET(0x31); id <= (int32_t)PL_ANIMTURRET(0x34); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    for (int32_t id = (int32_t)PL_ANIMTURRET(0xd); id <= (int32_t)PL_ANIMTURRET(0x10); id++) {
        Object_SetProperty((ObjId)id, PROP_ARMOUR, 0x3c);
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x64);
    }
    for (int32_t n = 0x2f; n <= 0x33; n++)
        Object_SetProperty(PL_ANIMDESTR(n), PROP_ARMOUR, 0xff);
    Object_SetProperty(PL_ANIMDESTR(0x01), PROP_ARMOUR, 0xff);
    Media_AudioPlay(1);
    gNbWaypoints = pl_nbWaypoints;
    gWaypoints = pl_waypoints;
    EventType_AddHandler(EV_PROJECTILE_HIT, NULL, PL_OnIcicleHit);
    EventType_AddHandler(EV_ANIM_TURNED, NULL, PL_OnIcicleFallen);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_OnSurfacePlanes);
    Object_SetProperty(PL_SWITCH(0x1c), PROP_STATE, 1);
    Object_SetProperty(PL_SWITCH(0x1d), PROP_STATE, 1);
    Object_SetProperty(PL_SWITCH(0x1e), PROP_STATE, 1);
    st.beaconsLit = 1;
    EventType_AddHandler(EV_SWITCH, NULL, PL_OnBeaconSwitch);
    Object_SetAlive(PL_EMITTER(0x5c));
    Sound_PlayLoop(PL_EMITTER(0x5c), 0xfa, -1);
    MobObj_Wake(6);
    Object_SetProperty(PL_EMITTER(0x5c), PROP_POSITION, (intptr_t)pl_beaconPos[3]);   // 0x10009108
    Object_SetDead(PL_DESTR(0x76));
    Object_SetDead(PL_DESTR(0x77));
    MobObj_Wake((int32_t)(st.cortex1 & 0xffff));
    Object_SetProperty(st.cortex1, PROP_TARGET, st.lander);     // the first cortex is in tow
    Pod_Script_SetupMass(st.cortex1, 0x3e8);
    Pod_Script_SetupDrag(st.cortex1, 0x1333);
}

// 0x10003720  EV_PICKUP: full shield, +2 missiles
static void PL_OnPickup(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] == PL_PICKUP(0x54) || (ObjId)ev->arg[0] == PL_PICKUP(0x55)) {
        Object_SetProperty(st.lander, PROP_HITPOINTS, 0x100);
        Voice_ShieldAcquired(PL_LANDER_ID, 100);
    }
    if ((ObjId)ev->arg[0] == PL_PICKUP(0x87) || (ObjId)ev->arg[0] == PL_PICKUP(0x88)) {
        if (Lander_AddMissiles(PL_LANDER_ID, 2) == 0) {
            Voice_MissilesFull(PL_LANDER_ID);
            Object_SetAlive((ObjId)ev->arg[0]);
            return;
        }
        Voice_MissilesAcquired(PL_LANDER_ID);
    }
}

// 0x100037b0  common set-up
static void PL_Setup(void)
{
    st.text = pl_text[gLanguage];
    SetGravity(0xb);
    gAirResistance = 0x10;
    gMainThrottlePower = 0x19;
    st.win = WindowCreate(WIN_TEXTLINE | WIN_FRAMED, NULL);   // 0x48
    WindowSetSize(st.win, 20, 20, 500, 12);
    WindowSetText(st.win, st.text[0]);
    EventType_AddHandler(EV_PICKUP, NULL, PL_OnPickup);
    Object_SetDead(PL_DESTR(0x83));
    Object_SetDead(PL_DESTR(0x84));
    Object_SetDead(PL_DESTR(0x85));
    TexAnimPlay(5, 3, 100);
    st.lander = PL_LANDER_ID;
    st.camera = PL_CAMERA_ID;
    st.cortex1 = MOB_ID(OT_POD, 0x27);
    st.cortex2 = MOB_ID(OT_POD, 0x26);
    st.cortex3 = MOB_ID(OT_POD, 0x25);
    st.cortex4 = MOB_ID(OT_POD, 0x24);
    MissionScore_Init(PL_LANDER_ID, 600000, 4);                 // 0x100035f0
    for (int32_t n = 0x78; n <= 0x81; n++)
        Object_AdvanceDestroyLevel(PL_DESTR(n), 1);
    PL_SurfaceSetup();
    Media_AudioPlay(0);
}

// 0x10003950  LevelStartLevel_Lander (export 0x100029b0)
static void PL_StartLander(void)
{
    MobObj_Wake(PL_MOB_LANDER);
    MobObj_Wake(2);                 // camera
    MobObj_Wake(3);
    MobObj_Wake(4);
    MobObj_Wake(5);
    MobObj_Wake(0x30);
    for (int32_t n = 0x59; n <= 0x5d; n++)
        Object_SetDead(PL_EMITTER(n));
    for (int32_t n = 0x59; n <= 0x5d; n++)
        Sound_PlayLoop(PL_EMITTER(n), 0xc6, -1);
    Object_SetProperty(PL_CAMERA_ID, PROP_TARGET, PL_LANDER_ID);
    Environment_OverrideInteriorFog(0, 0, 0);
    PL_Setup();
}

// 0x10003a40  LevelStartLevel_Flycam (export 0x100029c0)
static void PL_StartFlycam(void)
{
    PL_Setup();
    MobObj_Wake(PL_MOB_FLYCAM);
}

// 0x10003a60  LevelStopLevel (export 0x100029d0): empty
static void PL_Stop(void)
{
}

// ================================================================================================
// The interior
// ================================================================================================

static void PL_GateA(int32_t open);
static void PL_GateB(int32_t open);
static void PL_GateC(int32_t open);

// 0x10003a70  EV_SWITCH: gate switches 0xa003c..3e can only be turned on; 0xa003f closes the gates in
// turn (A, B, C) and opens the barrier 0xf0062
static void PL_OnInteriorSwitch(const LanderEvent *ev)
{
    Sound_PlayOnce((ObjId)ev->arg[0], 0x398);
    int32_t sw = (int32_t)ev->arg[0];
    if (ev->arg[1] == 0) {
        if (sw >= (int32_t)PL_SWITCH(0x3c) && sw <= (int32_t)PL_SWITCH(0x3e)) {
            Object_SetProperty((ObjId)sw, PROP_STATE, 1);
            return;
        }
        if ((ObjId)sw != PL_SWITCH(0x3f))
            return;
        Object_SetAlive(PL_ANIMDESTR(0x62));
        Object_SetDead(PL_DESTR(0x50));
        Object_SetDead(PL_DESTR(0x51));
        if (st.gateTurn == 1)
            PL_GateA(0);
        else if (st.gateTurn == 2)
            PL_GateB(0);
        else if (st.gateTurn == 3)
            PL_GateC(0);
        if (++st.gateTurn == 4)
            st.gateTurn = 1;
        return;
    }
    if ((ObjId)sw == PL_SWITCH(0x3c)) {
        PL_GateA(1);
        return;
    }
    if ((ObjId)sw == PL_SWITCH(0x3d)) {
        PL_GateB(1);
        return;
    }
    if ((ObjId)sw == PL_SWITCH(0x3e)) {
        PL_GateC(1);
        return;
    }
    if ((ObjId)sw == PL_SWITCH(0x3f))
        Object_SetProperty((ObjId)sw, PROP_STATE, 1 - (int32_t)ev->arg[1]);
}

static void PL_GatesCommon(void);

// 0x10003b90
static void PL_GateA(int32_t open)
{
    if (open == 0) {
        Object_SetDead(PL_ANIMDESTR(0x5f));
        Object_SetDead(PL_ANIMDESTR(0x4d));
        Object_SetProperty(PL_SWITCH(0x3c), PROP_STATE, 0);
        return;
    }
    Object_SetAlive(PL_ANIMDESTR(0x5f));
    Object_SetAlive(PL_ANIMDESTR(0x4d));
    PL_GatesCommon();
}

// 0x10003bf0
static void PL_GateB(int32_t open)
{
    if (open == 0) {
        Object_SetDead(PL_ANIMDESTR(0x60));
        Object_SetDead(PL_ANIMDESTR(0x4e));
        Object_SetProperty(PL_SWITCH(0x3d), PROP_STATE, 0);
        return;
    }
    Object_SetAlive(PL_ANIMDESTR(0x60));
    Object_SetAlive(PL_ANIMDESTR(0x4e));
    PL_GatesCommon();
}

// 0x10003c50
static void PL_GateC(int32_t open)
{
    if (open == 0) {
        Object_SetDead(PL_ANIMDESTR(0x61));
        Object_SetDead(PL_ANIMDESTR(0x4f));
        Object_SetProperty(PL_SWITCH(0x3e), PROP_STATE, 0);
        return;
    }
    Object_SetAlive(PL_ANIMDESTR(0x61));
    Object_SetAlive(PL_ANIMDESTR(0x4f));
    PL_GatesCommon();
}

// 0x10003cb0
static void PL_GatesCommon(void)
{
    Object_SetDead(PL_ANIMDESTR(0x62));
    Object_SetAlive(PL_DESTR(0x50));
    Object_SetAlive(PL_DESTR(0x51));
    Object_SetProperty(PL_SWITCH(0x3f), PROP_STATE, 1);
}

// 0x10003cf0
static void PL_InteriorGatesInit(void)
{
    Object_SetAlive(PL_ANIMDESTR(0x4e));
    Object_SetAlive(PL_ANIMDESTR(0x4f));
    Object_SetDead(PL_ANIMDESTR(0x5f));
    Object_SetProperty(PL_SWITCH(0x3d), PROP_STATE, 1);
    Object_SetProperty(PL_SWITCH(0x3e), PROP_STATE, 1);
    EventType_AddHandler(EV_SWITCH, NULL, PL_OnInteriorSwitch);
}

static void PL_InteriorExit(void);

// 0x10003d50  EV_TRIGGER_ENTER 0x40070: on into the core
static void PL_OnInteriorExit(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] != PL_TRIGGER(0x70))
        return;
    Object_SetProperty(st.lander, PROP_HITPOINTS, 0xff);
    PL_Teleport(st.lander, st.cortex1, st.camera, PL_PLANE(0x44), PL_PLANE(0x46));
    Lander_SetFuel(PL_LANDER_ID, PL_FUEL_FULL);
    PL_InteriorExit();
}

// 0x10003dc0  EV_TRIGGER_PLANE 0x1a004a / 4b (side 0): the transport torus between markers
// 0x1190012 and 0x1190013 (its inertial frame mob 0x35 follows the lander on the way out)
static void PL_OnTorusPlanes(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[1] == PL_PLANE(0x4a)) {
        if (ev->arg[2] != 0)
            return;
        PL_Teleport(st.lander, st.cortex1, st.camera, PL_MARKER(0x12), PL_MARKER(0x13));
        MobObj_Wake(0x35);
        Object_SetProperty(PL_FRAME(0x35), PROP_TARGET, st.lander);
        Sound_PlayLoop(PL_FRAME(0x35), 0x3a7, -1);
        return;
    }
    if ((ObjId)ev->arg[1] == PL_PLANE(0x4b) && ev->arg[2] == 0) {
        PL_Teleport(st.lander, st.cortex1, st.camera, PL_MARKER(0x13), PL_MARKER(0x12));
        Sound_StopObject(PL_FRAME(0x35));
        MobObj_Sleep(0x35);
    }
}

// 0x10003e90  (InTime target)
static void PL_Advance7B(void)
{
    Object_AdvanceDestroyLevel(PL_DESTR(0x7b), 1);
}

// 0x10003eb0
static void PL_InteriorExit(void)
{
    Object_SetDead(PL_ANIMDESTR(0x4d));
    Object_SetDead(PL_ANIMDESTR(0x4e));
    EventType_RemoveHandler(EV_SWITCH, PL_OnInteriorSwitch);
    EventType_RemoveHandler(EV_TRIGGER_ENTER, PL_OnInteriorExit);
    EventType_RemoveHandler(EV_TRIGGER_PLANE, PL_OnTorusPlanes);
    PL_EnterCoreFirst();
}

// 0x10003f00  EV_COLLISION: the lander breaks a spike 0xf0020..2b by touching it
static void PL_OnSpikeTouched(const LanderEvent *ev)
{
    if ((ObjId)ev->arg[0] != st.lander)
        return;
    int32_t spike = (int32_t)ev->arg[1];
    if (spike < (int32_t)PL_ANIMDESTR(0x20) || spike > (int32_t)PL_ANIMDESTR(0x2b))
        return;
    Sound_PlayOnce((ObjId)ev->arg[0], 0x3a2);
    Object_SetProperty((ObjId)ev->arg[1], PROP_HITPOINTS, 0);
}

// 0x10003f50
static void PL_InteriorSetup(void)
{
    Object_SetDead(PL_ANIMDESTR(0x4d));
    Object_SetDead(PL_ANIMDESTR(0x4e));
    Object_SetDead(PL_ANIMDESTR(0x4f));
    Object_SetDead(PL_DESTR(0x50));
    Object_SetDead(PL_DESTR(0x51));
    gInOutOverride = 1;
    EventType_AddHandler(EV_TRIGGER_ENTER, NULL, PL_OnInteriorExit);
    EventType_AddHandler(EV_TRIGGER_PLANE, NULL, PL_OnTorusPlanes);
    EventType_AddHandler(EV_COLLISION, NULL, PL_OnSpikeTouched);
    for (int32_t id = (int32_t)PL_ANIMDESTR(0x20); id - (int32_t)PL_ANIMDESTR(0x20) < 0xc; id++) {
        Object_SetProperty((ObjId)id, PROP_EXPLOSION, 0x96);
        AnimPlay((ObjId)id, id - (int32_t)OBJ_ID(OT_ANIMDESTRUCTIBLE, 0), 0x81);   // anim 0x20..0x2b
        Sound_PlayLoop((ObjId)id, 0x3a1, -1);
        AnimSetUpdateInterval((ObjId)id, 0x14);
    }
    SetGravity(0xb);
    gAirResistance = 0x10;
    gMainThrottlePower = 0x19;
    Object_AdvanceDestroyLevel(PL_DESTR(0x7b), -1);
    Object_AdvanceDestroyLevel(PL_DESTR(0x7c), -1);
    InTime0(1000, PL_Advance7B);
    PL_InteriorGatesInit();
}

// 0x100029e0
static uint32_t PL_GetVersion(void)
{
    return LEVEL_VERSION;
}

// 0x100029a0  two calls of the empty 0x10003a60, return 0. Port: fresh DLL data.
static int32_t PL_Load(const char *dllPath)
{
    (void)dllPath;
    memset(&st, 0, sizeof st);
    st.airlockBusy = pl_airlockBusyInit;
    st.gateTurn = pl_gateTurnInit;
    MissionScore_Reset();
    return 0;
}

// the DLL's text, read at run time (gametext.c)
static const GameText pl_gametext[] = {
    { .kind = GT_POINTERS, .va = 0x10009268, .count = 3 * 9, .out = pl_text },
    { 0 },
};

const mission_script_t mission_pluto_1 = {
    .name          = "PLUTO\\1",
    .get_version   = PL_GetVersion,
    .load          = PL_Load,
    .start_lander  = PL_StartLander,
    .start_flycam  = PL_StartFlycam,
    .start_animcam = NULL,
    .stop          = PL_Stop,
    .text          = pl_gametext,
};
