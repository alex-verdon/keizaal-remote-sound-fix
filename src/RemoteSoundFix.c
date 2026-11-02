// RemoteSoundFix - plugin SKSE (client uniquement, sans .esp)
// Certains sons joués par les PNJ (donc les autres joueurs SkyMP) utilisent le modèle
// de sortie SOMMono01400Player1st, de type "Defined Speaker Output" (2D, sans distance).
// (ou SOMUIDefault, 2D lui aussi). Au chargement des données, ce plugin les rebascule
// sur SOMMono01400Player3rd (3D).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

typedef struct {
    uint32_t dataVersion, pluginVersion;
    char name[256], author[256], supportEmail[252];
    uint32_t versionIndependenceEx, versionIndependence;
    uint32_t compatibleVersions[16];
    uint32_t seVersionRequired;
} SKSEPluginVersionData;

EXPORT SKSEPluginVersionData SKSEPlugin_Version = {
    1, 3, "RemoteSoundFix", "Alex", "",
    0,
    1 | 4,   /* AddressLibraryPostAE | StructsPost629 */
    {0}, 0
};

typedef struct {
    uint32_t skseVersion, runtimeVersion, editorVersion, isEditor;
    void *(*QueryInterface)(uint32_t id);
    uint32_t (*GetPluginHandle)(void);
    uint32_t (*GetReleaseIndex)(void);
    const void *(*GetPluginInfo)(const char *name);
} SKSEInterface;

typedef struct { const char *sender; uint32_t type; uint32_t dataLen; void *data; } SKSEMessage;
typedef void (*EventCallback)(SKSEMessage *msg);
typedef struct {
    uint32_t interfaceVersion;
    char (*RegisterListener)(uint32_t listener, const char *sender, EventCallback handler);
} SKSEMessagingInterface;

static FILE *g_log;
static uint32_t g_runtime;
static uint32_t g_handle;
#define LOG(...) do { if (g_log) { fprintf(g_log, __VA_ARGS__); fputc('\n', g_log); fflush(g_log); } } while (0)

/* ---- Address Library (format AE = 2) ---- */
static int read_bytes(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }
static uint64_t rd(FILE *f, int n, int *ok) { if (n > 8) { *ok = 0; return 0; } uint64_t v = 0; if (!read_bytes(f, &v, n)) *ok = 0; return v; }

static uint64_t addrlib_offset(uint64_t wanted) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "Data\\SKSE\\Plugins\\versionlib-%u-%u-%u-%u.bin",
             g_runtime >> 24, (g_runtime >> 16) & 0xFF, (g_runtime >> 4) & 0xFFF, g_runtime & 0xF);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("Address Library introuvable: %s", path); return 0; }
    int ok = 1;
    int32_t format = (int32_t)rd(f, 4, &ok);
    if (format != 2) { LOG("Format Address Library inattendu: %d", format); fclose(f); return 0; }
    fseek(f, 16, SEEK_CUR);  /* version[4] */
    int32_t nameLen = (int32_t)rd(f, 4, &ok);
    fseek(f, nameLen, SEEK_CUR);
    uint64_t ptrSize = (uint64_t)(int32_t)rd(f, 4, &ok);
    int32_t count = (int32_t)rd(f, 4, &ok);
    uint64_t id = 0, off = 0, pid = 0, poff = 0, result = 0;
    for (int32_t i = 0; i < count && ok; i++) {
        uint8_t type = (uint8_t)rd(f, 1, &ok);
        uint8_t lo = type & 0xF, hi = type >> 4;
        switch (lo) {
            case 0: id = rd(f, 8, &ok); break;
            case 1: id = pid + 1; break;
            case 2: id = pid + rd(f, 1, &ok); break;
            case 3: id = pid - rd(f, 1, &ok); break;
            case 4: id = pid + rd(f, 2, &ok); break;
            case 5: id = pid - rd(f, 2, &ok); break;
            case 6: id = rd(f, 2, &ok); break;
            case 7: id = rd(f, 4, &ok); break;
        }
        uint64_t tmp = (hi & 8) ? (poff / ptrSize) : poff;
        switch (hi & 7) {
            case 0: off = rd(f, 8, &ok); break;
            case 1: off = tmp + 1; break;
            case 2: off = tmp + rd(f, 1, &ok); break;
            case 3: off = tmp - rd(f, 1, &ok); break;
            case 4: off = tmp + rd(f, 2, &ok); break;
            case 5: off = tmp - rd(f, 2, &ok); break;
            case 6: off = rd(f, 2, &ok); break;
            case 7: off = rd(f, 4, &ok); break;
        }
        if (hi & 8) off *= ptrSize;
        if (id == wanted) { result = off; break; }
        pid = id; poff = off;
    }
    fclose(f);
    if (!result) LOG("ID %llu absent de l'Address Library", (unsigned long long)wanted);
    return result;
}

static int readable(const void *p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

/* ---- Recherche de forms : parcours linéaire de la table globale (BSTHashMap<FormID,TESForm*>) ---- */
static uint8_t *g_map;
static uint8_t *lookup(uint32_t formID) {
    uint32_t cap = *(uint32_t *)(g_map + 0x0C);
    uint8_t *entries = *(uint8_t **)(g_map + 0x28);
    if (!entries || !readable(entries, (size_t)cap * 0x18)) return NULL;
    for (uint32_t i = 0; i < cap; i++) {
        uint8_t *e = entries + (size_t)i * 0x18;
        if (*(void **)(e + 0x10) == NULL) continue;           /* slot vide */
        if (*(uint32_t *)e != formID) continue;
        uint8_t *form = *(uint8_t **)(e + 0x08);
        if (form && readable(form, 0x30) && *(uint32_t *)(form + 0x14) == formID) return form;
    }
    return NULL;
}

/* Sons à corriger (Skyrim.esm) */
#define PLAYER1ST 0x0B4058  /* SOMMono01400Player1st (2D, sans atténuation) */
#define UIDEFAULT 0x0B75FB  /* SOMUIDefault (2D, sans atténuation) */
#define PLAYER3RD 0x0428B6  /* SOMMono01400Player3rd (3D) */

/* Sons à corriger : id (ordre de chargement standard Skyrim/Update/Dawnguard/HearthFires/Dragonborn),
   nom, et modèle de sortie attendu. Un son dont le modèle n'est pas celui attendu n'est pas touché. */
static const struct { uint32_t id; const char *name; uint32_t from; } TARGETS[] = {
    /* sauts / atterrissage */
    {0x071FDB, "FSTGravelJumpUpSD", PLAYER1ST}, {0x06A161, "FSTStoneSolidJumpUpSD", PLAYER1ST},
    {0x071AE5, "FSTSnowJumpUpSD", PLAYER1ST},   {0x06A15D, "FSTDirtJumpUpSD", PLAYER1ST},
    {0x100FFC, "FSTWaterJumpUpSD", PLAYER1ST},  {0x06A15F, "FSTGrassJumpUpSD", PLAYER1ST},
    {0x071AE3, "FSTWoodJumpUpSD", PLAYER1ST},   {0x06A162, "FSTStoneSolidJumpDownSD", PLAYER1ST},
    /* manger / boire */
    {0x0CAF94, "ITMFoodEat", PLAYER1ST}, {0x0B6435, "ITMPotionUse", PLAYER1ST},
    {0x106614, "ITMPoisonUse", PLAYER1ST}, {0x10E2EA, "NPCHumanEatSoup", PLAYER1ST},
    /* ramasser / poser des objets */
    {0x03E877, "ITMArrowsDownSD", PLAYER1ST}, {0x03E7B7, "ITMArrowsUpSD", PLAYER1ST},
    {0x07EA3B, "ITMBookCloseSD", PLAYER1ST}, {0x07EA3A, "ITMBookOpenSD", PLAYER1ST},
    {0x03E909, "ITMClothingDownSD", PLAYER1ST}, {0x03E879, "ITMClothingUpSD", PLAYER1ST},
    {0x0899AE, "ITMCoinPouchDown", PLAYER1ST}, {0x0899AD, "ITMCoinPouchUp", PLAYER1ST},
    {0x03E60B, "ITMGenericArmorDownSD", PLAYER1ST}, {0x03E609, "ITMGenericArmorUpSD", PLAYER1ST},
    {0x03EE0C, "ITMGenericBookDownSD", PLAYER1ST}, {0x03EDDE, "ITMGenericBookUpSD", PLAYER1ST},
    {0x03E62C, "ITMGenericClothingDownSD", PLAYER1ST}, {0x03E633, "ITMGenericClothingUpSD", PLAYER1ST},
    {0x03C7B9, "ITMGenericDownSD", PLAYER1ST}, {0x000DD0, "ITMGenericUpLoFi", PLAYER1ST}, {0x03C7BA, "ITMGenericUpSD", PLAYER1ST},
    {0x03C7C0, "ITMGenericWeaponDownSD", PLAYER1ST}, {0x03C7BE, "ITMGenericWeaponUpSD", PLAYER1ST},
    {0x03E955, "ITMGoldDownSD", PLAYER1ST}, {0x03E952, "ITMGoldUpSD", PLAYER1ST},
    {0x03ED78, "ITMKeyDownSD", PLAYER1ST}, {0x03ED75, "ITMKeyUpSD", PLAYER1ST},
    {0x03EDC0, "ITMPotionDownSD", PLAYER1ST}, {0x03EDBD, "ITMPotionUpSD", PLAYER1ST},
    /* récolte, ingrédients, mots (v1.1.0) */
    {0x00517C9, "ITMIngredientBushUp", UIDEFAULT}, {0x00517C8, "ITMIngredientBushDown", UIDEFAULT},
    {0x00519D5, "ITMIngredientStemUp", UIDEFAULT}, {0x00519D1, "ITMIngredientStemDown", UIDEFAULT},
    {0x00BAD13, "ITMIngredientMushroomUp", UIDEFAULT}, {0x00FB974, "ITMIngredientMossUp", UIDEFAULT},
    {0x00FB975, "ITMIngredientRootUp", UIDEFAULT}, {0x0100F88, "ITMIngredientGrassUp", UIDEFAULT},
    {0x003CC09, "ITMIngredientDigUp", UIDEFAULT}, {0x004DBAB, "ITMIngredientMeatUp", UIDEFAULT},
    {0x00FC21E, "ITMIngredientHeartUp", UIDEFAULT}, {0x00FC21F, "ITMIngredientBowlUp", UIDEFAULT},
    {0x00BFB1F, "ITMIngredientEggWetUp", UIDEFAULT}, {0x00F3661, "ITMIngredientGarlicUp", UIDEFAULT},
    {0x0100526, "ITMIngredientClamUp", UIDEFAULT}, {0x0100527, "ITMIngredientBuzzUp", UIDEFAULT},
    {0x010FD50, "ITMIngredientMammothCheeseUp", UIDEFAULT}, {0x010FD51, "ITMIngredientBeehiveHuskUp", UIDEFAULT},
    {0x200EF9F, "ITMIngredientSoulHuskUp", UIDEFAULT},    /* Dawnguard */
    {0x401DBED, "ITMIngredientScathecraw", UIDEFAULT},    /* Dragonborn */
    {0x401DBEF, "ITMIngredientTramaRoot", UIDEFAULT},     /* Dragonborn */
    {0x003C7C2, "ITMGenericIngredientUpSD", UIDEFAULT}, {0x003C7C3, "ITMGenericIngredientDownSD", UIDEFAULT},
    {0x003C7C4, "ITMGenericIngredientUpFailSD", UIDEFAULT}, {0x0000F73, "ITMGenericIngredientUpLoFi", UIDEFAULT},
    {0x00C7A54, "ITMNoteUp", UIDEFAULT}, {0x010F21C, "ITMFishUp", UIDEFAULT},
};
#define AE_ALLFORMS_ID 400507

static void apply(void) {
    uint64_t off = addrlib_offset(AE_ALLFORMS_ID);
    if (!off) return;
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    uint8_t **pmap = (uint8_t **)(base + off);
    if (!readable(pmap, 8) || !readable(*pmap, 0x30)) { LOG("Table des forms illisible"); return; }
    g_map = *pmap;
    uint8_t *p1 = lookup(PLAYER1ST), *pu = lookup(UIDEFAULT), *p3 = lookup(PLAYER3RD);
    LOG("SOMMono01400Player1st=%p  SOMUIDefault=%p  SOMMono01400Player3rd=%p", p1, pu, p3);
    if (!p1 || !pu || !p3) { LOG("Modèles de sortie introuvables, abandon"); return; }
    int fixed = 0;
    for (size_t i = 0; i < sizeof TARGETS / sizeof *TARGETS; i++) {
        uint8_t *form = lookup(TARGETS[i].id);
        if (!form) { LOG("  %-30s introuvable", TARGETS[i].name); continue; }
        uint8_t *def = *(uint8_t **)(form + 0x28);
        if (!readable(def, 0x58)) { LOG("  %-30s descripteur illisible", TARGETS[i].name); continue; }
        uint8_t **om = (uint8_t **)(def + 0x48);
        uint8_t *expected = TARGETS[i].from == UIDEFAULT ? pu : p1;
        if (*om == expected) { *om = p3; fixed++; LOG("  %-30s corrigé", TARGETS[i].name); }
        else LOG("  %-30s ignoré (modèle %p, déjà modifié par un autre mod ?)", TARGETS[i].name, *om);
    }
    LOG("%d son(s) corrigé(s)", fixed);
}

static void on_message(SKSEMessage *msg) {
    if (msg && msg->type == 8 /* kMessage_DataLoaded */) apply();
}

EXPORT int SKSEPlugin_Load(const SKSEInterface *skse) {
    g_log = fopen("Data\\SKSE\\Plugins\\RemoteSoundFix.log", "w");
    g_runtime = skse->runtimeVersion;
    g_handle = skse->GetPluginHandle();
    LOG("RemoteSoundFix 1.1.0 - runtime %u.%u.%u", g_runtime >> 24, (g_runtime >> 16) & 0xFF, (g_runtime >> 4) & 0xFFF);
    SKSEMessagingInterface *m = (SKSEMessagingInterface *)skse->QueryInterface(5);
    if (!m || !m->RegisterListener(g_handle, "SKSE", on_message)) { LOG("Messaging SKSE indisponible"); return 0; }
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) { (void)h; (void)r; (void)p; return TRUE; }
