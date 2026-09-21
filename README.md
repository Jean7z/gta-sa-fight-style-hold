# GTA:SA — Fight Style Hold

[![release](https://img.shields.io/github/v/release/Jean7z/gta-sa-fight-style-hold)](https://github.com/Jean7z/gta-sa-fight-style-hold/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](./LICENSE)

A mod for **GTA: San Andreas** on Android (Android Mod Loader) that makes the
melee style you learned at the gym actually drive your punches, instead of the
generic unarmed combo. Supports **2.10 (arm64)** and **2.00 (armv7)**.

In vanilla, attacking with bare fists always resolves to the default unarmed
combo. The combo set of the style you unlocked — Boxing, Karate or Kung Fu — is
only reachable through a different attack command that the game never issues
for fists, so learning a style changes nothing about how you punch. This mod
rewrites that single command. No keys, no config file, no HUD changes.

## Table of contents

- [Install](#install)
- [How it works](#how-it-works)
- [Verify it loaded](#verify-it-loaded)
- [Building from source](#building-from-source)
- [Verified offsets](#verified-offsets)
- [Notes and limitations](#notes-and-limitations)
- [License](#license)

## Install

Pick the `.so` matching your game version:

| Game version | Architecture | File |
| --- | --- | --- |
| 2.10 | arm64-v8a | `libAML_PSDK_FightStyle64.so` |
| 2.00 | armeabi-v7a | `libAML_PSDK_FightStyle.so` |

Copy it into the game's mods folder:

```bash
/sdcard/Android/data/com.rockstargames.gtasa/mods/
```

Then launch the game. Learn a fighting style at any gym (or load a save that
already has one) and attack with bare fists.

> [!NOTE]
> Requires a game build with Android Mod Loader installed. There is no config
> file: the mod has nothing to configure and stays out of the way until you
> actually have a style. The game version is detected from the process ABI —
> the 2.10 APK ships arm64-only and the 2.00 APK ships armv7-only, so each
> build carries its own verified offset (see [Verified offsets](#verified-offsets)).

## How it works

`CTaskSimpleFight::ProcessPed` picks the combo set every attack from the attack
command it was handed:

```text
m_nComboSet = GetAvailableComboSet(ped, m_nNextCommand);
```

and `GetAvailableComboSet` maps that command to a combo set:

| Attack command | Combo set it resolves to |
| --- | --- |
| `0xC` | `ped->m_nFightingStyle` — the style you learned |
| `0xB` | the weapon's fight level, which is `4` (`BASIC`) for fists |

Fists therefore always land on `0xB` → `BASIC`, and the learned style is only
reachable through `0xC`. The mod hooks `GetAvailableComboSet` and rewrites the
command in one narrow case:

1. the ped is the player,
2. the player has a style learned (`m_nFightingStyle != STYLE_DEFAULT`),
3. the command is one of the four attack commands (`0xB`–`0xE`),
4. the hand is empty — `m_Weapons[m_nActiveWeaponSlot].m_eWeaponType == WEAPON_UNARMED`.

The last condition is the fix: the engine's own `0xC` path returns the learned
style no matter what's in hand, so without the gate a knife, bat or crowbar
would replay the gym combos too. Gating on `WEAPON_UNARMED` keeps armed melee
on its native weapon fight level and reserves the style for bare fists.

It then hands the engine command `0xC`. Everything else about the attack is the
engine's own style path, untouched: animation block refcounting, the chain
counter, target selection and the attack period all run as shipped. Because the
game re-issues the attack command every attack period, holding the attack
button keeps the style's combo advancing instead of restarting the default one.

NPCs are never touched, and nothing about the mod is written to your save.

## Verify it loaded

```bash
logcat -d | grep -E "FightStyle|AndroidModLoader"
```

Expected output:

```text
I AndroidModLoader: Mod (GUID net.psdk.samod.fightstyle) has been preprocessed.
I FightStyle: FightStyle loaded [2.10 arm64-v8a]. GetAvailableComboSet=0x77a57e0664
I FightStyle: Style combo active: attack command 11 -> 12 (style=5)
```

The first line after the load banner is logged once, the first time the mod
rewrites a command — fighting unarmed with a style learned — so you can confirm
the hook fires without flooding the log on every punch. On a 2.00 (armv7) game
the bracketed variant reads `[2.00 armeabi-v7a]`, and a `style=4` rewrite never
happens — that is the "no style learned" case.

## Building from source

Clone with the SDK submodule and build with the Android NDK (r29 used here):

```bash
git clone --recursive https://github.com/Jean7z/gta-sa-fight-style-hold
cd gta-sa-fight-style-hold
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk \
  NDK_APPLICATION_MK=./Application.mk
```

Outputs:

- `libs/arm64-v8a/libAML_PSDK_FightStyle64.so`
- `libs/armeabi-v7a/libAML_PSDK_FightStyle.so`

## Verified offsets

Symbols are resolved at runtime from the game's dynamic symbol table
(`CTaskSimpleFight::GetAvailableComboSet(CPed*, signed char)` and
`FindPlayerPed(int)`) — no hard-coded function addresses. Only the struct
offsets and command constants below are compiled in, each read straight out of
the instruction that uses it in the shipped `libGTASA.so`:

| Constant / field | 2.10 (arm64) | 2.00 (armv7) | Read in |
| --- | --- | --- | --- |
| `CPed::m_nFightingStyle` | `+0x8FD` | `+0x735` | `GetAvailableComboSet` |
| Attack command, style path | `0xC` | `0xC` | `GetAvailableComboSet` |
| Attack command, default path | `0xB` | `0xB` | `GetAvailableComboSet` |
| `STYLE_DEFAULT` (no style) | `4` | `4` | `GetAvailableComboSet` |
| `CPed::m_nActiveWeaponSlot` | `+0x8DC` | `+0x71C` | `GetAvailableComboSet` |
| `CPed::m_Weapons` (array base) | `+0x730` | `+0x5A4` | `GetAvailableComboSet` |
| `m_Weapons` slot stride | `0x20` | `0x1C` | `GetAvailableComboSet` |
| `WEAPON_UNARMED` | `0` | `0` | `GetAvailableComboSet` |

Evidence, for anyone re-deriving these on another build:

```text
2.10 arm64  GetAvailableComboSet @0x5DA664   ldrb   w20, [x22, #0x8FD]   ; command 0xC
2.10 arm64  GetAvailableComboSet @0x5DA664   ldrsb  x8,  [x22, #0x8DC]   ; active weapon slot
2.10 arm64  GetAvailableComboSet @0x5DA664   ldr    w0,  [x8,  #0x730]   ; m_Weapons[slot].m_eWeaponType
2.00 armv7  GetAvailableComboSet @0x4D90F8   ldrb.w r9,  [r6,  #0x735]   ; command 0xC
2.00 armv7  GetAvailableComboSet @0x4D90F8   ldrsb.w r0, [r6,  #0x71C]   ; active weapon slot
2.00 armv7  GetAvailableComboSet @0x4D90F8   ldr.w  r0,  [r0,  #0x5A4]   ; m_Weapons[slot].m_eWeaponType
```

Both versions take the identical branch: command `0xC` returns the fighting
style, any other attack command returns the weapon fight level and falls back to
`4` when that level is `4`. The weapon in hand is resolved the same way on both
ABIs — `ldrsb` the active slot, index `m_Weapons` by it, read `m_eWeaponType` —
mirroring the classic PC pattern
`CWeaponInfo::GetWeaponInfo(ped->m_Weapons[ped->m_nActiveWeaponSlot].m_eWeaponType)`.

## Notes and limitations

- **Fists only.** A knife, bat or crowbar keeps its native weapon fight level;
  the gym style is applied only while unarmed (`WEAPON_UNARMED`).
- **Player only.** NPCs keep the vanilla behaviour, so a learned style is never
  imposed on enemies.
- **The 2.00 (armv7) build is verified statically**, not run-tested: its offset
  comes from disassembling the shipped 2.00 `libGTASA.so`, not from a live
  session on a 2.00 device. The 2.10 build was run-tested in game.
- **It does not add styles.** If you never learned one, behaviour is unchanged.
- The mod does not read or write the save file, and no `.ini` is created.

## License

[MIT](./LICENSE). Built on the
[aml-psdk](https://github.com/AndroidModLoader/aml-psdk) template.
