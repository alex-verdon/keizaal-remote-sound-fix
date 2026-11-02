# Keizaal Remote Sound Fix

Plugin SKSE pour **Skyrim Special Edition / Anniversary Edition**, à installer uniquement côté client.
Fait pour [Keizaal](https://keizaal.com) (SkyMP), il marche aussi en solo.

Les sauts, atterrissages, potions, nourriture et objets ramassés **des autres joueurs** s'entendent
à leur position et baissent avec la distance. Avant le correctif, ils résonnaient « dans ta tête »,
comme si c'était toi, même à l'autre bout de la ville.

## Installation

1. Télécharge `RemoteSoundFix.dll` dans les [Releases](../../releases), ou compile-le (voir plus bas).
2. Copie-le dans `<dossier Skyrim Special Edition>\Data\SKSE\Plugins`.
3. Lance le jeu normalement (launcher Keizaal).

Prérequis : SKSE et [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444),
tous deux déjà installés par Keizaal.

**Vérifier :** `Data\SKSE\Plugins\RemoteSoundFix.log` doit se terminer par `37 son(s) corrigé(s)`.
**Désinstaller :** supprime `RemoteSoundFix.dll`.

## Le problème

Dans SkyMP, chaque autre joueur est affiché comme un PNJ et déclenche les sons des PNJ.
Chaque son de Skyrim passe par un *Sound Output Model*, qui le joue soit en 3D (positionné),
soit en 2D (« Defined Speaker Output », sans position ni atténuation).

Dans `Skyrim.esm`, 37 sons que les PNJ utilisent sont reliés par erreur à
`SOMMono01400Player1st` (`000B4058`), un modèle 2D prévu pour le joueur en vue 1re personne :

| Famille | Nb | Exemples |
| --- | --- | --- |
| Sauts (toutes surfaces) et atterrissage sur pierre | 8 | `FSTStoneSolidJumpUpSD`, `FSTStoneSolidJumpDownSD` |
| Manger / boire | 4 | `ITMFoodEat`, `ITMPotionUse`, `ITMPoisonUse`, `NPCHumanEatSoup` |
| Ramasser / poser des objets | 25 | `ITMGoldUpSD`, `ITMPotionUpSD`, `ITMGenericWeaponUpSD` |

Chaîne pour les sauts : `FSTP DefaultFootJumpUp/Down` → `IPDS` → `IPCT` (son 1 = PNJ, son 2 = joueur) → `SNDR` → `SOPM`.
Pour la nourriture et les potions : le champ *Consume Sound* des `ALCH`.

En solo, les PNJ font rarement ces actions. En multijoueur, les autres joueurs les font sans arrêt.

## Pourquoi un plugin SKSE et pas un .esp

Un .esp qui change l'`ONAM` de ces `SNDR` suffirait (c'est ce que fait
[ce mod](https://www.nexusmods.com/skyrimspecialedition/mods/99539) pour les sauts).
Mais le launcher Keizaal désactive les plugins qu'il ne connaît pas : il les déplace dans `Data\_disabledByKzl`.
Il ne touche pas aux DLL SKSE.

## Fonctionnement

Au message SKSE `kDataLoaded`, le plugin :

1. lit l'Address Library du jeu (`versionlib-*.bin`) pour trouver la table globale des forms (ID AE `400507`) ;
2. y cherche les 37 `BGSSoundDescriptorForm` (parcours linéaire de la `BSTHashMap`) ;
3. pour chacun, remplace `BGSStandardSoundDef::outputModel` (offset `0x48`) par
   `SOMMono01400Player3rd` (`000428B6`, 3D), **seulement** s'il pointe encore sur `SOMMono01400Player1st`.
   Un son déjà modifié par un autre mod n'est pas touché.

Rien n'est écrit sur le disque, à part le log. Tous les accès mémoire sont vérifiés (`VirtualQuery`) :
en cas de problème, le plugin n'applique rien et l'écrit dans le log.

Écrit en C sans CommonLibSSE, et donc compilable sans Visual Studio.

## Compiler

```sh
pip install ziglang
./build.sh            # → build/RemoteSoundFix.dll
```

## Compatibilité

- Testé sur Skyrim 1.6.1170 (Keizaal, novembre 2026).
- Les versions AE (1.6.x) avec Address Library devraient marcher. Les versions SE 1.5.x ne sont pas prises en charge.

## Historique

- **1.0.1** : corrige un plantage au chargement (débordement de pile en lisant l'Address Library).
- **1.0.0** : première version.

## Licence

MIT, voir [LICENSE](LICENSE).
