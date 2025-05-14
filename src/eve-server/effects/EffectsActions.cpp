/**
* @name EffectsActions.cpp
*   This file is for decoding and processing the effect's action data
*   Copyright 2017  EVEmu Team
*
* @Author:    Allan
* @date:      27 March 2017
*
*/

/** @note  not currently used.  may not be used.  */

#include "Client.h"
#include "effects/EffectsActions.h"
#include "system/SystemEntity.h"

/*
 * # Effects Logging:
 * EFFECTS=0
 * EFFECTS__ERROR=1
 * EFFECTS__WARNING=0
 * EFFECTS__MESSAGE=0
 * EFFECTS__DEBUG=0
 * EFFECTS__TRACE=0
 */


void FxAction::DoAction(uint8 action, SystemEntity* pSE)
{
    using namespace FX;
    if (action == FX::Action::Invalid)
        ; //make error and return

    switch (action) {
        case FX::Action::ATTACK: { // 13,
        } break;
        case FX::Action::CHEATTELEDOCK: { // 15,
        } break;
        case FX::Action::CHEATTELEGATE: { // 16,
        } break;
        case FX::Action::DECLOAKWAVE: { // 19,
        } break;
        case FX::Action::ECMBURST: { //    30,
        } break;
        case FX::Action::EMPWAVE: { //     32,
        } break;
        case FX::Action::LAUNCH: { //      44,
        } break;
        case FX::Action::LAUNCHDEFENDERMISSILE: { // 45,
        } break;
        case FX::Action::LAUNCHDRONE: { // 46,
        } break;
        case FX::Action::LAUNCHFOFMISSILE: { // 47,
        } break;
        case FX::Action::POWERBOOST: { //  53,   //effectID 48  - Consumes power booster charges to increase the available power in the capacitor.
        } break;
        case FX::Action::TARGETHOSTILES: { // 70,
        } break;
        case FX::Action::TARGETSILENTLY: { // 71,
        } break;
        case FX::Action::TOOLTARGETSKILLS: { // 72,
        } break;
        case FX::Action::VERIFYTARGETGROUP: { // 74,
        } break;
        /* unique/special to EVEmu */
        case FX::Action::SPEEDBOOST: { // effectID 14 - prop mod to call destiny speed updates
            if (pSE != nullptr && pSE->DestinyMgr() != nullptr) {
                if (pSE->SysBubble() == nullptr) {
                    _log(EFFECTS__ERROR, "FxAction::DoAction(): SPEEDBOOST - SysBubble is null for entity %s(%u). Aborting SpeedBoost.",
                         pSE->GetName(), pSE->GetID());
                    return;
                }
                pSE->DestinyMgr()->SpeedBoost();
            } else {
                _log(EFFECTS__ERROR, "FxAction::DoAction(): SPEEDBOOST called with invalid pSE or DestinyMgr");
            }
        } break;
        /* these below are special as they are called AFTER the module's cycle */
        case FX::Action::CARGOSCAN: { //   14,
        } break;
        case FX::Action::MINE: { //        50,
        } break;
        case FX::Action::SHIPSCAN: { //    66,
        } break;
        case FX::Action::SURVEYSCAN: { //  69,
        } break;
    }

}
