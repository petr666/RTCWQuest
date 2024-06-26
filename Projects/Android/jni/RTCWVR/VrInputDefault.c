/************************************************************************************

Filename	:	VrInputDefault.c
Content		:	Handles default controller input
Created		:	August 2019
Authors		:	Simon Brown

*************************************************************************************/

#include "../../../../../../VrApi/Include/VrApi.h"
#include "../../../../../../VrApi/Include/VrApi_Helpers.h"
#include "../../../../../../VrApi/Include/VrApi_SystemUtils.h"
#include "../../../../../../VrApi/Include/VrApi_Input.h"
#include "../../../../../../VrApi/Include/VrApi_Types.h"
#include <android/keycodes.h>

#include "VrInput.h"
#include "VrCvars.h"

#include <src/qcommon/qcommon.h>
#include <src/client/client.h>

void SV_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, int capsule );

void RTCWVR_HapticEvent(const char* event, int position, int flags, int intensity, float angle, float yHeight );

static inline float AngleBetweenVectors(const vec3_t a, const vec3_t b)
{
    return degrees(acosf(DotProduct(a, b)/(VectorLength(a) * VectorLength(b))));
}

void HandleInput_Default( ovrInputStateGamepad *pFootTrackingNew, ovrInputStateGamepad *pFootTrackingOld,
                          ovrInputStateTrackedRemote *pDominantTrackedRemoteNew, ovrInputStateTrackedRemote *pDominantTrackedRemoteOld, ovrTracking* pDominantTracking,
                          ovrInputStateTrackedRemote *pOffTrackedRemoteNew, ovrInputStateTrackedRemote *pOffTrackedRemoteOld, ovrTracking* pOffTracking,
                          int domButton1, int domButton2, int offButton1, int offButton2 )

{
	//Ensure handedness is set correctly
	vr.right_handed = vr_control_scheme->value < 10 ||
            vr_control_scheme->value == 99; // Always right-handed for weapon calibration

	vr.teleportenabled = vr_teleport->integer != 0;
    vr.visible_hud = vr_draw_hud->integer;
    vr.vrIrlCrouchEnabled = vr_irl_crouch_enabled->integer != 0;
    vr.vrIrlCrouchToStandRatio = vr_irl_crouch_to_stand_ratio->value;

    static qboolean dominantGripPushed = false;
	static float dominantGripPushTime = 0.0f;
    static bool canUseBackpack = false;
    static bool canUseQuickSave = false;

    //Need this for the touch screen
    ovrTracking * pWeapon = pDominantTracking;
    ovrTracking * pOff = pOffTracking;

    //All this to allow stick and button switching!
    ovrVector2f *pPrimaryJoystick;
    ovrVector2f *pSecondaryJoystick;
    uint32_t primaryButtonsNew;
    uint32_t primaryButtonsOld;
    uint32_t secondaryButtonsNew;
    uint32_t secondaryButtonsOld;
    int primaryButton1;
    int primaryButton2;
    int secondaryButton1;
    int secondaryButton2;
    if (vr_switch_sticks->integer)
    {
        //
        // This will switch the joystick and A/B/X/Y button functions only
        // Move, Strafe, Turn, Jump, Crouch, Notepad, HUD mode, Weapon Switch
        pSecondaryJoystick = &pDominantTrackedRemoteNew->Joystick;
        pPrimaryJoystick = &pOffTrackedRemoteNew->Joystick;
        secondaryButtonsNew = pDominantTrackedRemoteNew->Buttons;
        secondaryButtonsOld = pDominantTrackedRemoteOld->Buttons;
        primaryButtonsNew = pOffTrackedRemoteNew->Buttons;
        primaryButtonsOld = pOffTrackedRemoteOld->Buttons;
        primaryButton1 = offButton1;
        primaryButton2 = offButton2;
        secondaryButton1 = domButton1;
        secondaryButton2 = domButton2;
    }
    else
    {
        pPrimaryJoystick = &pDominantTrackedRemoteNew->Joystick;
        pSecondaryJoystick = &pOffTrackedRemoteNew->Joystick;
        primaryButtonsNew = pDominantTrackedRemoteNew->Buttons;
        primaryButtonsOld = pDominantTrackedRemoteOld->Buttons;
        secondaryButtonsNew = pOffTrackedRemoteNew->Buttons;
        secondaryButtonsOld = pOffTrackedRemoteOld->Buttons;
        primaryButton1 = domButton1;
        primaryButton2 = domButton2;
        secondaryButton1 = offButton1;
        secondaryButton2 = offButton2;
    }



    {
        //Set gun angles - We need to calculate all those we might need (including adjustments) for the client to then take its pick
        vec3_t rotation = {0};
        rotation[PITCH] = -25;
        QuatToYawPitchRoll(pDominantTracking->HeadPose.Pose.Orientation, rotation, vr.dominanthandangles);
        rotation[PITCH] = 30;
        QuatToYawPitchRoll(pWeapon->HeadPose.Pose.Orientation, rotation, vr.weaponangles_knife);
        qboolean addRecoil = vr.pistol && (!vr.dualwielding || vr.akimboFire);
        rotation[PITCH] = vr_weapon_pitchadjust->value + (addRecoil ? vr.weapon_recoil : 0.0f); // Our hacked recoil effect
        QuatToYawPitchRoll(pWeapon->HeadPose.Pose.Orientation, rotation, vr.weaponangles);

        VectorSubtract(vr.weaponangles_last, vr.weaponangles, vr.weaponangles_delta);
        VectorCopy(vr.weaponangles, vr.weaponangles_last);

        ALOGV("        weaponangles_last: %f, %f, %f", vr.weaponangles_last[0], vr.weaponangles_last[1], vr.weaponangles_last[2]);

        //GB Also set offhand angles just in case we want to use those.
        vec3_t rotation_off = {0};
        rotation_off[PITCH] = -25;
        QuatToYawPitchRoll(pOff->HeadPose.Pose.Orientation, rotation_off, vr.offhandangles);
        qboolean addRecoil_off = vr.pistol && (!vr.dualwielding || !vr.akimboFire);
        rotation_off[PITCH] = vr_weapon_pitchadjust->value + (addRecoil_off ? vr.weapon_recoil : 0.0f); // Our hacked recoil effect
        QuatToYawPitchRoll(pOff->HeadPose.Pose.Orientation, rotation_off, vr.offhandweaponangles);

        VectorSubtract(vr.offhandangles_last, vr.offhandangles, vr.offhandangles_delta);
        VectorCopy(vr.offhandangles, vr.offhandangles_last);

        vr.weapon_recoil *= 0.8f; // quick reduction on synthetic recoil
    }

    //Menu button
	handleTrackedControllerButton(&leftTrackedRemoteState_new, &leftTrackedRemoteState_old, ovrButton_Enter, K_ESCAPE);
	if (vr.toggleMainMenu) { // Menu toggled via wheel selector
		vr.toggleMainMenu = qfalse;
		Sys_QueEvent( 0, SE_KEY, K_ESCAPE, qtrue, 0, NULL );
	}

    static float menuYaw = 0;
    if ( RTCWVR_useScreenLayer() )
    {
        bool controlsLeftHanded = vr_control_scheme->integer >= 10;
        if (controlsLeftHanded == vr.menu_right_handed) {
            interactWithTouchScreen(menuYaw, vr.offhandangles);
            handleTrackedControllerButton(pOffTrackedRemoteNew, pOffTrackedRemoteOld, offButton1, K_MOUSE1);
            handleTrackedControllerButton(pOffTrackedRemoteNew, pOffTrackedRemoteOld, ovrButton_Trigger, K_MOUSE1);
            if ((pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger) != (pDominantTrackedRemoteOld->Buttons & ovrButton_Trigger) && (pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger)) {
                vr.menu_right_handed = !vr.menu_right_handed;
                RTCWVR_Vibrate(40, vr.menu_right_handed ? 1 : 0, 0.5);
            }
        } else {
            interactWithTouchScreen(menuYaw, vr.dominanthandangles);
            handleTrackedControllerButton(pDominantTrackedRemoteNew, pDominantTrackedRemoteOld, domButton1, K_MOUSE1);
            handleTrackedControllerButton(pDominantTrackedRemoteNew, pDominantTrackedRemoteOld, ovrButton_Trigger, K_MOUSE1);
            if ((pOffTrackedRemoteNew->Buttons & ovrButton_Trigger) != (pOffTrackedRemoteOld->Buttons & ovrButton_Trigger) && (pOffTrackedRemoteNew->Buttons & ovrButton_Trigger)) {
                vr.menu_right_handed = !vr.menu_right_handed;
                RTCWVR_Vibrate(40, vr.menu_right_handed ? 1 : 0, 0.5);
            }
        }
        handleTrackedControllerButton(pOffTrackedRemoteNew, pOffTrackedRemoteOld, offButton2, K_ESCAPE);
        handleTrackedControllerButton(pDominantTrackedRemoteNew, pDominantTrackedRemoteOld, domButton2, K_ESCAPE);
        if (vr_menu_item_touched->integer) {
            RTCWVR_Vibrate(40, vr.menu_right_handed ? 1 : 0, 0.5);
            Cvar_SetValue("vr_menu_item_touched", 0);
        }
    }
    else
    {
        menuYaw = vr.hmdorientation[YAW];

        float distance = sqrtf(powf(pOff->HeadPose.Pose.Position.x - pWeapon->HeadPose.Pose.Position.x, 2) +
                               powf(pOff->HeadPose.Pose.Position.y - pWeapon->HeadPose.Pose.Position.y, 2) +
                               powf(pOff->HeadPose.Pose.Position.z - pWeapon->HeadPose.Pose.Position.z, 2));

        float distanceToHMD = sqrtf(powf(vr.hmdposition[0] - pWeapon->HeadPose.Pose.Position.x, 2) +
                                    powf(vr.hmdposition[1] - pWeapon->HeadPose.Pose.Position.y, 2) +
                                    powf(vr.hmdposition[2] - pWeapon->HeadPose.Pose.Position.z, 2));

        float distanceToHMDOff = sqrtf(powf(vr.hmdposition[0] - pOff->HeadPose.Pose.Position.x, 2) +
                                    powf(vr.hmdposition[1] - pOff->HeadPose.Pose.Position.y, 2) +
                                    powf(vr.hmdposition[2] - pOff->HeadPose.Pose.Position.z, 2));

        float controllerYawHeading = 0.0f;
        //Turn on weapon stabilisation?
        qboolean stabilised = qfalse;
        qboolean usingBinoculars = vr.backpackitemactive == 3 || vr.binocularsActive;
        if (vr.weaponstabilizable && !usingBinoculars && // Don't stabilise dual guns, single hand weapons and binoculars
            (pOffTrackedRemoteNew->Buttons & ovrButton_GripTrigger) && (distance < STABILISATION_DISTANCE))
        {
            stabilised = qtrue;
        }

        vr.weapon_stabilised = stabilised;

        //Engage scope / virtual stock if conditions are right
        qboolean scopeready = vr.weapon_stabilised && (distanceToHMD < SCOPE_ENGAGE_DISTANCE);
        static qboolean lastScopeReady = qfalse;
        if (scopeready != lastScopeReady) {
            if (vr.scopedweapon && (!vr.detachablescope || !vr.scopedetached)) {
                if (!vr.scopeengaged && scopeready) {
                    ALOGV("**WEAPON EVENT**  trigger scope mode");
                    sendButtonActionSimple("weapalt");
                }
                else if (vr.scopeengaged && !scopeready) {
                    ALOGV("**WEAPON EVENT**  disable scope mode");
                    sendButtonActionSimple("weapalt");
                }
                lastScopeReady = scopeready;
            }
        }

        //ALOGV("**GB WEAPON ACTIVE** %i",vr.weaponid);
        if(!scopeready && vr.weaponid >= 15 && vr.weaponid <= 17)
        {
            lastScopeReady = false;
            ALOGV("**WEAPON EVENT**  disable scope mode forced");
            sendButtonActionSimple("weapalt");
        }

        //Engage scope / virtual stock (iron sight lock) if conditions are right
        static qboolean scopeEngaged = qfalse;
        if (scopeEngaged != vr.scopeengaged)
        {
            scopeEngaged = vr.scopeengaged;

            //Resync on either transition
            RTCWVR_ResyncClientYawWithGameYaw();
        }


        static qboolean binocularstate = qfalse;
        qboolean binocularsactive = (vr.hasbinoculars && (vr.backpackitemactive == 3 || vr.binocularsActive) &&
                (distanceToHMD < BINOCULAR_ENGAGE_DISTANCE) &&
                (pDominantTracking->Status & (VRAPI_TRACKING_STATUS_POSITION_TRACKED | VRAPI_TRACKING_STATUS_POSITION_VALID)));
        if (binocularstate != binocularsactive)
        {
            //Engage scope if conditions are right
            binocularstate = binocularsactive;
            sendButtonAction("+zoom", binocularstate);
        }

        //dominant hand stuff first
        {
            //Record recent weapon position for trajectory based stuff
            for (int i = (NUM_WEAPON_SAMPLES-1); i != 0; --i)
            {
                VectorCopy(vr.weaponoffset_history[i-1], vr.weaponoffset_history[i]);
                vr.weaponoffset_history_timestamp[i] = vr.weaponoffset_history_timestamp[i-1];
            }
            VectorCopy(vr.current_weaponoffset, vr.weaponoffset_history[0]);
            vr.weaponoffset_history_timestamp[0] = vr.current_weaponoffset_timestamp;

			///Weapon location relative to view
            vr.current_weaponoffset[0] = pWeapon->HeadPose.Pose.Position.x - vr.hmdposition[0];
            vr.current_weaponoffset[1] = pWeapon->HeadPose.Pose.Position.y - vr.hmdposition[1];
            vr.current_weaponoffset[2] = pWeapon->HeadPose.Pose.Position.z - vr.hmdposition[2];
            vr.current_weaponoffset_timestamp = Sys_Milliseconds( );

            //Just copy to calculated offset, used to use this in case we wanted to apply any modifiers, but don't any more
            VectorCopy(vr.current_weaponoffset, vr.calculated_weaponoffset);

            static qboolean wasVelocityTriggered = false;
            if (!wasVelocityTriggered && vr.velocitytriggered) {
                // In case we are switching from standard weapon to velocity triggered
                // weapon disable ongoing attack to prevent attack being stuck.
                sendButtonAction("+attack", false);
            }
            wasVelocityTriggered = vr.velocitytriggered;
            //Does weapon velocity trigger attack (knife) and is it fast enough
            static qboolean velocityTriggeredAttack = false;
            if (vr.velocitytriggered)
            {
                static qboolean fired = qfalse;
                float velocity = sqrtf(powf(pWeapon->HeadPose.LinearVelocity.x, 2) +
                                       powf(pWeapon->HeadPose.LinearVelocity.y, 2) +
                                       powf(pWeapon->HeadPose.LinearVelocity.z, 2));

                velocityTriggeredAttack = (velocity > VELOCITY_TRIGGER);

                if (fired != velocityTriggeredAttack) {
                    ALOGV("**WEAPON EVENT**  veocity triggered %s", velocityTriggeredAttack ? "+attack" : "-attack");
                    sendButtonAction("+attack", velocityTriggeredAttack);
                    fired = velocityTriggeredAttack;
                }
            }
            else if (velocityTriggeredAttack)
            {
                //send a stop attack as we have an unfinished velocity attack
                velocityTriggeredAttack = qfalse;
                ALOGV("**WEAPON EVENT**  veocity triggered -attack");
                sendButtonAction("+attack", velocityTriggeredAttack);
            }

            if (vr.weapon_stabilised)
            {
                if (vr.scopeengaged || vr_virtual_stock->integer == 1)
                {
                    //offset to the appropriate eye a little bit
                    vec2_t xy;
                    rotateAboutOrigin(Cvar_VariableValue("cg_stereoSeparation") / 2.0f, 0.0f, -vr.hmdorientation[YAW], xy);
                    float x = pOff->HeadPose.Pose.Position.x - (vr.hmdposition[0] + xy[0]);
                    float y = pOff->HeadPose.Pose.Position.y - (vr.hmdposition[1] - 0.1f); // Use a point lower
                    float z = pOff->HeadPose.Pose.Position.z - (vr.hmdposition[2] + xy[1]);
                    float zxDist = length(x, z);

                    if (zxDist != 0.0f && z != 0.0f) {
                        VectorSet(vr.weaponangles, -degrees(atanf(y / zxDist)),
                                  -degrees(atan2f(x, -z)), 0);
                    }
                }
                else
                {
                    float x = pOff->HeadPose.Pose.Position.x - pWeapon->HeadPose.Pose.Position.x;
                    float y = pOff->HeadPose.Pose.Position.y - pWeapon->HeadPose.Pose.Position.y;
                    float z = pOff->HeadPose.Pose.Position.z - pWeapon->HeadPose.Pose.Position.z;
                    float zxDist = length(x, z);

                    if (zxDist != 0.0f && z != 0.0f) {
                        VectorSet(vr.weaponangles, -degrees(atanf(y / zxDist)),
                                  -degrees(atan2f(x, -z)), vr.weaponangles[ROLL] / 2.0f); //Dampen roll on stabilised weapon
                    }
                }
            }

            static bool finishReloadNextFrame = false;
            if (finishReloadNextFrame)
            {
                ALOGV("**WEAPON EVENT**  -reload");
                sendButtonActionSimple("-reload");
                finishReloadNextFrame = false;
            }

            // Calculate if player tries to reach backpack
            bool handInBackpack = false;
            bool bpDistToHMDOk = false, bpWeaponHeightOk = false, bpWeaponAngleOk = false, bpHmdToWeaponAngleOk = false;
            vec3_t hmdForwardXY = {}, weaponForwardXY = {};
            float weaponToDownAngle = 0, hmdToWeaponDotProduct = 0;
            static vec3_t downVector = {0.0, 0.0, -1.0};

            bool bpTrackOk = pOffTracking->Status & VRAPI_TRACKING_STATUS_POSITION_TRACKED;                        // 1) Position must be tracked
            if (bpTrackOk && (bpDistToHMDOk = distanceToHMD >= 0.2 && distanceToHMD <= 0.35)                       // 2) Weapon-to-HMD distance must be within <0.2-0.35> range
                && (bpWeaponHeightOk = vr.current_weaponoffset[1] >= -0.10 && vr.current_weaponoffset[1] <= 0.10)) // 3) Weapon height in relation to HMD must be within <-0.10, 0.10> range
            {
                AngleVectors(vr.hmdorientation, hmdForwardXY, NULL, NULL);
                AngleVectors(vr.weaponangles, weaponForwardXY, NULL, NULL);

                float weaponToDownAngle = AngleBetweenVectors(downVector, weaponForwardXY);
                // 4) Angle between weapon forward vector and a down vector must be within 80-140 degrees
                if(bpWeaponAngleOk = weaponToDownAngle >= 80.0 && weaponToDownAngle <= 140.0)
                {
                    hmdForwardXY[2] = 0;
                    VectorNormalize(hmdForwardXY);

                    weaponForwardXY[2] = 0;
                    VectorNormalize(weaponForwardXY);

                    hmdToWeaponDotProduct = DotProduct(hmdForwardXY, weaponForwardXY);
                    // 5) HMD and weapon forward on XY plane must go in opposite directions (i.e. dot product < 0)
                    handInBackpack = bpHmdToWeaponAngleOk = hmdToWeaponDotProduct < 0;
                }
            }

            // Uncomment to debug backpack reaching
            /*
            ALOGV("Backpack> Dist: %f | WpnToDownAngle: %f | WpnOffs: %f %f %f\nHmdWpnDot: %f | HmdFwdXY: %f %f | WpnFwdXY: %f %f\nTrackOk: %i, DistOk: %i, HeightOk: %i, WpnAngleOk: %i, HmdWpnDotOk: %i",
                  distanceToHMD, weaponToDownAngle, vr.current_weaponoffset[0], vr.current_weaponoffset[1], vr.current_weaponoffset[2],
                  hmdToWeaponDotProduct, hmdForwardXY[0], hmdForwardXY[1], weaponForwardXY[0], weaponForwardXY[1],
                  bpTrackOk, bpDistToHMDOk, bpWeaponHeightOk, bpWeaponAngleOk, bpHmdToWeaponAngleOk);
            */


            //off-hand stuff (done here as I reference it in the save state thing
            {
                vr.offhandoffset[0] = pOff->HeadPose.Pose.Position.x - vr.hmdposition[0];
                vr.offhandoffset[1] = pOff->HeadPose.Pose.Position.y - vr.hmdposition[1];
                vr.offhandoffset[2] = pOff->HeadPose.Pose.Position.z - vr.hmdposition[2];

                vec3_t rotation = {0};
                rotation[PITCH] = -25;
                QuatToYawPitchRoll(pOff->HeadPose.Pose.Orientation, rotation, vr.offhandangles);

                if (vr_walkdirection->value == 0) {
                    controllerYawHeading = vr.offhandangles[YAW] - vr.hmdorientation[YAW];
                }
                else
                {
                    controllerYawHeading = 0.0f;
                }
            }

            // Use off hand as well to trigger save condition
            canUseQuickSave = false;
            bool bpOffhandDistToHMDOk = false, bpOffhandHeightOk = false, bpOffhandAngleOk = false, bpHmdToOffhandAngleOk = false;
            vec3_t offhandForwardXY = {};
            float hmdToOffhandDotProduct = 0;
            float offhandToDownAngle = 0;
            if (bpTrackOk && (bpOffhandDistToHMDOk = distanceToHMDOff >= 0.2 && distanceToHMDOff <= 0.35)   // 2) Off-to-HMD distance must be within <0.2-0.35> range
                && (bpOffhandHeightOk = vr.offhandoffset[1] >= -0.10 && vr.offhandoffset[1] <= 0.10)) // 3) Offhand height in relation to HMD must be within <-0.10, 0.10> range
            {
                //Need to do this again as might not have done it above and cant be bothered to refactor
                AngleVectors(vr.hmdorientation, hmdForwardXY, NULL, NULL);
                AngleVectors(vr.offhandangles, offhandForwardXY, NULL, NULL);

                offhandToDownAngle = AngleBetweenVectors(downVector, offhandForwardXY);

                // 4) Angle between weapon forward vector and a down vector must be within 80-140 degrees
                if(bpOffhandAngleOk = offhandToDownAngle >= 80.0 && offhandToDownAngle <= 140.0)
                {
                    hmdForwardXY[2] = 0;
                    VectorNormalize(hmdForwardXY);

                    offhandForwardXY[2] = 0;
                    VectorNormalize(offhandForwardXY);

                    hmdToOffhandDotProduct = DotProduct(hmdForwardXY, offhandForwardXY);
                    // 5) HMD and weapon forward on XY plane must go in opposite directions (i.e. dot product < 0)
                    canUseQuickSave = bpHmdToOffhandAngleOk = hmdToOffhandDotProduct < 0;
                }
            }

            // Uncomment to debug offhand reaching

            ALOGV("Quick Save> Dist: %f | OffHandToDownAngle: %f | HandOffs: %f %f %f\nHmdHandDot: %f | HmdFwdXY: %f %f | WpnFwdXY: %f %f\nTrackOk: %i, DistOk: %i, HeightOk: %i, HnadAngleOk: %i, HmdHandDotOk: %i",
                  distanceToHMDOff, offhandToDownAngle, vr.offhandoffset[0], vr.offhandoffset[1], vr.offhandoffset[2],
                  hmdToOffhandDotProduct, hmdForwardXY[0], hmdForwardXY[1], offhandForwardXY[0], offhandForwardXY[1],
                  bpTrackOk, bpOffhandDistToHMDOk, bpOffhandHeightOk, bpOffhandAngleOk, bpHmdToOffhandAngleOk);


            // Check quicksave
            if (canUseQuickSave) {
                int channel = (vr_control_scheme->integer >= 10) ? 1 : 0;
                RTCWVR_Vibrate(40, channel, 0.5); // vibrate to let user know they can switch

                if (((pOffTrackedRemoteNew->Buttons & offButton1) !=
                     (pOffTrackedRemoteOld->Buttons & offButton1)) &&
                    (pOffTrackedRemoteNew->Buttons & offButton1)) {
                    sendButtonActionSimple("savegame quicksave");
                }

                if (((pOffTrackedRemoteNew->Buttons & offButton2) !=
                     (pOffTrackedRemoteOld->Buttons & offButton2)) &&
                    (pOffTrackedRemoteNew->Buttons & offButton2)) {
                    sendButtonActionSimple("loadgame quicksave");
                }
            }

            if (!handInBackpack) {
                canUseBackpack = false;
            }
            else if (!canUseBackpack && vr.backpackitemactive == 0) {
                int channel = (vr_control_scheme->integer >= 10) ? 0 : 1;
                    RTCWVR_Vibrate(40, channel, 0.5); // vibrate to let user know they can switch

                canUseBackpack = true;
            }

            dominantGripPushed = (pDominantTrackedRemoteNew->Buttons &
                                  ovrButton_GripTrigger) != 0;
            bool dominantButton1Pushed = (pDominantTrackedRemoteNew->Buttons &
                                     domButton1) != 0;
            bool dominantButton2Pushed = (pDominantTrackedRemoteNew->Buttons &
                                          domButton2) != 0;

            if (!canUseBackpack)
            {
                if (dominantGripPushed) {
                    if (dominantGripPushTime == 0) {
                        dominantGripPushTime = GetTimeInMilliSeconds();
                    } else if (!handInBackpack && !vr.backpackitemactive && !vr.mountedgun && !vr.scopeengaged && !vr.weapon_stabilised &&
                            (GetTimeInMilliSeconds() - dominantGripPushTime) > vr_reloadtimeoutms->integer) {
                        vr.wheelSelectorEnabled = true;
                    }
                }
                else
                {
                    if (vr.wheelSelectorEnabled) {
                        vr.wheelSelectorEnabled = false;
                        sendButtonActionSimple("wheelselectorselect");
                    }
                    else if (vr.backpackitemactive == 1) {
                        //Restores last used weapon if possible
                        char buffer[32];
                        sprintf(buffer, "weapon %i", vr.lastweaponid);
                        sendButtonActionSimple(buffer);
                        vr.backpackitemactive = 0;
                    }
                    else if ((GetTimeInMilliSeconds() - dominantGripPushTime) <
                        vr_reloadtimeoutms->integer) {
                        sendButtonActionSimple("+reload");
                        finishReloadNextFrame = true;
                    }
                    dominantGripPushTime = 0;
                }

                if (!dominantButton1Pushed && vr.backpackitemactive == 2)
                {
                    char buffer[32];
                    sprintf(buffer, "weapon %i", vr.lastweaponid);
                    sendButtonActionSimple(buffer);
                    vr.backpackitemactive = 0;
                }

                if (!dominantButton2Pushed && vr.backpackitemactive == 3)
                {
                    vr.backpackitemactive = 0;
                }
            } else {
                if (vr.backpackitemactive == 0) {
                    if (dominantGripPushed) {
                        vr.lastweaponid = vr.weaponid;
                        //Initiate grenade from backpack mode
                        sendButtonActionSimple("weaponbank 6");
                        int channel = (vr_control_scheme->integer >= 10) ? 0 : 1;
                        RTCWVR_Vibrate(80, channel, 0.8); // vibrate to let user know they switched
                        vr.backpackitemactive = 1;
                    }
                    else if (dominantButton1Pushed)
                    {
                        vr.lastweaponid = vr.weaponid;
                        //Initiate knife from backpack mode
                        sendButtonActionSimple("weapon 1");
                        int channel = (vr_control_scheme->integer >= 10) ? 0 : 1;
                        RTCWVR_Vibrate(80, channel, 0.8); // vibrate to let user know they switched
                        vr.backpackitemactive = 2;
                    }
                    else if (dominantButton2Pushed && vr.hasbinoculars)
                    {
                        int channel = (vr_control_scheme->integer >= 10) ? 0 : 1;
                        RTCWVR_Vibrate(80, channel, 0.8); // vibrate to let user know they switched
                        vr.backpackitemactive = 3;
                    }
                }
            }
        }



        //Right-hand specific stuff
        {
            //This section corrects for the fact that the controller actually controls direction of movement, but we want to move relative to the direction the
            //player is facing for positional tracking

            //Positional movement speed correction for when we are not hitting target framerate
            static double lastframetime = 0;
            int refresh = GetRefresh();
            int requestedRefresh = GetRequestedRefresh();
            double newframetime = GetTimeInMilliSeconds();
            float multiplier = (float)((1000.0 / refresh) / (newframetime - lastframetime));
            lastframetime = newframetime;

            vec2_t v;
            float factor = (refresh / requestedRefresh) * vr_positional_factor->value; // adjust positional factor based on refresh rate
            rotateAboutOrigin(-vr.hmdposition_delta[0] * factor * multiplier,
                              vr.hmdposition_delta[2] * factor * multiplier, - vr.hmdorientation[YAW], v);
            positional_movementSideways = v[0];
            positional_movementForward = v[1];

            ALOGV("        positional_movementSideways: %f, positional_movementForward: %f",
                  positional_movementSideways,
                  positional_movementForward);

            //Jump (B Button)
            if (vr.backpackitemactive != 2 && !canUseBackpack) {
                if ((primaryButtonsNew & primaryButton2) != (primaryButtonsOld & primaryButton2))
                {
                    if (vr.mountedgun) {
                        sendButtonAction("+activate", (primaryButtonsNew & primaryButton2) != 0);
                    } else {
                        Sys_QueEvent( 0, SE_KEY, K_SPACE, (primaryButtonsNew & primaryButton2) != 0, 0, NULL );
                    }
                }
            }




            //Fire Primary
            if (vr.backpackitemactive != 3 && !vr.binocularsActive && // Can't fire while holding binoculars
                !vr.velocitytriggered && // Don't fire velocity triggered weapons
                (pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger) !=
                (pDominantTrackedRemoteOld->Buttons & ovrButton_Trigger)) {

                ALOGV("**WEAPON EVENT**  Not Grip Pushed %sattack", (pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger) ? "+" : "-");
                qboolean firing = (pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger);
                if (vr.dualwielding) {
                    if (firing) {
                        vr.akimboTriggerState |= ACTIVE_WEAPON_HAND;
                        sendButtonAction("+attack", firing);
                    } else {
                        vr.akimboTriggerState &= ~ACTIVE_WEAPON_HAND;
                        if (!vr.akimboTriggerState) { // Stop firing only if we are not still firing with off-hand weapon
                            sendButtonAction("+attack", firing);
                        }
                    }
                } else {
                    vr.akimboTriggerState = 0;
                    sendButtonAction("+attack", firing);
                }
            }
            else if (binocularsactive) // trigger can zoom-in binoculars, remove from face to reset
            {
                static qboolean zoomin = true;
                if (pDominantTrackedRemoteNew->Buttons & ovrButton_Trigger) {
                    sendButtonActionSimple(zoomin ? "weapnext" : "weapprev");
                } else if (pDominantTrackedRemoteOld->Buttons & ovrButton_Trigger)
                {
                    zoomin = !zoomin;
                }
            }

            //Duck
            if (vr.backpackitemactive != 2 &&
                !canUseBackpack &&
                (primaryButtonsNew & primaryButton1) !=
                (primaryButtonsOld & primaryButton1)) {

                sendButtonAction("+movedown", (primaryButtonsNew & primaryButton1));
                // Reset max height for IRL crouch
                vr.maxHeight = 0;
            }

			//Weapon Chooser
			static qboolean itemSwitched = false;
			if (!vr.wheelSelectorEnabled && between(-0.2f, pPrimaryJoystick->x, 0.2f) &&
				(between(0.8f, pPrimaryJoystick->y, 1.0f) || between(-1.0f, pPrimaryJoystick->y, -0.8f)))
			{
				if (!itemSwitched) {
					if (between(0.8f, pPrimaryJoystick->y, 1.0f))
					{
                        vr.binocularsActive = false;
                        sendButtonActionSimple("weapprev");
					}
					else
					{
                        vr.binocularsActive = false;
                        sendButtonActionSimple("weapnext");
					}
					itemSwitched = true;
				}
			} else {
				itemSwitched = false;
			}
        }

        {
            //"Use" (open doors etc)
            if ((pDominantTrackedRemoteNew->Buttons & ovrButton_Joystick) !=
                (pDominantTrackedRemoteOld->Buttons & ovrButton_Joystick)) {

                sendButtonAction("+activate",
                                 (pDominantTrackedRemoteNew->Buttons & ovrButton_Joystick) ? 1 : 0);
            }

            //Apply a filter and quadratic scaler so small movements are easier to make
            float dist = length(pSecondaryJoystick->x, pSecondaryJoystick->y);
            float nlf = nonLinearFilter(dist);
            float x = (nlf * pSecondaryJoystick->x) + pFootTrackingNew->LeftJoystick.x;
            float y = (nlf * pSecondaryJoystick->y) - pFootTrackingNew->LeftJoystick.y;

            vr.player_moving = (fabs(x) + fabs(y)) > 0.05f;

            //Adjust to be off-hand controller oriented
            vec2_t v;
            rotateAboutOrigin(x, y, controllerYawHeading, v);

            //Move a lot slower if scope is engaged
            remote_movementSideways = v[0] * (vr.scopeengaged ? 0.3f : 1.0f) * vr_movement_multiplier->value;
            remote_movementForward = v[1] * (vr.scopeengaged ? 0.3f : 1.0f) * vr_movement_multiplier->value;
            ALOGV("        remote_movementSideways: %f, remote_movementForward: %f",
                  remote_movementSideways,
                  remote_movementForward);


            static qboolean stopUseItemNextFrame = false;
            static qboolean startUseItemNextFrame = false;
            if (vr.useHoldableItem) // Item toggled via wheel selector
            {
                startUseItemNextFrame = true;
                vr.useHoldableItem = false;
            }
            if (startUseItemNextFrame)
            {
                Cbuf_AddText("+useitem\n");
                startUseItemNextFrame = qfalse;
                stopUseItemNextFrame = qtrue;
            }
            if (stopUseItemNextFrame)
            {
                Cbuf_AddText("-useitem\n");
                stopUseItemNextFrame = false;
            }

            // Detach scopes, draw akimbo
            if (!canUseQuickSave) {
                if (((secondaryButtonsNew & secondaryButton1) !=
                     (secondaryButtonsOld & secondaryButton1)) &&
                    (secondaryButtonsNew & secondaryButton1)) {
                    if (!vr.scopedweapon) {
                        ALOGV("**WEAPON EVENT**  weapalt");
                        sendButtonActionSimple("weapalt");
                    } else if (vr.detachablescope) {
                        //See if we are able to detach the scope
                        ALOGV("**WEAPON EVENT**  weapdetachscope");
                        sendButtonActionSimple("weapdetachscope");
                    }
                }
            }

            // Notebook
            if (!canUseQuickSave) {
                if (((secondaryButtonsNew & secondaryButton2) !=
                     (secondaryButtonsOld & secondaryButton2)) &&
                    (secondaryButtonsNew & secondaryButton2)) {
                    sendButtonActionSimple("notebook");
                }
            }


            //Kick!
            if ((pOffTrackedRemoteNew->Buttons & ovrButton_Joystick) !=
                (pOffTrackedRemoteOld->Buttons & ovrButton_Joystick)) {
                sendButtonAction("+kick", (pOffTrackedRemoteNew->Buttons & ovrButton_Joystick));
            }

            //We need to record if we have started firing primary so that releasing trigger will stop definitely firing, if user has pushed grip
            //in meantime, then it wouldn't stop the gun firing and it would get stuck
            if (!vr.teleportenabled || vr.dualwielding)
            {
                if ((vr.dualwielding) && vr.backpackitemactive != 3 && !vr.binocularsActive) {
                    // Fire off-hand weapon
                    if ((pOffTrackedRemoteNew->Buttons & ovrButton_Trigger) != (pOffTrackedRemoteOld->Buttons & ovrButton_Trigger)) {
                        ALOGV("**WEAPON EVENT**  Off-hand trigger %sattack", (pOffTrackedRemoteNew->Buttons & ovrButton_Trigger) ? "+" : "-");
                        qboolean firing = (pOffTrackedRemoteNew->Buttons & ovrButton_Trigger);
                        if (firing) {
                            vr.akimboTriggerState |= ACTIVE_OFF_HAND;
                            sendButtonAction("+attack", firing);
                        } else {
                            vr.akimboTriggerState &= ~ACTIVE_OFF_HAND;
                            if (!vr.akimboTriggerState) { // Stop firing only if we are not still firing with main weapon
                                sendButtonAction("+attack", firing);
                            }
                        }
                    }
                    // When holding off-hand weapon, run (or teleport) with left grip
                    if (!vr.teleportenabled) {
                        handleTrackedControllerButton(pOffTrackedRemoteNew, pOffTrackedRemoteOld, ovrButton_GripTrigger, K_SHIFT);
                    } else {
                        if (pOffTrackedRemoteNew->Buttons & ovrButton_GripTrigger) {
                            vr.teleportseek = qtrue;
                        } else if (vr.teleportseek) {
                            vr.teleportseek = qfalse;
                            vr.teleportexecute = vr.teleportready;
                            vr.teleportready = qfalse;
                        }
                    }
                } else {
                    if (vr.akimboTriggerState) {
                        // Akimbo no longer active, stop attacking
                        vr.akimboTriggerState = 0;
                        sendButtonAction("+attack", qfalse);
                    }
                    // Run
                    handleTrackedControllerButton(pOffTrackedRemoteNew, pOffTrackedRemoteOld, ovrButton_Trigger, K_SHIFT);
                }
            } else {
                if (pOffTrackedRemoteNew->Buttons & ovrButton_Trigger)
                {
                    vr.teleportseek = qtrue;
                }
                else if (vr.teleportseek)
                {
                    vr.teleportseek = qfalse;
                    vr.teleportexecute = vr.teleportready;
                    vr.teleportready = qfalse;
                }
            }


            //Resync Yaw on mounted gun transition
            static int usingMountedGun = false;
            if (vr.mountedgun != usingMountedGun)
            {
                resyncClientYawWithGameYaw = 10; // HACK
                usingMountedGun = vr.mountedgun;
            }

            if (vr.wheelSelectorEnabled) {
                static int wheelSelectorSwitched = false;
                if (between(0.8f, pPrimaryJoystick->x, 1.0f) || between(-1.0f, pPrimaryJoystick->x, -0.8f)) {
                    if (!wheelSelectorSwitched) {
					    if (between(0.8f, pPrimaryJoystick->x, 1.0f)) {
                            sendButtonActionSimple("wheelselectornext");
					    } else {
                            sendButtonActionSimple("wheelselectorprev");
					    }
                        wheelSelectorSwitched = true;
                    }
                } else {
                    wheelSelectorSwitched = false;
                }
            } else {
                //No snap turn when using mounted gun
                static int syncCount = 0;
                static int increaseSnap = true;
                if (!vr.mountedgun && !vr.scopeengaged) {
                    if (pPrimaryJoystick->x > 0.7f) {
                        if (increaseSnap) {
                            float turnAngle = vr_turn_mode->integer ? (vr_turn_angle->value / 9.0f) : vr_turn_angle->value;
                            snapTurn -= turnAngle;

                            if (vr_turn_mode->integer == 0) {
                                increaseSnap = false;
                            }

                            if (snapTurn < -180.0f) {
                                snapTurn += 360.f;
                            }

                            RTCWVR_ResyncClientYawWithGameYaw();
                        }
                    } else if (pPrimaryJoystick->x < 0.3f) {
                        increaseSnap = true;
                    }

                    static int decreaseSnap = true;
                    if (pPrimaryJoystick->x < -0.7f) {
                        if (decreaseSnap) {

                            float turnAngle = vr_turn_mode->integer ? (vr_turn_angle->value / 9.0f) : vr_turn_angle->value;
                            snapTurn += turnAngle;

                            //If snap turn configured for less than 10 degrees
                            if (vr_turn_mode->integer == 0) {
                                decreaseSnap = false;
                            }

                            if (snapTurn > 180.0f) {
                                snapTurn -= 360.f;
                            }

                            RTCWVR_ResyncClientYawWithGameYaw();
                        }
                    } else if (pPrimaryJoystick->x > -0.3f) {
                        decreaseSnap = true;
                    }
                }
                else {
                    if (fabs(pPrimaryJoystick->x) > 0.5f) {
                        if (increaseSnap)
                        {
                            RTCWVR_ResyncClientYawWithGameYaw();
                        }
                        increaseSnap = false;
                    }
                    else
                    {
                        increaseSnap = true;
                    }
                }
            }
        }

        updateScopeAngles();
    }

    // Process "use" gesture
    if (vr_gesture_triggered_use->integer) {
        int thirdPersonActive = Cvar_VariableIntegerValue("cg_thirdPerson");
        bool gestureUseAllowed = !vr.weapon_stabilised && !vr.mountedgun && !vr.scopeengaged && !vr.wheelSelectorEnabled && !thirdPersonActive;
        // Off-hand gesture
        float distanceToBody = sqrt(vr.offhandoffset[0]*vr.offhandoffset[0] + vr.offhandoffset[2]*vr.offhandoffset[2]);
        if (gestureUseAllowed && (distanceToBody > vr_use_gesture_boundary->value)) {
            if (!(vr.useGestureState & ACTIVE_OFF_HAND)) {
                sendButtonAction("+activate2", true);
            }
            vr.useGestureState |= ACTIVE_OFF_HAND;
        } else {
            if (vr.useGestureState & ACTIVE_OFF_HAND) {
                sendButtonAction("+activate2", false);
            }
            vr.useGestureState &= ~ACTIVE_OFF_HAND;
        }
        // Weapon-hand gesture
        distanceToBody = sqrt(vr.current_weaponoffset[0]*vr.current_weaponoffset[0] + vr.current_weaponoffset[2]*vr.current_weaponoffset[2]);
        if (gestureUseAllowed && (distanceToBody > vr_use_gesture_boundary->value)) {
            if (!(vr.useGestureState & ACTIVE_WEAPON_HAND)) {
                sendButtonAction("+activate", true);
            }
            vr.useGestureState |= ACTIVE_WEAPON_HAND;
        } else {
            if (vr.useGestureState & ACTIVE_WEAPON_HAND) {
                sendButtonAction("+activate", false);
            }
            vr.useGestureState &= ~ACTIVE_WEAPON_HAND;
        }
    } else {
        if (vr.useGestureState & ACTIVE_OFF_HAND) {
            sendButtonAction("+activate2", false);
        }
        if (vr.useGestureState & ACTIVE_WEAPON_HAND) {
            sendButtonAction("+activate", false);
        }
        vr.useGestureState = 0;
    }

    //Save state
    rightTrackedRemoteState_old = rightTrackedRemoteState_new;
    leftTrackedRemoteState_old = leftTrackedRemoteState_new;
}
