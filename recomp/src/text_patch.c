/*
 * Switch wording for the game's English text.
 *
 * NFSU2 keeps its text in a language chunk (id 0x00039000) inside the
 * ZZDATA pack files: header {0x10, count, table offset, pool offset}, then
 * a table of {label hash, pool offset} sorted by hash and a packed pool of
 * NUL-terminated strings. Button *icons* ($JOY_EVENT_...$ tokens) are
 * textures; this only changes words: "START" -> "+", the replay screen's
 * "Black" -> "R" (xinput_nx.c: Start = +, Black = R), and Xbox names
 * (Xbox Live, hard disk, Dashboard, Gamertag, Thumbstick) -> neutral ones.
 *
 * The chunk is patched as NtReadFile delivers it (xbox_file_read_hook):
 * every string is rewritten into a new pool, which must not grow, and the
 * table offsets follow. The Switch wording goes into the English table
 * only (recognised by one known string); NFSU2_SWITCH_TEXT=0 turns it off,
 * =1 turns it on outside the Switch build. s_always goes into every
 * language, on every build: the texts of the port's own options
 * (recomp_manual.c), which take over PC-only strings the Xbox never shows.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* kernel_file.c */
extern void (*xbox_file_read_hook)(void *buf, size_t len, int64_t offset);

#define LANG_CHUNK_ID   0x00039000u
#define LANG_ALIGN      0x800u      /* chunks start on pack sectors */
#define HASH_ENGLISH    0x45A6A9CEu /* TITLE_SCREEN_START_PROMPT_XBOX */
#define TEXT_ENGLISH    "Please press START to begin"

/* PS5: the options are in the port menu, the title's own strings stay. */
#ifdef NFSU2_PS5
#define TITLE_ROWS 0
#else
#define TITLE_ROWS 1
#endif

/* Whole strings, by label hash, for the Options rows (not on PS5). The pool
 * must not grow, so these are no longer than what they replace. */
static const struct { uint32_t hash; const char *text; } s_always[] = {
    /* Options -> Video rows */
    { 0x8FE9288Eu, "Car Reflections" },          /* "Car Reflection Detail" */
    { 0x4AC50BCFu, "Resolution Scale" },         /* "Car Geometry Detail" */
    { 0x2A2A4AB3u, "1x" },                       /* "Road Reflection Detail" */
    { 0x822E4E9Bu, "1.5x" },                     /* "Motion Blur" */
    { 0xB55E7665u, "2x" },                       /* "Level Of Detail" */
    { 0xD3588630u, "2.5x" },                     /* "Widescreen" */
    { 0x3EB318CEu, "3x" },                       /* "Shift Up" */
    { 0x153E7337u, "4.5x" },                     /* "Steering (Left)" */
    { 0x4CD775ACu, "Anti-Aliasing" },            /* "Full Screen Anti-Aliasing" */
    { 0x4E57D355u, "Anisotropic" },              /* "Horizon Fog" */
    { 0xA9C27DFDu, "Square Pixels" },            /* "Depth Of Field" */
    { 0xA7A683ACu, "4x" },                       /* "Car Shadow" */
    { 0x6F2C7417u, "8x" },                       /* "Light Trails" */
    { 0x62925361u, "16x" },                      /* "Light Glow" */
    /* Options -> Gameplay row */
    { 0x5CF08EB7u, "Vibration" },                /* "Keyboard Configuration" */
    { 0x06CC3750u, "10%" },                      /* "Crowds" */
    { 0x612AF8A6u, "20%" },                      /* "High" */
    { 0x632B8DA7u, "30%" },                      /* "Medium" */
    { 0x130D90D1u, "40%" },                      /* "Refresh" */
    { 0xBDE2E94Bu, "50%" },                      /* "World Detail" */
    { 0x2522A4E9u, "60%" },                      /* "Particle System" */
    { 0xF6A08779u, "70%" },                      /* "Game Filters" */
    { 0x4DEAB931u, "80%" },                      /* "Wheel Options" */
    { 0x92423B95u, "90%" },                      /* "Wheel Options" */
    { 0x14D4BD3Bu, "100%" },                     /* "Wheel Configuration" */
};

static int s_switch_words;

/* Whole strings, by label hash. */
static const struct { uint32_t hash; const char *text; } s_by_hash[] = {
#if defined(NFSU2_PS5)
    { 0x45A6A9CEu, "Please press OPTIONS to begin" },  /* TITLE_SCREEN_START_PROMPT_XBOX */
    { 0xEEEB248Eu, "Press OPTIONS" },            /* TITLE_SCREEN_START_PROMPT */
    { 0x1F181F57u, "Press OPTIONS" },            /* TITLE_SCREEN_START_PROMPT_GC */
    { 0xFC6731A6u, "Player One Press OPTIONS" }, /* PLAYER_ONE_PRESS_START */
    { 0x5BC4BC1Eu, "Player Two Press OPTIONS" }, /* PLAYER_TWO_PRESS_START */
    { 0x5A890BDAu, "Player One Press OPTIONS" }, /* PLAYER_ONE_PRESS_START_PS2 */
    { 0xEA171252u, "Player Two Press OPTIONS" }, /* PLAYER_TWO_PRESS_START_PS2 */
    { 0xD461C58Au, "OPTIONS" },                  /* UI_START (replay legend) */
    { 0xC5146FE8u, "R1" },                       /* UI_REPLAY_SELECT_XBOX "Black" */
    { 0x97B3C8CFu, "PS5 storage" },              /* SAVELOAD_TITLE_XBOX */
    { 0x3991DD49u, "PS5 storage" },              /* IO_DEVICE_XBOX_DESCRIPTION */
    { 0xF46A8B02u, "Headset" },                  /* OLX_XBOX_COMMUNICATOR */
    { 0xF20BF2F0u, "Online" },                   /* UI_ONLINE_XBOX_LIVE */
    { 0xF673DAB6u, "Please reconnect your DualSense." },  /* ERROR_UNPLUGGED_CONTROLLER */
    { 0x2A5F78F6u, "Please reconnect your DualSense." },  /* ERROR_UNPLUGGED_CONTROLLER_XBOX */
    { 0x2A5B5F42u, "Please reconnect your DualSense." },  /* ERROR_UNPLUGGED_CONTROLLER_PSX2 */
    { 0x1B99CA46u, "Your console cannot connect to the online service."
                   "  Do you want to start the troubleshooter?" },
    { 0xFD9E06CDu, "Are you sure you want to exit to the home screen"
                   " and create a new online account?" },
#else
    { 0x45A6A9CEu, "Please press + to begin" },  /* TITLE_SCREEN_START_PROMPT_XBOX */
    { 0xEEEB248Eu, "Press +" },                  /* TITLE_SCREEN_START_PROMPT */
    { 0x1F181F57u, "Press +" },                  /* TITLE_SCREEN_START_PROMPT_GC */
    { 0xFC6731A6u, "Player One Press +" },       /* PLAYER_ONE_PRESS_START */
    { 0x5BC4BC1Eu, "Player Two Press +" },       /* PLAYER_TWO_PRESS_START */
    { 0x5A890BDAu, "Player One Press +" },       /* PLAYER_ONE_PRESS_START_PS2 */
    { 0xEA171252u, "Player Two Press +" },       /* PLAYER_TWO_PRESS_START_PS2 */
    { 0xD461C58Au, "+" },                        /* UI_START (replay legend) */
    { 0xC5146FE8u, "R" },                        /* UI_REPLAY_SELECT_XBOX "Black" */
    { 0x3991DD49u, "SD Card" },                  /* IO_DEVICE_XBOX_DESCRIPTION */
    { 0xF46A8B02u, "Headset" },                  /* OLX_XBOX_COMMUNICATOR */
    { 0x1B99CA46u, "Your console cannot connect to the online service."
                   "  Do you want to start the troubleshooter?" },
    { 0xFD9E06CDu, "Are you sure you want to exit to the HOME Menu"
                   " and create a new online account?" },
#endif
};

/* Substrings, first match wins at each position; for every other string. */
static const struct { const char *from, *to; } s_rules[] = {
    { "EA Messenger and Xbox Live Friends List", "EA Messenger Friends List" },
    { "Xbox Live OptiMatch ", "OptiMatch " },
    { "Signing in to Xbox Live", "Signing in online" },
    { "Sign in to Xbox Live", "Sign In Online" },
    { "sign out of Xbox Live", "sign out" },
    { "Sign out of Xbox Live", "Sign out" },
    { "signed out of Xbox Live", "signed out" },
    { "connection to Xbox Live", "connection to the online service" },
    { "from Xbox Live", "from the online service" },
    { "the Xbox Live service", "the online service" },
    { "The Xbox Live service", "The online service" },
    { "Xbox Live message", "online message" },
    { "to Xbox Live until", "online until" },
    { "Xbox Live", "Online" },
    { "your Xbox personal information", "your personal information" },
#if defined(NFSU2_PS5)
    { "Xbox hard disk", "PS5 storage" },
    { "Xbox Dashboard", "home screen" },
#else
    { "Xbox hard disk", "SD card" },
    { "Xbox Dashboard", "HOME Menu" },
#endif
    { "Gamertag", "Username" },
    { "Thumbstick", "Stick" },
};

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* ── Second language ─────────────────────────────────────────────
 *
 * <game>/lang/lang.txt turns it on. Its lines are TAB-separated:
 *   text<TAB>first<TAB>second   a kind and the names of its two choices,
 *   voice<TAB>first<TAB>second  in the encoding of the text tables
 *   ui<TAB>english<TAB>first    the port's option texts (s_always and the
 *                               rows below) while the first text is in force
 * The first choice of a kind is what the data holds. The second is
 * <game>/lang/<kind>.zdir: pairs of 24-byte NFSUNDER\ZDIR.BIN records, the
 * record as the data has it and the one to use instead, applied as the
 * directory is read (lang_zdir). The choice in force is the one saved at
 * boot (nfsu2x_options.txt, recomp_manual.c): it applies after a restart.
 *
 * The rows (Options -> Gameplay, recomp_manual.c) take over more PC-only strings: a label
 * and four values per kind -- each choice's name, and the name with
 * " (after restart)" while it is chosen but not yet in force. */
enum { LANG_TEXT, LANG_VOICE, LANG_KINDS };
#define LANG_PAIR       48u
#define LANG_PAIRS_MAX  256u
#define LANG_UI_MAX     64
#define ZDIR_SCAN_END   0x10000     /* the directory is under 32 KiB */

static const char *const s_lang_kind[LANG_KINDS] = { "text", "voice" };
static const char *const s_lang_label_en[LANG_KINDS] = { "Text Language", "Voice Language" };
static const uint32_t s_lang_label[LANG_KINDS] = {
    0x10600AA4u,                            /* PC_MESSENGER_BLOCK */
    0x9DE6B485u,                            /* PC_NAV_CONFIRM_QUIT */
};
static const uint32_t s_lang_value[LANG_KINDS][4] = {
    { 0x97EFCB16u, 0x2BAB7212u, 0x9E067677u, 0x2A795CD8u },  /* PC_NAV_MESSAGE_MANAGEMENT, _WM_HIDE,
                                                              * _INSTALL_PACKAGE, _DEACTIVATE_GPS */
    { 0x5E0AB884u, 0x5EE1C847u, 0x5F52D4FDu, 0x7EFF16C2u },  /* PC_NAV_INSTALL_DECAL, _PAINT,
                                                              * _VINYL, _PART */
};
static char *s_lang_file;                   /* lang.txt, cut in place */
static const char *s_lang_name[LANG_KINDS][2];
static uint8_t *s_lang_pairs[LANG_KINDS];
static size_t s_lang_npairs[LANG_KINDS];
static int s_lang_active[LANG_KINDS];       /* in force since boot */
static int s_lang_chosen[LANG_KINDS];       /* as last chosen */
static struct { const char *en, *tr; } s_ui[LANG_UI_MAX];
static int s_nui;

int nfsu2_lang_has(int kind)
{
    return kind >= 0 && kind < LANG_KINDS && s_lang_name[kind][0];
}

const char *nfsu2_lang_kind_name(int kind)
{
    return kind >= 0 && kind < LANG_KINDS ? s_lang_kind[kind] : "";
}

/* The choice in force, given once at boot; later ones are only chosen. */
void nfsu2_lang_choose(int kind, int choice, int at_boot)
{
    if (!nfsu2_lang_has(kind))
        return;
    s_lang_chosen[kind] = choice != 0;
    if (at_boot) {
        s_lang_active[kind] = choice != 0;
        printf("[LANG] %s: %s\n", s_lang_kind[kind], s_lang_name[kind][choice != 0]);
    }
}

/* The value text for a choice: its name, marked while it waits for a restart. */
uint32_t nfsu2_lang_value(int kind, int choice)
{
    if (kind < 0 || kind >= LANG_KINDS)
        return 0;
    choice = choice != 0;
    return s_lang_value[kind][choice + (choice != s_lang_active[kind] ? 2 : 0)];
}

/* A choice's name for the port menu: lang.txt has it in the text tables'
 * Windows-1251, the menu draws UTF-8. */
const char *nfsu2_lang_choice_utf8(int kind, int choice, char *buf, unsigned cap)
{
    const unsigned char *p;
    unsigned n = 0;

    if (!cap)
        return "";
    buf[0] = 0;
    if (!nfsu2_lang_has(kind))
        return buf;
    choice = choice != 0;
    for (p = (const unsigned char *)s_lang_name[kind][choice]; *p && n + 4 < cap; p++) {
        unsigned c = *p;
        if (c >= 0xC0)
            c += 0x410 - 0xC0;              /* А..я */
        else if (c == 0xA8)
            c = 0x401;                      /* Ё */
        else if (c == 0xB8)
            c = 0x451;                      /* ё */
        else if (c >= 0x80)
            c = '?';
        if (c < 0x80) {
            buf[n++] = (char)c;
        } else {
            buf[n++] = (char)(0xC0 | c >> 6);
            buf[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    buf[n] = 0;
    if (choice != s_lang_active[kind] && n + 18 < cap)
        memcpy(buf + n, " (after restart)", 17);
    return buf;
}

/* An option text as shown: the first text language's wording, if lang.txt has one. */
static const char *ui_text(const char *en)
{
    int i;
    if (s_lang_active[LANG_TEXT] || !nfsu2_lang_has(LANG_TEXT))
        return en;
    for (i = 0; i < s_nui; i++)
        if (!strcmp(s_ui[i].en, en))
            return s_ui[i].tr;
    return en;
}

/* The language rows' strings, into buf; NULL for other hashes. */
static const char *lang_text(uint32_t hash, char *buf, size_t cap)
{
    int k, j;
    for (k = 0; k < LANG_KINDS; k++) {
        if (!nfsu2_lang_has(k))
            continue;
        if (hash == s_lang_label[k])
            return ui_text(s_lang_label_en[k]);
        for (j = 0; j < 4; j++)
            if (hash == s_lang_value[k][j]) {
                snprintf(buf, cap, "%s%s", s_lang_name[k][j & 1], j >= 2 ? ui_text(" (after restart)") : "");
                return buf;
            }
    }
    return NULL;
}

static char *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (b = malloc((size_t)n + 1)) != NULL) {
        if (fread(b, 1, (size_t)n, f) != (size_t)n) {
            free(b);
            b = NULL;
        } else {
            b[n] = 0;
            *size = (size_t)n;
        }
    }
    fclose(f);
    return b;
}

void nfsu2_lang_init(const char *game_dir)
{
    char path[1024], *line, *save = NULL;
    size_t size;
    int k;

    snprintf(path, sizeof path, "%s/lang/lang.txt", game_dir);
    if (s_lang_file || !(s_lang_file = read_all(path, &size)))
        return;
    for (line = strtok_r(s_lang_file, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        char *a = strchr(line, '\t'), *b = a ? strchr(a + 1, '\t') : NULL;
        if (!b)
            continue;
        *a++ = 0;
        *b++ = 0;
        if (!strcmp(line, "ui")) {
            if (s_nui < LANG_UI_MAX) {
                s_ui[s_nui].en = a;
                s_ui[s_nui++].tr = b;
            }
            continue;
        }
        for (k = 0; k < LANG_KINDS; k++)
            if (!strcmp(line, s_lang_kind[k]) && !s_lang_name[k][0]) {
                uint8_t *pairs;
                snprintf(path, sizeof path, "%s/lang/%s.zdir", game_dir, s_lang_kind[k]);
                pairs = (uint8_t *)read_all(path, &size);
                if (!pairs || !size || size % LANG_PAIR || size / LANG_PAIR > LANG_PAIRS_MAX) {
                    fprintf(stderr, "[LANG] %s: no usable %s, kind left out\n", s_lang_kind[k], path);
                    free(pairs);
                    break;
                }
                s_lang_name[k][0] = a;
                s_lang_name[k][1] = b;
                s_lang_pairs[k] = pairs;
                s_lang_npairs[k] = size / LANG_PAIR;
            }
    }
    for (k = 0; k < LANG_KINDS; k++)
        if (nfsu2_lang_has(k))
            printf("[LANG] %s: %zu directory records for the second choice\n",
                   s_lang_kind[k], s_lang_npairs[k]);
}

/* ZDIR.BIN records of the kinds whose second choice is in force, swapped
 * where the buffer holds them whole (file offsets below ZDIR_SCAN_END). */
static void lang_zdir(uint8_t *b, size_t len, int64_t pos)
{
    size_t i, end, p;
    int k, swapped = 0;

    if (pos < 0 || pos >= ZDIR_SCAN_END || !(s_lang_active[LANG_TEXT] || s_lang_active[LANG_VOICE]))
        return;
    end = (uint64_t)pos + len > ZDIR_SCAN_END ? (size_t)(ZDIR_SCAN_END - pos) : len;
    for (i = (size_t)((24 - pos % 24) % 24); i + 24 <= end; i += 24) {
        uint32_t h = rd32(b + i);
        for (k = 0; k < LANG_KINDS; k++) {
            if (!s_lang_active[k])
                continue;
            for (p = 0; p < s_lang_npairs[k]; p++) {
                const uint8_t *pair = s_lang_pairs[k] + p * LANG_PAIR;
                if (rd32(pair) == h && !memcmp(pair, b + i, 24)) {
                    memcpy(b + i, pair + 24, 24);
                    swapped++;
                    goto next;
                }
            }
        }
    next:;
    }
    if (swapped)
        printf("[LANG] %d directory records swapped\n", swapped);
}

/* src -> dst (cap bytes), NUL-terminated; returns the length or -1. */
static int rewrite(uint32_t hash, const char *src, char *dst, size_t cap, int english)
{
    size_t n = 0, i;
    char lang_buf[160];
    const char *lang = TITLE_ROWS ? lang_text(hash, lang_buf, sizeof lang_buf) : NULL;
    if (lang) { src = lang; goto copy; }
    for (i = 0; TITLE_ROWS && i < sizeof s_always / sizeof s_always[0]; i++)
        if (s_always[i].hash == hash) { src = ui_text(s_always[i].text); goto copy; }
    if (!s_switch_words || !english)
        goto copy;
    for (i = 0; i < sizeof s_by_hash / sizeof s_by_hash[0]; i++)
        if (s_by_hash[i].hash == hash) { src = s_by_hash[i].text; goto copy; }
    while (*src) {
        for (i = 0; i < sizeof s_rules / sizeof s_rules[0]; i++) {
            size_t fl = strlen(s_rules[i].from), tl = strlen(s_rules[i].to);
            if (strncmp(src, s_rules[i].from, fl) == 0) {
                if (n + tl >= cap) return -1;
                memcpy(dst + n, s_rules[i].to, tl);
                n += tl; src += fl;
                break;
            }
        }
        if (i == sizeof s_rules / sizeof s_rules[0]) {
            if (n + 1 >= cap) return -1;
            dst[n++] = *src++;
        }
    }
    dst[n] = 0;
    return (int)n;
copy:
    n = strlen(src);
    if (n >= cap) return -1;
    memcpy(dst, src, n + 1);
    return (int)n;
}

/* One language chunk at c, avail bytes of it in the buffer. */
static void patch_chunk(uint8_t *c, size_t avail)
{
    uint32_t size = rd32(c + 4);
    uint8_t *base = c + 8;
    if (rd32(base) != 0x10) return;
    uint32_t count = rd32(base + 4), tab = rd32(base + 8), pool = rd32(base + 12);
    if (count == 0 || count > 65536 || tab < 0x10 || pool < tab ||
        (uint64_t)tab + 8ull * count > pool || pool >= size)
        return;
    if (avail < 8 + (size_t)size) {
        fprintf(stderr, "[TEXT] language chunk split across reads, not patched\n");
        return;
    }
    uint8_t *t = base + tab;
    char *p = (char *)base + pool;
    size_t plen = size - pool;
    uint32_t k;
    int english = 0;

    for (k = 0; k < count; k++)
        if (rd32(t + 8 * k) == HASH_ENGLISH) {
            uint32_t o = rd32(t + 8 * k + 4);
            english = o < plen && strncmp(p + o, TEXT_ENGLISH, plen - o) == 0;
            break;
        }

    char *out = malloc(plen);
    uint32_t *noff = malloc(count * sizeof *noff);
    if (!out || !noff) { free(out); free(noff); return; }
    size_t used = 0;
    int changed = 0;
    for (k = 0; k < count; k++) {
        uint32_t o = rd32(t + 8 * k + 4);
        if (o >= plen || !memchr(p + o, 0, plen - o)) goto fail;
        int n = rewrite(rd32(t + 8 * k), p + o, out + used, plen - used, english);
        if (n < 0) goto fail;
        if (strcmp(out + used, p + o) != 0) changed++;
        noff[k] = (uint32_t)used;
        used += (size_t)n + 1;
    }
    memcpy(p, out, used);
    memset(p + used, 0, plen - used);
    for (k = 0; k < count; k++)
        wr32(t + 8 * k + 4, noff[k]);
    printf("[TEXT] %s: %d of %u strings changed\n",
           english && s_switch_words ? "console wording" : "option labels", changed, count);
    free(out); free(noff);
    return;
fail:
    fprintf(stderr, "[TEXT] language chunk not patched (pool overflow or bad offset)\n");
    free(out); free(noff);
}

#if defined(NFSU2_PS5)
/* The XDK's save messages are not in the language tables: each language has a
 * small LOCH file of UTF-16 strings, LOCL: id, size, zero, count, offsets from
 * LOCL, strings. The rows are the XDK's; the file only gets shorter. */
static const struct { uint32_t row; const char *text; } s_save_text[] = {
    {  8, "Your PS5 has insufficient space for saved data." },
    {  9, "Loading %1. Please do not close the game." },
    { 11, "Save storage %1 is unavailable. Load failed." },
    { 13, "Your PS5 has insufficient space for saved data." },
    { 14, "Insufficient space in save storage %1." },
    { 16, "Are you sure you want to overwrite %1 in save storage %2?" },
    { 17, "Saving %1. Please do not turn off your PS5." },
    { 18, "Saving to save storage %1. Please do not close the game." },
    { 20, "Save storage %1 is unavailable. Save failed." },
    { 22, "Deleting %1. Please do not turn off your PS5." },
    { 23, "Deleting %1 from save storage %2. Please do not close the game." },
    { 25, "Save storage %1 is unavailable. Delete failed." },
    { 26, "PS5 storage" },
    { 27, "Saved data" },
};
#define LOCL_ROWS 28u

static const char *save_text(uint32_t row)
{
    for (size_t i = 0; i < sizeof s_save_text / sizeof s_save_text[0]; i++)
        if (s_save_text[i].row == row) return s_save_text[i].text;
    return NULL;
}

/* A file of avail bytes at b; only the English one, told by row 26. */
static void patch_save_text(uint8_t *b, size_t avail)
{
    static const char english[] = "Xbox hard";
    uint32_t file = 0, l = 0;
    for (size_t i = 0; i + 4 <= avail && i < 512; i += 4)
        if (!memcmp(b + i, "LOCL", 4)) { l = (uint32_t)i; break; }
    if (!l || avail < l + 16 + 4ull * LOCL_ROWS || rd32(b + l + 12) != LOCL_ROWS) return;
    file = l + rd32(b + l + 4);
    if (file > avail) return;
    uint32_t first = 16 + 4 * LOCL_ROWS, at = rd32(b + l + 16 + 4 * 26);
    if (at + 2 * sizeof english > file - l) return;
    for (size_t i = 0; i + 1 < sizeof english; i++)
        if (b[l + at + 2 * i] != (uint8_t)english[i] || b[l + at + 2 * i + 1]) return;

    uint8_t *out = calloc(1, file - l);
    if (!out) return;
    uint32_t pos = first;
    for (uint32_t r = 0; r < LOCL_ROWS; r++) {
        uint32_t a = rd32(b + l + 16 + 4 * r);
        uint32_t e = r + 1 < LOCL_ROWS ? rd32(b + l + 16 + 4 * (r + 1)) : file - l;
        const char *text = save_text(r);
        uint32_t chars = text ? (uint32_t)strlen(text) : (e - a) / 2;
        if (a > e || e > file - l) { free(out); return; }
        if (!text)
            while (chars && !b[l + a + 2 * chars - 2] && !b[l + a + 2 * chars - 1]) chars--;
        if (pos + 2 * chars + 4 > file - l) { free(out); return; }
        wr32(out + 16 + 4 * r, pos);
        if (text)
            for (uint32_t i = 0; i < chars; i++) out[pos + 2 * i] = (uint8_t)text[i];
        else
            memcpy(out + pos, b + l + a, 2 * chars);
        pos += 2 * chars + 4;
    }
    memcpy(out, b + l, 16);
    memcpy(b + l, out, file - l);
    free(out);
    printf("[TEXT] save messages: %zu rows changed\n", sizeof s_save_text / sizeof s_save_text[0]);
}
#endif

static void on_read(void *buf, size_t len, int64_t offset)
{
    uint8_t *b = buf;
    size_t i = 0;
    lang_zdir(b, len, offset);
    if (len < 32) return;
    if (offset >= 0 && offset % LANG_ALIGN)
        i = LANG_ALIGN - (size_t)(offset % LANG_ALIGN);
    for (; i + 32 <= len; i += LANG_ALIGN)
        if (rd32(b + i) == LANG_CHUNK_ID)
            patch_chunk(b + i, len - i);
#if defined(NFSU2_PS5)
        else if (s_switch_words && !memcmp(b + i, "LOCH", 4))
            patch_save_text(b + i, len - i);
#endif
}

void nfsu2_text_patch_init(void)
{
    const char *e = getenv("NFSU2_SWITCH_TEXT");
#if defined(__SWITCH__) || defined(NFSU2_PS5)
    int on = !(e && e[0] == '0');
#else
    int on = e && e[0] == '1';
#endif
    s_switch_words = on;
    xbox_file_read_hook = on_read;
}
