#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <aml-psdk/game_sa/plugin.h>

#include <cstdint>

MYMODCFG(net.psdk.samod.fightstyle, SA Android Fight Style Hold, 1.1, Jean7z)

/* CTaskSimpleFight::ProcessPed picks the melee combo set from the attack command
 * it was handed:
 *
 *     m_nComboSet = GetAvailableComboSet(ped, m_nNextCommand);
 *
 * and GetAvailableComboSet maps that command to a combo set:
 *
 *     command 0xc  ->  ped->m_nFightingStyle    (the gym style you learned)
 *     command 0xb  ->  weapon info fight level, which is 4 (BASIC) for fists
 *
 * Fists with no style therefore always resolve to command 0xb, so every attack
 * replays the default unarmed combo no matter which style you learned.
 *
 * This mod rewrites only that command, for the player only, and only when a
 * style is actually learned. The engine's own style path then runs to
 * completion: anim block refcounting, chain counter and target selection are
 * left untouched, and the combo keeps advancing because the game re-issues the
 * attack command every attack period.
 *
 * Verified against the shipped libGTASA.so (real binaries, not the reversed
 * reimplementation):
 *   2.10 arm64  GetAvailableComboSet @0x5da664   ldrb w20, [x22, #0x8fd]  (style)
 *   2.00 armv7  GetAvailableComboSet @0x4d90f8   ldrb.w sb,[r6, #0x735]   (style)
 *
 * The engine resolves the weapon in hand the same way on both ABIs:
 *   type = m_Weapons[m_nActiveWeaponSlot].m_eWeaponType
 *   2.10 arm64: ldrb slot @ped+0x8dc,  array @ped+0x730,  stride 0x20
 *   2.00 armv7: ldrb slot @ped+0x71c,  array @ped+0x5a4,  stride 0x1c
 * which mirrors the classic PC pattern
 *   CWeaponInfo::GetWeaponInfo(ped->m_Weapons[ped->m_nActiveWeaponSlot].m_eWeaponType)
 *
 * The style isn't valid for armed melee: the engine's own 0xc path returns
 * the learned style no matter what's in hand, so we gate the rewrite on
 * WEAPON_UNARMED. Knife/bat/crowbar keep their native weapon fight level.
 */

#if defined(__aarch64__)
  #define P_STYLE       0x8fd /* CPed::m_nFightingStyle, 2.10 arm64-v8a  */
  #define P_WEAPON_SLOT 0x8dc /* CPed::m_nActiveWeaponSlot               */
  #define P_WEAPONS     0x730 /* CPed::m_Weapons                          */
  #define P_WEAPON_STRIDE 0x20
  #define ABI_LABEL     "2.10 arm64-v8a"
#else
  #define P_STYLE       0x735 /* CPed::m_nFightingStyle, 2.00 armeabi-v7a */
  #define P_WEAPON_SLOT 0x71c /* CPed::m_nActiveWeaponSlot               */
  #define P_WEAPONS     0x5a4 /* CPed::m_Weapons                          */
  #define P_WEAPON_STRIDE 0x1c
  #define ABI_LABEL     "2.00 armeabi-v7a"
#endif

#define STYLE_DEFAULT      4   /* eFightingStyle::STYLE_DEFAULT: no style learned */
#define WEAPON_UNARMED     0   /* eWeaponType::WEAPON_UNARMED: fists              */
#define CMD_ATTACK_FIRST   0xb /* first of the four fight attack commands */
#define CMD_ATTACK_LAST    0xe
#define CMD_ATTACK_STYLE   0xc /* the command whose combo set is the learned style */

typedef void* (*FindPlayerPedFn)(int);
static FindPlayerPedFn pFindPlayerPed;

DECL_HOOK(int, Fight_GetAvailableComboSet, void* self, void* ped, int mode)
{
    if(pFindPlayerPed && ped && ped == pFindPlayerPed(-1))
    {
        const uint8_t style = *(uint8_t*)((uintptr_t)ped + P_STYLE);
        const uint8_t m     = (uint8_t)mode;
        if(style != STYLE_DEFAULT && m >= CMD_ATTACK_FIRST && m <= CMD_ATTACK_LAST)
        {
            /* The learned style combo set only makes sense for fists: while
             * the engine's 0xc path happily returns the style for any weapon,
             * a knife/bat/crowbar should keep its native weapon fight level.
             * Mirror the engine's own read of the weapon in hand:
             *   m_Weapons[m_nActiveWeaponSlot].m_eWeaponType */
            const int8_t slot    = *(int8_t*)((uintptr_t)ped + P_WEAPON_SLOT);
            const uint32_t type  = *(uint32_t*)((uintptr_t)ped + P_WEAPONS + slot * P_WEAPON_STRIDE);
            if(type != WEAPON_UNARMED)
            {
                return Fight_GetAvailableComboSet(self, ped, mode);
            }

            /* Log the first rewrite only: enough to confirm the hook fires
             * without spamming a line every attack for the whole session. */
            static bool logged = false;
            if(!logged)
            {
                logged = true;
                logger->Info("Style combo active: attack command %d -> %d (style=%u)",
                             m, CMD_ATTACK_STYLE, style);
            }
            mode = CMD_ATTACK_STYLE;
        }
    }
    return Fight_GetAvailableComboSet(self, ped, mode);
}

ON_MOD_LOAD()
{
    logger->SetTag("FightStyle");

    pFindPlayerPed = (FindPlayerPedFn)GetMainLibrarySymbol("_Z13FindPlayerPedi");
    void* pCombo   = GetMainLibrarySymbol("_ZN16CTaskSimpleFight20GetAvailableComboSetEP4CPeda");
    if(!pFindPlayerPed || !pCombo)
    {
        logger->Error("FightStyle: symbols not found (FindPlayerPed=%p GetAvailableComboSet=%p)",
                      pFindPlayerPed, pCombo);
        return;
    }

    HOOK(Fight_GetAvailableComboSet, pCombo);

    logger->Info("FightStyle loaded [" ABI_LABEL "]. GetAvailableComboSet=%p", pCombo);
}
