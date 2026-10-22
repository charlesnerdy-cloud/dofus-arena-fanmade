/* ============================================================================
 * DOFUS ARENA FANMADE - P2P 1v1/2v2/5v5 en C / SDL2
 * ---------------------------------------------------------------------------
 * - 13 classes jouables, 16 sorts chacune.
 * - Phase de DRAFT : chaque joueur choisit 4 sorts parmi les 16 de sa classe
 *   avant chaque partie. La sélection est synchronisée en P2P.
 * - Combat tactique au tour par tour sur grille (style Dofus Arena).
 * - Réseau Peer-To-Peer en UDP : host + join, port 7777.
 * - Assets : atlas de personnages, sorts, UI, effets et tuiles dans ./assets/.
 *
 * Compilation :
 *   make
 *
 * Lancement :
 *   ./dofus-arena host              (héberger, port 7777)
 *   ./dofus-arena join 192.168.x.x  (rejoindre)
 *
 * Contrôles : interface a la souris ; saisie clavier pour IP et chat.
 *   - clic gauche = sélectionner / valider / cibler
 * Déroulement : MENU -> PICK (classe) -> DRAFT (4 sorts) -> STUFF
 *               (panoplie) -> BATTLE
  *
  * ==========================================================================*/

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
#endif

#ifdef __ANDROID__
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <SDL_system.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "tools/bot_model.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
typedef SOCKET Sock;
#define SOCK_INVALID INVALID_SOCKET
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
typedef int Sock;
#define SOCK_INVALID (-1)
#endif

/* ------------------------------------------------------------------ */
/* Constantes                                                          */
/* ------------------------------------------------------------------ */
#if !SDL_VERSION_ATLEAST(2, 0, 18)
#error "SDL 2.0.18 ou superieur requis (SDL_RenderGeometry, rendu isometrique)"
#endif

#define GRID_W 14
#define GRID_H 10
#define TILE   48
/* rendu isometrique 2.5D : tuiles en losange, murs extrudes           */
#define ISO_TILE_W 56
#define ISO_TILE_H 28
#define ISO_WALL_H 20
#define ISO_EDGE_H 14
#define MAP_THEME_COUNT 8
#define CHARACTER_FRAME_W 16
#define CHARACTER_FRAME_H 16
#define CHARACTER_ROW_COUNT 12
#define CHARACTER_SEQUENCE_COUNT 5
#define CHARACTER_SEQUENCE_FRAME_COUNT 4
#define SPELL_ICON_COLS 9
#define SPELL_ICON_ROWS 8
#define EMOTE_COLS 16
#define EMOTE_ROWS 16
#define EMOTE_CELL_W 12
#define EMOTE_CELL_H 13
#define VFX_FRAME_COUNT 30
#define VFX_FRAME_MS 33
#define VFX_REDUCED_DURATION_MS 1200
#define WIN_W  1024
#define WIN_H  480
#define BAND_H 160                     /* bandeau UI (haut, mode paysage) */
#define PORT   7777
#define NET_KEEPALIVE_MS 1000
#define NET_TIMEOUT_MS   8000
#define NET_CONNECT_TIMEOUT_MS 10000
#define N_CLASSES 13
#define N_PLAYABLE 13                   /* toutes les classes jouables  */
#define CLASS_MONSTER N_CLASSES         /* pseudo-classe des monstres   */
#define N_CLASS_SPELLS 16
#define N_EQUIPPED  4
#define N_PANOPLIES 24
#define N_DOFUS 6
#define MAX_TEAM_SIZE 5
#define MAX_FIGHTERS (MAX_TEAM_SIZE * 2)
#define MAX_SUMMONS 32
#define MAX_SUMMONS_PER_OWNER 3
#define N_SUMMON_TEMPLATES 10
#define N_MONSTER_TEMPLATES 10
#define N_CREATURES 20
#define SUMMON_FRAME_COUNT 10
#define MONSTER_FRAME_COUNT 10
#define DAILY_CHALLENGES 3
#define MAX_FAVORITES 5
#define ELEMENT_COUNT 4
#define ELEMENT_POINTS 100
#define MAX_HP 100

/* types de cases de l'arene                                           */
#define CELL_GROUND 0   /* sol unique (praticable)                     */
#define CELL_WALL   1   /* MUR    : bloque le deplacement ET la vue     */
#define CELL_GAP    2   /* FAUSSE : bloque le deplacement, PAS la vue   */
#define CELL_VOID   3   /* hors de la forme de l'arene                  */

#define ANIM_FRAME_MS 150              /* durée d'une frame             */
#define ANIM_ATK_MS   600              /* durée totale de l'anim attaque */
#define ANIM_FRAMES_PER_CLASS 4

/* ------------------------------------------------------------------ */
/* Assets                                                              */
/* ------------------------------------------------------------------ */
#define UI_HP_BAR_X 5
#define UI_HP_BAR_Y 73
#define UI_HP_BAR_W 91
#define UI_HP_BAR_H 19
#define UI_PANEL_X 0
#define UI_PANEL_Y 112
#define UI_PANEL_W 94
#define UI_PANEL_H 80
#define UI_PANEL_BORDER 8
#define UI_OK_X 744
#define UI_OK_Y 459
#define UI_OK_W 120
#define UI_OK_H 54
#define MAX_MANIFEST_FRAMES 1024
#define MANIFEST_FRAME_NAME_MAX 96

static SDL_Texture *g_ui_atlas;
static int g_ui_atlas_width;
static int g_ui_atlas_height;
static SDL_Texture *g_tilesheet;
static SDL_Texture *g_characters_sheet;
static SDL_Texture *g_spell_icons;
static SDL_Texture *g_emote_sheet;
static SDL_Texture *g_creatures_sheet;
static SDL_Rect g_creature_frames[N_CREATURES];
static SDL_Window *g_window;            /* fenetre principale (plein ecran) */

static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *txt,
                      int x, int y, SDL_Color c);
static SDL_Texture *load_asset(SDL_Renderer *r, const char *path);
static void pref_erase(const char *name);
#define PIN_MAX_TRIES 5
static void draw_tinted(SDL_Renderer *r, SDL_Texture *t, SDL_Rect dst,
                        int cr, int cg, int cb);

/* --- nouvelle interface : panneaux et boutons 9-slice ---------------- */
typedef enum {
    UI9_PANEL_TAN, UI9_PANEL_BROWN, UI9_PANEL_GREY, UI9_PANEL_WHITE,
    UI9_BTN_YELLOW, UI9_BTN_GREEN, UI9_BTN_GREY, UI9_BTN_RED, UI9_COUNT
} Ui9Id;

static const char *UI9_FILES[UI9_COUNT] = {
    "assets/UI/pixel/panel_tan.png",
    "assets/UI/pixel/panel_brown.png",
    "assets/UI/pixel/panel_grey.png",
    "assets/UI/pixel/panel_white.png",
    "assets/UI/pixel/btn_yellow.png",
    "assets/UI/pixel/btn_green.png",
    "assets/UI/pixel/btn_grey.png",
    "assets/UI/pixel/btn_red.png",
};
static SDL_Texture *g_ui9[UI9_COUNT];

/* --- Pack Humble Pixel "Paper UI System" (CC0, licence jointe) ------- */
static SDL_Texture *g_hp_panel;      /* cadre papier du panneau d'etat  */
static SDL_Texture *g_hp_bar;        /* segment de barre de vie         */
static SDL_Texture *g_hp_bar_frame;  /* fond de la barre                */
static SDL_Texture *g_hp_mp;         /* segment de ressource             */
static SDL_Texture *g_hp_ribbon;     /* ruban de titre                   */
static SDL_Texture *g_hp_sheet;      /* grande feuille de parchemin      */
static SDL_Texture *g_hp_slot;       /* emplacement (carre 32x32)        */
static SDL_Texture *g_hp_btn;        /* bouton papier (16x16)            */
static SDL_Texture *g_hp_knob;       /* bouton rond                      */

static void load_humble_ui(SDL_Renderer *renderer)
{
    g_hp_panel     = load_asset(renderer, "assets/UI/humble/hud_panel.png");
    g_hp_bar       = load_asset(renderer, "assets/UI/humble/bar_hp.png");
    g_hp_bar_frame = load_asset(renderer, "assets/UI/humble/bar_hp_frame.png");
    g_hp_mp        = load_asset(renderer, "assets/UI/humble/bar_mp.png");
    g_hp_ribbon    = load_asset(renderer, "assets/UI/humble/header_ribbon.png");
    g_hp_sheet     = load_asset(renderer, "assets/UI/humble/paper_sheet.png");
    g_hp_slot      = load_asset(renderer, "assets/UI/humble/slot.png");
    g_hp_btn       = load_asset(renderer, "assets/UI/humble/btn_paper.png");
    g_hp_knob      = load_asset(renderer, "assets/UI/humble/knob.png");
}

/* dessine une texture teintee (le papier creme devient sombre)         */
static void draw_tinted(SDL_Renderer *r, SDL_Texture *t, SDL_Rect dst,
                        int cr, int cg, int cb)
{
    if (!t) return;
    SDL_SetTextureColorMod(t, cr, cg, cb);
    SDL_RenderCopy(r, t, NULL, &dst);
    SDL_SetTextureColorMod(t, 255, 255, 255);
}

/* barre segmentee : le remplissage est repete jusqu'a la largeur voulue */
static void draw_seg_bar(SDL_Renderer *r, SDL_Texture *fill,
                         SDL_Texture *frame, SDL_Rect dst, int pct)
{
    int i, fw;
    if (frame) SDL_RenderCopy(r, frame, NULL, &dst);
    if (!fill || pct <= 0) return;
    fw = (dst.w * pct) / 100;
    if (fw > dst.w) fw = dst.w;
    for (i = 0; i * 16 < fw; i++) {
        SDL_Rect src = {0, 0, 16, 16};
        SDL_Rect d = {dst.x + i * 16, dst.y, 16, dst.h};
        if (d.x + 16 > dst.x + fw) {
            src.w = dst.x + fw - d.x;
            d.w = src.w;
        }
        if (src.w <= 0) break;
        SDL_RenderCopy(r, fill, &src, &d);
    }
}

/* --- HUD importe (optionnel) : assets/UI/hud.png + hud.frames.json ---- */
static SDL_Texture *g_hud_sheet;
typedef struct { int x, y, w, h; int ok; } HudFrame;
static HudFrame g_hud_frame[8];
static const char *HUD_NAMES[8] = {
    "portrait_frame", "hp_bar", "hp_bar_fill", "pa",
    "pm", "mana_bar", "mana_bar_fill", "slot"
};
enum { HUD_PORTRAIT, HUD_HPBAR, HUD_HPFILL, HUD_PA, HUD_PM,
       HUD_MANABAR, HUD_MANAFILL, HUD_SLOT };

/* charge le HUD importe ; facultatif (le jeu marche sans)              */
static void load_hud_sheet(SDL_Renderer *renderer)
{
    SDL_Surface *surface;
    FILE *file;
    char line[512];
    int i;
    for (i = 0; i < 8; i++) g_hud_frame[i].ok = 0;
    surface = IMG_Load("assets/UI/hud.png");
    if (!surface) return;
    file = fopen("assets/UI/hud.frames.json", "r");
    if (!file) { SDL_FreeSurface(surface); return; }
    while (fgets(line, sizeof line, file)) {
        char name[64];
        int x, y, w, h, k;
        if (sscanf(line, "\"name\": \"%63[^\"]\"", name) != 1) continue;
        if (sscanf(line, "\"x\": %d", &x) != 1) continue;
        if (sscanf(line, "\"y\": %d", &y) != 1) continue;
        if (sscanf(line, "\"w\": %d", &w) != 1) continue;
        if (sscanf(line, "\"h\": %d", &h) != 1) continue;
        for (k = 0; k < 8; k++)
            if (!strcmp(name, HUD_NAMES[k]) && !g_hud_frame[k].ok) {
                g_hud_frame[k].x = x; g_hud_frame[k].y = y;
                g_hud_frame[k].w = w; g_hud_frame[k].h = h;
                g_hud_frame[k].ok = 1;
                break;
            }
    }
    fclose(file);
    g_hud_sheet = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
}

/* dessine une frame du HUD importe a la position demandee              */
static void draw_hud_frame(SDL_Renderer *r, int id, SDL_Rect dst, int tint)
{
    HudFrame *f;
    SDL_Rect src;
    if (!g_hud_sheet || id < 0 || id >= 8) return;
    f = &g_hud_frame[id];
    if (!f->ok) return;
    src.x = f->x; src.y = f->y; src.w = f->w; src.h = f->h;
    if (tint == 1) SDL_SetTextureColorMod(g_hud_sheet, 255, 90, 80);
    else if (tint == 2) SDL_SetTextureColorMod(g_hud_sheet, 120, 220, 255);
    else SDL_SetTextureColorMod(g_hud_sheet, 255, 255, 255);
    SDL_RenderCopy(r, g_hud_sheet, &src, &dst);
    SDL_SetTextureColorMod(g_hud_sheet, 255, 255, 255);
}

/* dessine un panneau 9-slice teinte (le papier creme est assombri)   */
static void draw_nine_slice_tinted(SDL_Renderer *r, SDL_Texture *t,
                                   SDL_Rect dst, int b,
                                   int cr, int cg, int cb)
{
    int tw, th, i;
    SDL_Rect sc[9], dc[9];
    if (!t) return;
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    if (b <= 0 || b * 2 >= tw || b * 2 >= th || dst.w < b * 2
        || dst.h < b * 2) {
        draw_tinted(r, t, dst, cr, cg, cb);
        return;
    }
    sc[0].x = 0;         sc[0].y = 0;         sc[0].w = b; sc[0].h = b;
    sc[1].x = b;         sc[1].y = 0;         sc[1].w = tw - 2*b; sc[1].h = b;
    sc[2].x = tw - b;    sc[2].y = 0;         sc[2].w = b; sc[2].h = b;
    sc[3].x = 0;         sc[3].y = b;         sc[3].w = b; sc[3].h = th - 2*b;
    sc[4].x = b;         sc[4].y = b;         sc[4].w = tw - 2*b; sc[4].h = th - 2*b;
    sc[5].x = tw - b;    sc[5].y = b;         sc[5].w = b; sc[5].h = th - 2*b;
    sc[6].x = 0;         sc[6].y = th - b;    sc[6].w = b; sc[6].h = b;
    sc[7].x = b;         sc[7].y = th - b;    sc[7].w = tw - 2*b; sc[7].h = b;
    sc[8].x = tw - b;    sc[8].y = th - b;    sc[8].w = b; sc[8].h = b;
    dc[0].x = dst.x;               dc[0].y = dst.y;               dc[0].w = b; dc[0].h = b;
    dc[1].x = dst.x + b;           dc[1].y = dst.y;               dc[1].w = dst.w - 2*b; dc[1].h = b;
    dc[2].x = dst.x + dst.w - b;   dc[2].y = dst.y;               dc[2].w = b; dc[2].h = b;
    dc[3].x = dst.x;               dc[3].y = dst.y + b;           dc[3].w = b; dc[3].h = dst.h - 2*b;
    dc[4].x = dst.x + b;           dc[4].y = dst.y + b;           dc[4].w = dst.w - 2*b; dc[4].h = dst.h - 2*b;
    dc[5].x = dst.x + dst.w - b;   dc[5].y = dst.y + b;           dc[5].w = b; dc[5].h = dst.h - 2*b;
    dc[6].x = dst.x;               dc[6].y = dst.y + dst.h - b;   dc[6].w = b; dc[6].h = b;
    dc[7].x = dst.x + b;           dc[7].y = dst.y + dst.h - b;   dc[7].w = dst.w - 2*b; dc[7].h = b;
    dc[8].x = dst.x + dst.w - b;   dc[8].y = dst.y + dst.h - b;   dc[8].w = b; dc[8].h = b;
    SDL_SetTextureColorMod(t, cr, cg, cb);
    for (i = 0; i < 9; i++)
        SDL_RenderCopy(r, t, &sc[i], &dc[i]);
    SDL_SetTextureColorMod(t, 255, 255, 255);
}

/* dessine un panneau 9-slice (bord de 16 px) etrille a toute taille   */
static void draw_nine_slice(SDL_Renderer *r, SDL_Texture *t, SDL_Rect dst, int b)
{
    int tw, th, i;
    SDL_Rect sc[9], dc[9];
    if (!t) return;
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    if (b <= 0 || b * 2 >= tw || b * 2 >= th || dst.w < b * 2
        || dst.h < b * 2) {
        SDL_RenderCopy(r, t, NULL, &dst);
        return;
    }
    /* 0 coin haut-gauche  1 haut  2 coin haut-droit
       3 gauche            4 centre 5 droite
       6 coin bas-gauche   7 bas    8 coin bas-droit                */
    sc[0].x = 0;         sc[0].y = 0;         sc[0].w = b; sc[0].h = b;
    sc[1].x = b;         sc[1].y = 0;         sc[1].w = tw - 2*b; sc[1].h = b;
    sc[2].x = tw - b;    sc[2].y = 0;         sc[2].w = b; sc[2].h = b;
    sc[3].x = 0;         sc[3].y = b;         sc[3].w = b; sc[3].h = th - 2*b;
    sc[4].x = b;         sc[4].y = b;         sc[4].w = tw - 2*b; sc[4].h = th - 2*b;
    sc[5].x = tw - b;    sc[5].y = b;         sc[5].w = b; sc[5].h = th - 2*b;
    sc[6].x = 0;         sc[6].y = th - b;    sc[6].w = b; sc[6].h = b;
    sc[7].x = b;         sc[7].y = th - b;    sc[7].w = tw - 2*b; sc[7].h = b;
    sc[8].x = tw - b;    sc[8].y = th - b;    sc[8].w = b; sc[8].h = b;
    dc[0].x = dst.x;               dc[0].y = dst.y;               dc[0].w = b; dc[0].h = b;
    dc[1].x = dst.x + b;           dc[1].y = dst.y;               dc[1].w = dst.w - 2*b; dc[1].h = b;
    dc[2].x = dst.x + dst.w - b;   dc[2].y = dst.y;               dc[2].w = b; dc[2].h = b;
    dc[3].x = dst.x;               dc[3].y = dst.y + b;           dc[3].w = b; dc[3].h = dst.h - 2*b;
    dc[4].x = dst.x + b;           dc[4].y = dst.y + b;           dc[4].w = dst.w - 2*b; dc[4].h = dst.h - 2*b;
    dc[5].x = dst.x + dst.w - b;   dc[5].y = dst.y + b;           dc[5].w = b; dc[5].h = dst.h - 2*b;
    dc[6].x = dst.x;               dc[6].y = dst.y + dst.h - b;   dc[6].w = b; dc[6].h = b;
    dc[7].x = dst.x + b;           dc[7].y = dst.y + dst.h - b;   dc[7].w = dst.w - 2*b; dc[7].h = b;
    dc[8].x = dst.x + dst.w - b;   dc[8].y = dst.y + dst.h - b;   dc[8].w = b; dc[8].h = b;
    for (i = 0; i < 9; i++)
        SDL_RenderCopy(r, t, &sc[i], &dc[i]);
}

/* bouton 9-slice + libelle centre                                    */
static void ui9_button(SDL_Renderer *r, SDL_Rect b, const char *label,
                        TTF_Font *font, int hovered, int selected, int enabled)
{
    SDL_Texture *t = g_ui9[UI9_BTN_GREY];
    SDL_Color col = {190, 185, 175, 255};
    if (!enabled) { t = g_ui9[UI9_BTN_GREY]; col = (SDL_Color){140,136,132,255}; }
    else if (selected) { t = g_ui9[UI9_BTN_YELLOW]; col = (SDL_Color){40,30,10,255}; }
    else if (hovered) { t = g_ui9[UI9_BTN_YELLOW]; col = (SDL_Color){60,45,15,255}; }
    else { t = g_ui9[UI9_BTN_GREEN]; col = (SDL_Color){245,245,235,255}; }
    draw_nine_slice(r, t, b, 16);
    if (label)
        draw_text(r, font, label, b.x + 8, b.y + (b.h - 16) / 2, col);
}

/* Cadre utile d'une frame : rectangle des pixels opaques.             */
typedef struct {
    int x0, y0, x1, y1;
} ContentBox;

static ContentBox g_char_boxes[CHARACTER_ROW_COUNT]
                              [CHARACTER_SEQUENCE_COUNT]
                              [CHARACTER_SEQUENCE_FRAME_COUNT];
static ContentBox g_creature_boxes[N_CREATURES];
static SDL_Texture *g_vfx_frames[VFX_FRAME_COUNT];
static SDL_Renderer *g_ren;      /* pour la conversion souris -> logique */

/* Fenetre redimensionnable : le rendu reste en 672x640 logiques,
 * mis a l'echelle automatiquement par SDL (bandes noires si besoin).
 * Les evenements souris arrivent en pixels fenetre : on convertit.      */
static void point_logical(int wx, int wy, int *lx, int *ly)
{
    float sx = 1.0f, sy = 1.0f;
    SDL_Rect vp = {0, 0, WIN_W, WIN_H};
    if (g_ren) {
        SDL_RenderGetScale(g_ren, &sx, &sy);
        SDL_RenderGetViewport(g_ren, &vp);
        if (sx <= 0.0f) sx = 1.0f;
        if (sy <= 0.0f) sy = 1.0f;
    }
    *lx = (int)((wx - vp.x) / sx);
    *ly = (int)((wy - vp.y) / sy);
}

static void mouse_logical(int *lx, int *ly)
{
    int wx, wy;
    SDL_GetMouseState(&wx, &wy);
    point_logical(wx, wy, lx, ly);
}

typedef struct {
    SDL_Rect source;
    char name[MANIFEST_FRAME_NAME_MAX];
} SpriteManifestFrame;

static SpriteManifestFrame g_ui_frames[MAX_MANIFEST_FRAMES];
static int g_ui_frame_count;
static SpriteManifestFrame g_character_frames[MAX_MANIFEST_FRAMES];
static int g_character_frame_count;

typedef enum {
    SFX_MENU_SELECT,
    SFX_MENU_CANCEL,
    SFX_CAST,
    SFX_HIT,
    SFX_DEATH,
    SFX_MOVE,
    SFX_VICTORY,
    SFX_GAME_OVER,
    SFX_PACIFY,
    SFX_ENCOURAGE,
    SFX_COUNT
} SoundEffect;

static Mix_Music *g_menu_music;
static Mix_Music *g_battle_music[4];
static Mix_Chunk *g_sounds[SFX_COUNT];
static int g_achievements[69];
static int g_audio_ready;
static int g_current_music_track = -1; /* piste de combat en cours       */

/* Horloge haute precision : nanosecondes (QPC sous Windows).          */
static long long music_ns_now(void)
{
#ifndef _WIN32
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
#else
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return now.QuadPart * 1000000000LL / freq.QuadPart;
#endif
}

static const char *BATTLE_MUSIC_PATHS[4] = {
    "assets/BGM/ES_Expiation - Christoffer Moe Ditlevsen.mp3",
    "assets/BGM/ES_Galaxy Verse - Ruiqi Zhao.mp3",
    "assets/BGM/ES_Terra Requiem II - Dian Shuai.mp3",
    "assets/BGM/ES_Thunder Storm 03 - Fredrik Ekstrom.mp3"
};
static const char *SOUND_PATHS[SFX_COUNT] = {
    "assets/Sounds/Menu/Menu_Select.mp3",
    "assets/Sounds/Menu/Menu_Cancel.mp3",
    "assets/Sounds/CastMagic.mp3",
    "assets/Sounds/DamageSurvivor.mp3",
    "assets/Sounds/DeathUndead.mp3",
    "assets/Sounds/Footstep.mp3",
    "assets/Sounds/Victory.mp3",
    "assets/Sounds/GameOver.mp3",
    "assets/Sounds/Balance_Calm.wav",
    "assets/Sounds/Balance_Encourage.wav"
};

static const int CLASS_CHARACTER_ROW[N_CLASSES] = {
    1,   /* Iop */
    10,  /* Cra */
    3,   /* Ecaflip */
    11,  /* Eniripsa */
    7,   /* Enutrof */
    4,   /* Sram */
    0,   /* Sacrieur */
    8,   /* Sadida */
    9,   /* Osamodas */
    6,   /* Xelor */
    5,   /* Feca */
    3,   /* Pandawa */
    4    /* Roublard */
};

static const int CHARACTER_FRAME_X[CHARACTER_SEQUENCE_COUNT]
                                  [CHARACTER_SEQUENCE_FRAME_COUNT] = {
    {16, 30, 44, 58},
    {82, 96, 110, 124},
    {146, 160, 174, 188},
    {212, 228, 244, 260},
    {278, 294, 310, 326}
};

static const int CHARACTER_FRAME_WIDTH[CHARACTER_SEQUENCE_COUNT] = {
    14, 14, 14, 16, 16
};

static const SDL_Rect MAP_FLOOR_RECTS[MAP_THEME_COUNT] = {
    {366,130,24,24}, {58,186,24,24}, {758,214,24,24}, {422,270,24,24},
    {86,326,24,24}, {786,354,24,24}, {450,410,24,24}, {142,466,24,24}
};
static const SDL_Rect MAP_WALL_RECTS[MAP_THEME_COUNT] = {
    {30,158,24,24}, {730,186,24,24}, {422,242,24,24}, {86,298,24,24},
    {758,326,24,24}, {450,382,24,24}, {114,438,24,24}, {814,466,24,24}
};
static const SDL_Rect MAP_VOID_RECTS[MAP_THEME_COUNT] = {
    {814,130,24,24}, {506,186,24,24}, {198,242,24,24}, {870,270,24,24},
    {534,326,24,24}, {226,382,24,24}, {898,410,24,24}, {590,466,24,24}
};

/* ------------------------------------------------------------------ */
/* Sorts                                                               */
/* ------------------------------------------------------------------ */
typedef enum { SK_ATK, SK_HEAL, SK_BUFF, SK_SPECIAL, SK_SUMMON } SkillKind;
typedef enum { ELEM_AIR, ELEM_FIRE, ELEM_EARTH, ELEM_WATER } Element;

typedef struct {
    const char *name;
    SkillKind   kind;
    int         pa;        /* coût en points d'action              */
    int         range;     /* portée en cases (manhattan)          */
    int         power;     /* dégâts / soin / % boost             */
    int         cooldown;  /* tours de rechargement                */
    int         summon;    /* 0 = aucun, sinon template + 1        */
    int         effect;    /* effet special (transformation...)    */
} Skill;

typedef struct {
    const char *name;
    Skill       spells[N_CLASS_SPELLS];
} ClassDef;

/* ------------------------------------------------------------------ */
/* Bestiaire : 0-9 = invocations, 10-19 = monstres du Songe            */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    int   hp;          /* PV de l'invocation                        */
    int   atk;         /* degats par attaque                        */
    int   range;       /* portee d'attaque (cases)                  */
    int   pm;          /* deplacement par tour                     */
    int   frame;       /* index dans la planche creatures.png       */
    int   shape;       /* forme de decoupe papier                   */
    Uint8 r, gr, b;    /* couleur du papier                         */
} SummonTemplate;

static const SummonTemplate SUMMON_TEMPLATES[SUMMON_FRAME_COUNT] = {
    {"Tofu",        45,  8, 1, 3, 0, 0, 238, 228, 198},
    {"Gobball",     70, 12, 1, 2, 1, 1, 152, 196, 118},
    {"Prespic",    115, 16, 1, 2, 2, 2, 122, 172, 214},
    {"Sanglier",    95, 18, 1, 3, 3, 3, 164, 122,  88},
    {"Bwork",      155, 24, 1, 2, 4, 4, 118, 106,  96},
    {"Crackler",   135, 26, 2, 2, 5, 5, 206, 122,  86},
    {"Wyrmling",   185, 30, 3, 3, 6, 6, 176,  84, 220},
    {"Dopeul",     125, 10, 3, 2, 7, 7, 196, 214, 124},
    {"Coffre",      80, 14, 1, 2, 8, 8, 214, 176,  86},
    {"Arbre",      105, 12, 2, 1, 9, 9, 112, 202, 128},
};

/* Monstres du Songe : plus le niveau monte, plus ils sont forts      */
static const SummonTemplate MONSTER_TEMPLATES[MONSTER_FRAME_COUNT] = {
    {"Chauve-souris",   40,  9, 1, 4, 10, 0, 180, 180, 190},
    {"Loup-garou",      70, 15, 1, 4, 11, 1, 160, 160, 170},
    {"Araignee",        60, 13, 1, 3, 12, 2, 200, 110,  70},
    {"Araignee-brune",  90, 18, 1, 3, 13, 3, 150, 110,  80},
    {"Arbre-mort",     110, 16, 2, 2, 14, 4, 130, 100,  70},
    {"Buisson",         95, 14, 2, 3, 15, 5, 110, 170,  90},
    {"Fleur-bleue",     80, 17, 2, 3, 16, 6, 110, 150, 210},
    {"Fleur-de-feu",   100, 21, 2, 3, 17, 7, 220, 130,  70},
    {"Fleur-maudite",  130, 24, 3, 2, 18, 8, 160,  90, 200},
    {"Torche",         120, 22, 2, 4, 19, 9, 230, 150,  60},
};

/* Les 13 classes de Dofus, 16 sorts chacun (grimoire fanmade).        */
static ClassDef CLASSES[N_CLASSES] = {
    {"Iop Cogneur", {
        {"Pression",           SK_ATK,     3, 4, 14, 0},
        {"Ferveur",            SK_BUFF,    2, 0, 18, 4},
        {"Intimidation",       SK_SPECIAL, 3, 3, 0,  3},
        {"Saut",               SK_SPECIAL, 2, 4, 0,  2},
        {"Vitalité",           SK_BUFF,    2, 0, 20, 4},
        {"Puissance",          SK_BUFF,    3, 0, 22, 5},
        {"Endurance",          SK_BUFF,    2, 0, 15, 3},
        {"Vertu",              SK_BUFF,    3, 0, 18, 4},
        {"Epée de Iop",        SK_ATK,     4, 1, 20, 1},
        {"Friction",           SK_ATK,     4, 3, 16, 2},
        {"Epée céleste",       SK_ATK,     5, 4, 26, 3},
        {"Précipitation",      SK_ATK,     4, 2, 20, 2},
        {"Epée du destin",     SK_ATK,     5, 3, 24, 3},
        {"Poigne",             SK_SPECIAL, 3, 4, 0,  3},
        {"Colère",             SK_ATK,     5, 2, 30, 4},
        {"Courroux",           SK_ATK,     4, 5, 22, 3},
    }},
    {"Cra Fléchard", {
        {"Flèche enflammée",   SK_ATK,     3, 7, 16, 0},
        {"Flèche magique",     SK_ATK,     3, 6, 14, 0},
        {"Oeil de taupe",      SK_SPECIAL, 2, 6, 0,  3},
        {"Tir puissant",       SK_BUFF,    2, 0, 18, 4},
        {"Tir éloigné",        SK_BUFF,    2, 0, 14, 3},
        {"Flèche cinglante",   SK_SPECIAL, 3, 5, 0,  3},
        {"Flèche de recul",    SK_SPECIAL, 3, 4, 0,  2},
        {"Flèche explosive",   SK_ATK,     4, 6, 24, 3},
        {"Maîtrise de l'arc",  SK_BUFF,    1, 0, 20, 5},
        {"Flèche dispersion",  SK_SPECIAL, 3, 4, 0,  3},
        {"Flamiche",           SK_ATK,     2, 1, 8,  0},
        {"Flèche vampire",     SK_ATK,     4, 6, 16, 1},
        {"Flèche glaçon",      SK_ATK,     3, 7, 11, 0},
        {"Flèche BBQ",         SK_ATK,     4, 7, 18, 1},
        {"Tir énorme",         SK_ATK,     5, 7, 26, 3},
        {"Recette fuite",      SK_SPECIAL, 2, 3, 0,  2},
    }},
    {"Ecaflip Casino", {
        {"Griffe",             SK_ATK,     3, 1, 15, 0},
        {"Pile ou Face",       SK_SPECIAL, 2, 4, 20, 2},
        {"Roulette",           SK_SPECIAL, 3, 0, 0,  4},
        {"Griffe aiguisée",    SK_ATK,     4, 1, 20, 1},
        {"Bond",               SK_SPECIAL, 2, 4, 0,  2},
        {"Roue de la fortune", SK_SPECIAL, 3, 3, 0,  4},
        {"Griffe sauvage",     SK_ATK,     5, 2, 28, 2},
        {"Topkaj",             SK_ATK,     4, 3, 22, 2},
        {"Odorat",             SK_BUFF,    2, 0, 18, 4},
        {"Contrecoup",         SK_SPECIAL, 3, 4, 8,  2},
        {"Griffe de Ceangal",  SK_ATK,     4, 2, 18, 1},
        {"Bluff",              SK_SPECIAL, 2, 3, 0,  3},
        {"Chatiment",          SK_ATK,     5, 3, 26, 3},
        {"Reflexes",           SK_BUFF,    2, 0, 15, 3},
        {"Griffe joueuse",     SK_ATK,     3, 1, 14, 0},
        {"Destin d'Ecaflip",   SK_SPECIAL, 4, 5, 12, 3},
    }},
    {"Eniripsa Blouse", {
        {"Mot de soin",        SK_HEAL,   3, 5, 20, 0},
        {"Mot de régén",       SK_HEAL,   4, 6, 28, 2},
        {"Mot de préven",      SK_HEAL,   4, 0, 12, 3},
        {"Mot de sacrifice",   SK_HEAL,   3, 7, 16, 2},
        {"Mot de frappe",      SK_ATK,    4, 4, 15, 0},
        {"Mot d'immob",        SK_SPECIAL, 3, 0, 15, 3},
        {"Mot de silence",     SK_SPECIAL, 4, 5, 0,  4},
        {"Mot de stimulation", SK_BUFF,   2, 0, 15, 3},
        {"Mot de défense",     SK_BUFF,   3, 0, 20, 4},
        {"Mot de soin zone",   SK_HEAL,   5, 0, 34, 4},
        {"Mot de soin ciblé",  SK_HEAL,   4, 3, 22, 3},
        {"Mot de buff",        SK_BUFF,   2, 0, 14, 3},
        {"Mot de retrait",     SK_SPECIAL, 3, 6, 0,  3},
        {"Mot de boost",       SK_BUFF,   2, 0, 18, 4},
        {"Mot de soin léger",  SK_HEAL,   3, 5, 12, 2},
        {"Mot de soin lourd",  SK_HEAL,   5, 0, 30, 5},
    }},
    {"Enutrof Magot", {
        {"Lancer de pièce",    SK_ATK,     2, 8, 10, 0},
        {"Pelle",              SK_ATK,     3, 3, 17, 0},
        {"Retrait PM",         SK_SPECIAL, 3, 5, 0,  3},
        {"Accélération",       SK_BUFF,    2, 0, 18, 4},
        {"Sac animé",          SK_SPECIAL, 3, 2, 0,  3},
        {"Pelle de jugement",  SK_ATK,     4, 4, 20, 2},
        {"Pelle massacrante",  SK_ATK,     5, 1, 24, 2},
        {"Corruption",         SK_SPECIAL, 4, 4, 0,  4},
        {"Chance",             SK_BUFF,    2, 0, 14, 3},
        {"Coffre animé",       SK_SUMMON,  4, 2, 0,  5, 9},   /* invoque un coffre */
        {"Maladresse",         SK_SPECIAL, 1, 4, 0,  2},
        {"Clef réductrice",    SK_SPECIAL, 3, 5, 0,  3},
        {"Pelle de massacrante", SK_ATK,   5, 2, 28, 3},
        {"Jet",                SK_ATK,     4, 5, 19, 2},
        {"Pelle cachée",       SK_ATK,     3, 4, 15, 0},
        {"Coup de pot",        SK_SPECIAL, 4, 0, 14, 4},
    }},
    {"Sram Fourbe", {
        {"Coup fantôme",       SK_ATK,     4, 1, 22, 0},
        {"Piège empoisonné",   SK_SPECIAL, 3, 4, 0,  3},
        {"Invisibilité",       SK_SPECIAL, 3, 0, 0,  5},
        {"Piège de masse",     SK_SPECIAL, 4, 4, 0,  4},
        {"Attaque sournoise",  SK_ATK,     3, 2, 16, 1},
        {"Frappe",             SK_ATK,     4, 3, 20, 2},
        {"Lame traître",       SK_ATK,     5, 2, 27, 3},
        {"Piège",              SK_SPECIAL, 3, 4, 11, 3},
        {"Piège zip",          SK_SPECIAL, 4, 6, 0,  4},
        {"Piège mortel",       SK_SPECIAL, 5, 4, 0,  5},
        {"Piège de recul",     SK_SPECIAL, 3, 3, 0,  3},
        {"Piège de silence",   SK_SPECIAL, 4, 4, 0,  4},
        {"Piège de poison",    SK_SPECIAL, 3, 4, 0,  3},
        {"Piège de feu",       SK_SPECIAL, 3, 4, 0,  3},
        {"Piège de glace",     SK_SPECIAL, 3, 4, 0,  3},
        {"Piège de terre",     SK_SPECIAL, 3, 4, 0,  3},
    }},
    {"Sacrieur Maso", {
        {"Coup de genou",      SK_ATK,     3, 1, 15, 0},
        {"Roulade",            SK_ATK,     4, 1, 20, 1},
        {"Frappe",             SK_ATK,     5, 1, 28, 2},
        {"Transposition",      SK_SPECIAL, 3, 5, 0,  3},
        {"Punition",           SK_ATK,     4, 2, 21, 2},
        {"Sacrifice",          SK_SPECIAL, 3, 2, 0,  3},
        {"Furie",              SK_ATK,     5, 1, 30, 4},
        {"Douleur partagée",   SK_SPECIAL, 4, 3, 0,  3},
        {"Adrénaline",         SK_BUFF,   2, 0, 25, 5},
        {"Contre",             SK_BUFF,    2, 0, 22, 5},
        {"Attirance",          SK_SPECIAL, 2, 6, 0,  2},
        {"Echange",            SK_SPECIAL, 3, 5, 0,  3},
        {"Soin",               SK_HEAL,    4, 0, 15, 4},
        {"Recharge",           SK_HEAL,    5, 0, 30, 5},
        {"Non contact",        SK_SPECIAL, 2, 3, 0,  2},
        {"Roquette",           SK_BUFF,    3, 0, 28, 5},
    }},
    {"Sadida Ronceux", {
        {"Ronce",              SK_ATK,     3, 5, 16, 0},
        {"Poupée bloqueuse",   SK_SPECIAL, 3, 2, 0,  3},
        {"Poupée sacrifiée",   SK_SPECIAL, 3, 4, 0,  3},
        {"Ronce multiple",     SK_ATK,     4, 5, 21, 2},
        {"Arbre",              SK_SUMMON,  3, 3, 0,  4, 10},  /* invoque un arbre */
        {"Ronce agressive",    SK_ATK,     5, 4, 27, 3},
        {"Poupée gonflable",   SK_HEAL,   4, 3, 18, 3},
        {"Vent",               SK_SPECIAL, 3, 3, 6,  2},
        {"Ronce puissante",    SK_ATK,     4, 4, 18, 1},
        {"Soin",               SK_HEAL,   3, 0, 12, 2},
        {"Ronce lourde",       SK_ATK,     5, 5, 25, 3},
        {"Ronce bulldozer",    SK_ATK,     5, 3, 29, 4},
        {"Trempe",             SK_SPECIAL, 4, 5, 13, 3},
        {"Pousse",             SK_SPECIAL, 3, 3, 8,  2},
        {"Ronce glourbo",      SK_ATK,     3, 3, 17, 1},
        {"Sève",               SK_HEAL,    3, 0, 12, 2},
    }},
    {"Osamodas Zoo", {
        {"Griffe spectrale",   SK_ATK,     3, 4, 15, 0},
        {"Cri de l'ours",      SK_BUFF,    2, 0, 18, 4},
        {"Invocation Tofu",    SK_SUMMON,  3, 2, 0,  3, 1},
        {"Corbeau",            SK_ATK,     3, 6, 12, 0},
        {"Fouet",              SK_ATK,     4, 2, 18, 1},
        {"Invocation Gobball", SK_SUMMON,  3, 2, 0,  3, 2},
        {"Crapaud",            SK_SPECIAL, 3, 3, 0,  3},
        {"Invocation Prespic", SK_SUMMON,  3, 2, 0,  3, 3},
        {"Fouet du mulou",     SK_ATK,     4, 1, 22, 2},
        {"Soin animal",        SK_HEAL,    3, 4, 16, 2},
        {"Invocation sanglier",SK_SUMMON,  3, 2, 0,  3, 4},
        {"Défense naturelle",  SK_BUFF,    2, 0, 15, 3},
        {"Crocs",              SK_ATK,     4, 1, 19, 1},
        {"Invocation Bwork",   SK_SUMMON,  4, 0, 0,  5, 5},
        {"Invocation Crackler",SK_SUMMON, 4, 0, 0,  5, 6},
        {"Invocation Wyrmling",SK_SUMMON, 4, 0, 0,  5, 7},
    }},
    {"Xelor Retard", {
        {"Rayon Obscur",       SK_ATK,     3, 4, 14, 0},
        {"Aiguille",           SK_ATK,     3, 4, 12, 0},
        {"Horloge",            SK_SPECIAL, 2, 3, 0,  3},
        {"Ralentissement",     SK_SPECIAL, 3, 5, 0,  4},
        {"Frappe du Xelor",    SK_ATK,     4, 2, 20, 2},
        {"Téléportation",      SK_SPECIAL, 3, 6, 0,  3},
        {"Vol de PA",          SK_SPECIAL, 4, 4, 0,  4},
        {"Cadran",             SK_SPECIAL, 3, 3, 0,  3},
        {"Poussière",          SK_ATK,     4, 5, 19, 2},
        {"Laser",              SK_ATK,     4, 4, 18, 1},
        {"Sablier",            SK_SPECIAL, 3, 3, 0,  4},
        {"Frappe Temporelle",  SK_ATK,     5, 6, 24, 3},
        {"Portail",            SK_SPECIAL, 3, 7, 0,  4},
        {"Paradoxe",           SK_SPECIAL, 3, 5, 0,  3},
        {"Rembobinage",        SK_SPECIAL, 4, 0, 18, 4},
        {"Cadran Tactique",    SK_SPECIAL, 4, 4, 12, 3},
    }},
    {"Feca Boucliné", {
        {"Attaque naturelle",  SK_ATK,     3, 4, 13, 0},
        {"Armure terrestre",   SK_BUFF,    2, 0, 15, 3},
        {"Glyphe agressif",    SK_SPECIAL, 3, 3, 8,  2},
        {"Renvoi de sort",     SK_SPECIAL, 3, 0, 0,  4},
        {"Aveuglement",        SK_SPECIAL, 3, 4, 0,  3},
        {"Armure incandescente", SK_BUFF,  3, 0, 18, 4},
        {"Attaque nuageuse",   SK_ATK,     4, 4, 17, 1},
        {"Armure aqueuse",     SK_BUFF,    3, 0, 15, 3},
        {"Immunité",           SK_SPECIAL, 4, 0, 0,  5},
        {"Armure venteuse",    SK_BUFF,    3, 0, 18, 4},
        {"Glyphe d'aveuglement", SK_SPECIAL, 4, 4, 0,  4},
        {"Glyphe enflammé",    SK_SPECIAL, 4, 4, 0,  4},
        {"Bouclier Féca",      SK_BUFF,    4, 0, 20, 4},
        {"Glyphe d'immobilisation", SK_SPECIAL, 4, 4, 0,  4},
        {"Glyphe de silence",  SK_SPECIAL, 4, 4, 0,  4},
        {"Invocation dopeul",  SK_SPECIAL, 4, 0, 0,  5},
    }},
    {"Pandawa Bourre", {
        {"Lame imbibée",       SK_ATK,     4, 2, 18, 0},
        {"Haleine",            SK_ATK,     4, 4, 20, 2},
        {"Rage",               SK_ATK,     5, 1, 26, 3},
        {"Soin",               SK_HEAL,    3, 3, 18, 2},
        {"Vulnérabilité",      SK_SPECIAL, 2, 4, 0,  3},
        {"Portage",            SK_SPECIAL, 3, 1, 0,  3},
        {"Boost",              SK_BUFF,    2, 0, 18, 4},
        {"Cocktail",           SK_BUFF,    2, 0, 18, 4},
        {"Bond",               SK_SPECIAL, 2, 3, 0,  2},
        {"Fracas",             SK_ATK,     4, 3, 21, 2},
        {"Punch",              SK_ATK,     3, 1, 16, 0},
        {"Boum",               SK_ATK,     5, 3, 25, 3},
        {"Tai Chi",            SK_SPECIAL, 4, 2, 0,  4},
        {"Bambou",             SK_ATK,     4, 4, 19, 2},
        {"Gorgée",             SK_SPECIAL, 3, 0, 12, 3},
        {"Panoplie",           SK_BUFF,    4, 0, 35, 6, 0, 1},  /* forme d'ours */
    }},
    {"Roublard Boom", {
        {"Plomb",              SK_ATK,     3, 5, 15, 0},
        {"Trépan",             SK_ATK,     4, 6, 19, 1},
        {"Pièce montée",       SK_ATK,     4, 6, 24, 2},
        {"Superchute",         SK_ATK,     5, 2, 30, 4},
        {"Lasso",              SK_SPECIAL, 2, 3, 0,  2},
        {"Croche-patte",       SK_SPECIAL, 3, 5, 0,  4},
        {"Classe mulot",       SK_BUFF,    2, 0, 20, 4},
        {"Ponzi",              SK_SPECIAL, 3, 4, 10, 3},
        {"Fumigène",           SK_SPECIAL, 3, 0, 0,  3},
        {"Poudre",             SK_ATK,     5, 5, 27, 3},
        {"Lunette",            SK_ATK,     4, 7, 21, 2},
        {"Colis suspect",      SK_ATK,     5, 4, 26, 3},
        {"Boum",               SK_ATK,     4, 3, 22, 2},
        {"Carabine",           SK_ATK,     3, 6, 17, 1},
        {"Sortie",             SK_SPECIAL, 2, 4, 0,  2},
        {"Fiole",              SK_SPECIAL, 4, 5, 13, 3},
    }},
};

/* ------------------------------------------------------------------ */
/* Panoplies (stuff) : bonus appliques au combattant                   */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    const char *desc;
    int hp_bonus;    /* PV max supplementaires                          */
    int dmg_pct;     /* +% degats toutes sortes (attaque)               */
    int heal_pct;    /* +% soins                                        */
    int puissance;   /* +degats bruts fixes sur chaque sort             */
    int range_bonus; /* +portee sur tous les sorts                      */
    int pa_bonus;    /* +PA par tour                                     */
    int pm_bonus;    /* +PM par tour                                     */
    int ret_pa;      /* retrait PA a l'adversaire a son tour            */
    int ret_pm;      /* retrait PM a l'adversaire a son tour            */
    int reflect;     /* renvoie X degats a l'attaquant quand on est touche */
} Panoplie;

static Panoplie PANOPLIES[N_PANOPLIES] = {
    {"Panoplie du Bouftou Royal",
     "tank PV",
     .hp_bonus = 50, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Prespic",
     "renvoi de dommages",
     .hp_bonus = 10, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 15},
    {"Panoplie du Craqueleur Legendaire",
     "degats + puissance",
     .hp_bonus = 15, .dmg_pct = 30, .heal_pct = 0, .puissance = 5,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie de l'Abraknyde",
     "portee",
     .hp_bonus = 10, .dmg_pct = 10, .heal_pct = 0, .puissance = 0,
     .range_bonus = 2, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Kwak de Terre",
     "PA + PM",
     .hp_bonus = 0, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 1, .pm_bonus = 1,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Tofu",
     "mobilité PM",
     .hp_bonus = 5, .dmg_pct = 5, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 2,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Xelor du Temps",
     "retraits PA/PM a l'adversaire",
     .hp_bonus = 10, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 1, .ret_pm = 1, .reflect = 0},
    {"Panoplie de la Soigneur",
     "soins",
     .hp_bonus = 20, .dmg_pct = 0, .heal_pct = 40, .puissance = 0,
     .range_bonus = 1, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Bouftou Rieur",
     "critique (jeu de mot)",
     .hp_bonus = 15, .dmg_pct = 12, .heal_pct = 0, .puissance = 3,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie de la Dragodinde Dinde",
     "mobilité et panique",
     .hp_bonus = 5, .dmg_pct = 5, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 3,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Chacha Chatouilleux",
     "esquive et taquineries",
     .hp_bonus = 8, .dmg_pct = 0, .heal_pct = 10, .puissance = 0,
     .range_bonus = 1, .pa_bonus = 0, .pm_bonus = 1,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Tofu Fou",
     "vitesse et cacophonie",
     .hp_bonus = 0, .dmg_pct = 8, .heal_pct = 0, .puissance = 2,
     .range_bonus = 0, .pa_bonus = 1, .pm_bonus = 2,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie de l'Ecaflip Chanceux",
     "degats et soins",
     .hp_bonus = 12, .dmg_pct = 18, .heal_pct = 18, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 1,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Bwork Archer",
     "portee et degats",
     .hp_bonus = 8, .dmg_pct = 10, .heal_pct = 0, .puissance = 2,
     .range_bonus = 2, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie de la Prespic Blindee",
     "renvoi de dommages",
     .hp_bonus = 22, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 28},
    {"Panoplie du Crackler Colere",
     "action et vitesse",
     .hp_bonus = 6, .dmg_pct = 8, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 2, .pm_bonus = 1,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Sanglier Chargeur",
     "mobilite",
     .hp_bonus = 16, .dmg_pct = 6, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 3,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Wyrmling Ancestral",
     "puissance brute",
     .hp_bonus = 10, .dmg_pct = 14, .heal_pct = 0, .puissance = 9,
     .range_bonus = 1, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Dopeul Vif",
     "soins vegetal",
     .hp_bonus = 28, .dmg_pct = 0, .heal_pct = 60, .puissance = 0,
     .range_bonus = 1, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie de l'Enutrof Fortune",
     "retraits de ressources",
     .hp_bonus = 12, .dmg_pct = 6, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 1,
     .ret_pa = 2, .ret_pm = 1, .reflect = 0},
    {"Panoplie du Xelor Double",
     "actions et degats",
     .hp_bonus = 0, .dmg_pct = 15, .heal_pct = 0, .puissance = 3,
     .range_bonus = 1, .pa_bonus = 1, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie de la Sadida Foisonnante",
     "soins et endurance",
     .hp_bonus = 34, .dmg_pct = 0, .heal_pct = 34, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Panoplie du Coffre Ancestral",
     "butin et protection",
     .hp_bonus = 18, .dmg_pct = 10, .heal_pct = 0, .puissance = 4,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 2,
     .ret_pa = 0, .ret_pm = 0, .reflect = 12},
    {"Panoplie de l'Osamodas",
     "invocations et mobilite",
     .hp_bonus = 14, .dmg_pct = 8, .heal_pct = 12, .puissance = 2,
     .range_bonus = 1, .pa_bonus = 1, .pm_bonus = 1,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
};

/* ------------------------------------------------------------------ */
/* Dofus : bonus d'equipe choisi par l'equipe entiere                   */
/* ------------------------------------------------------------------ */
static Panoplie DOFUS[N_DOFUS] = {
    {"Dofus Turquoise", "agilite et vitesse",
     .hp_bonus = 0, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 1, .pa_bonus = 0, .pm_bonus = 2,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Dofus Emeraude", "vitalite et soins",
     .hp_bonus = 40, .dmg_pct = 0, .heal_pct = 20, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Dofus Pourpre", "force brute",
     .hp_bonus = 10, .dmg_pct = 20, .heal_pct = 0, .puissance = 6,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Dofus Ocre", "intelligence et portee",
     .hp_bonus = 0, .dmg_pct = 15, .heal_pct = 0, .puissance = 0,
     .range_bonus = 2, .pa_bonus = 1, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 0},
    {"Dofus Vulbis", "retraits et controle",
     .hp_bonus = 5, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 2, .ret_pm = 2, .reflect = 0},
    {"Dofus Abyssal", "renvoi et defense",
     .hp_bonus = 25, .dmg_pct = 0, .heal_pct = 0, .puissance = 0,
     .range_bonus = 0, .pa_bonus = 0, .pm_bonus = 0,
     .ret_pa = 0, .ret_pm = 0, .reflect = 20},
};

/* ------------------------------------------------------------------ */
/* état du combattant                                                  */
/* ------------------------------------------------------------------ */
typedef enum { ANIM_IDLE, ANIM_MOVE, ANIM_ATTACK } AnimState;

typedef struct {
    int  cls;
    char nickname[32];
    int  x, y;
    int  hp;
    int  pa;            /* points d'action du tour                  */
    int  pm;            /* points de mouvement du tour              */
    int  atk_mod;       /* bonus/malus en % cumulés (buffs)         */
    int  alive;
    int  team;
    int  initiative;
    int  elements[ELEMENT_COUNT];
    int  air_penalty;
    int  air_penalty_turns;
    int  pano;               /* panoplie equipee (-1 = aucune)           */
    int  hp_max;             /* PV max (base + panoplie)                 */
    Skill spells[N_EQUIPPED];  /* les 4 sorts choisis pour la partie  */
    int   spell_icon[N_EQUIPPED];
    int  cd_left[N_EQUIPPED];
    /* animation (cosmétique, jamais synchronisée sur le réseau)      */
    AnimState anim;
    int       anim_frame;
    Uint32    anim_until;   /* timestamp de retour a ANIM_IDLE       */
} Fighter;

/* ------------------------------------------------------------------ */
/* réseau : protocole P2P minimal                                      */
/*   HELLO / CLASS <who> <idx> / SPELLS <who> <a> <b> <c> <d>          */
/*   MAP <seed>  (host -> join : graine de generation de l'arene)       */
/*   MOVE <who> <x> <y> / SKILL <who> <idx> <tx> <ty> / END <who> / BYE*/
/* ------------------------------------------------------------------ */
typedef enum { NET_NONE, NET_HOST, NET_JOIN } NetRole;

typedef struct {
    Sock          sock;
    struct sockaddr_in peer;
    int           has_peer;
    int           connected;
    NetRole       role;
    Uint32        last_recv_ms;
    Uint32        last_keepalive_ms;
    Uint32        connect_started_ms;
} Net;

/* ------------------------------------------------------------------ */
/* Infobulle générique : popup affiché au survol d'un élément cliquable */
/* ------------------------------------------------------------------ */
#define TIP_MAX_LINES 6
#define TIP_TITLE_MAX 80
#define TIP_LINE_MAX  128

static char g_tip_title[TIP_TITLE_MAX];
static char g_tip_lines[TIP_MAX_LINES][TIP_LINE_MAX];
static int  g_tip_count;
static int  g_tip_active;

static void tip_reset(void)
{
    g_tip_count = 0;
    g_tip_active = 0;
    g_tip_title[0] = 0;
}

static void tip_begin(const char *title)
{
    tip_reset();
    g_tip_active = 1;
    if (title)
        snprintf(g_tip_title, sizeof g_tip_title, "%s", title);
}

static void tip_add(const char *fmt, ...)
{
    va_list ap;
    if (!g_tip_active || g_tip_count >= TIP_MAX_LINES) return;
    va_start(ap, fmt);
    vsnprintf(g_tip_lines[g_tip_count], TIP_LINE_MAX, fmt, ap);
    va_end(ap);
    g_tip_count++;
}

static void draw_tooltip(SDL_Renderer *r, TTF_Font *font, int mx, int my);
static void ui_button(SDL_Renderer *r, SDL_Rect b, const char *label,
                      TTF_Font *font, int hovered, int selected, int enabled);

/* ------------------------------------------------------------------ */
/* Défis du jour : 3 défis par jour, déterminés par la date            */
/* ------------------------------------------------------------------ */
typedef enum {
    CH_WIN,          /* gagner un combat solo                        */
    CH_FAST,         /* gagner un combat solo en moins de N tours   */
    CH_CLASS,        /* gagner avec une classe imposée               */
    CH_SUMMON,       /* gagner en utilisant une invocation            */
    CH_FLAWLESS,     /* gagner sans perdre un seul PV               */
    CH_HEALER        /* gagner en finissant le combat au-dessus de 50% */
} ChallengeKind;

typedef struct {
    ChallengeKind kind;
    const char   *title;
    const char   *desc;
    int  param;      /* classe imposée / limite de tours / seuil PV  */
    int  reward;     /* points gagnés                                */
} ChallengeDef;

typedef struct {
    int  active;
    int  team;
    int  owner;      /* indice du combattant invoqueur                  */
    int  slot;       /* rang de l'invocation chez son proprietaire      */
    int  x, y;
    int  hp;
    int  hp_max;
    int  atk;        /* degats par attaque                              */
    int  range;      /* portee d'attaque                                */
    int  pm;         /* deplacement par tour                            */
    int  tmpl;       /* index dans SUMMON_TEMPLATES                     */
    AnimState anim;
    int   anim_frame;
    Uint32 anim_until;
    char name[32];
} Summon;

/* ------------------------------------------------------------------ */
/* Ǹtat global du jeu                                                  */
/* ------------------------------------------------------------------ */
typedef enum { ST_MENU, ST_PIN, ST_PICK, ST_DRAFT, ST_STUFF, ST_ELEMENTS,
               ST_DOFUS, ST_ACHIEVEMENTS, ST_CHALLENGES, ST_DEFS, ST_SONGE,
               ST_BATTLE, ST_GAMEOVER } GameState;

typedef struct {
    GameState state;
    int       cell[GRID_H][GRID_W];  /* sol, mur, fausse ou hors-arene  */
    int       map_width;
    int       map_height;
    int       map_theme;
    Fighter   f[MAX_FIGHTERS];
    int       my_index;
    int       turn;
    int       turn_count;
    int       team_size;
    int       setup_unit;
    int       solo_mode;
    int       neural_bot;
    unsigned  challenge_seed;
    Uint32    ai_next_action_ms;
    int       turn_order[MAX_FIGHTERS];
    int       turn_pos;
    int       team_size_choice;
    int       sel_skill;
    int       winner;
    Net       net;
    int       class_chosen[MAX_FIGHTERS];
    int       draft[MAX_FIGHTERS][N_CLASS_SPELLS];
    int       draft_count[MAX_FIGHTERS];
    int       draft_ready[MAX_FIGHTERS];
    int       stuff_chosen[MAX_FIGHTERS];
    int       stuff_ready[MAX_FIGHTERS];
    int       team_dofus[2];
    int       dofus_ready[2];
    int       achievement_scroll;
    int       favorite_class[MAX_FAVORITES];
    int       favorite_pano[MAX_FAVORITES];
    int       selected_favorite;
    /* --- interface reseau et chat ---                                 */
    int       ip_editing;
    char      ip_text[64];
    char      menu_status[160];
    int       chat_active;             /* saisie du chat active          */
    char      chat_input[256];
    char      chat_log[6][128];        /* derniers messages             */
    int       chat_log_n;
    Uint32    social_last_chat_ms[2];
    int       social_chat_burst[2];
    Uint32    social_last_emote_ms[2];
    int       social_emote_burst[2];
    Uint32    social_last_pacify_ms;
    Uint32    social_last_encourage_ms;
    /* --- contre musical anti-humiliation ---                          */
    int       anti_humil_active;       /* contre en cours               */
    int       anti_humil_prev_track;   /* piste avant le contre         */
    int       anti_humil_used[4];      /* pistes deja employées         */
    Uint32    anti_humil_restore_ms;   /* reprise programmée           */
    long long anti_humil_last_ns;      /* horodatage ns du dernier acte */
    int       emote_menu_open;
    int       in_game_menu_open;
    int       surrender_confirm;
    int       settings_open;
    int       music_volume;
    int       sfx_volume;
    int       colorblind_mode;
    int       reduce_flashing;
    int       subtitles_when_muted;
    char      audio_subtitle[64];
    Uint32    audio_subtitle_until;
    int       emote_fighter;
    int       emote_index;
    Uint32    emote_until;
    /* --- fenetre plein ecran sans bordure et definitions ---------------- */
    int       fullscreen;         /* mode fenetre plein ecran (sans bordure) */
    int       defs_cat;           /* categorie affichee dans les defs      */
    int       defs_scroll;        /* defilement de la liste des definitions */
    /* --- verrouillage par code PIN -------------------------------------- */
    int       pin_set;            /* 1 = un code PIN est deja enregistre   */
    int       pin_mode;           /* 0 = creation, 1 = deverrouillage      */
    char      pin_entry[8];       /* saisie en cours (6 chiffres)           */
    int       pin_len;
    char      pin_first[8];       /* premiere saisie (mode creation)        */
    int       pin_have_first;
    int       pin_attempts;       /* essais restants                        */
    int       pin_forget_armed;   /* confirmation de l'effacement           */
    Uint32    pin_lock_until;     /* fin de blocage (ms)                    */
    char      pin_msg[96];        /* message a l'ecran                      */
    char      pin_salt[32];       /* sel du hash                            */
    char      pin_hash[32];       /* hash du code                          */
    /* --- commande de ligne de commande, executee apres le deverrouillage */
    int       cli_pending;        /* 0 = rien, 1 = host, 2 = join, 3 = defi */
    unsigned  cli_seed;
    char      cli_ip[64];
    int       vfx_active;
    int       vfx_x;
    int       vfx_y;
    Uint32    vfx_started;
    /* --- invocations ---------------------------------------------------- */
    Summon    summons[MAX_SUMMONS];
    int       summon_used;        /* invocations creees pendant la partie */
    /* --- defis du jour -------------------------------------------------- */
    int       challenge_day;      /* jour courant (numero de jour)         */
    int       challenge_progress[DAILY_CHALLENGES];
    int       challenge_done[DAILY_CHALLENGES];
    ChallengeKind challenge_kind[DAILY_CHALLENGES];
    int       challenge_param[DAILY_CHALLENGES];
    int       challenge_reward[DAILY_CHALLENGES];
    char      challenge_desc[DAILY_CHALLENGES][160];
    int       challenge_points;   /* points de score cumules              */
    int       active_challenge;   /* -1 = aucun defi en cours             */
    int       match_summons;      /* invocations utilisees dans la partie */
    int       match_turns;        /* nombre de tours joues                */
    int       match_class;        /* classe utilisee pendant la partie    */
    int       match_untouched;    /* 1 si aucun PV perdu                  */
    /* --- mode Songe (expedition infinie) ------------------------------- */
    int       songe_mode;         /* expedition Songe en cours            */
    int       songe_want;         /* le joueur veut joining un Songe      */
    int       songe_level;        /* niveau courant (1, 2, 3...)          */
    int       songe_best;         /* record de niveau atteint             */
    int       songe_party;        /* taille de l'equipe (1..5)             */
    int       songe_seed;         /* graine du niveau courant             */
    int       slot_owner[MAX_TEAM_SIZE]; /* joueur controlant chaque slot */
    int       monster_tmpl[MAX_FIGHTERS]; /* template du monstre (equipe 1)*/
} Game;

static int challenge_day_number(void)
{
    return (int)(time(NULL) / 86400);
}

static void challenges_build(Game *g)
{
    int day = challenge_day_number();
    unsigned rng = (unsigned)day * 2654435761u + 12345u;
    int i, used[N_CLASSES];
    int used_kind[6];
    memset(used, 0, sizeof used);
    memset(used_kind, 0, sizeof used_kind);
    g->challenge_day = day;
    for (i = 0; i < DAILY_CHALLENGES; i++) {
        ChallengeKind kind = (ChallengeKind)(rng % 6);
        int param = 0;
        rng = rng * 1664525u + 1013904223u;
        /* pas deux defis de meme type le meme jour                    */
        if (used_kind[kind]) {
            int k, pick = (int)((rng >> 16) % 6);
            for (k = 0; k < 6; k++)
                if (!used_kind[(pick + k) % 6]) { pick = (pick + k) % 6; break; }
            kind = (ChallengeKind)pick;
        }
        used_kind[kind] = 1;
        switch (kind) {
            case CH_FAST:    param = 6 + (int)((rng >> 8) % 5); break;
            case CH_CLASS: {
                int c;
                do { rng = rng * 1664525u + 1013904223u;
                     c = (int)((rng >> 8) % N_CLASSES); }
                while (used[c]);
                used[c] = 1;
                param = c;
                break;
            }
            case CH_HEALER:  param = 50 + (int)((rng >> 8) % 3) * 10; break;
            default:         param = 0; break;
        }
        {
            static const char *titles[6] = {
                "Vainqueur du jour",
                "Victoire rapide",
                "Classe imposée",
                " Maître des invocations",
                "Sans une égratignure",
                "Marmotte de guerre"
            };
            char desc[160];
            switch (kind) {
                case CH_WIN:
                    snprintf(desc, sizeof desc,
                             "Gagnez un combat solo (mode 1v1) contre l'IA.");
                    break;
                case CH_FAST:
                    snprintf(desc, sizeof desc,
                             "Gagnez un combat solo en %d tours ou moins (le chronometre est severe).",
                             param);
                    break;
                case CH_CLASS:
                    snprintf(desc, sizeof desc,
                             "Gagnez un combat solo en jouant %s.",
                             CLASSES[param].name);
                    break;
                case CH_SUMMON:
                    snprintf(desc, sizeof desc,
                             "Gagnez un combat solo en ayant invoqué au moins "
                             "une creature.");
                    break;
                case CH_FLAWLESS:
                    snprintf(desc, sizeof desc,
                             "Gagnez un combat solo sans perdre le moindre PV. Un cheveu de plus et c'est fini.");
                    break;
                case CH_HEALER:
                    snprintf(desc, sizeof desc,
                             "Gagnez un combat solo en finissant avec plus de "
                             "%d %% de PV. Les monstres ne pardonnent pas.", param);
                    break;
            }
            g->challenge_progress[i] = 0;
            g->challenge_done[i] = 0;
            snprintf(g->challenge_desc[i], sizeof g->challenge_desc[i],
                     "%s", desc);
            g->challenge_kind[i] = kind;
            g->challenge_param[i] = param;
            g->challenge_reward[i] = 50 + (kind == CH_FAST ? 25 : 0)
                                     + (kind == CH_CLASS ? 25 : 0);
            (void)titles;
        }
    }
}



/* prototypes des fonctions moteur utilisées par net_poll */
static void apply_skill_remote(Game *g, int who, int idx, int tx, int ty);
static void end_turn(Game *g, int from_net);
static int move_distance(const Game *g, int sx, int sy, int tx, int ty);
static int fighter_at(const Game *g, int x, int y);
static void sync_draft(Game *g, int who);
static void check_draft_start(Game *g);
static void start_battle(Game *g, unsigned seed);
static void sync_stuff(Game *g, int who);
static void check_stuff_start(Game *g);
static void check_dofus_start(Game *g);
static void set_anim(Game *g, int idx, AnimState st);
static void check_winner(Game *g);
static void start_vfx(Game *g, int x, int y);
static void songe_record_save(Game *g);
static void songe_next_level(Game *g);
static void songe_start_level(Game *g, int level, unsigned seed);
static void summons_clear(Game *g);
static void summon_attack(Game *g, int owner, int slot, int tx, int ty);
static void sort_turn_order(Game *g);
static int unit_for_slot(const Game *g, int team, int slot);
static int side_has_all(const int values[MAX_FIGHTERS], const Game *g, int team);
static void advance_setup(Game *g);
static int fighter_at(const Game *g, int x, int y);
static int manhattan(int x1, int y1, int x2, int y2);
static void show_emote(Game *g, int who, int emote);
static void play_sound(Game *g, SoundEffect sound);
static void set_social_sound_volume(void);
static void try_play_social_cue(Game *g, SoundEffect sound,
                                unsigned probability_percent);
static void process_social_message(Game *g, int team, const char *message);
static void process_social_emote(Game *g, int team);
static void anti_humil_music(Game *g, long long trigger_ns);
static void anti_humil_restore(Game *g);

static void favorites_load(Game *g)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    char path[512];
    FILE *file;
    int i;
    for (i = 0; i < MAX_FAVORITES; i++) {
        g->favorite_class[i] = -1;
        g->favorite_pano[i] = -1;
    }
    if (!dir) return;
    snprintf(path, sizeof path, "%sfavorites.txt", dir);
    SDL_free(dir);
    file = fopen(path, "r");
    if (!file) return;
    for (i = 0; i < MAX_FAVORITES; i++) {
        int cls, pano;
        if (fscanf(file, "%d %d", &cls, &pano) != 2) break;
        if (cls >= 0 && cls < N_CLASSES && pano >= 0 && pano < N_PANOPLIES) {
            g->favorite_class[i] = cls;
            g->favorite_pano[i] = pano;
        }
    }
    fclose(file);
}

static int favorites_save(Game *g)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    char path[512];
    FILE *file;
    int i, ok = 1;
    if (!dir) return 0;
    snprintf(path, sizeof path, "%sfavorites.txt", dir);
    SDL_free(dir);
    file = fopen(path, "w");
    if (!file) return 0;
    for (i = 0; i < MAX_FAVORITES; i++)
        if (fprintf(file, "%d %d\n", g->favorite_class[i],
                    g->favorite_pano[i]) < 0)
            ok = 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

/* ------------------------------------------------------------------ */
/* utilitaires réseau                                                  */
/* ------------------------------------------------------------------ */
static int net_init(void)
{
#ifdef _WIN32
    WSADATA w; return WSAStartup(MAKEWORD(2,2), &w) == 0;
#else
    return 1;
#endif
}

static void net_close(Sock s)
{
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

static int net_open(Game *g, NetRole role, const char *host_ip)
{
    g->net.sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g->net.sock == SOCK_INVALID) return 0;
#ifdef _WIN32
    {
        u_long nonblocking = 1;
        if (ioctlsocket(g->net.sock, FIONBIO, &nonblocking) == SOCKET_ERROR) {
            net_close(g->net.sock);
            g->net.sock = SOCK_INVALID;
            return 0;
        }
    }
#endif
    g->net.role = role;
    g->net.has_peer = 0;
    g->net.connected = 0;
    g->net.last_recv_ms = 0;
    g->net.last_keepalive_ms = 0;
    g->net.connect_started_ms = 0;
    if (role == NET_HOST) {
        struct sockaddr_in a;
        int yes = 1;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = INADDR_ANY;
        a.sin_port = htons(PORT);
        setsockopt(g->net.sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof yes);
        if (bind(g->net.sock, (struct sockaddr*)&a, sizeof a) < 0) {
            net_close(g->net.sock);
            g->net.sock = SOCK_INVALID;
            return 0;
        }
    } else {
        memset(&g->net.peer, 0, sizeof g->net.peer);
        g->net.peer.sin_family = AF_INET;
        g->net.peer.sin_port = htons(PORT);
        if (inet_pton(AF_INET, host_ip, &g->net.peer.sin_addr) != 1) {
            net_close(g->net.sock);
            g->net.sock = SOCK_INVALID;
            return 0;
        }
        g->net.has_peer = 1;
    }
    return 1;
}

static void net_send(Game *g, const char *msg)
{
    if (!g->net.has_peer) return;
    char buf[128];
    snprintf(buf, sizeof buf, "%s\n", msg);
    sendto(g->net.sock, buf, (int)strlen(buf), 0,
           (struct sockaddr*)&g->net.peer, sizeof g->net.peer);
}

static void show_emote(Game *g, int who, int emote)
{
    char msg[32];
    if (who < 0 || who >= 2 * g->team_size
        || emote < 0 || emote >= EMOTE_COLS * EMOTE_ROWS)
        return;
    g->emote_fighter = who;
    g->emote_index = emote;
    g->emote_until = SDL_GetTicks() + 2500;
    g->emote_menu_open = 0;
    process_social_emote(g, who / g->team_size);
    snprintf(msg, sizeof msg, "EMOTE %d %d", who, emote);
    net_send(g, msg);
}

static int unit_for_slot(const Game *g, int team, int slot)
{
    return team * g->team_size + slot;
}

/* ------------------------------------------------------------------ */
/* Mode Songe : l'equipe est partagee entre les joueurs connectes      */
/* ------------------------------------------------------------------ */

/* le slot d'equipe appartient-il au joueur local ?                     */
static int slot_is_mine(const Game *g, int slot)
{
    if (slot < 0 || slot >= MAX_TEAM_SIZE) return 0;
    if (!g->songe_mode) return 0;   /* equipe 0 = joueur 0, equipe 1 = 1 */
    return g->slot_owner[slot] == g->my_index;
}

/* le combattant est-il controle par le joueur local ?                  */
static int unit_is_mine(const Game *g, int unit)
{
    if (unit < 0 || unit >= 2 * g->team_size) return 0;
    if (g->songe_mode)
        return g->slot_owner[unit % g->team_size] == g->my_index;
    return unit / g->team_size == g->my_index;
}

/* index du combattant en cours de preparation (mode Songe : equipe 0) */
static int setup_unit_index(const Game *g)
{
    if (g->songe_mode) return g->setup_unit;
    return unit_for_slot(g, g->my_index, g->setup_unit);
}

/* repartition des slots entre les joueurs connectes                    */
static void songe_assign_slots(Game *g, int players)
{
    int s;
    for (s = 0; s < MAX_TEAM_SIZE; s++)
        g->slot_owner[s] = (players <= 1) ? 0 : (s % players);
}

/* nombre de monstres pour un niveau donne                              */
static int songe_monster_count(int level)
{
    int n = 1 + level / 2;
    if (n > 5) n = 5;
    return n;
}

/* nom du monstre d'un combattant (mode Songe)                          */
static const char *fighter_name(const Game *g, const Fighter *f)
{
    if (f->cls == CLASS_MONSTER) {
        int t = g->monster_tmpl[f - g->f];
        if (t >= 0 && t < N_MONSTER_TEMPLATES)
            return MONSTER_TEMPLATES[t].name;
        return "Monstre";
    }
    if (f->cls < 0 || f->cls >= N_CLASSES) return "?";
    return CLASSES[f->cls].name;
}

static int side_has_all(const int values[MAX_FIGHTERS], const Game *g, int team)
{
    int slot;
    for (slot = 0; slot < g->team_size; slot++)
        if (values[unit_for_slot(g, team, slot)] < 0)
            return 0;
    return 1;
}

static void reset_match(Game *g)
{
    int i;
    g->state = ST_PICK;
    g->solo_mode = 0;
    g->challenge_seed = 0;
    g->ai_next_action_ms = 0;
    g->turn = 0;
    g->turn_count = 0;
    g->turn_pos = 0;
    g->sel_skill = -1;
    g->winner = -1;
    g->setup_unit = 0;
    g->emote_menu_open = 0;
    g->in_game_menu_open = 0;
    g->surrender_confirm = 0;
    for (i = 0; i < MAX_FIGHTERS; i++) {
        g->class_chosen[i] = -1;
        g->draft_ready[i] = -1;
        g->draft_count[i] = 0;
        g->stuff_chosen[i] = -1;
        g->stuff_ready[i] = -1;
        memset(g->draft[i], 0, sizeof g->draft[i]);
        memset(&g->f[i], 0, sizeof g->f[i]);
        g->f[i].elements[ELEM_AIR] = 25;
        g->f[i].elements[ELEM_FIRE] = 25;
        g->f[i].elements[ELEM_EARTH] = 25;
        g->f[i].elements[ELEM_WATER] = 25;
    }
    g->team_dofus[0] = g->team_dofus[1] = -1;
    g->dofus_ready[0] = g->dofus_ready[1] = 0;
    g->achievement_scroll = 0;
}

static void net_disconnect(Game *g, const char *status)
{
    if (g->net.sock != SOCK_INVALID)
        net_close(g->net.sock);
    g->net.sock = SOCK_INVALID;
    g->net.has_peer = 0;
    g->net.connected = 0;
    g->net.last_recv_ms = 0;
    reset_match(g);
    g->state = ST_MENU;
    g->chat_active = 0;
    g->ip_editing = 0;
    SDL_StopTextInput();
    snprintf(g->menu_status, sizeof g->menu_status, "%s", status);
}

static void return_to_main_menu(Game *g)
{
    if (g->net.sock != SOCK_INVALID) {
        net_send(g, "BYE");
        net_disconnect(g, "Deconnecte de la partie. Vous pouvez vous reconnecter.");
    } else {
        reset_match(g);
        g->state = ST_MENU;
    }
    g->in_game_menu_open = 0;
    g->surrender_confirm = 0;
    g->emote_menu_open = 0;
    g->chat_active = 0;
    SDL_StopTextInput();
}

static void surrender_match(Game *g)
{
    char msg[32];
    if (g->state != ST_BATTLE) return;
    snprintf(msg, sizeof msg, "SURRENDER %d", g->my_index);
    net_send(g, msg);
    g->winner = 1 - g->my_index;
    g->state = ST_GAMEOVER;
    g->in_game_menu_open = 0;
    g->surrender_confirm = 0;
    g->emote_menu_open = 0;
    g->sel_skill = -1;
}

static void advance_setup(Game *g)
{
    int units[MAX_TEAM_SIZE], n = 0, i, slot;
    g->setup_unit = 0;
    /* unites que je prepare : mes slots (Songe) ou mon equipe           */
    if (g->songe_mode) {
        for (slot = 0; slot < g->team_size; slot++)
            if (g->slot_owner[slot] == g->my_index) units[n++] = slot;
    } else {
        for (slot = 0; slot < g->team_size; slot++)
            units[n++] = unit_for_slot(g, g->my_index, slot);
    }
    if (n == 0) units[n++] = unit_for_slot(g, g->my_index, 0);

    if (g->state == ST_PICK) {
        for (i = 0; i < n; i++)
            if (g->class_chosen[units[i]] < 0) {
                g->setup_unit = units[i];
                return;
            }
        g->state = ST_DRAFT;
    }
    if (g->state == ST_DRAFT) {
        for (i = 0; i < n; i++)
            if (g->draft_ready[units[i]] != 1) {
                g->setup_unit = units[i];
                return;
            }
        g->state = ST_STUFF;
    }
    if (g->state == ST_STUFF) {
        for (i = 0; i < n; i++)
            if (g->stuff_ready[units[i]] != 1) {
                g->setup_unit = units[i];
                return;
            }
        g->state = ST_ELEMENTS;
    }
    if (g->state == ST_ELEMENTS) {
        for (i = 0; i < n; i++)
            if (g->draft_ready[units[i]] != 2) {
                g->setup_unit = units[i];
                return;
            }
        g->state = ST_DOFUS;
    }
    if (g->state == ST_DOFUS) {
        if (!g->dofus_ready[g->songe_mode ? 0 : g->my_index]) {
            g->setup_unit = units[0];
            return;
        }
        check_dofus_start(g);
    }
}

static int net_begin_join(Game *g, const char *host_ip)
{
    Uint32 now;
    NetRole previous_role = g->net.role;
    if (!net_open(g, NET_JOIN, host_ip)) {
        g->net.role = previous_role;
        return 0;
    }
    g->my_index = 1;
    g->state = ST_MENU;
    now = SDL_GetTicks();
    g->net.connect_started_ms = now;
    g->net.last_keepalive_ms = now;
    net_send(g, "HELLO client");
    snprintf(g->menu_status, sizeof g->menu_status,
             "Connexion a %s:%d (UDP)...", host_ip, PORT);
    return 1;
}

/* réception non bloquante : dispatch vers le moteur de jeu            */
static void net_poll(Game *g)
{
    char buf[256];
    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof from;
        int n = (int)recvfrom(g->net.sock, buf, sizeof buf - 1,
#ifdef _WIN32
                              0,
#else
                              MSG_DONTWAIT,
#endif
                              (struct sockaddr*)&from, &flen);
        if (n <= 0) break;
        buf[n] = 0;
        char *nl = strchr(buf, '\n'); if (nl) *nl = 0;

        char cmd[16]; int who, a, b, c, d; unsigned sd;
        if (sscanf(buf, "%15s", cmd) != 1) continue;
        if (g->net.role == NET_HOST && !strcmp(cmd, "HELLO")) {
            g->net.peer = from;
            g->net.has_peer = 1;
        }
        if (!strcmp(cmd, "HELLO")) {
            if (g->net.role == NET_HOST) {
                if (!g->net.connected) {
                    reset_match(g);
                    if (g->songe_mode) {
                        /* le Songe partage l'equipe entre les 2 joueurs   */
                        g->team_size = g->songe_party;
                        g->solo_mode = 0;
                        g->challenge_seed = 0x51ED270Bu
                                          ^ (unsigned)g->team_size;
                        songe_assign_slots(g, 2);
                    }
                    g->state = ST_PICK;
                }
                g->net.connected = 1;
                g->net.last_keepalive_ms = SDL_GetTicks();
                {
                    char welcome[32];
                    snprintf(welcome, sizeof welcome, "WELCOME %d", g->team_size);
                    net_send(g, welcome);
                }
                g->menu_status[0] = 0;
            }
        } else if (!strcmp(cmd, "WELCOME")
                   && g->net.role == NET_JOIN
                   && !g->net.connected
                   && sscanf(buf, "%*s %d", &a) == 1
                   && a >= 1 && a <= MAX_TEAM_SIZE) {
                g->team_size = a;
                if (!g->net.connected)
                    reset_match(g);
                if (g->songe_want) {
                    g->songe_mode = 1;
                    g->songe_party = a;
                    g->solo_mode = 0;
                    g->challenge_seed = 0x51ED270Bu ^ (unsigned)a;
                    songe_assign_slots(g, 2);
                }
                g->net.connected = 1;
                g->net.last_keepalive_ms = SDL_GetTicks();
                g->state = ST_PICK;
                g->menu_status[0] = 0;
                advance_setup(g);
        } else if (!strcmp(cmd, "PING")) {
            if (g->net.connected) net_send(g, "PONG");
        } else if (!strcmp(cmd, "PONG")) {
            /* Any packet refreshes the peer timeout below. */
        } else if (!strcmp(cmd, "MAP") && sscanf(buf, "%*s %u", &sd) == 1) {
            /* le host arbitre la carte : meme arene des deux cotes    */
            if (g->state == ST_ELEMENTS || g->state == ST_STUFF
                || g->state == ST_DOFUS)
                start_battle(g, sd);
        } else if (!strcmp(cmd, "CLASS") && sscanf(buf, "%*s %d %d", &who, &a) == 2) {
            if (who >= 0 && who < 2 * g->team_size
                && !unit_is_mine(g, who)
                && a >= 0 && a < N_CLASSES) {
                g->class_chosen[who] = a;
                advance_setup(g);
            }
        } else if (!strcmp(cmd, "SPELLS") &&
                   sscanf(buf, "%*s %d %d %d %d %d", &who, &a, &b, &c, &d) == 5) {
            if (who >= 0 && who < 2 * g->team_size
                && !unit_is_mine(g, who)
                && a >= 0 && a < N_CLASS_SPELLS
                && b >= 0 && b < N_CLASS_SPELLS
                && c >= 0 && c < N_CLASS_SPELLS
                && d >= 0 && d < N_CLASS_SPELLS) {
                memset(g->draft[who], 0, sizeof g->draft[who]);
                g->draft[who][a] = g->draft[who][b] = 1;
                g->draft[who][c] = g->draft[who][d] = 1;
                g->draft_ready[who] = 1;
                check_draft_start(g);
            }
        } else if (!strcmp(cmd, "STUFF") && sscanf(buf, "%*s %d %d", &who, &a) == 2) {
            if (who >= 0 && who < 2 * g->team_size
                && !unit_is_mine(g, who)
                && a >= 0 && a < N_PANOPLIES) {
                g->stuff_chosen[who] = a;
                g->stuff_ready[who] = 1;
                advance_setup(g);
            }
        } else if (!strcmp(cmd, "ELEMENTS")
                   && sscanf(buf, "%*s %d %d %d %d %d",
                             &who, &a, &b, &c, &d) == 5) {
            if (who >= 0 && who < 2 * g->team_size
                && !unit_is_mine(g, who)
                && a >= 0 && b >= 0 && c >= 0 && d >= 0
                && a + b + c + d == ELEMENT_POINTS) {
                g->f[who].elements[ELEM_AIR] = a;
                g->f[who].elements[ELEM_FIRE] = b;
                g->f[who].elements[ELEM_EARTH] = c;
                g->f[who].elements[ELEM_WATER] = d;
                g->draft_ready[who] = 2;
                advance_setup(g);
            }
        } else if (!strcmp(cmd, "DOFUS") && sscanf(buf, "%*s %d %d", &a, &b) == 2) {
            if (a >= 0 && a < 2 && b >= 0 && b < N_DOFUS
                && a != g->my_index) {
                g->team_dofus[a] = b;
                g->dofus_ready[a] = 1;
                check_dofus_start(g);
            }
        } else if (!strcmp(cmd, "MOVE") && sscanf(buf, "%*s %d %d %d", &who, &a, &b) == 3) {
            if (who >= 0 && who < 2 * g->team_size
                && who == g->turn
                && !unit_is_mine(g, who)
                && a >= 0 && a < g->map_width
                && b >= 0 && b < g->map_height
                && g->cell[b][a] == CELL_GROUND
                && fighter_at(g, a, b) < 0 && g->state == ST_BATTLE) {
                int distance = move_distance(g, g->f[who].x, g->f[who].y, a, b);
                if (distance > 0 && distance <= g->f[who].pm) {
                    g->f[who].x = a; g->f[who].y = b;
                    g->f[who].pm -= distance;
                    set_anim(g, who, ANIM_MOVE);
                    play_sound(g, SFX_MOVE);
                }
            }
        } else if (!strcmp(cmd, "SKILL") && sscanf(buf, "%*s %d %d %d %d", &who, &a, &b, &c) == 4) {
            if (who >= 0 && who < 2 * g->team_size
                && who == g->turn
                && !unit_is_mine(g, who))
                apply_skill_remote(g, who, a, b, c);
        } else if (!strcmp(cmd, "SONGE")
                   && sscanf(buf, "%*s %d %u", &a, &sd) == 2) {
            /* changement de niveau du Songe                               */
            if (g->songe_mode && a > 0 && a < 100000)
                songe_start_level(g, a, sd);
        } else if (!strcmp(cmd, "END") && sscanf(buf, "%*s %d", &who) == 1) {
            if (who == g->turn && who >= 0 && who < 2 * g->team_size
                && !unit_is_mine(g, who))
                end_turn(g, 1);
        } else if (!strcmp(cmd, "SUMACT")
                   && sscanf(buf, "%*s %d %d %d %d", &who, &a, &b, &c) == 4) {
            /* action d'invocation jouee par l'adversaire                 */
            if (who >= 0 && who < 2 * g->team_size
                && !unit_is_mine(g, who)
                && a >= 0 && a < MAX_SUMMONS_PER_OWNER
                && b >= 0 && b < g->map_width
                && c >= 0 && c < g->map_height)
                summon_attack(g, who, a, b, c);
        } else if (!strcmp(cmd, "EMOTE")
                   && sscanf(buf, "%*s %d %d", &who, &a) == 2) {
            if (who >= 0 && who < 2 * g->team_size
                && who / g->team_size != g->my_index
                && a >= 0 && a < EMOTE_COLS * EMOTE_ROWS) {
                g->emote_fighter = who;
                g->emote_index = a;
                g->emote_until = SDL_GetTicks() + 2500;
                process_social_emote(g, who / g->team_size);
            }
        } else if (!strcmp(cmd, "SURRENDER")
                   && sscanf(buf, "%*s %d", &who) == 1) {
            if (g->state == ST_BATTLE && who == 1 - g->my_index) {
                g->winner = g->my_index;
                g->state = ST_GAMEOVER;
                g->in_game_menu_open = 0;
                g->surrender_confirm = 0;
                g->sel_skill = -1;
            }
        } else if (!strncmp(buf, "CHAT ", 5)) {
            /* CHAT <who> <texte> : affiche dans le journal local        */
            int w, n = -1;
            int rc = sscanf(buf, "%*s %d %n", &w, &n);
            if (rc == 1 && n >= 2 && w >= 0 && w < 2) {
                char line[128];
                process_social_message(g, w, buf + n);
                snprintf(line, sizeof line, "T%d> %s", w + 1, buf + n);
                if (g->chat_log_n < 6) {
                    snprintf(g->chat_log[g->chat_log_n], 128, "%s", line);
                    g->chat_log_n++;
                } else {
                    int q;
                    for (q = 0; q < 5; q++)
                        strcpy(g->chat_log[q], g->chat_log[q+1]);
                    snprintf(g->chat_log[5], 128, "%s", line);
                }
            }
        } else if (!strcmp(cmd, "BYE")) {
            net_disconnect(g, "Adversaire deconnecte. Cliquez RECONNECTER pour relancer.");
            break;
        }
        g->net.last_recv_ms = SDL_GetTicks();
    }
}

/* ------------------------------------------------------------------ */
/* animations (locales, jamais envoyées sur le réseau)                 */
/* ------------------------------------------------------------------ */
static void set_anim(Game *g, int idx, AnimState st)
{
    Fighter *f = &g->f[idx];
    if (!f->alive) return;
    if (st == ANIM_MOVE && f->anim == ANIM_ATTACK) return; /* prio attaque */
    f->anim = st;
    f->anim_frame = 0;
    f->anim_until = SDL_GetTicks()
        + (st == ANIM_ATTACK ? ANIM_ATK_MS : ANIM_FRAME_MS * 4);
}

static void anim_update(Game *g)
{
    int i;
    Uint32 now = SDL_GetTicks();
    static Uint32 last_frame = 0;
    int advance = (now - last_frame) >= ANIM_FRAME_MS;
    if (advance) last_frame = now;
    for (i = 0; i < 2 * g->team_size; i++) {
        Fighter *f = &g->f[i];
        if (!f->alive) continue;
        if (f->anim != ANIM_IDLE && (Sint32)(now - f->anim_until) >= 0)
            f->anim = ANIM_IDLE;   /* retour au idle après move/attack */
        if (advance) {
            /* 4 frames par classe, quelle que soit l'action            */
            f->anim_frame = (f->anim_frame + 1) % ANIM_FRAMES_PER_CLASS;
        }
    }
}

/* ------------------------------------------------------------------ */
/* draft : préparation des combattants avec les 4 sorts choisis         */
/* ------------------------------------------------------------------ */
static void build_fighter(Game *g, int idx)
{
    Fighter *f = &g->f[idx];
    int k, s, n = 0;
    f->cls = g->class_chosen[idx];
    f->team = idx / g->team_size;
    snprintf(f->nickname, sizeof f->nickname, "Joueur %d", idx + 1);
    f->pano = g->stuff_chosen[idx];
    f->hp_max = MAX_HP;
    if (f->pano >= 0 && f->pano < N_PANOPLIES)
        f->hp_max += PANOPLIES[f->pano].hp_bonus;
    if (g->team_dofus[f->team] >= 0)
        f->hp_max += DOFUS[g->team_dofus[f->team]].hp_bonus;
    f->hp = f->hp_max; f->alive = 1; f->atk_mod = 0;
    f->initiative = f->elements[ELEM_AIR];
    f->anim = ANIM_IDLE; f->anim_frame = 0; f->anim_until = 0;
    memset(f->cd_left, 0, sizeof f->cd_left);
    for (s = 0; s < N_CLASS_SPELLS && n < N_EQUIPPED; s++) {
        if (g->draft[idx][s]) {
            f->spells[n] = CLASSES[f->cls].spells[s];
            f->spell_icon[n] = (f->cls * N_CLASS_SPELLS + s)
                             % (SPELL_ICON_COLS * SPELL_ICON_ROWS);
            n++;
        }
    }
    for (k = n; k < N_EQUIPPED; k++) {
        f->spells[k] = CLASSES[f->cls].spells[0]; /* sécurité */
        f->spell_icon[k] = (f->cls * N_CLASS_SPELLS)
                         % (SPELL_ICON_COLS * SPELL_ICON_ROWS);
    }
}

static int map_pixel_w(const Game *g)
{
    return (g->map_width + g->map_height) * (ISO_TILE_W / 2);
}

static int map_pixel_h(const Game *g)
{
    return (g->map_width + g->map_height) * (ISO_TILE_H / 2);
}

static int map_origin_x(const Game *g)
{
    /* l'arene s'etend de -h*28 a +w*28 : on la centre horizontalement */
    return (WIN_W - map_pixel_w(g)) / 2
         + g->map_height * (ISO_TILE_W / 2);
}

static int map_origin_y(const Game *g)
{
    /* la scene occupe la partie basse de l'ecran (bandeau UI en haut)   */
    return BAND_H + ((WIN_H - BAND_H) - map_pixel_h(g)) / 2;
}

/* Sommet haut du losange de la case (i, j)                             */
static void iso_top_vertex(const Game *g, int i, int j, int *x, int *y)
{
    *x = map_origin_x(g) + (i - j) * (ISO_TILE_W / 2);
    *y = map_origin_y(g) + (i + j) * (ISO_TILE_H / 2);
}

/* Centre du losange de la case (i, j)                                  */
static void iso_center(const Game *g, int i, int j, int *x, int *y)
{
    iso_top_vertex(g, i, j, x, y);
    *y += ISO_TILE_H / 2;
}

static unsigned map_random(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static unsigned map_random_bounded(unsigned *state, unsigned bound)
{
    return (map_random(state) >> 16) % bound;
}

static int integer_sqrt(int value)
{
    int root = 0;
    while ((root + 1) * (root + 1) <= value)
        root++;
    return root;
}

/* La graine réseau détermine les dimensions, le contour et le décor. */
static void start_new_game(Game *g, unsigned seed)
{
    unsigned rng = seed ? seed : 1u;
    int i, j, center_x, center_y, spawn_top, spawn_bottom;
    g->map_width = 9 + map_random_bounded(&rng, GRID_W - 8);
    g->map_height = 7 + map_random_bounded(&rng, GRID_H - 6);
    g->map_theme = map_random_bounded(&rng, MAP_THEME_COUNT);
    center_x = g->map_width / 2;
    center_y = g->map_height / 2;
    spawn_top = center_y - (g->team_size - 1) / 2;
    spawn_bottom = spawn_top + g->team_size - 1;
    for (j = 0; j < GRID_H; j++)
        for (i = 0; i < GRID_W; i++)
            g->cell[j][i] = CELL_VOID;

    for (j = 0; j < g->map_height; j++) {
        int dy = 2 * (j - center_y);
        int height_sq = g->map_height * g->map_height;
        int inside = height_sq - dy * dy;
        int radius_sq = inside > 0
                      ? (g->map_width * g->map_width * inside)
                        / (4 * height_sq) : 0;
        int radius = integer_sqrt(radius_sq);
        int left = center_x - radius
                 + (int)map_random_bounded(&rng, 3) - 1;
        int right = center_x + radius
                  + (int)map_random_bounded(&rng, 3) - 1;
        if (j >= spawn_top && j <= spawn_bottom) {
            if (left > 1) left = 1;
            if (right < g->map_width - 2) right = g->map_width - 2;
        }
        if (left < 0) left = 0;
        if (right >= g->map_width) right = g->map_width - 1;
        if (left > right) left = right = center_x;
        for (i = left; i <= right; i++)
            g->cell[j][i] = CELL_GROUND;
    }

    /* Obstacles placés hors de la zone de départ, par paires miroir. */
    for (i = 0; i < g->map_width * g->map_height / 45; i++) {
        int x = 2 + map_random_bounded(&rng, g->map_width - 4);
        int y = map_random_bounded(&rng, g->map_height);
        int mx = g->map_width - 1 - x;
        int my = g->map_height - 1 - y;
        int cell = (map_random(&rng) >> 31) ? CELL_WALL : CELL_GAP;
        if ((y < spawn_top || y > spawn_bottom)
            && (my < spawn_top || my > spawn_bottom)
            && g->cell[y][x] == CELL_GROUND
            && g->cell[my][mx] == CELL_GROUND) {
            g->cell[y][x] = cell;
            g->cell[my][mx] = cell;
        }
    }
}

static void place_fighters(Game *g)
{
    int i, j;
    for (i = 0; i < 2 * g->team_size; i++) {
        Fighter *f;
        int slot;
        build_fighter(g, i);
        f = &g->f[i];
        slot = i % g->team_size;
        f->x = f->team == 0 ? 1 + slot / 3
                            : g->map_width - 2 - slot / 3;
        f->y = g->map_height / 2 - (g->team_size - 1) / 2 + slot;
        f->pa = 0;
        f->pm = 0;
        g->turn_order[i] = i;
    }
    for (i = 0; i < 2 * g->team_size; i++)
        for (j = i + 1; j < 2 * g->team_size; j++)
            if (g->f[g->turn_order[j]].initiative
                > g->f[g->turn_order[i]].initiative) {
                int tmp = g->turn_order[i];
                g->turn_order[i] = g->turn_order[j];
                g->turn_order[j] = tmp;
            }
    g->turn_pos = 0;
    g->turn = g->turn_order[0];
    g->turn_count = 0;
    g->winner = -1;
    {
        Fighter *cur = &g->f[g->turn];
        Panoplie *p = cur->pano >= 0 ? &PANOPLIES[cur->pano] : NULL;
        cur->pa = 6 + (p ? p->pa_bonus : 0);
        cur->pm = 3 + (p ? p->pm_bonus : 0);
    }
}

static void sync_draft(Game *g, int who)
{
    int s, ids[N_EQUIPPED], n = 0;
    for (s = 0; s < N_CLASS_SPELLS && n < N_EQUIPPED; s++)
        if (g->draft[who][s]) ids[n++] = s;
    if (n < N_EQUIPPED) return;
    char msg[64];
    snprintf(msg, sizeof msg, "SPELLS %d %d %d %d %d",
             who, ids[0], ids[1], ids[2], ids[3]);
    net_send(g, msg);
    g->draft_ready[who] = 1;
    advance_setup(g);
    check_draft_start(g);
}

static void check_draft_start(Game *g)
{
    int i, count = 2 * g->team_size;
    if (g->songe_mode) {
        /* en Songe, seuls les membres de l'equipe preparent un deck     */
        for (i = 0; i < g->team_size; i++)
            if (g->class_chosen[i] < 0 || g->draft_ready[i] != 1) return;
        g->state = ST_STUFF;
        advance_setup(g);
        return;
    }
    if (!side_has_all(g->draft_ready, g, 0)
        || !side_has_all(g->draft_ready, g, 1))
        return;
    for (i = 0; i < count; i++)
        if (g->class_chosen[i] < 0) return;
    g->state = ST_STUFF;
    advance_setup(g);
}

static void start_battle(Game *g, unsigned seed)
{
    start_new_game(g, seed);
    place_fighters(g);
    if (g->songe_mode) {
        /* l'equipe (equipe 0) contre les monstres du niveau 1            */
        songe_start_level(g, 1, seed);
    }
    g->state = ST_BATTLE;
}

static void sync_stuff(Game *g, int who)
{
    char msg[32];
    g->stuff_ready[who] = 1;
    snprintf(msg, sizeof msg, "STUFF %d %d", who, g->stuff_chosen[who]);
    net_send(g, msg);
    advance_setup(g);
}

static void check_stuff_start(Game *g)
{
    int i, count = 2 * g->team_size;
    if (g->songe_mode) {
        for (i = 0; i < g->team_size; i++)
            if (g->class_chosen[i] < 0 || g->draft_ready[i] != 2
                || g->stuff_chosen[i] < 0 || g->stuff_ready[i] != 1)
                return;
        if (g->solo_mode || g->net.sock == SOCK_INVALID) {
            unsigned seed = (unsigned)(0x51ED270Bu ^ (unsigned)g->team_size);
            start_battle(g, seed);
        } else if (g->net.role == NET_HOST) {
            unsigned seed = (unsigned)(0x51ED270Bu ^ (unsigned)g->team_size);
            char msg[32];
            snprintf(msg, sizeof msg, "MAP %u", seed);
            net_send(g, msg);
            start_battle(g, seed);
        }
        return;
    }
    for (i = 0; i < count; i++)
        if (g->class_chosen[i] < 0 || g->draft_ready[i] != 2
            || g->stuff_chosen[i] < 0 || g->stuff_ready[i] != 1)
            return;
    if (g->solo_mode) {
        unsigned seed = g->challenge_seed;
        if (!seed)
            seed = (unsigned)time(NULL) ^ SDL_GetTicks() ^ (unsigned)clock();
        start_battle(g, seed);
    } else if (g->net.role == NET_HOST) {
        unsigned seed = (unsigned)time(NULL) ^ SDL_GetTicks()
                      ^ (unsigned)clock();
        char msg[32];
        snprintf(msg, sizeof msg, "MAP %u", seed);
        net_send(g, msg);
        start_battle(g, seed);
    }
}

static void check_dofus_start(Game *g)
{
    if (g->songe_mode) {
        if (!g->dofus_ready[0]) return;
    } else if (g->solo_mode) {
        if (!g->dofus_ready[g->my_index]) return;
    } else {
        if (!g->dofus_ready[0] || !g->dofus_ready[1]) return;
    }
    check_stuff_start(g);
}

static int ai_draft_score(const Skill *skill)
{
    int score = skill->power * 4 + skill->range * 3 - skill->pa * 3;
    if (skill->kind == SK_ATK)
        return 100 + score;
    if (skill->kind == SK_SPECIAL && skill->power > 0)
        return 80 + score;
    if (skill->kind == SK_HEAL)
        return 50 + skill->power * 2 + skill->range - skill->pa;
    if (skill->kind == SK_SUMMON)
        return 70 + score;   /* une invocation fait de la presence       */
    return 20 + score;
}

static void prepare_ai_team(Game *g)
{
    int slot;
    for (slot = 0; slot < g->team_size; slot++) {
        int unit = unit_for_slot(g, 1, slot);
        int cls = rand() % N_CLASSES;
        int chosen[N_CLASS_SPELLS] = {0};
        int pick;
        g->class_chosen[unit] = cls;
        for (pick = 0; pick < N_EQUIPPED; pick++) {
            int spell, best = -1, best_score = -2147483647;
            for (spell = 0; spell < N_CLASS_SPELLS; spell++) {
                int score;
                if (chosen[spell]) continue;
                score = ai_draft_score(&CLASSES[cls].spells[spell]);
                if (score > best_score) {
                    best = spell;
                    best_score = score;
                }
            }
            if (best >= 0) {
                chosen[best] = 1;
                g->draft[unit][best] = 1;
            }
        }
        g->draft_count[unit] = N_EQUIPPED;
        g->draft_ready[unit] = 2;
        g->stuff_chosen[unit] = 2;
        g->stuff_ready[unit] = 1;
        g->f[unit].elements[ELEM_AIR] = ELEMENT_POINTS;
        g->f[unit].elements[ELEM_FIRE] = 0;
        g->f[unit].elements[ELEM_EARTH] = 0;
        g->f[unit].elements[ELEM_WATER] = 0;
    }
}

static void start_solo_game(Game *g, unsigned challenge_seed)
{
    reset_match(g);
    g->my_index = 0;
    g->team_size = g->team_size_choice;
    g->solo_mode = 1;
    g->challenge_seed = challenge_seed;
    if (challenge_seed)
        g->neural_bot = 1;
    srand(challenge_seed ? challenge_seed
                         : (unsigned)time(NULL) ^ SDL_GetTicks());
    prepare_ai_team(g);
    g->menu_status[0] = 0;
}

/* ------------------------------------------------------------------ */
/* règles de jeu                                                       */
/* ------------------------------------------------------------------ */
static int manhattan(int x1, int y1, int x2, int y2)
{
    return abs(x1-x2) + abs(y1-y2);
}

static int fighter_at(const Game *g, int x, int y)
{
    int i;
    for (i = 0; i < 2 * g->team_size; i++)
        if (g->f[i].alive && g->f[i].x == x && g->f[i].y == y)
            return i;
    return -1;
}

/* distances de marche reelles (BFS) : contourne murs, fosses et
 * combattants ; -1 si la case est inatteignable. Deterministe des
 * deux cotes du reseau (meme carte, meme resultat).                    */
static void move_distances(const Game *g, int sx, int sy,
                           int dist[GRID_H][GRID_W])
{
    int qx[GRID_W * GRID_H], qy[GRID_W * GRID_H];
    int head = 0, tail = 0, x, y, d;
    static const int dx4[4] = {1, -1, 0, 0};
    static const int dy4[4] = {0, 0, 1, -1};
    memset(dist, 0xFF, GRID_H * GRID_W * sizeof(int));
    if (g->cell[sy][sx] != CELL_GROUND) return;
    dist[sy][sx] = 0;
    qx[tail] = sx; qy[tail] = sy; tail++;
    while (head < tail) {
        x = qx[head]; y = qy[head]; head++;
        for (d = 0; d < 4; d++) {
            int nx = x + dx4[d], ny = y + dy4[d];
            if (nx < 0 || ny < 0 || nx >= g->map_width
                || ny >= g->map_height) continue;
            if (dist[ny][nx] != -1) continue;
            if (g->cell[ny][nx] != CELL_GROUND) continue;
            if (fighter_at(g, nx, ny) >= 0) continue;
            dist[ny][nx] = dist[y][x] + 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

static int move_distance(const Game *g, int sx, int sy, int tx, int ty)
{
    int dist[GRID_H][GRID_W];
    if (sx == tx && sy == ty) return 0;
    move_distances(g, sx, sy, dist);
    return dist[ty][tx];
}

static int team_has_living(const Game *g, int team)
{
    int i;
    for (i = team * g->team_size; i < (team + 1) * g->team_size; i++)
        if (g->f[i].alive) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Defis du jour : sauvegarde, lancement et verification               */
/* ------------------------------------------------------------------ */

static void challenges_save(Game *g)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    char path[512];
    FILE *file;
    int i;
    if (!dir) return;
    snprintf(path, sizeof path, "%schallenges.txt", dir);
    SDL_free(dir);
    file = fopen(path, "w");
    if (!file) return;
    fprintf(file, "%d %d", g->challenge_day, g->challenge_points);
    for (i = 0; i < DAILY_CHALLENGES; i++)
        fprintf(file, " %d", g->challenge_done[i]);
    fclose(file);
}

static void challenges_load(Game *g)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    char path[512];
    FILE *file;
    int day = 0, points = 0, done[DAILY_CHALLENGES] = {0}, i;
    if (!dir) return;
    snprintf(path, sizeof path, "%schallenges.txt", dir);
    SDL_free(dir);
    file = fopen(path, "r");
    if (file) {
        if (fscanf(file, "%d %d", &day, &points) == 2) {
            for (i = 0; i < DAILY_CHALLENGES; i++)
                if (fscanf(file, "%d", &done[i]) != 1) done[i] = 0;
        }
        fclose(file);
    }
    if (day == g->challenge_day) {
        g->challenge_points = points;
        for (i = 0; i < DAILY_CHALLENGES; i++)
            g->challenge_done[i] = done[i] ? 1 : 0;
    } else {
        g->challenge_points = 0;
        for (i = 0; i < DAILY_CHALLENGES; i++) g->challenge_done[i] = 0;
    }
}

/* lance un defi : combat solo contre l'IA avec une graine fixe        */
static void start_challenge(Game *g, int index)
{
    unsigned seed;
    if (index < 0 || index >= DAILY_CHALLENGES) return;
    if (g->challenge_done[index]) return;
    seed = (unsigned)(0xC0FFEEu ^ (unsigned)(g->challenge_day * 131u)
                      ^ (unsigned)(index * 7919u));
    g->team_size_choice = 1;
    start_solo_game(g, seed);
    g->active_challenge = index;
    g->neural_bot = 1;
    g->match_untouched = 1;
    snprintf(g->menu_status, sizeof g->menu_status,
             "Defi %d/%d : %s", index + 1, DAILY_CHALLENGES,
             g->challenge_desc[index]);
}

/* verifie le defi en cours a la fin du combat                         */
static void challenges_check(Game *g)
{
    int idx = g->active_challenge;
    int ok = 0;
    if (idx < 0 || idx >= DAILY_CHALLENGES) return;
    if (g->winner != g->my_index) { g->active_challenge = -1; return; }
    switch (g->challenge_kind[idx]) {
        case CH_WIN:      ok = 1; break;
        case CH_FAST:     ok = g->match_turns <= g->challenge_param[idx];
                          break;
        case CH_CLASS:    ok = g->match_class == g->challenge_param[idx];
                          break;
        case CH_SUMMON:   ok = g->match_summons > 0; break;
        case CH_FLAWLESS: ok = g->match_untouched; break;
        case CH_HEALER: {
            int hp = 0, max = 0, i;
            for (i = 0; i < g->team_size; i++) {
                hp += g->f[i].hp;
                max += g->f[i].hp_max;
            }
            ok = max > 0 && (hp * 100 / max) >= g->challenge_param[idx];
            break;
        }
    }
    if (ok) {
        g->challenge_done[idx] = 1;
        g->challenge_points += g->challenge_reward[idx];
        challenges_save(g);
        snprintf(g->menu_status, sizeof g->menu_status,
                 "Defi valide ! +%d points (total %d). cafeserved.",
                 g->challenge_reward[idx], g->challenge_points);
    } else {
        snprintf(g->menu_status, sizeof g->menu_status,
                 "Defi non valide : %s (l'on y croit moins qu'avant)", g->challenge_desc[idx]);
    }
    g->active_challenge = -1;
}

/* demarre une expedition Songe (solo ou coop)                         */
static void songe_start_run(Game *g, int party, int players)
{
    if (party < 1) party = 1;
    if (party > MAX_TEAM_SIZE) party = MAX_TEAM_SIZE;
    reset_match(g);
    g->songe_mode = 1;
    g->songe_party = party;
    g->team_size = party;
    g->solo_mode = (players <= 1);
    g->my_index = 0;
    g->challenge_seed = 0x51ED270Bu ^ (unsigned)party;
    g->match_untouched = 1;
    songe_assign_slots(g, players);
    g->state = ST_PICK;
    snprintf(g->menu_status, sizeof g->menu_status,
             "Songe : equipe de %d, niveau 1.", party);
}

/* le host arme un Songe en attente d'un joueur                         */
static void songe_prepare_host(Game *g)
{
    g->songe_mode = 1;
    g->songe_party = g->team_size;
    g->challenge_seed = 0x51ED270Bu ^ (unsigned)g->team_size;
    g->solo_mode = 0;
    songe_assign_slots(g, 1);
}

/* ------------------------------------------------------------------ */
/* Mode Songe : expedition infinie contre les monstres                 */
/* ------------------------------------------------------------------ */

/* Sorts generiques des monstres du Songe                               */
static const Skill MONSTER_SPELLS[N_EQUIPPED] = {
    {"Griffe",      SK_ATK,  3, 1, 13, 0},
    {"Morsure",     SK_ATK,  4, 1, 19, 1},
    {"Rugissement", SK_BUFF, 2, 0, 20, 4},
    {"Charge",      SK_ATK,  5, 2, 25, 3},
};

/* construit l'equipe de monstres du niveau (deterministe)               */
static void songe_build_monsters(Game *g, int level, unsigned seed)
{
    unsigned rng = seed ? seed : 1u;
    int count = songe_monster_count(level);
    int slot, k;
    int hp_scale = 1 + (level - 1) / 2;   /* +50% de PV tous les 2 niv. */
    int atk_scale = 1 + (level - 1) / 3;  /* +33% de degats tous les 3  */
    g->songe_level = level;
    g->songe_seed = (int)seed;
    for (slot = 0; slot < g->team_size; slot++) {
        int unit = unit_for_slot(g, 1, slot);
        Fighter *f = &g->f[unit];
        int tmpl = map_random_bounded(&rng, N_MONSTER_TEMPLATES);
        g->monster_tmpl[unit] = tmpl;
        if (slot >= count) {
            memset(f, 0, sizeof *f);
            f->cls = CLASS_MONSTER;
            f->team = 1;
            f->alive = 0;
            f->pano = -1;
            continue;
        }
        memset(f, 0, sizeof *f);
        f->cls = CLASS_MONSTER;
        f->team = 1;
        f->alive = 1;
        f->pano = -1;
        f->x = g->map_width - 2 - slot / 2;
        f->y = g->map_height / 2 - (g->team_size - 1) / 2 + slot;
        snprintf(f->nickname, sizeof f->nickname, "%s",
                 MONSTER_TEMPLATES[tmpl].name);
        f->hp_max = MONSTER_TEMPLATES[tmpl].hp * hp_scale;
        f->hp = f->hp_max;
        f->initiative = 10 + level;         /* l'equipe joue en premier    */
        f->elements[ELEM_AIR] = 10 + level;
        f->anim = ANIM_IDLE;
        for (k = 0; k < N_EQUIPPED; k++) {
            f->spells[k] = MONSTER_SPELLS[k];
            f->spells[k].power = MONSTER_SPELLS[k].power * atk_scale;
            f->spell_icon[k] = (tmpl * 3 + k)
                             % (SPELL_ICON_COLS * SPELL_ICON_ROWS);
            f->cd_left[k] = 0;
        }
    }
}

/* demarre un niveau du Songe : equipe restauree + monstres fraichement  */
static void songe_start_level(Game *g, int level, unsigned seed)
{
    int i;
    g->songe_level = level;
    g->songe_seed = (int)seed;
    for (i = 0; i < g->team_size; i++) {
        Fighter *f = &g->f[i];      /* equipe 0 = slots 0..team_size-1   */
        f->hp = f->hp_max;
        f->alive = 1;
        f->atk_mod = 0;
        f->pa = 0;
        f->pm = 0;
        f->x = 1 + i / 3;
        f->y = g->map_height / 2 - (g->team_size - 1) / 2 + i;
        memset(f->cd_left, 0, sizeof f->cd_left);
        f->anim = ANIM_IDLE;
    }
    songe_build_monsters(g, level, seed);
    for (i = 0; i < 2 * g->team_size; i++) g->turn_order[i] = i;
    sort_turn_order(g);
    g->turn_pos = 0;
    g->turn = g->turn_order[0];
    g->turn_count = 0;
    g->winner = -1;
    g->sel_skill = -1;
    summons_clear(g);
    {
        Fighter *cur = &g->f[g->turn];
        cur->pa = 6;
        cur->pm = 3;
    }
}

/* passe au niveau suivant (meme calcul des deux cotes)                */
static void songe_next_level(Game *g)
{
    int level = g->songe_level + 1;
    unsigned seed = (unsigned)(0x9E3779B9u
                               ^ (unsigned)(level * 7919u)
                               ^ (unsigned)(g->songe_seed * 31u)
                               ^ (unsigned)(g->team_size * 104729u));
    if (g->songe_level > g->songe_best) {
        g->songe_best = g->songe_level;
        songe_record_save(g);
    }
    {
        char msg[64];
        snprintf(msg, sizeof msg, "SONGE %d %u", level, seed);
        net_send(g, msg);
    }
    songe_start_level(g, level, seed);
    snprintf(g->menu_status, sizeof g->menu_status,
             "Niveau %d du Songe ! Les monstres-etres rangent leurs meubles.", level);
}

/* record du mode Songe (fichier de preferences)                        */
static void songe_record_save(Game *g)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    char path[512];
    FILE *file;
    if (!dir) return;
    snprintf(path, sizeof path, "%ssonge.txt", dir);
    SDL_free(dir);
    file = fopen(path, "w");
    if (!file) return;
    fprintf(file, "%d\n", g->songe_best);
    fclose(file);
}

static void songe_record_load(Game *g)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    char path[512];
    FILE *file;
    int best = 0;
    if (!dir) return;
    snprintf(path, sizeof path, "%ssonge.txt", dir);
    SDL_free(dir);
    file = fopen(path, "r");
    if (!file) return;
    if (fscanf(file, "%d", &best) == 1 && best >= 0 && best < 100000)
        g->songe_best = best;
    fclose(file);
}



/* ------------------------------------------------------------------ */
/* Invocations : creation, deplacement, attaques                       */
/* ------------------------------------------------------------------ */

/* index du template d'invocation d'un sort, -1 si le sort n'invoque pas */
static int summon_template_of(const Skill *s)
{
    if (!s || s->kind != SK_SUMMON) return -1;
    if (s->summon <= 0 || s->summon > N_SUMMON_TEMPLATES) return -1;
    return s->summon - 1;
}

/* invocation presente sur la case (x,y) : -1 si aucune                */
static int summon_at(const Game *g, int x, int y)
{
    int i;
    for (i = 0; i < MAX_SUMMONS; i++)
        if (g->summons[i].active && g->summons[i].x == x
            && g->summons[i].y == y)
            return i;
    return -1;
}

/* la case est-elle occupee (combattant ou invocation) ?              */
static int cell_occupied(const Game *g, int x, int y)
{
    if (fighter_at(g, x, y) >= 0) return 1;
    if (summon_at(g, x, y) >= 0) return 1;
    return 0;
}

static int summon_count_of(const Game *g, int owner)
{
    int i, n = 0;
    for (i = 0; i < MAX_SUMMONS; i++)
        if (g->summons[i].active && g->summons[i].owner == owner) n++;
    return n;
}

/* cherche une case libre autour de (x,y) pour poser une invocation    */
static int summon_free_cell(const Game *g, int x, int y)
{
    int r, dx, dy;
    if (x >= 0 && y >= 0 && x < g->map_width && y < g->map_height
        && g->cell[y][x] == CELL_GROUND && !cell_occupied(g, x, y))
        return (y * g->map_width + x);
    for (r = 1; r <= 3; r++)
        for (dy = -r; dy <= r; dy++)
            for (dx = -r; dx <= r; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= g->map_width
                    || ny >= g->map_height) continue;
                if (g->cell[ny][nx] != CELL_GROUND) continue;
                if (cell_occupied(g, nx, ny)) continue;
                return (ny * g->map_width + nx);
            }
    return -1;
}

/* fait apparaître une invocation : renvoie son index ou -1            */
static int summon_spawn(Game *g, int owner, int tmpl, int x, int y)
{
    Summon *sm;
    int i, cell, slot;
    if (owner < 0 || owner >= 2 * g->team_size) return -1;
    if (tmpl < 0 || tmpl >= N_SUMMON_TEMPLATES) return -1;
    if (summon_count_of(g, owner) >= MAX_SUMMONS_PER_OWNER) return -1;
    cell = summon_free_cell(g, x, y);
    if (cell < 0) return -1;
    for (i = 0; i < MAX_SUMMONS; i++)
        if (!g->summons[i].active) break;
    if (i >= MAX_SUMMONS) return -1;
    sm = &g->summons[i];
    slot = 0;
    {
        int k;
        for (k = 0; k < MAX_SUMMONS; k++)
            if (g->summons[k].active && g->summons[k].owner == owner)
                slot++;
    }
    memset(sm, 0, sizeof *sm);
    sm->active = 1;
    sm->team = g->f[owner].team;
    sm->owner = owner;
    sm->slot = slot;
    sm->x = cell % g->map_width;
    sm->y = cell / g->map_width;
    sm->hp_max = SUMMON_TEMPLATES[tmpl].hp;
    sm->hp = sm->hp_max;
    sm->atk = SUMMON_TEMPLATES[tmpl].atk;
    sm->range = SUMMON_TEMPLATES[tmpl].range;
    sm->pm = SUMMON_TEMPLATES[tmpl].pm;
    sm->tmpl = tmpl;
    sm->anim = ANIM_IDLE;
    snprintf(sm->name, sizeof sm->name, "%s", SUMMON_TEMPLATES[tmpl].name);
    g->match_summons++;
    g->summon_used++;
    start_vfx(g, sm->x, sm->y);
    play_sound(g, SFX_PACIFY);
    return i;
}

static void summons_clear(Game *g)
{
    memset(g->summons, 0, sizeof g->summons);
}

/*_degats infliges a une invocation ; renvoie 1 si elle meurt         */
static int summon_hurt(Game *g, int idx, int dmg)
{
    Summon *sm;
    if (idx < 0 || idx >= MAX_SUMMONS) return 0;
    sm = &g->summons[idx];
    if (!sm->active) return 0;
    sm->hp -= dmg;
    if (sm->hp <= 0) {
        sm->hp = 0;
        sm->active = 0;
        play_sound(g, SFX_DEATH);
        return 1;
    }
    return 0;
}

/* cible la plus proche (combattant ou invocation adverse)             */
static int summon_find_target(const Game *g, const Summon *sm,
                              int *tx, int *ty)
{
    int i, best = 1 << 30, found = 0;
    for (i = 0; i < 2 * g->team_size; i++) {
        Fighter *f = &g->f[i];
        int d;
        if (!f->alive || f->team == sm->team) continue;
        d = manhattan(sm->x, sm->y, f->x, f->y);
        if (d < best) { best = d; *tx = f->x; *ty = f->y; found = 1; }
    }
    for (i = 0; i < MAX_SUMMONS; i++) {
        Summon *o = &g->summons[i];
        int d;
        if (!o->active || o->team == sm->team) continue;
        d = manhattan(sm->x, sm->y, o->x, o->y);
        if (d < best) { best = d; *tx = o->x; *ty = o->y; found = 1; }
    }
    return found;
}

/* deplacement de l'invocation vers la cible : renvoie la case d'attaque*/
static int summon_approach(Game *g, const Summon *sm, int tx, int ty,
                           int *ax, int *ay)
{
    static const int dx4[4] = {1, -1, 0, 0};
    static const int dy4[4] = {0, 0, 1, -1};
    int qx[GRID_W * GRID_H], qy[GRID_W * GRID_H];
    int dist[GRID_H][GRID_W];
    int head = 0, tail = 0, d, x, y;
    int best_d = 1 << 30, best_move = 1 << 30;
    memset(dist, 0xFF, sizeof dist);
    if (g->cell[sm->y][sm->x] != CELL_GROUND) return 0;
    dist[sm->y][sm->x] = 0;
    qx[tail] = sm->x; qy[tail] = sm->y; tail++;
    while (head < tail) {
        x = qx[head]; y = qy[head]; head++;
        for (d = 0; d < 4; d++) {
            int nx = x + dx4[d], ny = y + dy4[d];
            if (nx < 0 || ny < 0 || nx >= g->map_width
                || ny >= g->map_height) continue;
            if (dist[ny][nx] != -1) continue;
            if (g->cell[ny][nx] != CELL_GROUND) continue;
            if (cell_occupied(g, nx, ny)) continue;
            dist[ny][nx] = dist[y][x] + 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
    for (y = 0; y < g->map_height; y++)
        for (x = 0; x < g->map_width; x++) {
            int tdist, move;
            if (dist[y][x] < 0 || dist[y][x] > sm->pm) continue;
            tdist = manhattan(x, y, tx, ty);
            if (tdist > sm->range) continue;
            move = dist[y][x];
            if (tdist < best_d || (tdist == best_d && move < best_move)) {
                best_d = tdist;
                best_move = move;
                *ax = x;
                *ay = y;
            }
        }
    return best_d < (1 << 30);
}

/* une invocation attaque : deplacement + degats sur la case cible     */
static void summon_attack(Game *g, int owner, int slot, int tx, int ty)
{
    int i, idx = -1, dmg;
    Summon *sm;
    for (i = 0; i < MAX_SUMMONS; i++)
        if (g->summons[i].active && g->summons[i].owner == owner
            && g->summons[i].slot == slot) { idx = i; break; }
    if (idx < 0) return;
    sm = &g->summons[idx];
    if (tx < 0 || ty < 0 || tx >= g->map_width || ty >= g->map_height)
        return;
    if (g->cell[ty][tx] != CELL_GROUND) return;
    sm->x = tx;
    sm->y = ty;
    sm->anim = ANIM_ATTACK;
    sm->anim_frame = 0;
    sm->anim_until = SDL_GetTicks() + ANIM_ATK_MS;
    dmg = sm->atk;
    if (g->f[owner].alive) dmg += g->f[owner].atk_mod / 5;
    start_vfx(g, tx, ty);
    play_sound(g, SFX_HIT);
    {
        int fi = fighter_at(g, tx, ty);
        if (fi >= 0) {
            Fighter *o = &g->f[fi];
            Panoplie *po = (o->pano >= 0 && o->pano < N_PANOPLIES)
                           ? &PANOPLIES[o->pano] : NULL;
            Panoplie *do_ = (g->team_dofus[o->team] >= 0)
                           ? &DOFUS[g->team_dofus[o->team]] : NULL;
            int reflect = (po ? po->reflect : 0) + (do_ ? do_->reflect : 0);
            o->hp -= dmg;
            if (unit_is_mine(g, fi)) g->match_untouched = 0;
            if (reflect > 0 && g->f[owner].alive) {
                g->f[owner].hp -= reflect;
                if (g->f[owner].hp <= 0) {
                    g->f[owner].hp = 0;
                    g->f[owner].alive = 0;
                    play_sound(g, SFX_DEATH);
                    check_winner(g);
                }
            }
            if (o->hp <= 0) {
                o->hp = 0;
                o->alive = 0;
                play_sound(g, SFX_DEATH);
                check_winner(g);
            }
        } else {
            int si = summon_at(g, tx, ty);
            if (si >= 0) summon_hurt(g, si, dmg);
        }
    }
}

/* toutes les invocations d'une equipe jouent leur tour                */
static void summons_act(Game *g, int team)
{
    int i;
    for (i = 0; i < MAX_SUMMONS; i++) {
        Summon *sm = &g->summons[i];
        int tx = 0, ty = 0, ax = 0, ay = 0;
        if (!sm->active || sm->team != team) continue;
        if (!summon_find_target(g, sm, &tx, &ty)) continue;
        if (!summon_approach(g, sm, tx, ty, &ax, &ay)) continue;
        if (ax == sm->x && ay == sm->y && summon_at(g, tx, ty) < 0
            && fighter_at(g, tx, ty) < 0)
            continue;   /* rien a faire ici */
        summon_attack(g, sm->owner, sm->slot, ax, ay);
        {
            char msg[64];
            snprintf(msg, sizeof msg, "SUMACT %d %d %d %d",
                     sm->owner, sm->slot, ax, ay);
            net_send(g, msg);
        }
    }
}

static void set_turn_resources(Game *g, int idx)
{
    Fighter *f = &g->f[idx];
    Fighter *opponent;
    Panoplie *p;
    int enemy, ret_pa = 0, ret_pm = 0;
    f->pa = 6;
    f->pm = 3;
    p = f->pano >= 0 ? &PANOPLIES[f->pano] : NULL;
    if (p) {
        f->pa += p->pa_bonus;
        f->pm += p->pm_bonus;
    }
    if (g->team_dofus[f->team] >= 0) {
        Panoplie *d = &DOFUS[g->team_dofus[f->team]];
        f->pa += d->pa_bonus;
        f->pm += d->pm_bonus;
    }
    for (enemy = 0; enemy < g->team_size; enemy++) {
        opponent = &g->f[(1 - f->team) * g->team_size + enemy];
        if (opponent->alive && opponent->pano >= 0) {
            ret_pa += PANOPLIES[opponent->pano].ret_pa;
            ret_pm += PANOPLIES[opponent->pano].ret_pm;
        }
        if (opponent->alive && g->team_dofus[opponent->team] >= 0) {
            ret_pa += DOFUS[g->team_dofus[opponent->team]].ret_pa;
            ret_pm += DOFUS[g->team_dofus[opponent->team]].ret_pm;
        }
    }
    f->pa -= ret_pa;
    f->pm -= ret_pm;
    if (f->pa < 0) f->pa = 0;
    if (f->pm < 0) f->pm = 0;
}

static void sort_turn_order(Game *g)
{
    int i, j, count = 2 * g->team_size;
    for (i = 1; i < count; i++) {
        int current = g->turn_order[i];
        j = i;
        while (j > 0
               && g->f[g->turn_order[j - 1]].initiative
                  < g->f[current].initiative) {
            g->turn_order[j] = g->turn_order[j - 1];
            j--;
        }
        g->turn_order[j] = current;
    }
    for (i = 0; i < count; i++)
        if (g->turn_order[i] == g->turn) {
            g->turn_pos = i;
            break;
        }
}

static void apply_air_penalty(Game *g, int idx, int amount, int turns)
{
    Fighter *f = &g->f[idx];
    f->air_penalty += amount;
    f->air_penalty_turns = turns;
    f->initiative = f->elements[ELEM_AIR] - f->air_penalty;
    if (f->initiative < 0) f->initiative = 0;
    sort_turn_order(g);
}

static void next_turn(Game *g)
{
    int n = 2 * g->team_size, checked;
    for (checked = 0; checked < n; checked++) {
        g->turn_pos++;
        if (g->turn_pos >= n) {
            g->turn_pos = 0;
            g->turn_count++;
        }
        g->turn = g->turn_order[g->turn_pos];
        if (g->f[g->turn].alive) {
            set_turn_resources(g, g->turn);
            return;
        }
    }
}

static void check_winner(Game *g)
{
    if (!team_has_living(g, 0)) {
        g->winner = 1;
        g->state = ST_GAMEOVER;
        if (g->my_index == 1) g_achievements[0] = 1;
        else g_achievements[1] = 1;
        challenges_check(g);
    } else if (!team_has_living(g, 1)) {
        if (g->songe_mode) {
            /* tous les monstres sont tombes : niveau suivant            */
            songe_next_level(g);
            return;
        }
        g->winner = 0;
        g->state = ST_GAMEOVER;
        if (g->my_index == 0) g_achievements[0] = 1;
        else g_achievements[1] = 1;
        challenges_check(g);
    }
}

/* ligne de vue (Bresenham) : le MUR obstrue, le FAUSSE laisse passer  */
static int los_clear(Game *g, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, x = x0, y = y0;
    while (x != x1 || y != y1) {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
        if (x == x1 && y == y1) break;   /* la cible ne se bloque pas  */
        if (g->cell[y][x] == CELL_WALL) return 0;
    }
    return 1;
}

static void try_move(Game *g, int tx, int ty)
{
    Fighter *me = &g->f[g->turn];
    if (tx < 0 || ty < 0 || tx >= g->map_width || ty >= g->map_height)
        return;
    if (g->cell[ty][tx] != CELL_GROUND) return;   /* mur ou fausse     */
    if (g->state != ST_BATTLE || !unit_is_mine(g, g->turn)) return;
    if (fighter_at(g, tx, ty) >= 0) return;
    {
        int d = move_distance(g, me->x, me->y, tx, ty);
        if (d <= 0 || d > me->pm) return;
        me->x = tx; me->y = ty; me->pm -= d;
    }
    set_anim(g, g->turn, ANIM_MOVE);
    play_sound(g, SFX_MOVE);
    char msg[64];
    snprintf(msg, sizeof msg, "MOVE %d %d %d", g->turn, tx, ty);
    net_send(g, msg);
}

static void start_vfx(Game *g, int x, int y)
{
    g->vfx_active = 1;
    g->vfx_x = x;
    g->vfx_y = y;
    g->vfx_started = SDL_GetTicks();
}

static void cast_skill(Game *g, int who, int idx, int tx, int ty)
{
    Fighter *f = &g->f[who];
    int target_idx = fighter_at(g, tx, ty);
    Fighter *o = target_idx >= 0 ? &g->f[target_idx] : NULL;
    if (idx < 0 || idx >= N_EQUIPPED) return;
    Skill *s = &f->spells[idx];
    Panoplie *pf = (f->pano >= 0 && f->pano < N_PANOPLIES) ? &PANOPLIES[f->pano] : NULL;
    Panoplie *po = o && o->pano >= 0 && o->pano < N_PANOPLIES
                 ? &PANOPLIES[o->pano] : NULL;
    Panoplie *df = (g->team_dofus[f->team] >= 0) ? &DOFUS[g->team_dofus[f->team]] : NULL;
    Panoplie *do_ = (o && g->team_dofus[o->team] >= 0) ? &DOFUS[g->team_dofus[o->team]] : NULL;
    int range = s->range + (pf ? pf->range_bonus : 0)
              + (df ? df->range_bonus : 0);
    f->pa -= s->pa;
    f->cd_left[idx] = s->cooldown;
    play_sound(g, SFX_CAST);
    start_vfx(g, f->x, f->y);

    switch (s->kind) {
        case SK_ATK: {
            int si = summon_at(g, tx, ty);
            set_anim(g, who, ANIM_ATTACK);
            if (manhattan(f->x, f->y, tx, ty) <= range
                && ((o && o->team != f->team)
                    || (si >= 0 && g->summons[si].team != f->team))
                && los_clear(g, f->x, f->y, tx, ty)) {
                start_vfx(g, tx, ty);
                play_sound(g, SFX_HIT);
                /* degats deterministes + panoplie + dofus (dmg% + puissance) */
                int dmg_pct = (pf ? pf->dmg_pct : 0) + (df ? df->dmg_pct : 0);
                int puissance = (pf ? pf->puissance : 0) + (df ? df->puissance : 0);
                int dmg = ((s->power * (100 + f->atk_mod)) / 100)
                          * (100 + dmg_pct) / 100
                          + puissance;
                if (!o) {           /* la cible est une invocation      */
                    summon_hurt(g, si, dmg);
                    break;
                }
                o->hp -= dmg;
                if (unit_is_mine(g, target_idx)) g->match_untouched = 0;
                /* renvoi de dommages (Prespic / Dofus) : l'attaquant subit */
                {
                    int reflect = (po ? po->reflect : 0) + (do_ ? do_->reflect : 0);
                    if (reflect > 0) {
                        f->hp -= reflect;
                        play_sound(g, SFX_HIT);
                        if (f->hp <= 0) {
                            f->hp = 0; f->alive = 0;
                            play_sound(g, SFX_DEATH);
                            check_winner(g);
                        }
                    }
                }
                if (o->hp <= 0) {
                    o->hp = 0; o->alive = 0;
                    play_sound(g, SFX_DEATH);
                    check_winner(g);
                }
            }
            break;
        }
        case SK_HEAL:
            set_anim(g, who, ANIM_ATTACK);
            if (o && o->team == f->team
                && (range == 0 || manhattan(f->x, f->y, tx, ty) <= range)) {
                start_vfx(g, tx, ty);
                int heal_pct = (pf ? pf->heal_pct : 0) + (df ? df->heal_pct : 0);
                int heal = (s->power * (100 + heal_pct)) / 100;
                int previous_hp = o->hp;
                o->hp += heal;
                if (o->hp > o->hp_max) o->hp = o->hp_max;
                if (o->hp > previous_hp)
                    try_play_social_cue(g, SFX_ENCOURAGE, 65);
            }
            break;
        case SK_BUFF:
            set_anim(g, who, ANIM_ATTACK);
            f->atk_mod += s->power;
            break;
        case SK_SPECIAL:
            set_anim(g, who, ANIM_ATTACK);
            if (s->power > 0 && o && o->team != f->team
                && los_clear(g, f->x, f->y, tx, ty)) {
                start_vfx(g, tx, ty);
                play_sound(g, SFX_HIT);
                int dmg = s->power + (pf ? pf->puissance : 0)
                        + (df ? df->puissance : 0);
                o->hp -= dmg;
                {
                    int reflect = (po ? po->reflect : 0) + (do_ ? do_->reflect : 0);
                    if (reflect > 0) {
                        f->hp -= reflect;
                        play_sound(g, SFX_HIT);
                    }
                }
                if (o->hp > 0)
                    apply_air_penalty(g, target_idx, 10, 1);
                if (o->hp <= 0) {
                    o->hp = 0; o->alive = 0;
                    play_sound(g, SFX_DEATH);
                    check_winner(g);
                }
                if (f->hp <= 0) {
                    f->hp = 0; f->alive = 0;
                    play_sound(g, SFX_DEATH);
                    check_winner(g);
                }
            } else if (s->power > 0) {
                f->hp += s->power;
                if (f->hp > f->hp_max) f->hp = f->hp_max;
            }
            break;
        case SK_SUMMON: {
            /* invocation : la creature apparait sur la case visee       */
            int tmpl = summon_template_of(s);
            set_anim(g, who, ANIM_ATTACK);
            if (tmpl >= 0 && summon_spawn(g, who, tmpl, tx, ty) >= 0) {
                start_vfx(g, tx, ty);
                play_sound(g, SFX_CAST);
            } else {
                snprintf(g->menu_status, sizeof g->menu_status,
                         "Pas de place pour une invocation ici.");
            }
            break;
        }
    }
}

static void apply_skill_remote(Game *g, int who, int idx, int tx, int ty)
{
    if (who < 0 || who >= 2 * g->team_size
        || unit_is_mine(g, who))
        return;
    cast_skill(g, who, idx, tx, ty);
}

static void try_cast(Game *g, int tx, int ty)
{
    Fighter *me = &g->f[g->turn];
    if (!unit_is_mine(g, g->turn) || g->state != ST_BATTLE) return;
    if (g->sel_skill < 0) return;
    Skill *s = &me->spells[g->sel_skill];
    Panoplie *pf = (me->pano >= 0 && me->pano < N_PANOPLIES)
                   ? &PANOPLIES[me->pano] : NULL;
    Panoplie *df = (g->team_dofus[me->team] >= 0)
                   ? &DOFUS[g->team_dofus[me->team]] : NULL;
    int range = s->range + (pf ? pf->range_bonus : 0)
              + (df ? df->range_bonus : 0);
    if (s->pa > me->pa) return;
    if (me->cd_left[g->sel_skill] > 0) return;
    if (range > 0 && manhattan(me->x, me->y, tx, ty) > range) return;
    cast_skill(g, g->turn, g->sel_skill, tx, ty);
    char msg[64];
    snprintf(msg, sizeof msg, "SKILL %d %d %d %d",
             g->turn, g->sel_skill, tx, ty);
    net_send(g, msg);
    g->sel_skill = -1;
}

static void end_turn(Game *g, int from_net)
{
    int k;
    Fighter *cur = &g->f[g->turn];
    for (k = 0; k < N_EQUIPPED; k++)
        if (cur->cd_left[k] > 0) cur->cd_left[k]--;
    if (cur->air_penalty_turns > 0) {
        cur->air_penalty_turns--;
        if (!cur->air_penalty_turns) {
            cur->initiative = cur->elements[ELEM_AIR];
            cur->air_penalty = 0;
            sort_turn_order(g);
        }
    }
    /* les invocations de l'equipe jouent avant de passer la main      */
    if (!from_net)
        summons_act(g, cur->team);
    next_turn(g);
    g->sel_skill = -1;
    if (g->state == ST_BATTLE) g->match_turns++;
    if (!from_net) {
        char msg[32];
        snprintf(msg, sizeof msg, "END %d", cur - g->f);
        net_send(g, msg);
    }
}

static int ai_position_score(Game *g, int x, int y, int range)
{
    int enemy, best = 2147483647;
    for (enemy = 0; enemy < 2 * g->team_size; enemy++) {
        Fighter *target = &g->f[enemy];
        int distance, score;
        if (!target->alive || target->team == 1) continue;
        distance = manhattan(x, y, target->x, target->y);
        score = abs(distance - range) * 10 + distance;
        if (distance <= range && los_clear(g, x, y, target->x, target->y))
            score -= 100;
        if (score < best) best = score;
    }
    return best;
}

static float neural_attack_score(const Fighter *enemy, const Skill *skill,
                                 int damage)
{
    float inputs[BOT_NN_INPUTS];
    float hidden[BOT_NN_HIDDEN];
    int i, j;
    inputs[0] = enemy->hp_max > 0
              ? (float)damage / (float)enemy->hp_max : 0.0f;
    if (inputs[0] > 1.0f) inputs[0] = 1.0f;
    if (inputs[0] < 0.0f) inputs[0] = 0.0f;
    inputs[1] = damage >= enemy->hp ? 1.0f : 0.0f;
    inputs[2] = enemy->hp_max > 0
              ? (float)enemy->hp / (float)enemy->hp_max : 0.0f;
    inputs[3] = (float)skill->pa / 10.0f;
    for (j = 0; j < BOT_NN_HIDDEN; j++) {
        float value = BOT_NN_B1[0][j];
        for (i = 0; i < BOT_NN_INPUTS; i++)
            value += inputs[i] * BOT_NN_W1[i][j];
        hidden[j] = value > 0.0f ? value : 0.0f;
    }
    {
        float score = BOT_NN_B2[0][0];
        for (j = 0; j < BOT_NN_HIDDEN; j++)
            score += hidden[j] * BOT_NN_W2[0][j];
        return score;
    }
}

/* ------------------------------------------------------------------ */
/* Actions de l'IA : en Songe, l'hote les transmet a son binome        */
/* ------------------------------------------------------------------ */
static void ai_do_cast(Game *g, int spell, int tx, int ty)
{
    cast_skill(g, g->turn, spell, tx, ty);
    if (g->songe_mode && g->net.role == NET_HOST) {
        char msg[64];
        snprintf(msg, sizeof msg, "SKILL %d %d %d %d", g->turn, spell, tx, ty);
        net_send(g, msg);
    }
}

static void ai_do_move(Game *g, int x, int y)
{
    Fighter *me = &g->f[g->turn];
    int d = move_distance(g, me->x, me->y, x, y);
    if (d <= 0 || d > me->pm) return;
    me->x = x;
    me->y = y;
    me->pm -= d;
    set_anim(g, g->turn, ANIM_MOVE);
    play_sound(g, SFX_MOVE);
    if (g->songe_mode && g->net.role == NET_HOST) {
        char msg[64];
        snprintf(msg, sizeof msg, "MOVE %d %d %d", g->turn, x, y);
        net_send(g, msg);
    }
}

static void ai_take_turn_action(Game *g)
{
    Fighter *me = &g->f[g->turn];
    Panoplie *pano = me->pano >= 0 && me->pano < N_PANOPLIES
                    ? &PANOPLIES[me->pano] : NULL;
    int best_spell = -1, best_target = -1, best_score = -2147483647;
    int spell, target;

    if (g->state != ST_BATTLE || me->team != 1) return;

    if (me->hp * 100 <= me->hp_max * 45) {
        int best_heal = 0, heal_spell = -1, heal_target = -1;
        for (spell = 0; spell < N_EQUIPPED; spell++) {
            Skill *skill = &me->spells[spell];
            if (skill->kind != SK_HEAL || skill->pa > me->pa
                || me->cd_left[spell] > 0)
                continue;
            for (target = 0; target < 2 * g->team_size; target++) {
                Fighter *ally = &g->f[target];
                int range = skill->range + (pano ? pano->range_bonus : 0);
                int heal, missing, useful;
                if (!ally->alive || ally->team != 1
                    || (range > 0
                        && manhattan(me->x, me->y, ally->x, ally->y) > range))
                    continue;
                heal = skill->power * (100 + (pano ? pano->heal_pct : 0)) / 100;
                missing = ally->hp_max - ally->hp;
                useful = heal < missing ? heal : missing;
                if (useful > best_heal) {
                    best_heal = useful;
                    heal_spell = spell;
                    heal_target = target;
                }
            }
        }
        if (heal_spell >= 0) {
            Fighter *ally = &g->f[heal_target];
            ai_do_cast(g, heal_spell, ally->x, ally->y);
            return;
        }
    }

    /* invocation : le bot appelle ses creatures quand il peut         */
    if (summon_count_of(g, g->turn) < MAX_SUMMONS_PER_OWNER) {
        for (spell = 0; spell < N_EQUIPPED; spell++) {
            Skill *skill = &me->spells[spell];
            if (summon_template_of(skill) < 0) continue;
            if (skill->pa > me->pa || me->cd_left[spell] > 0) continue;
            ai_do_cast(g, spell, me->x, me->y);
            return;
        }
    }

    /* liste des cibles : combattants adverses + invocations adverses   */
    {
        int tx[MAX_FIGHTERS + MAX_SUMMONS], ty[MAX_FIGHTERS + MAX_SUMMONS];
        int thp[MAX_FIGHTERS + MAX_SUMMONS];
        int tfi[MAX_FIGHTERS + MAX_SUMMONS];
        int ntarget = 0, t;
        for (t = 0; t < 2 * g->team_size; t++) {
            Fighter *f = &g->f[t];
            if (!f->alive || f->team == 1) continue;
            tx[ntarget] = f->x; ty[ntarget] = f->y;
            thp[ntarget] = f->hp;
            tfi[ntarget] = t;
            ntarget++;
        }
        for (t = 0; t < MAX_SUMMONS; t++) {
            Summon *sm = &g->summons[t];
            if (!sm->active || sm->team == 1) continue;
            tx[ntarget] = sm->x; ty[ntarget] = sm->y;
            thp[ntarget] = sm->hp;
            tfi[ntarget] = -1;
            ntarget++;
        }
        for (spell = 0; spell < N_EQUIPPED; spell++) {
            Skill *skill = &me->spells[spell];
            int range = skill->range + (pano ? pano->range_bonus : 0);
            if ((skill->kind != SK_ATK
                 && !(skill->kind == SK_SPECIAL && skill->power > 0))
                || skill->pa > me->pa || me->cd_left[spell] > 0)
                continue;
            for (target = 0; target < ntarget; target++) {
                int distance, damage, score;
                distance = manhattan(me->x, me->y, tx[target], ty[target]);
                if (distance > range
                    || !los_clear(g, me->x, me->y, tx[target], ty[target]))
                    continue;
                if (skill->kind == SK_ATK) {
                    damage = skill->power * (100 + me->atk_mod) / 100;
                    damage = damage * (100 + (pano ? pano->dmg_pct : 0)) / 100
                           + (pano ? pano->puissance : 0);
                } else {
                    damage = skill->power + (pano ? pano->puissance : 0);
                }
                if (g->neural_bot && tfi[target] >= 0)
                    score = (int)(neural_attack_score(&g->f[tfi[target]], skill,
                                                      damage) * 1000.0f);
                else
                    score = damage * 10 + (damage >= thp[target] ? 1000 : 0)
                          - skill->pa;
                if (score > best_score) {
                    best_score = score;
                    best_spell = spell;
                    best_target = target;
                }
            }
        }
        if (best_spell >= 0) {
            ai_do_cast(g, best_spell, tx[best_target], ty[best_target]);
            return;
        }
    }

    if (me->atk_mod < 25) {
        for (spell = 0; spell < N_EQUIPPED; spell++) {
            Skill *skill = &me->spells[spell];
            if (skill->kind == SK_BUFF && skill->pa <= me->pa
                && me->cd_left[spell] == 0) {
                ai_do_cast(g, spell, me->x, me->y);
                return;
            }
        }
    }

    if (me->pm > 0) {
        int range = 1, current_score, best_score, best_x = me->x;
        int best_y = me->y, x, y;
        for (spell = 0; spell < N_EQUIPPED; spell++) {
            Skill *skill = &me->spells[spell];
            if ((skill->kind == SK_ATK
                 || (skill->kind == SK_SPECIAL && skill->power > 0))
                && skill->pa <= me->pa && me->cd_left[spell] == 0
                && skill->range + (pano ? pano->range_bonus : 0) > range)
                range = skill->range + (pano ? pano->range_bonus : 0);
        }
        current_score = ai_position_score(g, me->x, me->y, range);
        best_score = current_score;
        for (y = 0; y < g->map_height; y++) {
            for (x = 0; x < g->map_width; x++) {
                int distance, score;
                if (g->cell[y][x] != CELL_GROUND || fighter_at(g, x, y) >= 0)
                    continue;
                distance = manhattan(me->x, me->y, x, y);
                if (distance == 0 || distance > me->pm) continue;
                score = ai_position_score(g, x, y, range);
                if (score < best_score) {
                    best_score = score;
                    best_x = x;
                    best_y = y;
                }
            }
        }
        if (best_x != me->x || best_y != me->y) {
            ai_do_move(g, best_x, best_y);
            return;
        }
    }

    end_turn(g, 0);
}

static void ai_update(Game *g)
{
    Uint32 now;
    if (g->state != ST_BATTLE || g->f[g->turn].team != 1)
        return;
    if (g->songe_mode) {
        /* en Songe, l'IA pilote les monstres : uniquement chez l'hote   */
        if (g->net.sock != SOCK_INVALID && g->net.role != NET_HOST) return;
    } else if (!g->solo_mode) {
        return;
    }
    now = SDL_GetTicks();
    if (g->ai_next_action_ms == 0) {
        g->ai_next_action_ms = now + 600;
        return;
    }
    if (now < g->ai_next_action_ms) return;
    ai_take_turn_action(g);
    if (g->state == ST_BATTLE && g->f[g->turn].team == 1)
        g->ai_next_action_ms = SDL_GetTicks() + 600;
    else
        g->ai_next_action_ms = 0;
}

/* ------------------------------------------------------------------ */
/* rendu                                                               */
/* ------------------------------------------------------------------ */
static SDL_Texture *load_asset(SDL_Renderer *r, const char *path)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        fprintf(stderr, "Asset image indisponible (%s): %s\n",
                path, IMG_GetError());
        return NULL;
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (!t)
        fprintf(stderr, "Texture indisponible (%s): %s\n",
                path, SDL_GetError());
    return t;
}

static void load_ui_atlas(SDL_Renderer *renderer)
{
    SDL_Surface *surface = IMG_Load("assets/UI/UI.png");
    if (!surface) {
        fprintf(stderr, "Planche UI indisponible (assets/UI/UI.png): %s\n",
                IMG_GetError());
        return;
    }
    g_ui_atlas_width = surface->w;
    g_ui_atlas_height = surface->h;
    g_ui_atlas = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!g_ui_atlas) {
        fprintf(stderr, "Texture UI indisponible: %s\n", SDL_GetError());
        return;
    }
    if (SDL_SetTextureBlendMode(g_ui_atlas, SDL_BLENDMODE_BLEND) < 0)
        fprintf(stderr, "Transparence UI indisponible: %s\n", SDL_GetError());
}

static void draw_ui_sprite(SDL_Renderer *renderer, SDL_Rect source,
                           SDL_Rect destination)
{
    if (g_ui_atlas && source.x >= 0 && source.y >= 0
            && source.w > 0 && source.h > 0
            && source.x <= g_ui_atlas_width - source.w
            && source.y <= g_ui_atlas_height - source.h)
        SDL_RenderCopy(renderer, g_ui_atlas, &source, &destination);
}

static void draw_ui_panel(SDL_Renderer *renderer, SDL_Rect destination)
{
    /* nouvelle interface : feuille de papier (Humble Pixel) teintee     */
    if (g_hp_sheet) {
        draw_nine_slice_tinted(renderer, g_hp_sheet, destination, 16,
                               72, 58, 45);
        SDL_SetRenderDrawColor(renderer, 150, 122, 78, 200);
        SDL_RenderDrawRect(renderer, &destination);
        return;
    }
    {
    int source_x[3] = {UI_PANEL_X, UI_PANEL_X + UI_PANEL_BORDER,
                       UI_PANEL_X + UI_PANEL_W - UI_PANEL_BORDER};
    int source_y[3] = {UI_PANEL_Y, UI_PANEL_Y + UI_PANEL_BORDER,
                       UI_PANEL_Y + UI_PANEL_H - UI_PANEL_BORDER};
    int source_w[3] = {UI_PANEL_BORDER, UI_PANEL_W - 2 * UI_PANEL_BORDER,
                       UI_PANEL_BORDER};
    int source_h[3] = {UI_PANEL_BORDER, UI_PANEL_H - 2 * UI_PANEL_BORDER,
                       UI_PANEL_BORDER};
    int dest_w[3] = {UI_PANEL_BORDER,
                     destination.w - 2 * UI_PANEL_BORDER,
                     UI_PANEL_BORDER};
    int dest_h[3] = {UI_PANEL_BORDER,
                     destination.h - 2 * UI_PANEL_BORDER,
                     UI_PANEL_BORDER};
    int x, y;

    if (destination.w < 2 * UI_PANEL_BORDER
            || destination.h < 2 * UI_PANEL_BORDER)
        return;
    for (y = 0; y < 3; y++) {
        for (x = 0; x < 3; x++) {
            SDL_Rect source = {source_x[x], source_y[y],
                               source_w[x], source_h[y]};
            SDL_Rect target = {destination.x + (x == 0 ? 0 :
                                  x == 1 ? UI_PANEL_BORDER :
                                  destination.w - UI_PANEL_BORDER),
                               destination.y + (y == 0 ? 0 :
                                  y == 1 ? UI_PANEL_BORDER :
                                  destination.h - UI_PANEL_BORDER),
                               dest_w[x], dest_h[y]};
            draw_ui_sprite(renderer, source, target);
        }
    }
    }
}

static void draw_ui_banner(SDL_Renderer *renderer, SDL_Rect destination)
{
    /* banniere : papier un peu plus clair que les panneaux              */
    if (g_hp_sheet) {
        draw_nine_slice_tinted(renderer, g_hp_sheet, destination, 16,
                               96, 78, 58);
        SDL_SetRenderDrawColor(renderer, 190, 158, 96, 220);
        SDL_RenderDrawRect(renderer, &destination);
        return;
    }
    draw_ui_panel(renderer, destination);
}

static void apply_audio_settings(const Game *g)
{
    if (!g_audio_ready) return;
    Mix_VolumeMusic(g->music_volume * MIX_MAX_VOLUME / 100);
    Mix_Volume(-1, g->sfx_volume * MIX_MAX_VOLUME / 100);
    set_social_sound_volume();
}

static void play_sound(Game *g, SoundEffect sound)
{
    static const char *SUBTITLES[SFX_COUNT] = {
        "Menu", "Annuler", "Sort lance", "Impact", "Combattant KO",
        "Deplacement", "Victoire", "Fin du combat",
        "Pause de recentrage", "Encouragement"
    };
    if (sound < 0 || sound >= SFX_COUNT) return;
    if (g->sfx_volume == 0 && g->subtitles_when_muted) {
        snprintf(g->audio_subtitle, sizeof g->audio_subtitle, "%s",
                 SUBTITLES[sound]);
        g->audio_subtitle_until = SDL_GetTicks() + 1600;
    }
    if (g_audio_ready && g->sfx_volume > 0 && g_sounds[sound])
        Mix_PlayChannel(-1, g_sounds[sound], 0);
}

static void set_social_sound_volume(void)
{
    if (!g_sounds[SFX_PACIFY] || !g_sounds[SFX_ENCOURAGE])
        return;
    Mix_VolumeChunk(g_sounds[SFX_PACIFY], MIX_MAX_VOLUME * 32 / 100);
    Mix_VolumeChunk(g_sounds[SFX_ENCOURAGE], MIX_MAX_VOLUME * 48 / 100);
}

static void play_social_cue(Game *g, SoundEffect sound)
{
    Uint32 now = SDL_GetTicks();
    Uint32 *last_played = sound == SFX_PACIFY
                        ? &g->social_last_pacify_ms
                        : &g->social_last_encourage_ms;
    Uint32 cooldown = sound == SFX_PACIFY ? 3500 : 1800;
    if (*last_played != 0 && now - *last_played < cooldown)
        return;
    *last_played = now ? now : 1;
    play_sound(g, sound);
}

static void try_play_social_cue(Game *g, SoundEffect sound,
                                unsigned probability_percent)
{
    if (probability_percent > 100)
        probability_percent = 100;
    if ((unsigned)(rand() % 100) < probability_percent)
        play_social_cue(g, sound);
}

static void normalize_social_text(const char *source, char *target,
                                 size_t target_size)
{
    size_t out = 0;
    const unsigned char *p = (const unsigned char *)source;
    while (*p && out + 1 < target_size) {
        unsigned char c = *p++;
        if (c < 0x80) {
            if (isalnum(c))
                target[out++] = (char)tolower(c);
            else if (out == 0 || target[out - 1] != ' ')
                target[out++] = ' ';
        } else if (c == 0xc3 && *p) {
            unsigned char accent = *p++;
            char folded = 0;
            if (accent == 0xa0 || accent == 0xa2 || accent == 0xa4)
                folded = 'a';
            else if (accent == 0xa7)
                folded = 'c';
            else if (accent == 0xa8 || accent == 0xa9 || accent == 0xaa
                     || accent == 0xab)
                folded = 'e';
            else if (accent == 0xae || accent == 0xaf)
                folded = 'i';
            else if (accent == 0xb1)
                folded = 'n';
            else if (accent == 0xb2 || accent == 0xb4 || accent == 0xb6)
                folded = 'o';
            else if (accent == 0xb9 || accent == 0xba || accent == 0xbc
                     || accent == 0xbd || accent == 0xbe)
                folded = 'u';
            else if (accent == 0xbf)
                folded = 'y';
            if (folded)
                target[out++] = folded;
            else if (out == 0 || target[out - 1] != ' ')
                target[out++] = ' ';
        } else if (out == 0 || target[out - 1] != ' ') {
            target[out++] = ' ';
        }
    }
    while (out > 0 && target[out - 1] == ' ')
        out--;
    target[out] = '\0';
}

static int social_text_has_phrase(const char *text, const char *phrase)
{
    char padded_text[520];
    char padded_phrase[96];
    size_t text_length = strlen(text);
    size_t phrase_length = strlen(phrase);
    if (text_length + 3 > sizeof padded_text
            || phrase_length + 3 > sizeof padded_phrase)
        return 0;
    padded_text[0] = ' ';
    memcpy(padded_text + 1, text, text_length);
    padded_text[text_length + 1] = ' ';
    padded_text[text_length + 2] = '\0';
    padded_phrase[0] = ' ';
    memcpy(padded_phrase + 1, phrase, phrase_length);
    padded_phrase[phrase_length + 1] = ' ';
    padded_phrase[phrase_length + 2] = '\0';
    return strstr(padded_text, padded_phrase) != NULL;
}

static void process_social_message(Game *g, int team, const char *message)
{
    static const char *TOXIC_PHRASES[] = {
        "idiot", "stupid", "dumb", "noob", "loser", "trash",
        "moron", "you suck", "piece of shit", "shut up", "stfu",
        "useless", "kill yourself", "hate you", "connard", "connasse",
        "debile", "nul", "nulle", "ta gueule", "ferme la",
        "va te faire voir", "encule", "va mourir"
    };
    static const char *SUPPORTIVE_PHRASES[] = {
        "gg", "wp", "well played", "good game", "nice play", "thanks",
        "thank you", "bravo", "merci", "bien joue", "courage",
        "good luck", "you got this", "respect", "sorry", "pardon"
    };
    char normalized[512];
    Uint32 now;
    size_t i;
    int toxic = 0, supportive = 0;
    if (!message || team < 0 || team >= 2)
        return;
    normalize_social_text(message, normalized, sizeof normalized);
    for (i = 0; i < sizeof TOXIC_PHRASES / sizeof TOXIC_PHRASES[0]; i++)
        if (social_text_has_phrase(normalized, TOXIC_PHRASES[i])) {
            toxic = 1;
            break;
        }
    if (!toxic)
        for (i = 0; i < sizeof SUPPORTIVE_PHRASES
                       / sizeof SUPPORTIVE_PHRASES[0]; i++)
            if (social_text_has_phrase(normalized, SUPPORTIVE_PHRASES[i])) {
                supportive = 1;
                break;
            }
    if (toxic) {
        try_play_social_cue(g, SFX_PACIFY, 70);
        return;
    }
    if (supportive) {
        try_play_social_cue(g, SFX_ENCOURAGE, 65);
        return;
    }

    now = SDL_GetTicks();
    if (g->social_chat_burst[team] == 0
            || now - g->social_last_chat_ms[team] > 6000)
        g->social_chat_burst[team] = 1;
    else
        g->social_chat_burst[team]++;
    g->social_last_chat_ms[team] = now;
    if (g->social_chat_burst[team] >= 4) {
        g->social_chat_burst[team] = 0;
        try_play_social_cue(g, SFX_PACIFY, 45);
    }
}

static void process_social_emote(Game *g, int team)
{
    Uint32 now;
    if (team < 0 || team >= 2)
        return;
    now = SDL_GetTicks();
    if (g->social_emote_burst[team] == 0
            || now - g->social_last_emote_ms[team] > 6000)
        g->social_emote_burst[team] = 1;
    else
        g->social_emote_burst[team]++;
    g->social_last_emote_ms[team] = now;
    /*Trois emotes en six secondes = vague d'humiliation : le contre   */
    if (g->social_emote_burst[team] == 3) {
        g->anti_humil_last_ns = music_ns_now();
        anti_humil_music(g, g->anti_humil_last_ns);
    }
    if (g->social_emote_burst[team] >= 4) {
        g->social_emote_burst[team] = 0;
        try_play_social_cue(g, SFX_PACIFY, 45);
    }
}

static void load_audio(void)
{
    int i;
    if ((Mix_Init(MIX_INIT_MP3) & MIX_INIT_MP3) == 0)
        fprintf(stderr, "SDL_mixer: decodeur MP3 indisponible: %s\n",
                Mix_GetError());
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "SDL_mixer: sortie audio indisponible: %s\n",
                Mix_GetError());
        return;
    }
    g_audio_ready = 1;
    g_menu_music = Mix_LoadMUS("assets/BGM/Menu/ES_Dragon's Honor - Yi Nantiro.mp3");
    if (!g_menu_music)
        fprintf(stderr, "SDL_mixer: musique menu indisponible: %s\n",
                Mix_GetError());
    for (i = 0; i < 4; i++) {
        g_battle_music[i] = Mix_LoadMUS(BATTLE_MUSIC_PATHS[i]);
        if (!g_battle_music[i])
            fprintf(stderr, "SDL_mixer: musique indisponible (%s): %s\n",
                    BATTLE_MUSIC_PATHS[i], Mix_GetError());
    }
    for (i = 0; i < SFX_COUNT; i++) {
        g_sounds[i] = Mix_LoadWAV(SOUND_PATHS[i]);
        if (!g_sounds[i])
            fprintf(stderr, "SDL_mixer: effet sonore indisponible (%s): %s\n",
                    SOUND_PATHS[i], Mix_GetError());
    }
    set_social_sound_volume();
}

static void play_music_for_state(Game *g, GameState state)
{
    if (!g_audio_ready) return;
    if (state == ST_BATTLE) {
        int track = (int)(SDL_GetTicks() % 4);
        if (g_battle_music[track])
            Mix_PlayMusic(g_battle_music[track], -1);
        g_current_music_track = track;
        memset(g->anti_humil_used, 0, sizeof g->anti_humil_used);
        g->anti_humil_active = 0;
        g->anti_humil_prev_track = track;
    } else if (state == ST_MENU || state == ST_PICK || state == ST_DRAFT
               || state == ST_STUFF || state == ST_ELEMENTS) {
        if (g_menu_music) Mix_PlayMusic(g_menu_music, -1);
        g_current_music_track = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Contre musical anti-humiliation (Pour le Bien et le Bon)            */
/* ------------------------------------------------------------------ */

/* LA musique répond, tunnée, en un temps mesuré à la nanoseconde.     */
static void anti_humil_music(Game *g, long long trigger_ns)
{
    long long detect_ns, span, t0, t1;
    int counter, tries;
    const char *path, *title;
    if (!g_audio_ready || g->state != ST_BATTLE
            || g_current_music_track < 0)
        return;
    detect_ns = music_ns_now();
    if (trigger_ns < 0)
        trigger_ns = detect_ns;
    span = detect_ns - trigger_ns;
    /* la piste rivale est tirée des nanosecondes (déterministe)       */
    counter = (int)((detect_ns / 1000LL) % 4);
    for (tries = 0; tries < 4; tries++) {
        if (counter != g_current_music_track
                && !g->anti_humil_used[counter]
                && g_battle_music[counter])
            break;
        counter = (counter + 1) % 4;
    }
    if (counter == g_current_music_track
            || !g_battle_music[counter])
        return;
    if (!g->anti_humil_active)
        g->anti_humil_prev_track = g_current_music_track;
    g->anti_humil_used[counter] = 1;
    g->anti_humil_restore_ms = SDL_GetTicks() + 8000;
    path = BATTLE_MUSIC_PATHS[counter];
    title = strstr(path, "BGM/") ? strstr(path, "BGM/") + 4 : path;
    if (!title) title = path;
    /* la bascule elle-meme est chronometree au ns pres                */
    t0 = music_ns_now();
    Mix_PlayMusic(g_battle_music[counter], -1);
    t1 = music_ns_now();
    g_current_music_track = counter;
    g->anti_humil_active = 1;
    fprintf(stderr,
            "ANTI-HUMILIATION: la musique riposte (%.40s) — visee %lld ns "
            "apres trigger, bascule en %lld ns, reprise dans %u ms. "
            "Pour le Bien et le Bon.\n",
            title, span, t1 - t0, 8000);
}

/* reprise de la piste d'origine, elle aussi chronometree au ns pres   */
static void anti_humil_restore(Game *g)
{
    long long t0, t1;
    const char *path, *title;
    if (!g->anti_humil_active) return;
    if (g->state != ST_BATTLE) {
        g->anti_humil_active = 0;
        return;
    }
    if ((Sint32)(SDL_GetTicks() - g->anti_humil_restore_ms) < 0)
        return;
    path = BATTLE_MUSIC_PATHS[g->anti_humil_prev_track < 0
                              ? 0 : g->anti_humil_prev_track];
    title = strstr(path, "BGM/") ? strstr(path, "BGM/") + 4 : path;
    t0 = music_ns_now();
    if (g_battle_music[g->anti_humil_prev_track])
        Mix_PlayMusic(g_battle_music[g->anti_humil_prev_track], -1);
    t1 = music_ns_now();
    g_current_music_track = g->anti_humil_prev_track;
    g->anti_humil_active = 0;
    fprintf(stderr,
            "ANTI-HUMILIATION: la musique pardonne (retour %.40s) en "
            "%lld ns. Pour le Bien et le Bon.\n",
            title, t1 - t0);
}

static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *txt,
                      int x, int y, SDL_Color c);
static int pt_in(SDL_Rect r, int x, int y);

/* Charge la planche de creatures (invocations + monstres du Songe).      */
static void content_box(SDL_Surface *s, int sx, int sy, int sw, int sh,
                        ContentBox *out);

static void load_creatures_sheet(SDL_Renderer *renderer)
{
    SDL_Surface *surface;
    int i;
    surface = IMG_Load("assets/character/creatures.png");
    if (!surface) {
        fprintf(stderr, "Bestiaire indisponible (assets/character/creatures.png): %s\n",
                IMG_GetError());
        return;
    }
    for (i = 0; i < N_CREATURES; i++) {
        g_creature_frames[i].x = i * 16;
        g_creature_frames[i].y = 0;
        g_creature_frames[i].w = 16;
        g_creature_frames[i].h = 16;
        content_box(surface, g_creature_frames[i].x, g_creature_frames[i].y,
                    16, 16, &g_creature_boxes[i]);
    }
    g_creatures_sheet = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!g_creatures_sheet)
        fprintf(stderr, "Texture bestiaire indisponible.\n");
}

/* Charge la nouvelle interface (panneaux 9-slice).  Si les fichiers
   manquent, le jeu continue avec l'ancien atlas (aucun crash).        */
static void load_ui9(SDL_Renderer *renderer)
{
    int i;
    for (i = 0; i < UI9_COUNT; i++) {
        g_ui9[i] = load_asset(renderer, UI9_FILES[i]);
        if (!g_ui9[i])
            fprintf(stderr, "Interface 9-slice absente: %s\n", UI9_FILES[i]);
    }
}

/* Charge l'atlas de personnages et calcule le cadre utile de chaque
   frame pour centrer proprement les decoupes sur les cases.            */
static void load_characters_sheet(SDL_Renderer *renderer)
{
    SDL_Surface *surface;
    int row, seq, f;
    surface = IMG_Load("assets/character/Characters.png");
    if (!surface) {
        fprintf(stderr, "Atlas personnages indisponible: %s\n", IMG_GetError());
        return;
    }
    for (row = 0; row < CHARACTER_ROW_COUNT; row++)
        for (seq = 0; seq < CHARACTER_SEQUENCE_COUNT; seq++)
            for (f = 0; f < CHARACTER_SEQUENCE_FRAME_COUNT; f++)
                content_box(surface, CHARACTER_FRAME_X[seq][f],
                            row * CHARACTER_FRAME_H,
                            CHARACTER_FRAME_WIDTH[seq], CHARACTER_FRAME_H,
                            &g_char_boxes[row][seq][f]);
    g_characters_sheet = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!g_characters_sheet)
        fprintf(stderr, "Texture personnages indisponible.\n");
}

static void load_visual_effects(SDL_Renderer *renderer)
{
    int i;
    char path[160];
    load_characters_sheet(renderer);
#ifndef __ANDROID__
    g_spell_icons = load_asset(renderer, "assets/character/spells_icon/spell4.webp");
#endif
    g_emote_sheet = load_asset(renderer, "assets/bubble_emotes.png");
    load_creatures_sheet(renderer);
    load_ui9(renderer);
    load_humble_ui(renderer);
    load_hud_sheet(renderer);
    for (i = 0; i < VFX_FRAME_COUNT; i++) {
        snprintf(path, sizeof path,
                 "assets/VFX/Effect_BloodImpact/30fps/Frames/"
                 "Effect_BloodImpact_1/Effect_BloodImpact_1_%03d.png", i);
        g_vfx_frames[i] = load_asset(renderer, path);
    }
}

static SDL_Rect spell_icon_rect(int icon)
{
    SDL_Rect r;
    icon %= SPELL_ICON_COLS * SPELL_ICON_ROWS;
    r.x = 112 + (icon % SPELL_ICON_COLS) * 100;
    r.y = 8 + (icon / SPELL_ICON_COLS) * 128;
    r.w = 92;
    r.h = 104;
    return r;
}

static SDL_Rect emote_icon_rect(int icon)
{
    SDL_Rect r;
    r.x = (icon % EMOTE_COLS) * EMOTE_CELL_W;
    r.y = (icon / EMOTE_COLS) * EMOTE_CELL_H;
    r.w = EMOTE_CELL_W;
    r.h = EMOTE_CELL_H;
    return r;
}

static SDL_Rect emote_option_rect(int option)
{
    SDL_Rect r = {WIN_W / 2 - 140 + 12 + (option % 4) * 64,
                  WIN_H / 2 - 72 + 24 + (option / 4) * 54, 54, 48};
    return r;
}

static void draw_character_sprite(SDL_Renderer *renderer, int row, int sequence,
                                  int frame, SDL_Rect destination,
                                  int tint_opponent)
{
    SDL_Rect source;
    SDL_Rect target = destination;
    int frame_index = frame % CHARACTER_SEQUENCE_FRAME_COUNT;
    if (!g_characters_sheet || row < 0 || row >= CHARACTER_ROW_COUNT
        || sequence < 0 || sequence >= CHARACTER_SEQUENCE_COUNT)
        return;
    source.x = CHARACTER_FRAME_X[sequence][frame_index];
    source.y = row * CHARACTER_FRAME_H;
    source.w = CHARACTER_FRAME_WIDTH[sequence];
    source.h = CHARACTER_FRAME_H;
    target.w = destination.w * source.w / CHARACTER_FRAME_W;
    target.x += (destination.w - target.w) / 2;
    if (tint_opponent)
        SDL_SetTextureColorMod(g_characters_sheet,
                               255,
                               tint_opponent == 2 ? 220 : 190,
                               tint_opponent == 2 ? 90 : 190);
    else
        SDL_SetTextureColorMod(g_characters_sheet, 255, 255, 255);
    SDL_RenderCopy(renderer, g_characters_sheet, &source, &target);
    SDL_SetTextureColorMod(g_characters_sheet, 255, 255, 255);
}

static int character_sequence_for_anim(AnimState anim)
{
    switch (anim) {
        case ANIM_MOVE: return 1;
        case ANIM_ATTACK: return 2;
        case ANIM_IDLE:
        default: return 0;
    }
}

static void draw_emote(Game *g, SDL_Renderer *renderer)
{
    Uint32 now = SDL_GetTicks();
    Fighter *f;
    SDL_Rect src, dst;
    if (!g_emote_sheet || g->emote_fighter < 0
        || g->emote_fighter >= 2 * g->team_size
        || (Sint32)(g->emote_until - now) <= 0)
        return;
    f = &g->f[g->emote_fighter];
    if (!f->alive) return;
    src = emote_icon_rect(g->emote_index);
    {
        int ex, ey;
        iso_center(g, f->x, f->y, &ex, &ey);
        dst.x = ex - 18;
        dst.y = ey - TILE - 38;
    }
    dst.w = 36;
    dst.h = 39;
    SDL_RenderCopy(renderer, g_emote_sheet, &src, &dst);
}

static void draw_emote_menu(Game *g, SDL_Renderer *renderer, TTF_Font *font)
{
    static const int emotes[8] = {0,1,2,3,16,17,18,19};
    SDL_Rect bg = {WIN_W / 2 - 140, WIN_H / 2 - 72, 280, 144};
    SDL_Color white = {255,255,255,255};
    int i, mx, my;
    if (!g->emote_menu_open || !g_emote_sheet) return;
    SDL_SetRenderDrawColor(renderer, 20, 20, 30, 235);
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 225, 205, 150, 255);
    SDL_RenderDrawRect(renderer, &bg);
    mouse_logical(&mx, &my);
    for (i = 0; i < 8; i++) {
        SDL_Rect option = emote_option_rect(i);
        SDL_Rect src = emote_icon_rect(emotes[i]);
        SDL_Rect icon = {option.x + 9, option.y + 3, 36, 39};
        SDL_SetRenderDrawColor(renderer,
                               pt_in(option, mx, my) ? 90 : 55,
                               pt_in(option, mx, my) ? 100 : 60,
                               pt_in(option, mx, my) ? 125 : 75, 255);
        SDL_RenderFillRect(renderer, &option);
        SDL_RenderCopy(renderer, g_emote_sheet, &src, &icon);
        {
            char label[8];
            snprintf(label, sizeof label, "%d", i + 1);
            draw_text(renderer, font, label, option.x + 3, option.y + 2, white);
        }
    }
    draw_text(renderer, font, "EMOTES - 1 a 8, ou clic (cliquer, c'est encore plus simple)", bg.x + 46,
              bg.y + 4, white);
}

static SDL_Rect ingame_menu_panel(void);
static SDL_Rect ingame_menu_option(int option);
static SDL_Rect surrender_dialog(void);
static SDL_Rect surrender_dialog_button(int confirm);

static void draw_ingame_menu(Game *g, SDL_Renderer *renderer, TTF_Font *font)
{
    static const char *options[] = {
        "Resume",
        "Main Menu (Disconnect)",
        "Surrender",
        "Quit"
    };
    SDL_Color white = {255,255,255,255};
    SDL_Rect panel;
    int mx, my, i;
    if (!g->in_game_menu_open) return;
    mouse_logical(&mx, &my);
    if (g->surrender_confirm) {
        panel = surrender_dialog();
        SDL_SetRenderDrawColor(renderer, 28, 22, 22, 255);
        SDL_RenderFillRect(renderer, &panel);
        draw_ui_panel(renderer, panel);
        SDL_SetRenderDrawColor(renderer, 215, 80, 70, 255);
        SDL_RenderDrawRect(renderer, &panel);
        draw_text(renderer, font, "Abandonner ce match ? (tres mauvaise idee, mais c'est votre vie)", panel.x + 78,
                  panel.y + 28, white);
        for (i = 0; i < 2; i++) {
            SDL_Rect button = surrender_dialog_button(i);
            int hover = pt_in(button, mx, my);
            SDL_SetRenderDrawColor(renderer,
                                   i == 0 ? (hover ? 180 : 145) : (hover ? 75 : 55),
                                   i == 0 ? (hover ? 55 : 42) : (hover ? 95 : 75),
                                   i == 0 ? (hover ? 45 : 36) : (hover ? 105 : 85),
                                   255);
            SDL_RenderFillRect(renderer, &button);
            SDL_SetRenderDrawColor(renderer, 225, 205, 170, 255);
            SDL_RenderDrawRect(renderer, &button);
            draw_text(renderer, font, i == 0 ? "Surrender" : "Cancel",
                      button.x + 42, button.y + 10, white);
        }
        return;
    }
    panel = ingame_menu_panel();
    SDL_SetRenderDrawColor(renderer, 24, 23, 28, 255);
    SDL_RenderFillRect(renderer, &panel);
    draw_ui_panel(renderer, panel);
    SDL_SetRenderDrawColor(renderer, 200, 170, 115, 255);
    SDL_RenderDrawRect(renderer, &panel);
    draw_text(renderer, font, "PAUSE-MENU", panel.x + 104, panel.y + 16, white);
    for (i = 0; i < 4; i++) {
        SDL_Rect button = ingame_menu_option(i);
        int hover = pt_in(button, mx, my);
        if (i == 2)
            SDL_SetRenderDrawColor(renderer, hover ? 160 : 125, 45, 40, 255);
        else
            SDL_SetRenderDrawColor(renderer, hover ? 93 : 65,
                                   hover ? 81 : 58, hover ? 65 : 48, 255);
        SDL_RenderFillRect(renderer, &button);
        SDL_SetRenderDrawColor(renderer, 215, 190, 145, 255);
        SDL_RenderDrawRect(renderer, &button);
        draw_text(renderer, font, options[i], button.x + 18,
                  button.y + 9, white);
    }
}

static void draw_vfx(Game *g, SDL_Renderer *renderer)
{
    Uint32 age;
    int frame;
    SDL_Rect dst;
    if (!g->vfx_active) return;
    age = SDL_GetTicks() - g->vfx_started;
    if (g->reduce_flashing) {
        if (age >= VFX_REDUCED_DURATION_MS) {
            g->vfx_active = 0;
            return;
        }
        frame = 15;
    } else {
        frame = (int)(age / VFX_FRAME_MS);
    }
    if (frame >= VFX_FRAME_COUNT) {
        g->vfx_active = 0;
        return;
    }
    if (!g_vfx_frames[frame]) return;
    {
        int vx, vy;
        iso_center(g, g->vfx_x, g->vfx_y, &vx, &vy);
        dst.x = vx - 35;
        dst.y = vy - 34;
    }
    dst.w = 70;
    dst.h = 60;
    SDL_RenderCopy(renderer, g_vfx_frames[frame], NULL, &dst);
}

static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *txt,
                      int x, int y, SDL_Color c)
{
    if (!f || !txt || !txt[0]) return;
    /* ombre portee pour la lisibilite                            */
    {
        SDL_Color sh = {0, 0, 0, 170};
        SDL_Surface *s = TTF_RenderUTF8_Blended(f, txt, sh);
        if (s) {
            SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
            SDL_Rect d = {x + 1, y + 1, s->w, s->h};
            if (t) { SDL_RenderCopy(r, t, NULL, &d); SDL_DestroyTexture(t); }
            SDL_FreeSurface(s);
        }
    }
    {
        SDL_Surface *s = TTF_RenderUTF8_Blended(f, txt, c);
        if (!s) return;
        SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
        SDL_Rect d = {x, y, s->w, s->h};
        if (t) { SDL_RenderCopy(r, t, NULL, &d); SDL_DestroyTexture(t); }
        SDL_FreeSurface(s);
    }
}

/* ------------------------------------------------------------------ */
/* primitives isometriques (losanges textures / colores)               */
/* ------------------------------------------------------------------ */
static SDL_Vertex geo_vertex(float x, float y, float u, float v,
                             Uint8 r, Uint8 gr, Uint8 b, Uint8 a)
{
    SDL_Vertex vt;
    vt.position.x = x;
    vt.position.y = y;
    vt.color.r = r;
    vt.color.g = gr;
    vt.color.b = b;
    vt.color.a = a;
    vt.tex_coord.x = u;
    vt.tex_coord.y = v;
    return vt;
}

/* quad texturé : p1=TL p2=TR p3=BR p4=BL de la tuile source           */
static void geo_textured_quad(SDL_Renderer *r, SDL_Texture *tex,
                              const SDL_Rect *src,
                              float x1, float y1, float x2, float y2,
                              float x3, float y3, float x4, float y4,
                              Uint8 shade)
{
    int tw = 1, th = 1;
    float u0, v0, u1, v1;
    SDL_Vertex v[6];
    if (!tex) return;
    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
    u0 = (float)src->x / tw;        v0 = (float)src->y / th;
    u1 = (float)(src->x + src->w) / tw;
    v1 = (float)(src->y + src->h) / th;
    v[0] = geo_vertex(x1, y1, u0, v0, shade, shade, shade, 255);
    v[1] = geo_vertex(x2, y2, u1, v0, shade, shade, shade, 255);
    v[2] = geo_vertex(x3, y3, u1, v1, shade, shade, shade, 255);
    v[3] = geo_vertex(x1, y1, u0, v0, shade, shade, shade, 255);
    v[4] = geo_vertex(x3, y3, u1, v1, shade, shade, shade, 255);
    v[5] = geo_vertex(x4, y4, u0, v1, shade, shade, shade, 255);
    SDL_RenderGeometry(r, tex, v, 6, NULL, 0);
}

/* losange translucide (zones vertes / bleues, survol)                  */
static void iso_fill_diamond(SDL_Renderer *r, Game *g, int i, int j,
                             SDL_Color c)
{
    int px, py, hw = ISO_TILE_W / 2, hh = ISO_TILE_H / 2;
    SDL_Vertex v[6];
    iso_top_vertex(g, i, j, &px, &py);
    v[0] = geo_vertex((float)px,        (float)py,          0, 0,
                      c.r, c.g, c.b, c.a);
    v[1] = geo_vertex((float)(px + hw), (float)(py + hh),   0, 0,
                      c.r, c.g, c.b, c.a);
    v[2] = geo_vertex((float)px,        (float)(py + 2*hh), 0, 0,
                      c.r, c.g, c.b, c.a);
    v[3] = v[0];
    v[4] = v[2];
    v[5] = geo_vertex((float)(px - hw), (float)(py + hh),   0, 0,
                      c.r, c.g, c.b, c.a);
    SDL_RenderGeometry(r, NULL, v, 6, NULL, 0);
}

static void iso_outline_diamond(SDL_Renderer *r, Game *g, int i, int j,
                                SDL_Color c)
{
    int px, py, hw = ISO_TILE_W / 2, hh = ISO_TILE_H / 2;
    iso_top_vertex(g, i, j, &px, &py);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r, px, py, px + hw, py + hh);
    SDL_RenderDrawLine(r, px + hw, py + hh, px, py + 2 * hh);
    SDL_RenderDrawLine(r, px, py + 2 * hh, px - hw, py + hh);
    SDL_RenderDrawLine(r, px - hw, py + hh, px, py);
}

/* losange texturé (dessus de tuile), relevé de « lift » pixels        */
static void iso_textured_diamond(SDL_Renderer *r, SDL_Texture *tex,
                                 const SDL_Rect *src, int px, int py,
                                 int lift, Uint8 shade)
{
    int hw = ISO_TILE_W / 2, hh = ISO_TILE_H / 2;
    geo_textured_quad(r, tex, src,
                      (float)px,        (float)(py - lift),
                      (float)(px + hw), (float)(py + hh - lift),
                      (float)px,        (float)(py + 2 * hh - lift),
                      (float)(px - hw), (float)(py + hh - lift),
                      shade);
}

/* face verticale (cote de mur / falaise) entre deux bords de losange  */
static void iso_side_face(SDL_Renderer *r, SDL_Texture *tex,
                          const SDL_Rect *src,
                          int x1, int y1, int x2, int y2, int depth,
                          Uint8 shade)
{
    geo_textured_quad(r, tex, src,
                      (float)x1, (float)(y1 - depth),
                      (float)x2, (float)(y2 - depth),
                      (float)x2, (float)y2,
                      (float)x1, (float)y1,
                      shade);
}

/* case survolee par la souris (test point dans losange)               */
static int iso_pick(const Game *g, int mx, int my, int *pi, int *pj)
{
    int i, j;
    *pi = -1;
    *pj = -1;
    if (g->map_width <= 0 || g->map_height <= 0) return 0;
    for (j = 0; j < g->map_height; j++)
        for (i = 0; i < g->map_width; i++) {
            int cx, cy, dx, dy;
            if (g->cell[j][i] == CELL_VOID) continue;
            iso_center(g, i, j, &cx, &cy);
            dx = mx - cx;
            dy = my - cy;
            if (abs(dx) * ISO_TILE_H + abs(dy) * ISO_TILE_W
                <= ISO_TILE_W * ISO_TILE_H / 2) {
                *pi = i;
                *pj = j;
                return 1;
            }
        }
    return 0;
}

static void draw_tile(SDL_Renderer *r, Game *g, int i, int j, int cell)
{
    int px, py, hw = ISO_TILE_W / 2, hh = ISO_TILE_H / 2;
    const SDL_Rect *floor_src = &MAP_FLOOR_RECTS[g->map_theme];
    const SDL_Rect *wall_src  = &MAP_WALL_RECTS[g->map_theme];
    const SDL_Rect *void_src  = &MAP_VOID_RECTS[g->map_theme];
    iso_top_vertex(g, i, j, &px, &py);
    if (cell == CELL_VOID) return;

    if (cell == CELL_WALL) {
        /* bloc 2.5D : deux faces + dessus relevé                       */
        if (g_tilesheet) {
            iso_side_face(r, g_tilesheet, wall_src,
                          px - hw, py + hh, px, py + 2 * hh,
                          ISO_WALL_H, 150);
            iso_side_face(r, g_tilesheet, wall_src,
                          px, py + 2 * hh, px + hw, py + hh,
                          ISO_WALL_H, 110);
            iso_textured_diamond(r, g_tilesheet, wall_src, px, py,
                                 ISO_WALL_H, 255);
        } else {
            SDL_SetRenderDrawColor(r, 85, 60, 40, 255);
            SDL_RenderDrawLine(r, px - hw, py + hh - ISO_WALL_H, px, py + 2*hh - ISO_WALL_H);
            SDL_RenderDrawLine(r, px - hw, py + hh, px, py + 2 * hh);
            SDL_RenderDrawLine(r, px - hw, py + hh, px - hw, py + hh - ISO_WALL_H);
            SDL_SetRenderDrawColor(r, 60, 42, 28, 255);
            SDL_RenderDrawLine(r, px, py + 2 * hh, px + hw, py + hh);
            iso_fill_diamond(r, g, i, j, (SDL_Color){120, 90, 60, 255});
        }
        return;
    }

    if (cell == CELL_GAP) {
        /* fosse : sombre, bloque le deplacement mais pas la vue        */
        if (g_tilesheet)
            iso_textured_diamond(r, g_tilesheet, void_src, px, py, 0, 70);
        else
            iso_fill_diamond(r, g, i, j, (SDL_Color){25, 25, 35, 255});
        return;
    }

    /* sol : falaises sur les bords de l'arene (ile flottante)         */
    {
        int sw = (j + 1 < g->map_height) ? g->cell[j + 1][i] : CELL_VOID;
        int se = (i + 1 < g->map_width)  ? g->cell[j][i + 1] : CELL_VOID;
        if (sw == CELL_VOID)
            iso_side_face(r, g_tilesheet ? g_tilesheet : NULL, floor_src,
                          px - hw, py + hh, px, py + 2 * hh,
                          ISO_EDGE_H, 130);
        if (se == CELL_VOID)
            iso_side_face(r, g_tilesheet ? g_tilesheet : NULL, floor_src,
                          px, py + 2 * hh, px + hw, py + hh,
                          ISO_EDGE_H, 100);
    }
    if (g_tilesheet)
        iso_textured_diamond(r, g_tilesheet, floor_src, px, py, 0, 255);
    else
        iso_fill_diamond(r, g, i, j, (SDL_Color){60, 140, 60, 255});
}

/* zone verte : toutes les cases atteignables avec les PM restants     */
static void draw_move_zone(SDL_Renderer *r, Game *g, int hover_i, int hover_j)
{
    Fighter *me = &g->f[g->turn];
    int dist[GRID_H][GRID_W];
    int i, j;
    if (g->state != ST_BATTLE || !unit_is_mine(g, g->turn)) return;
    if (g->sel_skill >= 0) return;          /* mode sort : zone bleue    */
    move_distances(g, me->x, me->y, dist);
    for (j = 0; j < g->map_height; j++)
        for (i = 0; i < g->map_width; i++) {
            int d = dist[j][i];
            if (d <= 0 || d > me->pm) continue;
            if (i == hover_i && j == hover_j)
                iso_fill_diamond(r, g, i, j,
                                 (SDL_Color){130, 255, 150, 170});
            else
                iso_fill_diamond(r, g, i, j,
                                 (SDL_Color){60, 210, 90, 100});
        }
}

/* zone bleue : portee du sort selectionne (1-4 ou clic sur la barre)  */
static void draw_spell_zone(SDL_Renderer *r, Game *g, int hover_i, int hover_j)
{
    Fighter *me = &g->f[g->turn];
    Skill *s;
    Panoplie *pf;
    int range, i, j;
    if (g->state != ST_BATTLE || g->sel_skill < 0) return;
    if (!unit_is_mine(g, g->turn)) return;
    s = &me->spells[g->sel_skill];
    pf = (me->pano >= 0 && me->pano < N_PANOPLIES) ? &PANOPLIES[me->pano]
                                                   : NULL;
    {
        Panoplie *df = (me->team >= 0 && g->team_dofus[me->team] >= 0)
                     ? &DOFUS[g->team_dofus[me->team]] : NULL;
        range = s->range + (pf ? pf->range_bonus : 0)
              + (df ? df->range_bonus : 0);
    }
    for (j = 0; j < g->map_height; j++)
        for (i = 0; i < g->map_width; i++) {
            int d;
            if (g->cell[j][i] == CELL_VOID) continue;
            d = manhattan(me->x, me->y, i, j);
            if (d > range) continue;
            if (range == 0 && d > 0) continue;  /* sort sur soi         */
            if (i == hover_i && j == hover_j)
                iso_fill_diamond(r, g, i, j,
                                 (SDL_Color){150, 205, 255, 180});
            else
                iso_fill_diamond(r, g, i, j,
                                 (SDL_Color){70, 140, 255, 100});
        }
}

/* survol : ennemi en rouge, allie en vert, contour blanc sinon        */
static void draw_hover_mark(SDL_Renderer *r, Game *g, int hover_i, int hover_j)
{
    int idx;
    if (hover_i < 0 || g->state != ST_BATTLE) return;
    idx = fighter_at(g, hover_i, hover_j);
    if (idx >= 0) {
        int enemy = !unit_is_mine(g, idx);
        SDL_Color fill;
        if (g->colorblind_mode)
            fill = enemy ? (SDL_Color){255, 205, 45, 120}
                         : (SDL_Color){45, 155, 255, 120};
        else
            fill = enemy ? (SDL_Color){235, 60, 50, 120}
                         : (SDL_Color){70, 230, 80, 120};
        iso_fill_diamond(r, g, hover_i, hover_j, fill);
        iso_outline_diamond(r, g, hover_i, hover_j,
                            (SDL_Color){255, 255, 255, 220});
    } else {
        iso_outline_diamond(r, g, hover_i, hover_j,
                            (SDL_Color){255, 255, 255, 110});
    }
}

/* ------------------------------------------------------------------ */
/* Aides visuelles : vert = deplacement, bleu = portee, rouge = attaques */
/* ------------------------------------------------------------------ */

/* marque en rouge les ennemis que le sort selectionne peut toucher    */
static void draw_attack_targets(SDL_Renderer *r, Game *g)
{
    Fighter *me;
    Skill *s;
    Panoplie *pf, *df;
    int range, i;
    if (g->state != ST_BATTLE || g->sel_skill < 0) return;
    if (!unit_is_mine(g, g->turn)) return;
    me = &g->f[g->turn];
    s = &me->spells[g->sel_skill];
    if (s->kind != SK_ATK && !(s->kind == SK_SPECIAL && s->power > 0))
        return;
    pf = (me->pano >= 0 && me->pano < N_PANOPLIES) ? &PANOPLIES[me->pano]
                                                   : NULL;
    df = (g->team_dofus[me->team] >= 0) ? &DOFUS[g->team_dofus[me->team]]
                                         : NULL;
    range = s->range + (pf ? pf->range_bonus : 0)
              + (df ? df->range_bonus : 0);
    for (i = 0; i < 2 * g->team_size; i++) {
        Fighter *o = &g->f[i];
        if (!o->alive || unit_is_mine(g, i)) continue;
        if (manhattan(me->x, me->y, o->x, o->y) > range) continue;
        if (!los_clear(g, me->x, me->y, o->x, o->y)) continue;
        iso_fill_diamond(r, g, o->x, o->y,
                         (SDL_Color){235, 60, 50, 110});
        iso_outline_diamond(r, g, o->x, o->y,
                            (SDL_Color){255, 90, 80, 220});
    }
    for (i = 0; i < MAX_SUMMONS; i++) {
        Summon *sm = &g->summons[i];
        if (!sm->active || sm->team == me->team) continue;
        if (manhattan(me->x, me->y, sm->x, sm->y) > range) continue;
        iso_fill_diamond(r, g, sm->x, sm->y,
                         (SDL_Color){235, 60, 50, 110});
        iso_outline_diamond(r, g, sm->x, sm->y,
                            (SDL_Color){255, 90, 80, 220});
    }
}

/* aide contextuelle au survol : infos sur l'entite cliquable           */
static void draw_battle_tooltip(SDL_Renderer *r, TTF_Font *font, Game *g,
                                int hover_i, int hover_j, int mx, int my)
{
    char lines[6][128];
    int n = 0, i;
    tip_reset();
    if (g->state != ST_BATTLE || hover_i < 0) return;
    {
        int idx = fighter_at(g, hover_i, hover_j);
        int si = summon_at(g, hover_i, hover_j);
        if (idx >= 0) {
            Fighter *f = &g->f[idx];
            snprintf(lines[n++], sizeof lines[0], "%s - %s (%s)",
                     f->nickname, fighter_name(g, f),
                     unit_is_mine(g, idx) ? "Allie" : "Ennemi");
            snprintf(lines[n++], sizeof lines[0],
                     "PV %d/%d  PA %d  PM %d",
                     f->hp, f->hp_max, f->pa, f->pm);
            if (g->sel_skill >= 0 && unit_is_mine(g, g->turn)
                && g->f[g->turn].alive) {
                Fighter *me = &g->f[g->turn];
                Skill *s = &me->spells[g->sel_skill];
                Panoplie *pf = (me->pano >= 0 && me->pano < N_PANOPLIES)
                             ? &PANOPLIES[me->pano] : NULL;
                Panoplie *po = (f->pano >= 0 && f->pano < N_PANOPLIES)
                             ? &PANOPLIES[f->pano] : NULL;
                Panoplie *df = (g->team_dofus[me->team] >= 0)
                             ? &DOFUS[g->team_dofus[me->team]] : NULL;
                Panoplie *do_ = (g->team_dofus[f->team] >= 0)
                              ? &DOFUS[g->team_dofus[f->team]] : NULL;
                int range = s->range + (pf ? pf->range_bonus : 0)
                          + (df ? df->range_bonus : 0);
                int valid = 1;
                if (s->pa > me->pa) valid = 0;
                if (me->cd_left[g->sel_skill] > 0) valid = 0;
                if (range > 0 && manhattan(me->x, me->y, hover_i, hover_j)
                    > range) valid = 0;
                if (s->kind == SK_ATK) {
                    if (unit_is_mine(g, idx)) valid = 0;
                    if (!los_clear(g, me->x, me->y, hover_i, hover_j))
                        valid = 0;
                } else if (s->kind == SK_HEAL) {
                    if (!unit_is_mine(g, idx)) valid = 0;
                } else if (s->kind == SK_SPECIAL && s->power > 0
                           && !unit_is_mine(g, idx)) {
                    if (!los_clear(g, me->x, me->y, hover_i, hover_j))
                        valid = 0;
                }
                if (valid) {
                    if (s->kind == SK_ATK) {
                        int dmg_pct = (pf ? pf->dmg_pct : 0) + (df ? df->dmg_pct : 0);
                        int puissance = (pf ? pf->puissance : 0) + (df ? df->puissance : 0);
                        int dmg = ((s->power * (100 + me->atk_mod)) / 100)
                                  * (100 + dmg_pct) / 100
                                  + puissance;
                        int reflect = (po ? po->reflect : 0) + (do_ ? do_->reflect : 0);
                        if (reflect > 0)
                            snprintf(lines[n++], sizeof lines[0],
                                     "Degats estimes: -%d  (renvoi -%d)",
                                     dmg, reflect);
                        else
                            snprintf(lines[n++], sizeof lines[0],
                                     "Degats estimes: -%d", dmg);
                    } else if (s->kind == SK_HEAL) {
                        int heal_pct = (pf ? pf->heal_pct : 0) + (df ? df->heal_pct : 0);
                        int heal = (s->power * (100 + heal_pct)) / 100;
                        snprintf(lines[n++], sizeof lines[0],
                                 "Soin estime: +%d", heal);
                    } else if (s->kind == SK_SPECIAL) {
                        if (s->power > 0 && f->team != me->team) {
                            int dmg = s->power + (pf ? pf->puissance : 0)
                                    + (df ? df->puissance : 0);
                            int reflect = (po ? po->reflect : 0) + (do_ ? do_->reflect : 0);
                            if (reflect > 0)
                                snprintf(lines[n++], sizeof lines[0],
                                         "Degats estimes: -%d (renvoi -%d)",
                                         dmg, reflect);
                            else
                                snprintf(lines[n++], sizeof lines[0],
                                         "Degats estimes: -%d", dmg);
                        } else if (s->power > 0) {
                            snprintf(lines[n++], sizeof lines[0],
                                     "Soin estime: +%d", s->power);
                        } else {
                            snprintf(lines[n++], sizeof lines[0],
                                     "Effet special");
                        }
                    } else {
                        snprintf(lines[n++], sizeof lines[0],
                                 "Buff +%d %%", s->power);
                    }
                    if (idx == g->turn) {
                        char tmp[128];
                        snprintf(tmp, sizeof tmp, "%s (soi)", lines[n-1]);
                        snprintf(lines[n-1], sizeof lines[0], "%s", tmp);
                    }
                } else {
                    snprintf(lines[n++], sizeof lines[0],
                             "Cible invalide / hors de portee");
                }
            }
        } else if (si >= 0) {
            /* survol d'une invocation                                      */
            Summon *sm = &g->summons[si];
            snprintf(lines[n++], sizeof lines[0], "%s (invocation)",
                     sm->name);
            snprintf(lines[n++], sizeof lines[0], "PV %d/%d  ATK %d  P%d",
                     sm->hp, sm->hp_max, sm->atk, sm->range);
            snprintf(lines[n++], sizeof lines[0],
                     "Allie a %s", unit_is_mine(g, sm->owner)
                     ? "votre equipe" : "l'equipe adverse");
        } else {
            Fighter *me = unit_is_mine(g, g->turn) ? &g->f[g->turn] : NULL;
            snprintf(lines[n++], sizeof lines[0], "Case (%d,%d)",
                     hover_i, hover_j);
            if (me && g->sel_skill < 0) {
                int dist[GRID_H][GRID_W];
                move_distances(g, me->x, me->y, dist);
                if (dist[hover_j][hover_i] > 0)
                    snprintf(lines[n++], sizeof lines[0],
                             "Cout: %d PM",
                             dist[hover_j][hover_i]);
            }
        }
    }
    if (n == 0) return;
    tip_begin(NULL);
    for (i = 0; i < n; i++)
        tip_add("%s", lines[i]);
    draw_tooltip(r, font, mx, my);
}

/* ------------------------------------------------------------------ */
/* Rendu « papier » : decoupe 3D (face + dos epais) et ombre portee   */
/* ------------------------------------------------------------------ */

/* calcule le rectangle des pixels visibles d'une frame                 */
static void content_box(SDL_Surface *s, int sx, int sy, int sw, int sh,
                        ContentBox *out)
{
    int x, y;
    out->x0 = sw; out->y0 = sh; out->x1 = -1; out->y1 = -1;
    if (!s || sw <= 0 || sh <= 0) {
        out->x0 = out->y0 = 0;
        out->x1 = sw > 0 ? sw - 1 : 0;
        out->y1 = sh > 0 ? sh - 1 : 0;
        return;
    }
    if (SDL_MUSTLOCK(s) && SDL_LockSurface(s) != 0) return;
    for (y = 0; y < sh; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch) + sx;
        for (x = 0; x < sw; x++) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(row[x], s->format, &r, &g, &b, &a);
            if (a > 16) {
                if (x < out->x0) out->x0 = x;
                if (y < out->y0) out->y0 = y;
                if (x > out->x1) out->x1 = x;
                if (y > out->y1) out->y1 = y;
            }
        }
    }
    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
    if (out->x1 < 0) {           /* frame vide : on prend tout */
        out->x0 = out->y0 = 0;
        out->x1 = sw - 1;
        out->y1 = sh - 1;
    }
}

/* Personnage en feuille epaisse : le dos (miroir assombri) decale de
   quelques pixels donne l'epaisseur de la decoupe.  Le personnage est
   centre horizontalement sur la case et pose (pieds) sur son centre.  */
static void draw_paper_character(SDL_Renderer *renderer, int row,
                                 int sequence, int frame,
                                 int cx, int cy, int target_h,
                                 int tint_opponent, int thickness)
{
    const ContentBox *b;
    SDL_Rect source, target, back;
    int frame_index = frame % CHARACTER_SEQUENCE_FRAME_COUNT;
    int cw, ch, w, h;
    float scale;
    if (!g_characters_sheet || row < 0 || row >= CHARACTER_ROW_COUNT
        || sequence < 0 || sequence >= CHARACTER_SEQUENCE_COUNT)
        return;
    frame_index = frame % CHARACTER_SEQUENCE_FRAME_COUNT;
    b = &g_char_boxes[row][sequence][frame_index];
    cw = b->x1 - b->x0 + 1;
    ch = b->y1 - b->y0 + 1;
    if (cw <= 0 || ch <= 0 || target_h <= 0) return;
    scale = (float)target_h / (float)ch;
    w = (int)(cw * scale);
    h = (int)(ch * scale);
    source.x = CHARACTER_FRAME_X[sequence][frame_index] + b->x0;
    source.y = row * CHARACTER_FRAME_H + b->y0;
    source.w = cw;
    source.h = ch;
    target.x = cx - w / 2;         /* centre sur la case                 */
    target.y = cy - h;             /* les pieds touchent le sol          */
    target.w = w;
    target.h = h;
    /* dos de la feuille : copie miroir, assombrie et decalee */
    back = target;
    back.x += thickness;
    back.y += thickness / 2;
    SDL_SetTextureColorMod(g_characters_sheet, 74, 62, 54);
    SDL_RenderCopyEx(renderer, g_characters_sheet, &source, &back,
                     0.0, NULL, SDL_FLIP_HORIZONTAL);
    /* face avant */
    if (tint_opponent)
        SDL_SetTextureColorMod(g_characters_sheet, 255,
                               tint_opponent == 2 ? 220 : 190,
                               tint_opponent == 2 ? 90 : 190);
    else
        SDL_SetTextureColorMod(g_characters_sheet, 255, 255, 255);
    SDL_RenderCopy(renderer, g_characters_sheet, &source, &target);
    SDL_SetTextureColorMod(g_characters_sheet, 255, 255, 255);
}

/* Creature en decoupe papier (invocations et monstres du Songe)        */
static void draw_paper_creature(SDL_Renderer *renderer, int frame,
                                int cx, int cy, int target_h,
                                int thickness, int tint)
{
    const ContentBox *b;
    SDL_Rect source, target, back;
    int cw, ch, w, h;
    float scale;
    if (!g_creatures_sheet || frame < 0 || frame >= N_CREATURES) return;
    b = &g_creature_boxes[frame];
    cw = b->x1 - b->x0 + 1;
    ch = b->y1 - b->y0 + 1;
    if (cw <= 0 || ch <= 0 || target_h <= 0) return;
    scale = (float)target_h / (float)ch;
    w = (int)(cw * scale);
    h = (int)(ch * scale);
    source.x = g_creature_frames[frame].x + b->x0;
    source.y = g_creature_frames[frame].y + b->y0;
    source.w = cw;
    source.h = ch;
    target.x = cx - w / 2;
    target.y = cy - h;
    target.w = w;
    target.h = h;
    back = target;
    back.x += thickness;
    back.y += thickness / 2;
    SDL_SetTextureColorMod(g_creatures_sheet, 78, 66, 58);
    SDL_RenderCopyEx(renderer, g_creatures_sheet, &source, &back,
                     0.0, NULL, SDL_FLIP_HORIZONTAL);
    if (tint == 1)      /* monstre : touche violette de la nuit      */
        SDL_SetTextureColorMod(g_creatures_sheet, 235, 205, 255);
    else if (tint == 2) /* allie : touche verte                      */
        SDL_SetTextureColorMod(g_creatures_sheet, 220, 255, 230);
    else
        SDL_SetTextureColorMod(g_creatures_sheet, 255, 255, 255);
    SDL_RenderCopy(renderer, g_creatures_sheet, &source, &target);
    SDL_SetTextureColorMod(g_creatures_sheet, 255, 255, 255);
}

/* dessine le combattant : frame courante du spritesheet ou repli
 * (position isometrique : les pieds sur le centre du losange)          */
static void draw_fighter(SDL_Renderer *r, Game *g, int idx, TTF_Font *font)
{
    Fighter *f = &g->f[idx];
    int sx, sy;
    SDL_Rect d;
    int mine = unit_is_mine(g, idx);
    if (!f->alive) return;
    iso_center(g, f->x, f->y, &sx, &sy);
    d.x = sx - TILE / 2;
    d.y = sy - TILE + 6;
    d.w = TILE;
    d.h = TILE - 6;

    if (f->cls == CLASS_MONSTER) {
        /* monstre du Songe : decoupe papier de creature                   */
        int frame = g->monster_tmpl[idx];
        iso_fill_diamond(r, g, f->x, f->y, (SDL_Color){0, 0, 0, 85});
        draw_paper_creature(r, frame, sx, sy, 34, 3, 1);
    } else if (g_characters_sheet && f->cls >= 0 && f->cls < N_CLASSES) {
        /* ombre portee sous le combattant (papier 2.5D)                  */
        iso_fill_diamond(r, g, f->x, f->y, (SDL_Color){0, 0, 0, 85});
        draw_paper_character(r, CLASS_CHARACTER_ROW[f->cls],
                             character_sequence_for_anim(f->anim),
                             f->anim_frame, sx, sy, 36,
                             mine ? 0 : (g->colorblind_mode ? 2 : 1), 3);
    } else {
        iso_fill_diamond(r, g, f->x, f->y,
                         (SDL_Color){mine ? 40 : 200,
                                     mine ? 120 : 40,
                                     mine ? 240 : 40,
                                     255});
    }
    /* marqueur du joueur actif : losange autour de sa case            */
    if (g->state == ST_BATTLE && g->turn == idx) {
        SDL_Color mark;
        if (g->colorblind_mode)
            mark = mine ? (SDL_Color){45, 155, 255, 255}
                        : (SDL_Color){255, 205, 45, 255};
        else
            mark = mine ? (SDL_Color){40, 255, 80, 255}
                        : (SDL_Color){255, 55, 55, 255};
        /* halo doux sous le marqueur                                    */
        iso_fill_diamond(r, g, f->x, f->y,
                         (SDL_Color){mark.r, mark.g, mark.b, 55});
        iso_outline_diamond(r, g, f->x, f->y, mark);
        {
            int px, py;
            iso_top_vertex(g, f->x, f->y, &px, &py);
            px += ISO_TILE_W / 4;
            py += ISO_TILE_H / 4;
            SDL_SetRenderDrawColor(r, mark.r, mark.g, mark.b, mark.a);
            SDL_RenderDrawLine(r, px, py, px, py + ISO_TILE_H / 2);
            SDL_RenderDrawLine(r, px, py + ISO_TILE_H / 2,
                               px - ISO_TILE_W / 4, py + ISO_TILE_H / 4);
            SDL_RenderDrawLine(r, px - ISO_TILE_W / 4, py + ISO_TILE_H / 4,
                               px, py);
            SDL_RenderDrawLine(r, px, py,
                               px + ISO_TILE_W / 4, py + ISO_TILE_H / 4);
            SDL_RenderDrawLine(r, px + ISO_TILE_W / 4, py + ISO_TILE_H / 4,
                               px, py + ISO_TILE_H / 2);
        }
    }
    /* barre de vie du nouvel atlas UI, ou repli colore                 */
    {
        SDL_Rect hb = {sx - 18, sy - TILE - 4, 36, 8};
        SDL_Rect bar = {UI_HP_BAR_X, UI_HP_BAR_Y, UI_HP_BAR_W, UI_HP_BAR_H};
        draw_ui_sprite(r, bar, hb);
        if (!g_ui_atlas) {
            int fill = (hb.w * f->hp) / (f->hp_max > 0 ? f->hp_max : MAX_HP);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderFillRect(r, &hb);
            SDL_Rect in = {hb.x + 1, hb.y + 1, fill > 2 ? fill - 2 : 0, hb.h - 2};
            if (f->hp > MAX_HP/2)      SDL_SetRenderDrawColor(r, 80, 220, 80, 255);
            else if (f->hp > MAX_HP/5) SDL_SetRenderDrawColor(r, 240, 180, 40, 255);
            else                       SDL_SetRenderDrawColor(r, 220, 60, 50, 255);
            SDL_RenderFillRect(r, &in);
        }
        if (g_ui_atlas && f->hp < f->hp_max) {
            int fill = (hb.w - 8) * f->hp
                     / (f->hp_max > 0 ? f->hp_max : MAX_HP);
            SDL_Rect depleted = {hb.x + 4 + fill, hb.y + 2,
                                 hb.w - 8 - fill, hb.h - 4};
            SDL_SetRenderDrawColor(r, 20, 20, 25, 255);
            SDL_RenderFillRect(r, &depleted);
        }
    }
    char hp[32];
    snprintf(hp, sizeof hp, "%d", f->hp);
    SDL_Color c = {255, 255, 255, 255};
    {
        const char *nm = fighter_name(g, f);
        int w = (int)strlen(nm) * 6;
        draw_text(r, font, nm, sx - w / 2, sy - TILE - 18, c);
    }
    draw_text(r, font, hp, sx - 10, sy - 20, c);
    if (g->emote_fighter == idx && g->emote_until > SDL_GetTicks())
        draw_emote(g, r);
}

/* dessine une invocation (creature invoquee) sur l'arene              */
static void draw_summon(SDL_Renderer *r, Game *g, int idx)
{
    Summon *sm = &g->summons[idx];
    int sx, sy, frame;
    if (!sm->active) return;
    iso_center(g, sm->x, sm->y, &sx, &sy);
    iso_fill_diamond(r, g, sm->x, sm->y, (SDL_Color){0, 0, 0, 70});
    frame = (sm->tmpl >= 0 && sm->tmpl < SUMMON_FRAME_COUNT)
            ? sm->tmpl : 0;
    draw_paper_creature(r, frame, sx, sy, 26, 2,
                        unit_is_mine(g, sm->owner) ? 2 : 0);
    /* barre de vie de l'invocation                                     */
    {
        SDL_Rect hb = {sx - 14, sy - 24, 28, 5};
        int fill = (hb.w * sm->hp) / (sm->hp_max > 0 ? sm->hp_max : 1);
        SDL_SetRenderDrawColor(r, 12, 12, 16, 220);
        SDL_RenderFillRect(r, &hb);
        SDL_SetRenderDrawColor(r, 90, 220, 120, 255);
        SDL_Rect in = {hb.x + 1, hb.y + 1, fill > 1 ? fill - 1 : 0, hb.h - 2};
        SDL_RenderFillRect(r, &in);
    }
}

/* rectangles cliquables (partages rendu / clics)                      */
static SDL_Rect menu_btn_host(void)  { SDL_Rect r = {20, 70, 320, 40};  return r; }
static SDL_Rect menu_btn_join(void)  { SDL_Rect r = {20, 120, 320, 40}; return r; }
static SDL_Rect menu_btn_solo(void)  { SDL_Rect r = {350, 170, 200, 40}; return r; }
static SDL_Rect menu_btn_bot_model(void) { SDL_Rect r = {350, 216, 200, 34}; return r; }
static SDL_Rect menu_btn_ipfield(void){SDL_Rect r = {350, 120, 200, 40};return r; }
static SDL_Rect menu_size_btn(int i) { SDL_Rect r = {350 + i*66, 70, 60, 40}; return r; }
static SDL_Rect menu_settings_btn(void) { SDL_Rect r = {880, 70, 124, 40}; return r; }
static SDL_Rect menu_btn_songe(void) { SDL_Rect r = {20, 170, 320, 40}; return r; }
static SDL_Rect menu_btn_challenges(void) { SDL_Rect r = {20, 220, 320, 40}; return r; }
static SDL_Rect menu_btn_defs(void) { SDL_Rect r = {350, 256, 200, 34}; return r; }
static SDL_Rect defs_cat_rect(int i) { SDL_Rect r = {24, 74 + i * 52, 200, 44}; return r; }
/* pave numerique du code PIN                                        */
static SDL_Rect pin_key(int i) { SDL_Rect r = {392 + (i % 3) * 88, 170 + (i / 3) * 62, 80, 54}; return r; }
static SDL_Rect pin_field(void) { SDL_Rect r = {392, 108, 240, 44}; return r; }
static SDL_Rect pin_forget(void) { SDL_Rect r = {24, 60, 190, 34}; return r; }
static SDL_Rect defs_back_btn(void) { SDL_Rect r = {WIN_W - 150, WIN_H - 60, 130, 32}; return r; }
static SDL_Rect defs_up_btn(void) { SDL_Rect r = {WIN_W - 60, 74, 40, 30}; return r; }
static SDL_Rect defs_down_btn(void) { SDL_Rect r = {WIN_W - 60, WIN_H - 130, 40, 30}; return r; }
static SDL_Rect pin_quit(void) { SDL_Rect r = {810, 60, 190, 34}; return r; }
static SDL_Rect menu_btn_achievements(void) { SDL_Rect r = {20, 270, 320, 40}; return r; }
/* ecran Songe                                                         */
static SDL_Rect songe_party_btn(int i) { SDL_Rect r = {40 + i*80, 168, 70, 46}; return r; }
static SDL_Rect songe_btn_solo(void) { SDL_Rect r = {40, 246, 240, 44}; return r; }
static SDL_Rect songe_btn_host(void) { SDL_Rect r = {40, 300, 240, 44}; return r; }
static SDL_Rect songe_btn_join(void) { SDL_Rect r = {300, 246, 240, 44}; return r; }
static SDL_Rect songe_btn_ipfield(void) { SDL_Rect r = {300, 300, 240, 40}; return r; }
static SDL_Rect songe_btn_back(void) { SDL_Rect r = {256, 380, 160, 40}; return r; }
/* clavier virtuel de l'ecran Songe (saisie de l'IP)                    */
static SDL_Rect songe_kbd(int i) { SDL_Rect r = {20 + (i%6)*55, 396 + (i/6)*45, 50, 38}; return r; }
static SDL_Rect songe_kbd_ok(void) { SDL_Rect r = {20 + 6*55, 441, 160, 38}; return r; }
/* ecran Defis du jour                                                 */
static SDL_Rect challenge_card(int i) { SDL_Rect r = {24, 96 + i*116, 624, 106}; return r; }
static SDL_Rect challenge_btn(int i)  { SDL_Rect r = {560, 148 + i*116, 76, 32}; return r; }
static SDL_Rect challenge_back_btn(void) { SDL_Rect r = {440, 424, 140, 32}; return r; }
static SDL_Rect settings_panel(void) { SDL_Rect r = {72, 64, 528, 480}; return r; }
static SDL_Rect settings_row(int row)
{ SDL_Rect r = {96, 132 + row * 50, 480, 40}; return r; }
static SDL_Rect settings_minus_btn(int row)
{ SDL_Rect r = {454, 136 + row * 50, 36, 32}; return r; }
static SDL_Rect settings_plus_btn(int row)
{ SDL_Rect r = {500, 136 + row * 50, 36, 32}; return r; }
static SDL_Rect settings_toggle_btn(int row)
{ SDL_Rect r = {444, 136 + row * 50, 120, 32}; return r; }
static SDL_Rect settings_back_btn(void)
{ SDL_Rect r = {264, 494, 144, 34}; return r; }
static SDL_Rect favorite_btn(int i)  { SDL_Rect r = {20 + i*126, 414, 118, 32}; return r; }
/* clavier virtuel pour l'IP (12 touches : 123456 / 7890.⌫ / OK)        */
static SDL_Rect menu_kbd(int i)     { SDL_Rect r = {20 + (i%6)*55, 180 + (i/6)*45, 50, 40}; return r; }
/* autres rectangles cliquables                                      */
static SDL_Rect pick_class_rect(int i){SDL_Rect r = {15 + (i%3)*220, 55 + (i/3)*30, 212, 28}; return r; }
static SDL_Rect pick_favorite_rect(int i){SDL_Rect r = {20 + (i%3)*220, 340 + (i/3)*34, 210, 30}; return r; }
static SDL_Rect draft_btn(void)     { SDL_Rect r = {20, 434, 260, 32};  return r; }
static SDL_Rect stuff_card(int i)    { SDL_Rect r = {16 + (i%3)*216, 54 + (i/3)*49, 208, 45}; return r; }
static SDL_Rect stuff_favorite_btn(int i){SDL_Rect r = {16 + i*100, 446, 92, 28}; return r;}
static SDL_Rect element_btn(int element, int add) { SDL_Rect r = {460 + add*70, 160 + element*60, 60, 36}; return r; }
static SDL_Rect endturn_btn(int y0)  { SDL_Rect r = {10, y0 + 92, 150, 48}; return r; }
static SDL_Rect chat_btn(int y0)     { SDL_Rect r = {170, y0 + 92, 90, 30}; return r; }
static SDL_Rect chat_field(int y0)   { SDL_Rect r = {270, y0 + 92, 300, 30}; return r; }
static SDL_Rect chat_send(int y0)    { SDL_Rect r = {580, y0 + 92, 80, 30}; return r; }
static SDL_Rect ingame_menu_btn(int y0) { SDL_Rect r = {868, y0 + 66, 112, 24}; return r; }
static SDL_Rect ingame_menu_panel(void) { SDL_Rect r = {186, 196, 300, 248}; return r; }
static SDL_Rect ingame_menu_option(int option)
{ SDL_Rect r = {201, 245 + option * 44, 270, 36}; return r; }
static SDL_Rect surrender_dialog(void) { SDL_Rect r = {156, 244, 360, 152}; return r; }
static SDL_Rect surrender_dialog_button(int confirm)
{ SDL_Rect r = {172 + confirm * 172, 340, 160, 36}; return r; }
static int pt_in(SDL_Rect r, int x, int y)
{ return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

static void draw_settings(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    static const char *labels[] = {
        "Volume de la musique",
        "Volume des effets sonores",
        "Taille de fenetre",
        "Mode daltonien",
        "Reduire les flashs (epilepsie)",
        "Sous-titres si effets muets",
        "Plein ecran sans bordure (F11)"
    };
    SDL_Rect panel = settings_panel();
    SDL_Color white = {245, 235, 215, 255};
    SDL_Color muted = {175, 165, 150, 255};
    int i, mx, my;
    mouse_logical(&mx, &my);

    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderFillRect(r, &panel);
    draw_ui_panel(r, panel);
    draw_ui_banner(r, (SDL_Rect){88, 70, 496, 48});
    draw_text(r, font, "PARAMETRES", 270, 84, white);
    for (i = 0; i < 7; i++) {
        SDL_Rect row = settings_row(i);
        SDL_SetRenderDrawColor(r, 31, 27, 28, 240);
        SDL_RenderFillRect(r, &row);
        draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, row);
        draw_text(r, font, labels[i], row.x + 14, row.y + 12, white);
        if (i < 2) {
            SDL_Rect minus = settings_minus_btn(i);
            SDL_Rect plus = settings_plus_btn(i);
            int volume = i == 0 ? g->music_volume : g->sfx_volume;
            draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, minus);
            draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, plus);
            draw_text(r, font, "-", minus.x + 14, minus.y + 7, white);
            draw_text(r, font, "+", plus.x + 13, plus.y + 7, white);
            {
                char value[8];
                snprintf(value, sizeof value, "%d%%", volume);
                draw_text(r, font, value, 540, row.y + 12, white);
            }
        } else if (i == 2) {
            draw_text(r, font, "1024 x 480 (fixe, comme nos habitudes)", 448, row.y + 12, white);
        } else {
            SDL_Rect toggle = settings_toggle_btn(i);
            int enabled = i == 3 ? g->colorblind_mode
                        : i == 4 ? g->reduce_flashing
                        : i == 5 ? g->subtitles_when_muted
                                 : g->fullscreen;
            SDL_SetRenderDrawColor(r, enabled ? 57 : 65,
                                   enabled ? 105 : 53,
                                   enabled ? 70 : 49, 255);
            SDL_RenderFillRect(r, &toggle);
            draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, toggle);
            draw_text(r, font, enabled ? "OUI" : "NON",
                      toggle.x + 12, toggle.y + 9, white);
        }
        if (pt_in(row, mx, my))
            SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        else
            SDL_SetRenderDrawColor(r, 115, 91, 66, 255);
        SDL_RenderDrawRect(r, &row);
    }
    {
        SDL_Rect back = settings_back_btn();
        SDL_SetRenderDrawColor(r, 64, 48, 38, 255);
        SDL_RenderFillRect(r, &back);
        draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, back);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &back);
        draw_text(r, font, "RETOUR", back.x + 42, back.y + 9, white);
    }
    draw_text(r, font, "ECHAP pour revenir (ou furter avec les reglages)", 236, 556, muted);
}

static void draw_audio_subtitle(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Rect banner;
    if (!g->audio_subtitle[0]
            || (Sint32)(g->audio_subtitle_until - SDL_GetTicks()) <= 0)
        return;
    banner = (SDL_Rect){WIN_W / 2 - 110, 6, 220, 38};
    SDL_SetRenderDrawColor(r, 20, 17, 18, 240);
    SDL_RenderFillRect(r, &banner);
    draw_ui_panel(r, banner);
    draw_text(r, font, g->audio_subtitle, banner.x + 20,
              banner.y + 11, (SDL_Color){255, 240, 210, 255});
}

/* ------------------------------------------------------------------ */
/* Nouvelle interface : panneau d'etat du joueur (HUD)                 */
/* ------------------------------------------------------------------ */
static void draw_player_status(SDL_Renderer *r, Game *g, TTF_Font *font,
                               int x, int y, int w, int h)
{
    Fighter *me = &g->f[g->turn];
    SDL_Color white = {250, 245, 230, 255};
    SDL_Color gold  = {255, 214, 120, 255};
    SDL_Color dark  = {30, 24, 20, 255};
    SDL_Rect panel = {x, y, w, h};
    int portrait = 42;
    int bar_x, bar_y, bar_w, bar_h;
    /* le cadre papier de Humble Pixel est clair : texte sombre          */
    if (g_hp_panel) {
        white = (SDL_Color){60, 48, 32, 255};
        gold  = (SDL_Color){150, 96, 24, 255};
    }

    /* cadre : papier Humble Pixel si present, sinon 9-slice parchemin   */
    if (g_hp_panel) {
        SDL_SetTextureAlphaMod(g_hp_panel, 235);
        SDL_RenderCopy(r, g_hp_panel, NULL, &panel);
        SDL_SetTextureAlphaMod(g_hp_panel, 255);
    } else {
        draw_nine_slice(r, g_ui9[UI9_PANEL_TAN], panel, 16);
    }
    SDL_SetRenderDrawColor(r, 60, 44, 28, 120);
    SDL_RenderDrawRect(r, &(SDL_Rect){x + 3, y + 3, w - 6, h - 6});

    /* portrait du combattant                                           */
    if (g_characters_sheet && me->cls >= 0 && me->cls < N_CLASSES) {
        SDL_Rect frame = {x + 6, y + 6, portrait, portrait};
        if (g_hud_frame[HUD_PORTRAIT].ok)
            draw_hud_frame(r, HUD_PORTRAIT, frame, 0);
        else
            draw_nine_slice(r, g_ui9[UI9_PANEL_BROWN], frame, 12);
        draw_character_sprite(r, CLASS_CHARACTER_ROW[me->cls], 0, 0,
                              (SDL_Rect){x + 10, y + 10, portrait - 8,
                                         portrait - 8}, 0);
    }
    /* nom + classe                                                     */
    draw_text(r, font, CLASSES[me->cls].name, x + 6 + portrait + 6, y + 6,
              gold);
    {
        char l[96];
        snprintf(l, sizeof l, "T%d - P%d   PV %d/%d",
                 me->team + 1, g->turn % g->team_size + 1, me->hp, me->hp_max);
        draw_text(r, font, l, x + 6 + portrait + 6, y + 22, white);
    }
    /* jauge de vie (HUD importe si disponible, sinon dessin interne)    */
    bar_x = x + 6 + portrait + 6;
    bar_y = y + 40;
    bar_w = w - (bar_x - x) - 10;
    bar_h = 12;
    if (g_hp_bar) {
        /* barre segmentee Humble Pixel (rouge) + liseré doré            */
        int fill = (bar_w * me->hp) / (me->hp_max > 0 ? me->hp_max : 1);
        SDL_Rect hb = {bar_x, bar_y, bar_w, bar_h};
        draw_seg_bar(r, g_hp_bar, g_hp_bar_frame, hb,
                     (bar_w > 0) ? (fill * 100) / bar_w : 0);
        SDL_SetRenderDrawColor(r, gold.r, gold.g, gold.b, 200);
        SDL_RenderDrawRect(r, &hb);
    } else if (g_hud_frame[HUD_HPBAR].ok) {
        SDL_Rect hb = {bar_x, bar_y, bar_w, bar_h};
        draw_hud_frame(r, HUD_HPBAR, hb, 0);
        {
            int fill = (bar_w * me->hp) / (me->hp_max > 0 ? me->hp_max : 1);
            SDL_Rect hf = {bar_x, bar_y, fill > 0 ? fill : 0, bar_h};
            draw_hud_frame(r, HUD_HPFILL, hf, 1);
        }
    } else {
        SDL_SetRenderDrawColor(r, 40, 20, 18, 220);
        SDL_RenderFillRect(r, &(SDL_Rect){bar_x, bar_y, bar_w, bar_h});
        {
            int fill = (bar_w * me->hp) / (me->hp_max > 0 ? me->hp_max : 1);
            SDL_SetRenderDrawColor(r, 190, 45, 40, 255);
            SDL_RenderFillRect(r, &(SDL_Rect){bar_x + 1, bar_y + 1,
                                             fill > 1 ? fill - 1 : 0, bar_h - 2});
            SDL_SetRenderDrawColor(r, 250, 180, 160, 120);
            SDL_RenderFillRect(r, &(SDL_Rect){bar_x + 1, bar_y + 1,
                                             fill > 1 ? fill - 1 : 0, 3});
        }
        SDL_SetRenderDrawColor(r, gold.r, gold.g, gold.b, 200);
        SDL_RenderDrawRect(r, &(SDL_Rect){bar_x, bar_y, bar_w, bar_h});
    }
    /* PA / PM / initiative : pastilles                                  */
    {
        int i;
        char l[32];
        snprintf(l, sizeof l, "PA %d", me->pa);
        draw_text(r, font, l, bar_x, bar_y + 16, white);
        snprintf(l, sizeof l, "PM %d", me->pm);
        draw_text(r, font, l, bar_x + 56, bar_y + 16, white);
        snprintf(l, sizeof l, "INIT %d", me->initiative);
        draw_text(r, font, l, bar_x + 116, bar_y + 16, white);
        /* points d'action en pastilles                                    */
        for (i = 0; i < me->pa && i < 8; i++) {
            if (g_hud_frame[HUD_PA].ok)
                draw_hud_frame(r, HUD_PA, (SDL_Rect){bar_x + 200 + i * 11,
                                                    bar_y + 16, 9, 9}, 0);
            else {
                SDL_SetRenderDrawColor(r, 255, 220, 120, 255);
                SDL_RenderFillRect(r, &(SDL_Rect){bar_x + 200 + i * 9,
                                                 bar_y + 17, 6, 6});
            }
        }
        if (g_hp_mp) {
            /* barre de deplacement segmentee (vert)                      */
            SDL_Rect mb = {bar_x, bar_y + 30, 120, 7};
            draw_seg_bar(r, g_hp_mp, NULL, mb,
                         (me->pm * 100) / (me->pm > 0 ? me->pm : 1));
        }
        for (i = 0; i < me->pm && i < 8; i++) {
            if (g_hud_frame[HUD_PM].ok)
                draw_hud_frame(r, HUD_PM, (SDL_Rect){bar_x + 200 + i * 11,
                                                    bar_y + 26, 9, 9}, 0);
            else {
                SDL_SetRenderDrawColor(r, 120, 220, 255, 255);
                SDL_RenderFillRect(r, &(SDL_Rect){bar_x + 200 + i * 9,
                                                 bar_y + 26, 6, 6});
            }
        }
    }
    (void)dark;
}

static void draw_skillbar(SDL_Renderer *r, Game *g, TTF_Font *font)
{
    Fighter *me = &g->f[g->turn];
    int y0 = 0;                     /* bandeau en haut (mode paysage)      */
    SDL_Rect panel = {0, y0, WIN_W, BAND_H};
    SDL_SetRenderDrawColor(r, 26, 22, 20, 255);
    SDL_RenderFillRect(r, &panel);
    draw_nine_slice(r, g_ui9[UI9_PANEL_BROWN], panel, 16);
    SDL_SetRenderDrawColor(r, 12, 10, 9, 235);
    SDL_RenderFillRect(r, &(SDL_Rect){2, 2, WIN_W - 4, BAND_H - 4});
    /* panneau d'etat du joueur (nouvelle interface)                    */
    draw_player_status(r, g, font, 8, 6, 330, 74);
    char line[128];
    SDL_Color white = {255,255,255,255};
    snprintf(line, sizeof line,
             "Tour %d  -  T%d P%d",
             g->turn_count + 1, me->team + 1,
             g->turn % g->team_size + 1);
    draw_ui_sprite(r, (SDL_Rect){4, 2, 29, 31},
                   (SDL_Rect){8, y0 + 5, 24, 25});
    draw_text(r, font, line, 350, y0 + 68, white);
    if (g->songe_mode) {
        char lv[64];
        snprintf(lv, sizeof lv, "SONGE - NIVEAU %d (record %d)",
                 g->songe_level, g->songe_best);
        draw_text(r, font, lv, 470, y0 + 68, (SDL_Color){255, 214, 120, 255});
    }

    int k;
    for (k = 0; k < N_EQUIPPED; k++) {
        Skill *s = &me->spells[k];
        SDL_Rect b = {350 + k * 165, y0 + 6, 160, 58};
        SDL_Rect icon = {b.x + 4, b.y + 8, 30, 36};
        if (g_hp_slot) {
            draw_nine_slice_tinted(r, g_hp_slot, b, 8, 78, 70, 60);
        } else {
            draw_nine_slice(r, g_ui9[UI9_PANEL_GREY], b, 12);
        }
        if (g->sel_skill == k) {
            if (g_hp_slot)
                draw_nine_slice_tinted(r, g_hp_slot, b, 8, 165, 130, 80);
            else
                draw_nine_slice(r, g_ui9[UI9_PANEL_TAN], b, 12);
            SDL_SetRenderDrawColor(r, 255, 214, 120, 200);
            SDL_RenderDrawRect(r, &b);
        }
        if (g_spell_icons) {
            SDL_Rect icon_src = spell_icon_rect(me->spell_icon[k]);
            SDL_RenderCopy(r, g_spell_icons, &icon_src, &icon);
        }
        char t[96];
        snprintf(t, sizeof t, "%d %.15s", k+1, s->name);
        draw_text(r, font, t, b.x + 38, b.y + 5, white);
        snprintf(t, sizeof t, "%dPA P%d", s->pa, s->range);
        draw_text(r, font, t, b.x + 38, b.y + 24, white);
        snprintf(t, sizeof t, "effet %d  CD %d/%d",
                 s->power, me->cd_left[k], s->cooldown);
        draw_text(r, font, t, b.x + 38, b.y + 41, white);
    }
    /* infobulle d'aide au survol d'un sort                             */
    {
        int mx, my;
        mouse_logical(&mx, &my);
        tip_reset();
        for (k = 0; k < N_EQUIPPED; k++) {
            SDL_Rect b = {350 + k * 165, y0 + 6, 160, 58};
            if (pt_in(b, mx, my)) {
                Skill *sk = &me->spells[k];
                const char *kind = sk->kind == SK_ATK ? "Attaque" :
                                   sk->kind == SK_HEAL ? "Soin" :
                                   sk->kind == SK_BUFF ? "Buff" :
                                   sk->kind == SK_SUMMON ? "Invocation"
                                                        : "Special";
                tip_begin(sk->name);
                tip_add("%s - %dPA - portee %d", kind, sk->pa, sk->range);
                if (sk->kind == SK_SUMMON) {
                    int t = summon_template_of(sk);
                    if (t >= 0)
                        tip_add("Invoque : %s (PV %d, ATK %d)",
                                SUMMON_TEMPLATES[t].name,
                                SUMMON_TEMPLATES[t].hp,
                                SUMMON_TEMPLATES[t].atk);
                } else {
                    tip_add("Effet %d  -  CD %d/%d", sk->power,
                            me->cd_left[k], sk->cooldown);
                }
                tip_add("Survolez une cible pour l'estimation des degats.");
            }
        }
        if (pt_in(endturn_btn(y0), mx, my)) {
            tip_begin("Fin de tour");
            tip_add("Termine le tour ; vos invocations en profitent.");
        } else if (pt_in(chat_btn(y0), mx, my)) {
            tip_begin("Chat");
            tip_add("Ouvrir le chat. Essayez de rester aimable.");
        } else if (pt_in(ingame_menu_btn(y0), mx, my)) {
            tip_begin("Menu de la partie");
            tip_add("Quitter, se rendre, ou supprimer la fenetre (honnete)." );
        }
        draw_tooltip(r, font, mx, my);
    }
    /* bouton FIN DE TOUR (clic)                                      */
    SDL_Rect be = endturn_btn(y0);
    int my_turn = unit_is_mine(g, g->turn);
    if (my_turn) SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
    else         SDL_SetRenderDrawColor(r, 50, 50, 60, 255);
    SDL_RenderFillRect(r, &be);
    draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, be);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &be);
    draw_text(r, font, my_turn ? "FIN DE TOUR" : "on patiente...",
              be.x + 12, be.y + 6, white);
    draw_text(r, font, "clic = avancer / viser", be.x, be.y + 50, white);
    if (me->pano >= 0)
        /* ligne du bas, a droite de l'aide : pas de chevauchement      */
        draw_text(r, font, PANOPLIES[me->pano].name, 600, be.y + 52, white);
    {
        SDL_Rect menu_button = ingame_menu_btn(y0);
        SDL_SetRenderDrawColor(r, g->in_game_menu_open ? 105 : 70,
                               g->in_game_menu_open ? 85 : 62,
                               g->in_game_menu_open ? 55 : 48, 255);
        SDL_RenderFillRect(r, &menu_button);
        SDL_SetRenderDrawColor(r, 220, 195, 145, 255);
        SDL_RenderDrawRect(r, &menu_button);
        draw_text(r, font, "MENU [ESC]", menu_button.x + 12,
                  menu_button.y + 4, white);
    }

    /* chat : bouton, champ, envoi                                    */
    SDL_Rect bt = chat_btn(y0);
    SDL_Rect bf = chat_field(y0);
    SDL_Rect bs = chat_send(y0);
    SDL_SetRenderDrawColor(r, g->chat_active ? 90 : 60,
                             g->chat_active ? 80 : 60, 120, 255);
    SDL_RenderFillRect(r, &bt);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &bt);
    draw_text(r, font, "Chat", bt.x + 16, bt.y + 6, white);
    SDL_SetRenderDrawColor(r, 25, 25, 25, 255);
    SDL_RenderFillRect(r, &bf);
    SDL_SetRenderDrawColor(r, g->chat_active ? 255 : 160,
                             g->chat_active ? 220 : 160, 80, 255);
    SDL_RenderDrawRect(r, &bf);
    {
        char t[300];
        snprintf(t, sizeof t, "%s%s", g->chat_input,
                 g->chat_active ? "_" : "");
        draw_text(r, font, t[0] ? t : "message...", bf.x + 6, bf.y + 6,
                  g->chat_active ? (SDL_Color){255,220,80,255}
                                 : (SDL_Color){130,130,130,255});
    }
    SDL_SetRenderDrawColor(r, 70, 90, 70, 255);
    SDL_RenderFillRect(r, &bs);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &bs);
    draw_text(r, font, "ENVOYER", bs.x + 6, bs.y + 6, white);

    /* journal : 3 derniers messages                                  */
    {
        int n = g->chat_log_n, first = n > 3 ? n - 3 : 0, q, ly = y0 + 126;
        for (q = first; q < n; q++, ly += 16)
            draw_text(r, font, g->chat_log[q], 170, ly,
                      (SDL_Color){200, 200, 200, 255});
    }
    if (!my_turn)
        draw_text(r, font, g->solo_mode ? "Tour de l'IA..."
                                        : "Tour de l'adversaire...",
                  500, y0 + 6, white);
}



static void draw_mouse_bubble(SDL_Renderer *r, TTF_Font *font, int mx, int my,
                              const char *text);
static void draw_achievements(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color grey = {130,130,130,255};
    SDL_Color green = {80,255,120,255};
    char line[96];
    int i, start = g->achievement_scroll;
    if (start < 0) start = 0;
    if (start > 69 - 18) start = 69 - 18;
    g->achievement_scroll = start;
    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderClear(r);
    draw_ui_panel(r, (SDL_Rect){8, 6, WIN_W - 16, WIN_H - 12});
    draw_text(r, font, "ACHIEVEMENTS (69, et le 70 vous observe)", 20, 14, white);
    for (i = 0; i < 18 && start + i < 69; i++) {
        int idx = start + i;
        snprintf(line, sizeof line, "%s Succes %d",
                 g_achievements[idx] ? "[X]" : "[ ]", idx + 1);
        draw_text(r, font, line, 20, 44 + i * 22,
                  g_achievements[idx] ? green : grey);
    }
    {
        SDL_Rect up = {600, 56, 40, 28};
        SDL_Rect down = {600, 384, 40, 28};
        SDL_Rect back = {260, 560, 150, 34};
        SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
        SDL_RenderFillRect(r, &up);
        SDL_RenderFillRect(r, &down);
        SDL_SetRenderDrawColor(r, 55, 140, 65, 255);
        SDL_RenderFillRect(r, &back);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &up);
        SDL_RenderDrawRect(r, &down);
        SDL_RenderDrawRect(r, &back);
        draw_text(r, font, "UP", up.x + 10, up.y + 6, white);
        draw_text(r, font, "DOWN", down.x + 2, down.y + 6, white);
        draw_text(r, font, "RETOUR", back.x + 34, back.y + 8, white);
        {
            int mx, my;
            mouse_logical(&mx, &my);
            if (pt_in(up, mx, my))
                draw_mouse_bubble(r, font, mx, my, "Page precedente");
            else if (pt_in(down, mx, my))
                draw_mouse_bubble(r, font, mx, my, "Page suivante");
            else if (pt_in(back, mx, my))
                draw_mouse_bubble(r, font, mx, my, "Retour menu");
        }
    }
}

/* bulle suiveuse de la souris pour toutes les zones cliquables        */
static void draw_mouse_bubble(SDL_Renderer *r, TTF_Font *font, int mx, int my,
                              const char *text)
{
    tip_begin(text);
    draw_tooltip(r, font, mx, my);
}

/* ------------------------------------------------------------------ */
/* Infobulle genérique : panneau sombre, titre doré, lignes lisibles   */
/* ------------------------------------------------------------------ */
static void draw_tooltip(SDL_Renderer *r, TTF_Font *font, int mx, int my)
{
    int w = 0, h, i, x, y, ty;
    SDL_Color gold = {255, 214, 120, 255};
    SDL_Color text_col = {226, 230, 240, 255};
    if (!g_tip_active) return;
    if (!g_tip_title[0] && g_tip_count == 0) return;
    for (i = 0; i < g_tip_count; i++) {
        int len = (int)strlen(g_tip_lines[i]);
        if (len * 7 + 24 > w) w = len * 7 + 24;
    }
    if (g_tip_title[0]) {
        int len = (int)strlen(g_tip_title);
        if (len * 8 + 28 > w) w = len * 8 + 28;
    }
    if (w < 90) w = 90;
    ty = g_tip_title[0] ? 22 : 6;
    h = g_tip_count * 16 + ty + 8;
    x = mx + 16;
    y = my + 16;
    if (x + w > WIN_W - 2) x = mx - w - 12;
    if (y + h > WIN_H - 2) y = my - h - 12;
    if (x < 2) x = 2;
    if (y < 2) y = 2;
    {
        SDL_Rect bg = {x, y, w, h};
        SDL_Rect inner = {x + 2, y + 2, w - 4, h - 4};
        /* ombre portee du panneau */
        SDL_SetRenderDrawColor(r, 0, 0, 0, 90);
        SDL_RenderFillRect(r, &(SDL_Rect){x + 3, y + 3, w, h});
        if (g_hp_sheet) {
            draw_nine_slice_tinted(r, g_hp_sheet, bg, 12, 46, 40, 36);
        } else {
            SDL_SetRenderDrawColor(r, 14, 16, 24, 238);
            SDL_RenderFillRect(r, &bg);
        }
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &bg);
        SDL_SetRenderDrawColor(r, 120, 100, 60, 255);
        SDL_RenderDrawRect(r, &inner);
        if (g_tip_title[0]) {
            draw_text(r, font, g_tip_title, x + 8, y + 5, gold);
            SDL_SetRenderDrawColor(r, 120, 100, 60, 255);
            SDL_RenderDrawLine(r, x + 6, y + 21, x + w - 6, y + 21);
        }
        for (i = 0; i < g_tip_count; i++)
            draw_text(r, font, g_tip_lines[i], x + 8, y + ty + i * 16, text_col);
    }
}

static void draw_menu(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color grey  = {130,130,130,255};
    char line[96];
    int mx, my;
    mouse_logical(&mx, &my);
    tip_reset();

    if (g->settings_open) {
        draw_settings(r, font, g);
        return;
    }
    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderClear(r);
    draw_ui_panel(r, (SDL_Rect){8, 6, WIN_W - 16, WIN_H - 12});
    draw_ui_banner(r, (SDL_Rect){14, 8, WIN_W - 28, 48});
    {
        draw_ui_sprite(r, (SDL_Rect){4, 2, 29, 31},
                       (SDL_Rect){22, 15, 24, 25});
        draw_text(r, font, "DOFUS ARENA - LEGEND (FTW°)", 54, 13, white);
        draw_text(r, font, "Seul, a deux, ou dans un reve qui n'en finit pas",
              54, 31, grey);
        draw_character_sprite(r, CLASS_CHARACTER_ROW[0], 0, 0,
                              (SDL_Rect){900, 150, 56, 56}, 0);
        draw_character_sprite(r, CLASS_CHARACTER_ROW[1], 0, 0,
                              (SDL_Rect){960, 150, 56, 56}, 1);
    }
    {
        SDL_Rect settings = menu_settings_btn();
        int hovered = pt_in(settings, mx, my);
        ui_button(r, settings, "OPTIONS", font, hovered, 0, 1);
        if (hovered) {
            tip_begin("Options");
            tip_add("Volumes, mode daltonien, anti-flashs.");
        }
    }
    {
        SDL_Rect ab = menu_btn_achievements();
        int hovered = pt_in(ab, mx, my);
        ui_button(r, ab, "ACHIEVE", font, hovered, 0, 1);
        if (hovered) {
            tip_begin("Achievements");
            tip_add("69 succes a debloquer. Le 70 est encore secret.");
        }
    }
    {
        SDL_Rect sb = menu_btn_songe();
        int hovered = pt_in(sb, mx, my);
        ui_button(r, sb, "SONGE", font, hovered, 0, 1);
        if (hovered) {
            tip_begin("Mode Songe");
            tip_add("Expedition infinie contre des monstres qui s'en lassent pas.");
            tip_add("En solo ou a plusieurs (1 a 5). Plus c'est long, plus c'est beau.");
            tip_add("Record actuel : niveau %d. Le reve attend.", g->songe_best);
        }
    }
    {
        SDL_Rect cb = menu_btn_challenges();
        int hovered = pt_in(cb, mx, my);
        ui_button(r, cb, "DEFIS", font, hovered, 0, 1);
        if (hovered) {
            tip_begin("Defis du jour");
            tip_add("3 defis par jour, et ils se renouvellent seuls.");
            tip_add("Score : %d points (depensez-les en cafe).", g->challenge_points);
        }
    }
    {
        SDL_Rect db = menu_btn_defs();
        int hovered = pt_in(db, mx, my);
        ui_button(r, db, "DEFINITIONS", font, hovered, 0, 1);
        if (hovered) {
            tip_begin("Definitions");
            tip_add("Le manuel de survie : regles, classes, equipement.");
            tip_add("F11 pour le plein ecran sans bordure.");
        }
    }
    draw_text(r, font, "Format (le gore ne compte que sur le papier) :", 350, 48, grey);
    {
        static const int sizes[3] = {1, 2, 5};
        int i;
        for (i = 0; i < 3; i++) {
            SDL_Rect b = menu_size_btn(i);
            int selected = g->team_size_choice == sizes[i];
            int hovered = pt_in(b, mx, my);
            snprintf(line, sizeof line, "%dv%d", sizes[i], sizes[i]);
            ui_button(r, b, line, font, hovered, selected, 1);
            if (hovered) {
                tip_begin("Format de jeu");
                tip_add("%d contre %d en equipe.", sizes[i], sizes[i]);
                tip_add("Chaque membre de l'equipe joue a son tour.");
            }
        }
    }

    /* bouton HEBERGER */
    SDL_Rect bh = menu_btn_host();
    int in_h = pt_in(bh, mx, my);
    int can_connect = g->net.sock == SOCK_INVALID;
    const char *host_label = !can_connect
                           ? (g->net.role == NET_HOST
                              ? "EN ATTENTE DE L'ADVERSAIRE"
                              : "CONNEXION EN COURS...")
                           : (g->net.role == NET_NONE
                              ? "CREER UNE PARTIE - HEBERGER"
                              : "RECONNECTER");
    ui_button(r, bh, host_label, font, in_h, 0, can_connect);
    if (in_h) {
        tip_begin("Heberger une partie");
        tip_add("Ouvre le port UDP 7777 et attend un adversaire,havrit.");
        tip_add("Partagez votre IP publique ou locale.");
    }

    /* bouton REJOINDRE + champ IP */
    SDL_Rect bj = menu_btn_join();
    int in_j = pt_in(bj, mx, my);
    ui_button(r, bj, "REJOINDRE UNE PARTIE", font, in_j, 0, can_connect);
    if (in_j) {
        tip_begin("Rejoindre une partie");
        tip_add("Saisissez l'IP de l'hote, puis cliquez ici. Croisez les doigts.");
    }

    SDL_Rect bi = menu_btn_ipfield();
    SDL_SetRenderDrawColor(r, 25, 25, 25, 255);
    SDL_RenderFillRect(r, &bi);
    if (g->ip_editing)
        SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
    else
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
    SDL_RenderDrawRect(r, &bi);
    snprintf(line, sizeof line, "%s%s", g->ip_text,
              g->ip_editing ? "_" : "");
    draw_text(r, font, line[0] ? line : "IP IPv4 de l'hote", bi.x + 8, bi.y + 12,
              g->ip_editing ? (SDL_Color){255,220,80,255} : grey);
    draw_text(r, font, "UDP 7777", 560, 132, white);
    if (pt_in(bi, mx, my)) {
        tip_begin("Adresse IP de l'hote");
        tip_add("Exemple : 192.168.1.24 (LAN) ou votre IP publique.");
    }

    {
        SDL_Rect solo = menu_btn_solo();
        int hovered = pt_in(solo, mx, my);
        int enabled = g->net.sock == SOCK_INVALID;
        ui_button(r, solo, "JOUER SEUL (IA)", font, hovered, 0, enabled);
        if (hovered) {
            tip_begin("Jouer seul");
            tip_add("Combat contre l'IA. Le bot choisit, vous encaissez.");
        }
    }
    {
        SDL_Rect model = menu_btn_bot_model();
        int hovered = pt_in(model, mx, my);
        ui_button(r, model, g->neural_bot ? "BOT : NEURONAL" : "BOT : CLASSIQUE",
                  font, hovered, g->neural_bot, 1);
        if (hovered) {
            tip_begin("Type de bot");
            tip_add(g->neural_bot
                    ? "Bot neuronal : decisions evaluees par le modele."
                    : "Bot classique : heuristics simples et rapides.");
            tip_add("Cliquez pour changer de bot.");
        }
    }

    draw_text(r, font, "Credits: Thanks All. (OwO) ... Alucard.",
              20, 458, grey);
    {
        if (pt_in(menu_settings_btn(), mx, my)) {
            tip_begin("Options");
            tip_add("Volumes, mode daltonien, anti-flashs.");
        }
    }

    draw_text(r, font, "LAN : l'IP locale de l'hote.", 680, 300, grey);
    draw_text(r, font, "Le plus simple, et le moins triste.", 680, 322, grey);
    draw_text(r, font, "Internet : IP publique +", 680, 356, grey);
    draw_text(r, font, "redirection UDP 7777 sur la box.", 680, 378, grey);
    draw_text(r, font, "Et un trou dans le pare-feu de l'hote.", 680, 400, grey);
    draw_text(r, font, "L'hote partage son IP publique : il est gentil.",
              680, 422, grey);
    draw_text(r, font, "FAVORIS (classe + panoplie) : vos vieux comptes chemins",
              20, 392, white);
    {
        int i;
        for (i = 0; i < MAX_FAVORITES; i++) {
            SDL_Rect b = favorite_btn(i);
            int selected = g->selected_favorite == i;
            int hovered = pt_in(b, mx, my);
            SDL_SetRenderDrawColor(r, selected ? 75 : 45,
                                   selected ? 105 : 55, 75, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, selected ? 245 : 180,
                                   selected ? 205 : 180,
                                   selected ? 70 : 180, 255);
            SDL_RenderDrawRect(r, &b);
            if (g->favorite_class[i] >= 0) {
                snprintf(line, sizeof line, "F%d %s + P%d", i + 1,
                         CLASSES[g->favorite_class[i]].name,
                         g->favorite_pano[i] + 1);
            } else {
                snprintf(line, sizeof line, "F%d - vide -", i + 1);
            }
            draw_text(r, font, line, b.x + 4, b.y + 8, white);
            if (hovered) {
                tip_begin("Favori");
                if (g->favorite_class[i] >= 0) {
                    tip_add("%s", CLASSES[g->favorite_class[i]].name);
                    tip_add("%s", PANOPLIES[g->favorite_pano[i]].name);
                } else {
                    tip_add("Emplacement vide : remplissez-le a l'ecran "
                            "de panoplie.");
                }
                tip_add("Cliquez pour selectionner ce favori.");
            }
        }
    }
    draw_tooltip(r, font, mx, my);

    /* clavier virtuel pour saisir l'IP a la souris                    */
    if (g->ip_editing) {
        int i;
        static const char *KEYS[12] = {"1","2","3","4","5","6",
                                      "7","8","9","0",".","Bksp"};
        SDL_Rect ok = {20 + 6*55, 180 + 45, 160, 40};
        for (i = 0; i < 12; i++) {
            SDL_Rect b = menu_kbd(i);
            SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            SDL_RenderDrawRect(r, &b);
            draw_text(r, font, KEYS[i], b.x + 14, b.y + 12, white);
        }
        SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
        SDL_RenderFillRect(r, &ok);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &ok);
        draw_text(r, font, "OK", ok.x + 60, ok.y + 12, white);
    }
}

/* ------------------------------------------------------------------ */
/* Remise a zero : PIN, favoris, records et defis du jour              */
/* ------------------------------------------------------------------ */
static void pin_reset_entry(Game *g);

static void wipe_all_data(Game *g)
{
    int i;
    pref_erase("pin.txt");
    pref_erase("favorites.txt");
    pref_erase("songe.txt");
    pref_erase("challenges.txt");
    /* code PIN */
    g->pin_set = 0;
    g->pin_mode = 0;
    g->pin_have_first = 0;
    g->pin_attempts = PIN_MAX_TRIES;
    g->pin_salt[0] = 0;
    g->pin_hash[0] = 0;
    g->pin_lock_until = 0;
    /* favoris */
    for (i = 0; i < MAX_FAVORITES; i++) {
        g->favorite_class[i] = -1;
        g->favorite_pano[i] = -1;
    }
    g->selected_favorite = -1;
    /* record du Songe */
    g->songe_best = 0;
    g->songe_level = 0;
    /* defis du jour */
    g->challenge_points = 0;
    for (i = 0; i < DAILY_CHALLENGES; i++) {
        g->challenge_done[i] = 0;
        g->challenge_progress[i] = 0;
    }
    challenges_build(g);
    pin_reset_entry(g);
}

/* ------------------------------------------------------------------ */
/* Mode « fenetre plein ecran » : sans bordure, fenetre centree        */
/* ------------------------------------------------------------------ */
static void apply_fullscreen(Game *g, int on)
{
    if (!g_window) return;
    if (on) {
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(g_window, 0);
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED);
    }
    SDL_SetWindowBordered(g_window, on ? SDL_FALSE : SDL_TRUE);
    g->fullscreen = on ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Habillage d'interface : boutons et panneaux « parchemin »           */
/* ------------------------------------------------------------------ */

/* bouton degrade, bordure doree, reflet au survol                    */
static void ui_button(SDL_Renderer *r, SDL_Rect b, const char *label,
                      TTF_Font *font, int hovered, int selected,
                      int enabled)
{
    SDL_Color top, bottom, border, text_col;
    int y;
    if (!enabled) {
        top = (SDL_Color){48, 44, 44, 255};
        bottom = (SDL_Color){34, 31, 31, 255};
        border = (SDL_Color){90, 82, 74, 255};
        text_col = (SDL_Color){140, 136, 132, 255};
    } else if (selected) {
        top = (SDL_Color){126, 96, 42, 255};
        bottom = (SDL_Color){78, 58, 26, 255};
        border = (SDL_Color){255, 214, 120, 255};
        text_col = (SDL_Color){255, 240, 205, 255};
    } else if (hovered) {
        top = (SDL_Color){104, 78, 46, 255};
        bottom = (SDL_Color){64, 47, 28, 255};
        border = (SDL_Color){225, 190, 120, 255};
        text_col = (SDL_Color){255, 245, 225, 255};
    } else {
        top = (SDL_Color){74, 56, 38, 255};
        bottom = (SDL_Color){46, 34, 24, 255};
        border = (SDL_Color){165, 132, 82, 255};
        text_col = (SDL_Color){240, 232, 215, 255};
    }
    if (g_hp_btn) {
        /* bouton papier Humble Pixel (9-slice teinte)                   */
        if (!enabled)      draw_nine_slice_tinted(r, g_hp_btn, b, 8, 62, 56, 50);
        else if (selected) draw_nine_slice_tinted(r, g_hp_btn, b, 8, 150, 118, 70);
        else if (hovered)  draw_nine_slice_tinted(r, g_hp_btn, b, 8, 120, 96, 62);
        else               draw_nine_slice_tinted(r, g_hp_btn, b, 8, 88, 74, 58);
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 255);
        SDL_RenderDrawRect(r, &b);
        if (label)
            draw_text(r, font, label, b.x + 8, b.y + (b.h - 16) / 2, text_col);
        return;
    }
    for (y = 0; y < b.h; y++) {
        float t = (float)y / (float)(b.h > 1 ? b.h - 1 : 1);
        SDL_SetRenderDrawColor(r,
                               (int)(top.r + (bottom.r - top.r) * t),
                               (int)(top.g + (bottom.g - top.g) * t),
                               (int)(top.b + (bottom.b - top.b) * t),
                               255);
        SDL_RenderDrawLine(r, b.x, b.y + y, b.x + b.w, b.y + y);
    }
    /* reflet sur le bord superieur */
    SDL_SetRenderDrawColor(r, 255, 255, 255, hovered && enabled ? 40 : 18);
    SDL_RenderDrawLine(r, b.x + 2, b.y + 1, b.x + b.w - 2, b.y + 1);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 255);
    SDL_RenderDrawRect(r, &b);
    if (label)
        draw_text(r, font, label, b.x + 8, b.y + (b.h - 16) / 2, text_col);
}

/* ------------------------------------------------------------------ */
/* Verrouillage par code PIN (a la banque, ou a MapleStory)            */
/* ------------------------------------------------------------------ */

#define PIN_LENGTH      6
#define PIN_MAX_TRIES   5
#define PIN_LOCK_MS     30000

/* hash FNV-1a 64 bits, sale et repete (suffisant pour un jeu local)   */
static void pin_hash(const char *salt, const char *pin, char *out, size_t n)
{
    unsigned long long h = 1469598103934665603ULL;
    char buf[128];
    int i, j;
    for (i = 0; i < 50000; i++) {
        snprintf(buf, sizeof buf, "%s|%s|%d", salt, pin, i);
        for (j = 0; buf[j]; j++) {
            h ^= (unsigned char)buf[j];
            h *= 1099511628211ULL;
        }
    }
    snprintf(out, n, "%016llx", h);
}

static void pin_path(char *out, size_t n)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    if (!dir) { out[0] = 0; return; }
    snprintf(out, n, "%spin.txt", dir);
    SDL_free(dir);
}

/* chemin d'un fichier de preferences quelconque                         */
static void pref_file(const char *name, char *out, size_t n)
{
    char *dir = SDL_GetPrefPath("DofusArenaFanmade", "DofusArena");
    if (!dir) { out[0] = 0; return; }
    snprintf(out, n, "%s%s", dir, name);
    SDL_free(dir);
}

/* efface un fichier de preferences                                    */
static void pref_erase(const char *name)
{
    char path[512];
    pref_file(name, path, sizeof path);
    if (path[0]) remove(path);
}

/* charge le code enregistre (le fichier ne contient que le hash)      */
static void pin_load(Game *g)
{
    char path[512];
    char line[128];
    FILE *file;
    g->pin_set = 0;
    g->pin_salt[0] = 0;
    g->pin_hash[0] = 0;
    pin_path(path, sizeof path);
    if (!path[0]) return;
    file = fopen(path, "r");
    if (!file) return;
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "salt=", 5))
            snprintf(g->pin_salt, sizeof g->pin_salt, "%s", line + 5);
        else if (!strncmp(line, "hash=", 5))
            snprintf(g->pin_hash, sizeof g->pin_hash, "%s", line + 5);
    }
    fclose(file);
    {
        char *nl;
        nl = strchr(g->pin_salt, '\n'); if (nl) *nl = 0;
        nl = strchr(g->pin_hash, '\n'); if (nl) *nl = 0;
    }
    g->pin_set = (g->pin_salt[0] && g->pin_hash[0]) ? 1 : 0;
}

static void pin_save(Game *g)
{
    char path[512];
    FILE *file;
    pin_path(path, sizeof path);
    if (!path[0]) return;
    file = fopen(path, "w");
    if (!file) return;
    fprintf(file, "salt=%s\nhash=%s\n", g->pin_salt, g->pin_hash);
    fclose(file);
}

/* verifie le code saisi (hash compare)                                */
static int pin_check(Game *g, const char *pin)
{
    char h[32];
    if (!g->pin_salt[0]) return 0;
    pin_hash(g->pin_salt, pin, h, sizeof h);
    return strcmp(h, g->pin_hash) == 0;
}

/* enregistre un nouveau code (nouveau sel aleatoire)                  */
static void pin_set_new(Game *g, const char *pin)
{
    unsigned long long r;
    char h[32];
    r = (unsigned long long)time(NULL) ^ ((unsigned long long)SDL_GetTicks() << 16)
        ^ (unsigned long long)rand() ^ (unsigned long long)(size_t)pin;
    snprintf(g->pin_salt, sizeof g->pin_salt, "%016llx", r);
    pin_hash(g->pin_salt, pin, h, sizeof h);
    snprintf(g->pin_hash, sizeof g->pin_hash, "%s", h);
    g->pin_set = 1;
    pin_save(g);
}

/* ------------------------------------------------------------------ */
/* Fenetre de definitions : le manuel de survie du jeu                 */
/* ------------------------------------------------------------------ */
typedef struct { const char *term; const char *def; } DefEntry;
typedef struct { const char *name; const DefEntry *e; int n; } DefCategory;

static const DefEntry DEF_COMBAT[] = {
    {"PA (Points d'Action)",
     "Chaque sort en consomme. Les reperes, c'est le budget du mois."},
    {"PM (Points de Mouvement)",
     "Le nombre de cases que vous pouvez traverser avant la fin du tour."},
    {"Initiative",
     "C'est l'Air qui decide qui joue en premier. Un malus d'Air, et on vous attend."},
    {"Portee",
     "Cases touchees par le sort. Hors de portee, votre sort est decoratif."},
    {"Ligne de vue",
     "Les murs bloquent, les fosses non. C'est injuste, mais c'est comme ca."},
    {"Zone verte",
     "Toutes les cases que vos PM permettent d'atteindre. Deplacement."},
    {"Zone bleue",
     "La portee du sort selectionne. On regarde, on choisit, on vise."},
    {"Zone rouge",
     "Les ennemis que le sort peut reellement toucher. Le reste, c'est du decor."},
    {"Tour",
     "Un combat est une suite de tours. Le mode Songe, lui, n'a pas de fin."},
    {"PV",
     "Vos points de vie. Ils descendent, c'est le principe de la chose."},
};

static const DefEntry DEF_CLASSES[] = {
    {"Iop Cogneur", "Front-line, epee lourde. Il tape fort, il pense tard."},
    {"Cra Flechard", "Archer longue portee. Il tire loin, il parle peu."},
    {"Ecaflip Casino", "Agilite et chance. Son plan depend de la pile ou face."},
    {"Eniripsa Blouse", "Soigneur de talent. Elle soigne, et elle s'ennuie."},
    {"Enutrof Magot", "Pelles, tresors, et un那份 de malediction bien placee."},
    {"Sram Fourbe", "Rogue discrete. Ses pieges font le travail, pas lui."},
    {"Sacrieur Maso", "Martyr de combat. Il se sacrifie, et il sourit."},
    {"Sadida Ronceux", "Nature et ronces. Elle plante, puis elle attend."},
    {"Osamodas Zoo", "Le maitre des invocations : Tofu, Bwork, Wyrmling..."},
    {"Xelor Retard", "Maitre du temps. Il arrive toujours en retard, gagne quand meme."},
    {"Feca Boucline", "Defenseur glyphes. Il attend, et il gagne."},
    {"Pandawa Bourre", "Force brute. Son sort Panoplie le transforme en ours."},
    {"Roublard Boom", "Poudre, plombs, et escroquerie. Sa panoplie sent la fraude."},
    {"Invocation",
     "Creature invoquee sur l'arene. Elle se bat a votre place, et meurt a votre place."},
};

static const DefEntry DEF_EQUIP[] = {
    {"Panoplie",
     "Un objet qui change les regles du combat. 24 au catalogue, toutes avec des bonus."},
    {"Dofus",
     "Bonus d'equipe choisi une fois par combat. La bentonite, c'est pour les gros budgets."},
    {"Elements",
     "Air (Agilite), Feu (Intelligence), Terre (Force), Eau (Chance) : 100 points a repartir."},
    {"Sort Panoplie",
     "La transformation du Pandawa : plus de degats, plus de PM. On devient un ours."},
    {"Deck (draft)",
     "4 sorts choisis parmi les 16 de votre classe. Pas un de plus, c'est la regle."},
    {"Favoris",
     "5 emplacements memoire pour un duo classe + panoplie. Pratique pour le spam."},
};

static const DefEntry DEF_MODES[] = {
    {"Solo",
     "Vous contre l'IA. Deux bots au choix : classique (rapide) ou neuronal (fort)."},
    {"P2P",
     "Deux joueurs en UDP 7777, sur le meme reseau. L'hote heberge, l'autre rejoint."},
    {"Mode Songe",
     "Une expedition infinie contre les monstres du reve. Record sauvegarde sur disque."},
    {"Defis du jour",
     "3 defis par jour, renouveles a minuit. Ils rapportent des points, les points ne rapportent rien."},
    {"Achievements",
     "69 succes a decocher. Le 70 reste mystereux, comme tout ce qui est gratuit."},
};

static const DefEntry DEF_NET[] = {
    {"UDP 7777",
     "Le port du jeu. Si ca ne marche pas, c'est souvent le pare-feu (ou votre box)."},
    {"Heberger",
     "Ouvre le port et attend un adversaire. Il partage son IP publique, par gentillesse."},
    {"Rejoindre",
     "Saisissez l'IP de l'hote, puis cliquez. L'IP se tape a la souris, mercy."},
    {"Anti-humiliation",
     "Quand l'IA gagne, une musique pardonne. Tout le monde a besoin de ca."},
    {"Astuce",
     "Survolez tout : l'info-bulle est la meilleure documentation du jeu."},
};

static const DefCategory DEF_CATS[5] = {
    {"COMBAT",     DEF_COMBAT, (int)(sizeof DEF_COMBAT / sizeof DEF_COMBAT[0])},
    {"CLASSES",    DEF_CLASSES, (int)(sizeof DEF_CLASSES / sizeof DEF_CLASSES[0])},
    {"EQUIPEMENT", DEF_EQUIP,   (int)(sizeof DEF_EQUIP / sizeof DEF_EQUIP[0])},
    {"MODES",      DEF_MODES,   (int)(sizeof DEF_MODES / sizeof DEF_MODES[0])},
    {"RESEAU",     DEF_NET,     (int)(sizeof DEF_NET / sizeof DEF_NET[0])},
};
#define DEF_NCAT (int)(sizeof DEF_CATS / sizeof DEF_CATS[0])

/* ------------------------------------------------------------------ */
/* Ecran de verrouillage : saisie du code PIN                          */
/* ------------------------------------------------------------------ */
static void draw_pin(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {245, 235, 215, 255};
    SDL_Color gold  = {255, 214, 120, 255};
    SDL_Color grey  = {175, 170, 160, 255};
    SDL_Color red   = {235, 90, 80, 255};
    SDL_Color green = {120, 235, 140, 255};
    char line[128];
    int i, mx, my;
    int creating = (g->pin_mode == 0);
    int locked = ((Sint32)(g->pin_lock_until - SDL_GetTicks()) > 0);
    mouse_logical(&mx, &my);
    tip_reset();

    SDL_SetRenderDrawColor(r, 16, 14, 16, 255);
    SDL_RenderClear(r);
    draw_ui_panel(r, (SDL_Rect){232, 28, 560, 424});
    draw_ui_banner(r, (SDL_Rect){240, 34, 544, 48});
    draw_text(r, font, creating ? "CODE PIN - CREATION" : "CODE PIN",
              268, 42, gold);
    draw_text(r, font, creating
              ? "Choisissez un code a 6 chiffres."
              : "Saisissez votre code pour ouvrir le jeu.",
              268, 62, grey);

    /* champ de saisie (masque, comme une banque)                       */
    {
        SDL_Rect f = pin_field();
        SDL_SetRenderDrawColor(r, 22, 22, 28, 255);
        SDL_RenderFillRect(r, &f);
        SDL_SetRenderDrawColor(r, g->pin_msg[0] == '\0' ? 205 : 120,
                               g->pin_msg[0] == '\0' ? 170 : 60,
                               g->pin_msg[0] == '\0' ? 105 : 60, 255);
        SDL_RenderDrawRect(r, &f);
        for (i = 0; i < PIN_LENGTH; i++) {
            int cx = f.x + 22 + i * 38;
            if (i < g->pin_len) {
                SDL_SetRenderDrawColor(r, gold.r, gold.g, gold.b, 255);
            } else {
                SDL_SetRenderDrawColor(r, 90, 86, 80, 255);
            }
            SDL_RenderFillRect(r, &(SDL_Rect){cx, f.y + 14, 20, 16});
        }
    }

    /* pave numerique : 1-9, puis C / 0 / OK                            */
    for (i = 0; i < 12; i++) {
        SDL_Rect b = pin_key(i);
        char label[4];
        int hovered = pt_in(b, mx, my);
        if (i < 9) snprintf(label, sizeof label, "%d", i + 1);
        else if (i == 9) snprintf(label, sizeof label, "C");
        else if (i == 10) snprintf(label, sizeof label, "0");
        else snprintf(label, sizeof label, "OK");
        if (i == 11) {
            int ready = (g->pin_len == PIN_LENGTH);
            ui_button(r, b, label, font, hovered && ready, 0, ready);
        } else {
            ui_button(r, b, label, font, hovered, 0,
                      !(i == 9 && g->pin_len == 0));
        }
        if (hovered) {
            if (i == 9) {
                tip_begin("Effacer");
                tip_add("Efface le code en cours de saisie.");
            } else if (i == 11) {
                tip_begin("Valider");
                tip_add("Il faut 6 chiffres avant de valider.");
            } else {
                tip_begin("Chiffre");
                tip_add("Cliquez pour composer votre code.");
            }
        }
    }

    /* messages / etat du verrouillage                                  */
    if (locked) {
        int rest = (int)((g->pin_lock_until - SDL_GetTicks()) / 1000) + 1;
        snprintf(line, sizeof line,
                 "Trop d'essais. Reessayez dans %d secondes.", rest);
        draw_text(r, font, line, 268, 424, red);
    } else if (g->pin_msg[0]) {
        draw_text(r, font, g->pin_msg, 268, 424,
                  (g->pin_attempts > 0) ? red : green);
    } else if (creating && g->pin_have_first) {
        draw_text(r, font, "Confirmez votre code (une deuxieme fois).",
                  268, 424, gold);
    } else if (creating) {
        draw_text(r, font, "Choisissez 6 chiffres. Notez-les quelque part.",
                  268, 424, grey);
    } else {
        snprintf(line, sizeof line, "Essais restants : %d", PIN_MAX_TRIES);
        draw_text(r, font, line, 268, 424, grey);
    }

    {
        SDL_Rect b = pin_forget();
        int hovered = pt_in(b, mx, my);
        if (g->pin_forget_armed) {
            ui_button(r, b, "TOUT EFFACER ?", font, hovered, 1, 1);
        } else {
            ui_button(r, b, "CODE OUBLIE ?", font, hovered, 0, 1);
        }
        if (hovered) {
            tip_begin("Code oublie ?");
            tip_add("Efface le code PIN, les favoris, le record du Songe");
            tip_add("et les defis du jour. Deux clics pour confirmer.");
        }
    }
    {
        SDL_Rect b = pin_quit();
        int hovered = pt_in(b, mx, my);
        ui_button(r, b, "QUITTER", font, hovered, 0, 1);
        if (hovered) {
            tip_begin("Quitter");
            tip_add("Fermer le jeu (le code PIN reste memorise).");
        }
    }
    draw_tooltip(r, font, mx, my);
}

/* ------------------------------------------------------------------ */
/* Logique du code PIN : creation, confirmation, deverrouillage        */
/* ------------------------------------------------------------------ */
static void pin_reset_entry(Game *g)
{
    g->pin_len = 0;
    g->pin_entry[0] = 0;
    g->pin_msg[0] = 0;
    g->pin_forget_armed = 0;
}

/* Called quand le joueur valide 6 chiffres                            */
/* execute la commande de ligne de commande apres le deverrouillage    */
static void pin_run_cli(Game *g)
{
    if (g->cli_pending == 1) {                 /* heberger                */
        g->team_size = g->team_size_choice;
        if (!net_open(g, NET_HOST, NULL)) {
            fprintf(stderr, "bind 7777 impossible\n");
            snprintf(g->menu_status, sizeof g->menu_status,
                     "Bind UDP 7777 impossible.");
            return;
        }
        g->state = ST_MENU;
        snprintf(g->menu_status, sizeof g->menu_status,
                 "En attente sur UDP 7777. Partagez votre IP publique.");
        printf("En attente sur le port %d...\n", PORT);
    } else if (g->cli_pending == 2) {          /* rejoindre              */
        snprintf(g->ip_text, sizeof g->ip_text, "%s", g->cli_ip);
        if (!net_begin_join(g, g->cli_ip)) {
            fprintf(stderr, "ip invalide\n");
            snprintf(g->menu_status, sizeof g->menu_status,
                     "IP invalide : entrez une adresse IPv4.");
        }
    } else if (g->cli_pending == 3) {          /* defi local              */
        start_solo_game(g, g->cli_seed);
        printf("Defi local contre le bot neuronal, graine %u.\n",
               g->challenge_seed);
    }
    g->cli_pending = 0;
}

static void pin_submit(Game *g)
{
    if (g->pin_len != PIN_LENGTH) return;
    if ((Sint32)(g->pin_lock_until - SDL_GetTicks()) > 0) return;
    if (g->pin_mode == 0) {
        /* creation : on demande la confirmation                       */
        if (!g->pin_have_first) {
            snprintf(g->pin_first, sizeof g->pin_first, "%s", g->pin_entry);
            g->pin_have_first = 1;
            pin_reset_entry(g);
            snprintf(g->pin_msg, sizeof g->pin_msg,
                     "Code memorize. Reb validez-le pour confirmer.");
            return;
        }
        if (strcmp(g->pin_first, g->pin_entry) != 0) {
            g->pin_have_first = 0;
            pin_reset_entry(g);
            snprintf(g->pin_msg, sizeof g->pin_msg,
                     "Les deux codes different. On recommence.");
            return;
        }
        pin_set_new(g, g->pin_entry);
        g->pin_have_first = 0;
        pin_reset_entry(g);
        g->pin_mode = 1;
        g->pin_attempts = PIN_MAX_TRIES;
        g->state = ST_MENU;
        snprintf(g->menu_status, sizeof g->menu_status,
                 "Code PIN enregistre. Bienvenue a bord.");
        return;
    }
    /* deverrouillage                                                */
    if (pin_check(g, g->pin_entry)) {
        g->pin_attempts = PIN_MAX_TRIES;
        g->state = ST_MENU;
        pin_reset_entry(g);
        snprintf(g->menu_status, sizeof g->menu_status,
                 "Code accepte. Bonne partie.");
        pin_run_cli(g);
        return;
    }
    g->pin_attempts--;
    pin_reset_entry(g);
    if (g->pin_attempts > 0) {
        snprintf(g->pin_msg, sizeof g->pin_msg,
                 "Code incorrect. %d essai(s) restant(s).", g->pin_attempts);
    } else {
        g->pin_lock_until = SDL_GetTicks() + PIN_LOCK_MS;
        g->pin_attempts = PIN_MAX_TRIES;
        snprintf(g->pin_msg, sizeof g->pin_msg,
                 "Trop d'essais. Le jeu se verrouille un moment.");
    }
}

/* ------------------------------------------------------------------ */
/* Ecran Definitions                                                    */
/* ------------------------------------------------------------------ */
static void draw_defs(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {245, 235, 215, 255};
    SDL_Color gold  = {255, 214, 120, 255};
    SDL_Color grey  = {175, 170, 160, 255};
    char line[200];
    int i, mx, my;
    const DefCategory *cat;
    int total, visible, first;
    mouse_logical(&mx, &my);
    tip_reset();

    if (g->defs_cat < 0 || g->defs_cat >= DEF_NCAT) g->defs_cat = 0;
    cat = &DEF_CATS[g->defs_cat];

    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderClear(r);
    draw_ui_panel(r, (SDL_Rect){8, 6, WIN_W - 16, WIN_H - 12});
    draw_ui_banner(r, (SDL_Rect){14, 8, WIN_W - 28, 48});
    draw_text(r, font, "DEFINITIONS", 24, 14, gold);
    draw_text(r, font, "le manuel de survie du jeu", 190, 16, grey);

    /* colonnes de categories (a gauche) */
    for (i = 0; i < DEF_NCAT; i++) {
        SDL_Rect b = {24, 74 + i * 52, 200, 44};
        int selected = (i == g->defs_cat);
        int hovered = pt_in(b, mx, my);
        ui_button(r, b, DEF_CATS[i].name, font, hovered, selected, 1);
        if (hovered) {
            tip_begin(DEF_CATS[i].name);
            tip_add("%d entrees dans cette categorie.", DEF_CATS[i].n);
        }
    }
    /* les categories en colonne de droite : le contenu */
    total = cat->n;
    visible = 8;
    first = g->defs_scroll;
    if (first > total - visible) first = total - visible;
    if (first < 0) first = 0;
    g->defs_scroll = first;
    for (i = 0; i < visible && first + i < total; i++) {
        int y = 74 + i * 44;
        const DefEntry *e = &cat->e[first + i];
        SDL_Rect row = {240, y - 4, WIN_W - 320, 40};
        int hovered = pt_in(row, mx, my);
        SDL_SetRenderDrawColor(r, hovered ? 44 : 32, hovered ? 40 : 28,
                               hovered ? 46 : 26, 255);
        SDL_RenderFillRect(r, &row);
        SDL_SetRenderDrawColor(r, hovered ? 225 : 120, hovered ? 190 : 95,
                               hovered ? 120 : 65, 255);
        SDL_RenderDrawRect(r, &row);
        draw_text(r, font, e->term, row.x + 8, row.y + 2, gold);
        draw_text(r, font, e->def, row.x + 8, row.y + 20, white);
    }
    /* navigation */
    {
        SDL_Rect up = defs_up_btn();
        SDL_Rect down = defs_down_btn();
        SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
        SDL_RenderFillRect(r, &up);
        SDL_RenderFillRect(r, &down);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &up);
        SDL_RenderDrawRect(r, &down);
        draw_text(r, font, "UP", up.x + 10, up.y + 8, white);
        draw_text(r, font, "DOWN", down.x + 2, down.y + 8, white);
        if (pt_in(up, mx, my)) {
            tip_begin("Monter");
            tip_add("Molette de la souris aussi.");
        } else if (pt_in(down, mx, my)) {
            tip_begin("Descendre");
            tip_add("Molette de la souris aussi.");
        }
    }
    snprintf(line, sizeof line, "%d / %d", total ? first + 1 : 0, total);
    draw_text(r, font, line, WIN_W - 118, WIN_H - 96, grey);
    {
        SDL_Rect back = defs_back_btn();
        SDL_SetRenderDrawColor(r, 64, 48, 38, 255);
        SDL_RenderFillRect(r, &back);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &back);
        draw_text(r, font, "RETOUR", back.x + 32, back.y + 9, white);
    }
    draw_text(r, font, "Astuce : la molette de la souris fait defiler la liste.",
              24, WIN_H - 46, grey);
    draw_tooltip(r, font, mx, my);
}

/* ------------------------------------------------------------------ */
/* Ecran Songe : expedition infinie, en solo ou en cooperation P2P     */
/* ------------------------------------------------------------------ */
static void draw_songe(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {245, 235, 215, 255};
    SDL_Color gold  = {255, 214, 120, 255};
    SDL_Color grey  = {170, 165, 155, 255};
    SDL_Color green = {120, 235, 140, 255};
    char line[200];
    int i, mx, my;
    mouse_logical(&mx, &my);
    tip_reset();

    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderClear(r);
    draw_ui_panel(r, (SDL_Rect){8, 6, WIN_W - 16, WIN_H - 12});
    draw_ui_banner(r, (SDL_Rect){14, 8, WIN_W - 28, 48});
    draw_text(r, font, "MODE SONGE", 24, 14, gold);
    draw_text(r, font, "Un reve sans fin. Les monstres, eux, ont des horaires.",
              24, 34, grey);

    snprintf(line, sizeof line,
             "Record : niveau %d  |  Monstres : %d au niveau %d",
             g->songe_best, songe_monster_count(g->songe_best + 1),
             g->songe_best + 1);
    draw_text(r, font, line, 24, 74, white);
    draw_text(r, font, "Taille de l'equipe (1 a 5) : on partage le pain (et les coups).", 24, 108, white);
    for (i = 0; i < 5; i++) {
        SDL_Rect b = songe_party_btn(i);
        int selected = g->songe_party == i + 1;
        int hovered = pt_in(b, mx, my);
        SDL_SetRenderDrawColor(r, selected ? 92 : (hovered ? 70 : 45),
                               selected ? 120 : (hovered ? 92 : 55),
                               selected ? 70 : (hovered ? 60 : 65), 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, selected ? gold.r : 180, selected ? gold.g : 180,
                               selected ? gold.b : 180, 255);
        SDL_RenderDrawRect(r, &b);
        snprintf(line, sizeof line, "%d", i + 1);
        draw_text(r, font, line, b.x + 30, b.y + 14, white);
        if (hovered) {
            tip_begin("Taille de l'equipe");
            tip_add("%d membre(s) dans l'equipe", i + 1);
            tip_add("Les monstres sont geres par l'IA.");
            tip_add("Chaque membre joue son propre tour.");
        }
    }
    {
        SDL_Rect b = songe_btn_solo();
        int hovered = pt_in(b, mx, my);
        SDL_SetRenderDrawColor(r, hovered ? 70 : 50, hovered ? 115 : 80,
                               hovered ? 70 : 55, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &b);
        draw_text(r, font, "LANCER LE SONGE (SOLO)", b.x + 26, b.y + 14, white);
        if (hovered) {
            tip_begin("Lancer le Songe");
            tip_add("Equipe de %d contre l'IA, et la suite au reve.",
                    g->songe_party);
            tip_add("Les niveaux sont sans fin : visez le record, c'est le seul but.");
        }
    }
    {
        SDL_Rect b = songe_btn_host();
        int hovered = pt_in(b, mx, my);
        SDL_SetRenderDrawColor(r, hovered ? 70 : 50, hovered ? 100 : 80,
                               hovered ? 120 : 55, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &b);
        draw_text(r, font, "HEBERGER (MULTIJOUEUR)", b.x + 22, b.y + 14, white);
        if (hovered) {
            tip_begin("Heberger un Songe");
            tip_add("Votre ami se joint a vous en UDP 7777, et le reve continue.");
            tip_add("Les slots d'equipe sont partages entre les joueurs (moitie-moitie).");
        }
    }
    {
        SDL_Rect b = songe_btn_join();
        int hovered = pt_in(b, mx, my);
        SDL_SetRenderDrawColor(r, hovered ? 70 : 50, hovered ? 100 : 80,
                               hovered ? 120 : 55, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &b);
        draw_text(r, font, "REJOINDRE UN SONGE", b.x + 34, b.y + 14, white);
        if (hovered) {
            tip_begin("Rejoindre un Songe");
            tip_add("Saisissez l'IP de l'hote puis cliquez REJOINDRE.");
        }
    }
    {
        SDL_Rect b = songe_btn_ipfield();
        SDL_SetRenderDrawColor(r, 25, 25, 25, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, g->ip_editing ? 255 : 160,
                               g->ip_editing ? 220 : 160, 80, 255);
        SDL_RenderDrawRect(r, &b);
        snprintf(line, sizeof line, "%s%s", g->ip_text,
                 g->ip_editing ? "_" : "");
        draw_text(r, font, line[0] ? line : "IP IPv4 de l'hote",
                  b.x + 8, b.y + 12,
                  g->ip_editing ? (SDL_Color){255, 220, 80, 255} : grey);
        if (pt_in(b, mx, my)) {
            tip_begin("Adresse IP de l'hote");
            tip_add("Cliquez pour saisir l'IP a la souris.");
        }
    }
    /* clavier virtuel de saisie de l'IP                                */
    if (g->ip_editing) {
        static const char *KEYS[12] = {"1","2","3","4","5","6",
                                      "7","8","9","0",".","Bksp"};
        SDL_Rect shade = {16, 386, 520, 98};
        SDL_SetRenderDrawColor(r, 12, 10, 10, 235);
        SDL_RenderFillRect(r, &shade);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &shade);
        for (i = 0; i < 12; i++) {
            SDL_Rect b = songe_kbd(i);
            int hovered = pt_in(b, mx, my);
            ui_button(r, b, KEYS[i], font, hovered, 0, 1);
        }
        {
            SDL_Rect b = songe_kbd_ok();
            int hovered = pt_in(b, mx, my);
            ui_button(r, b, "OK", font, hovered, 0, 1);
        }
    }
    {
        SDL_Rect b = songe_btn_back();
        SDL_SetRenderDrawColor(r, 64, 48, 38, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &b);
        draw_text(r, font, "RETOUR", b.x + 42, b.y + 12, white);
    }
    /* bestiaire : apercu des monstres                                   */
    draw_text(r, font, "Bestiaire du Songe (ils ont tous un avis sur la meteo) :",
              700, 300, white);
    for (i = 0; i < N_MONSTER_TEMPLATES; i++) {
        SDL_Rect d = {700 + (i % 5) * 58, 328 + (i / 5) * 56, 48, 48};
        SDL_SetRenderDrawColor(r, 30, 26, 24, 255);
        SDL_RenderFillRect(r, &d);
        SDL_SetRenderDrawColor(r, 90, 78, 66, 255);
        SDL_RenderDrawRect(r, &d);
        draw_paper_creature(r, SUMMON_FRAME_COUNT + i, d.x + d.w / 2,
                            d.y + d.h - 4, 40, 2, 0);
        if (pt_in(d, mx, my)) {
            const SummonTemplate *t = &MONSTER_TEMPLATES[i];
            tip_begin(t->name);
            tip_add("PV %d  -  degats %d  -  portee %d",
                    t->hp, t->atk, t->range);
            tip_add("Apparait a partir du niveau %d.",
                    (i % 3) * 3 + 1);
        }
    }
    if (g->menu_status[0])
        draw_text(r, font, g->menu_status, 24, 406, green);
    draw_text(r, font, "Niveau 1 : 1 monstre (doux).  Niveau 3 : 2 (ca se gatte).",
              24, 428, grey);
    draw_text(r, font, "Niveau 5 : 3 monstres.  Niveau 7 : 4, et ils se sont coordonnes.",
              24, 450, grey);
    draw_text(r, font, "Au-dela : 5 monstres, et ils ont pris des cours de gym.",
              24, 472, grey);
    draw_tooltip(r, font, mx, my);
}

/* ------------------------------------------------------------------ */
/* Ecran Defis du jour : 3 defis par jour                              */
/* ------------------------------------------------------------------ */
static void draw_challenges(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {245, 235, 215, 255};
    SDL_Color gold  = {255, 214, 120, 255};
    SDL_Color grey  = {170, 165, 155, 255};
    SDL_Color green = {120, 235, 140, 255};
    char line[220];
    int i, mx, my, done = 0;
    mouse_logical(&mx, &my);
    tip_reset();

    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderClear(r);
    draw_ui_panel(r, (SDL_Rect){8, 6, WIN_W - 16, WIN_H - 12});
    draw_ui_banner(r, (SDL_Rect){14, 8, WIN_W - 28, 48});
    draw_text(r, font, "DEFIS DU JOUR", 24, 14, gold);
    snprintf(line, sizeof line,
             "3 defis par jour, un cafe par defi (le cafe reste a vous)", g->challenge_points);
    draw_text(r, font, line, 24, 34, grey);
    for (i = 0; i < DAILY_CHALLENGES; i++) {
        SDL_Rect card = challenge_card(i);
        int hovered = pt_in(card, mx, my);
        int finished = g->challenge_done[i];
        if (finished) done++;
        SDL_SetRenderDrawColor(r, finished ? 34 : (hovered ? 44 : 32),
                               finished ? 52 : (hovered ? 40 : 28),
                               finished ? 34 : (hovered ? 46 : 26), 255);
        SDL_RenderFillRect(r, &card);
        SDL_SetRenderDrawColor(r, finished ? 120 : 205,
                               finished ? 190 : 170,
                               finished ? 120 : 105, 255);
        SDL_RenderDrawRect(r, &card);
        snprintf(line, sizeof line, "%d. %s", i + 1,
                 g->challenge_done[i] ? "Defi valide" : "Defi en cours");
        draw_text(r, font, line, card.x + 12, card.y + 6,
                  finished ? green : gold);
        draw_text(r, font, g->challenge_desc[i], card.x + 12, card.y + 30, white);
        snprintf(line, sizeof line, "Recompense : %d points",
                 g->challenge_reward[i]);
        draw_text(r, font, line, card.x + 12, card.y + 52, grey);
        {
            SDL_Rect b = challenge_btn(i);
            int hov = pt_in(b, mx, my);
            SDL_SetRenderDrawColor(r, finished ? 40 : (hov ? 80 : 55),
                                   finished ? 60 : (hov ? 130 : 95),
                                   finished ? 40 : (hov ? 70 : 55), 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
            SDL_RenderDrawRect(r, &b);
            draw_text(r, font, finished ? "OK" : "JOUER",
                      b.x + (finished ? 24 : 14), b.y + 8, white);
            if (hov) {
                tip_begin(finished ? "Defi deja valide" : "Jouer ce defi");
                tip_add("%s", g->challenge_desc[i]);
                tip_add("Le combat est lance en solo contre l'IA.");
                if (finished)
                    tip_add("Les 3 defis du jour sont signes.");
            }
        }
    }
    {
        SDL_Rect b = challenge_back_btn();
        SDL_SetRenderDrawColor(r, 64, 48, 38, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &b);
        draw_text(r, font, "RETOUR", b.x + 42, b.y + 12, white);
    }
    snprintf(line, sizeof line,
             "Defis valides aujourd'hui : %d / %d", done, DAILY_CHALLENGES);
    draw_text(r, font, line, 24, 60, grey);
    draw_text(r, font, "Les defis se renouvellent chaque jour a minuit. Dormez, c'est utile.",
              24, 460, grey);
    draw_tooltip(r, font, mx, my);
}

static void draw_pick(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color grey  = {130,130,130,255};
    char line[96];
    int unit = setup_unit_index(g);
    int pmx, pmy;
    mouse_logical(&pmx, &pmy);
    tip_reset();
    snprintf(line, sizeof line, "T%d - PERSONNAGE %d : la classe, ce choix qui pardonne peu",
             g->my_index + 1, g->setup_unit + 1);
    draw_text(r, font, line, 20, 20, white);
    if (g->challenge_seed) {
        snprintf(line, sizeof line, "Defi %u - bot neuronal",
                 g->challenge_seed);
        draw_text(r, font, line, 330, 20, grey);
    } else if (g->solo_mode) {
        draw_text(r, font, "Votre adversaire IA prepare son equipe. Il est deja sur.",
                  330, 20, grey);
    }
    int i;
    for (i = 0; i < N_CLASSES; i++) {
        snprintf(line, sizeof line, "%2d %-12s", i+1, CLASSES[i].name);
        /* classes sans sprite : grisees (Sadida en attente)          */
        int has_sprite = CLASS_CHARACTER_ROW[i] >= 0;
        SDL_Rect b = pick_class_rect(i);
        int mx, my;
        mouse_logical(&mx, &my);
        int hover = has_sprite && pt_in(b, mx, my);
        if (has_sprite)
            SDL_SetRenderDrawColor(r, hover ? 100 : 70, hover ? 100 : 70,
                                   hover ? 140 : 110, 255);
        else
            SDL_SetRenderDrawColor(r, 40, 40, 40, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 120, 120, 120, 255);
        SDL_RenderDrawRect(r, &b);
        draw_character_sprite(r, CLASS_CHARACTER_ROW[i], 0, 0,
                              (SDL_Rect){b.x, b.y, 28, 28}, 0);
        draw_text(r, font, line, b.x + 32, b.y + 6,
                  has_sprite ? white : grey);
        if (hover) {
            int k;
            tip_begin(CLASSES[i].name);
            tip_add("16 sorts : choisissez-en 4 au draft.");
            for (k = 0; k < 3; k++)
                tip_add("%s", CLASSES[i].spells[k].name);
            tip_add("... et 13 autres.");
        }
    }
    if (g->class_chosen[unit] >= 0) {
        snprintf(line, sizeof line, "Votre classe : %s",
                 CLASSES[g->class_chosen[unit]].name);
        draw_text(r, font, line, 20, 240, white);
    }
    {
        int slot;
        for (slot = 0; slot < g->team_size; slot++) {
            int enemy = unit_for_slot(g, 1 - g->my_index, slot);
            if (g->class_chosen[enemy] >= 0) {
                snprintf(line, sizeof line, "T%d perso %d : %s",
                         2 - g->my_index, slot + 1,
                         CLASSES[g->class_chosen[enemy]].name);
                draw_text(r, font, line, 300, 240 + slot*22, grey);
            }
        }
    }
    draw_text(r, font, "Favoris classe + panoplie :", 20, 315, white);
    {
        int i;
        for (i = 0; i < MAX_FAVORITES; i++) {
            SDL_Rect b = pick_favorite_rect(i);
            if (g->favorite_class[i] >= 0) {
                SDL_SetRenderDrawColor(r, 70, 85, 65, 255);
                SDL_RenderFillRect(r, &b);
                if (g->selected_favorite == i)
                    SDL_SetRenderDrawColor(r, 245, 205, 70, 255);
                else
                    SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
                SDL_RenderDrawRect(r, &b);
                snprintf(line, sizeof line, "F%d %s + P%d", i + 1,
                         CLASSES[g->favorite_class[i]].name,
                         g->favorite_pano[i] + 1);
                draw_text(r, font, line, b.x + 4, b.y + 7, white);
                if (pt_in(b, pmx, pmy)) {
                    tip_begin("Favori");
                    tip_add("%s + %s", CLASSES[g->favorite_class[i]].name,
                            PANOPLIES[g->favorite_pano[i]].name);
                    tip_add("Cliquez pour appliquer ce favori.");
                }
            }
        }
    }
    draw_tooltip(r, font, pmx, pmy);
}

/* ecran de draft : 16 sorts de la classe, en choisir 4                 */
static void draw_draft(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color green = {80,255,120,255};
    char line[128];
    int unit = setup_unit_index(g);
    int cls = g->class_chosen[unit];
    int i;
    if (cls < 0 || cls >= N_CLASSES) { g->state = ST_PICK; return; }

    snprintf(line, sizeof line, "PERSO %d - DRAFT %s : choisissez 4 sorts (%d/4)",
             g->setup_unit + 1, CLASSES[cls].name, g->draft_count[unit]);
    draw_text(r, font, line, 20, 20, white);
    for (i = 0; i < N_CLASS_SPELLS; i++) {
        Skill *s = &CLASSES[cls].spells[i];
        int col = i % 2, row = i / 2;
        SDL_Rect b = {10 + col * 330, 40 + row * 48, 320, 44};
        SDL_Rect icon = {b.x + 5, b.y + 4, 28, 36};
        if (g->draft[unit][i])
            SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
        else
            SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &b);
        if (g_spell_icons) {
            SDL_Rect icon_src = spell_icon_rect((cls * N_CLASS_SPELLS + i)
                                      % (SPELL_ICON_COLS * SPELL_ICON_ROWS));
            SDL_RenderCopy(r, g_spell_icons, &icon_src, &icon);
        }
        snprintf(line, sizeof line, "%X %s", i+1, s->name);
        draw_text(r, font, line, b.x + 40, b.y + 2, white);
        const char *kind = s->kind == SK_ATK ? "attaque" :
                           s->kind == SK_HEAL ? "soin"   :
                           s->kind == SK_BUFF ? "buff"   :
                           s->kind == SK_SUMMON ? "invocation" : "special";
        snprintf(line, sizeof line, "%s %d, %dPA, P%d, CD%d",
                 kind, s->power, s->pa, s->range, s->cooldown);
        draw_text(r, font, line, b.x + 40, b.y + 19, white);
        if (g->draft[unit][i]) {
            snprintf(line, sizeof line, "dans le deck");
            draw_text(r, font, line, b.x + 40, b.y + 33, green);
        }
    }
    {
        int mx, my, i;
        mouse_logical(&mx, &my);
        tip_reset();
        for (i = 0; i < N_CLASS_SPELLS; i++) {
            int col = i % 2, row = i / 2;
            SDL_Rect b = {10 + col * 330, 40 + row * 48, 320, 44};
            Skill *s = &CLASSES[cls].spells[i];
            if (pt_in(b, mx, my)) {
                int tmpl = summon_template_of(s);
                tip_begin(s->name);
                if (tmpl >= 0) {
                    tip_add("Invocation de %s", SUMMON_TEMPLATES[tmpl].name);
                    tip_add("PV %d - degats %d - portee %d",
                            SUMMON_TEMPLATES[tmpl].hp,
                            SUMMON_TEMPLATES[tmpl].atk,
                            SUMMON_TEMPLATES[tmpl].range);
                } else if (s->effect) {
                    tip_add("Transformation : +%d %% de degats, +2 PM.",
                            s->power);
                } else {
                    tip_add("Effet %d - %d PA - portee %d - CD %d tours",
                            s->power, s->pa, s->range, s->cooldown);
                }
                tip_add(g->draft[unit][i] ? "Deja dans votre deck."
                                           : "Cliquez pour l'ajouter.");
            }
        }
        if (g->draft_count[unit] >= N_EQUIPPED
            && pt_in(draft_btn(), mx, my)) {
            tip_begin("Valider le deck");
            tip_add("Vous avez choisi vos 4 sorts.");
        }
        draw_tooltip(r, font, mx, my);
    }
    if (g->draft_count[unit] >= N_EQUIPPED) {
        SDL_Rect vb = draft_btn();
        SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
        SDL_RenderFillRect(r, &vb);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &vb);
        draw_text(r, font, "VALIDER LE DECK (clic)", vb.x + 8, vb.y + 8, white);
    }
    {
        int slot;
        for (slot = 0; slot < g->team_size; slot++)
            if (g->draft_ready[unit_for_slot(g, 1 - g->my_index, slot)] == 1) {
                snprintf(line, sizeof line, "T%d a valide son deck (%d/%d)",
                         2 - g->my_index, slot + 1, g->team_size);
                draw_text(r, font, line, 300, 440 + slot*20, green);
            }
    }
    draw_text(r, font, "4 sorts, pas un de plus. Comme un plateau, mais en plus violent.",
              300, 440, (SDL_Color){150, 145, 135, 255});
}

/* ecran de choix de panoplie (apres le draft, avant le combat)          */
static void draw_stuff(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color gold = {255, 214, 120, 255};
    SDL_Color green = {80,255,120,255};
    char line[160];
    int i, mx, my;
    mouse_logical(&mx, &my);
    tip_reset();
    int unit = setup_unit_index(g);
    snprintf(line, sizeof line, "PERSO %d - choisissez votre panoplie",
             g->setup_unit + 1);
    draw_text(r, font, line, 20, 20, white);
    draw_text(r, font, "Survolez une panoplie : le detail de ses bonus, promis.",
              400, 38, (SDL_Color){160, 155, 145, 255});
    for (i = 0; i < N_PANOPLIES; i++) {
        Panoplie *p = &PANOPLIES[i];
        SDL_Rect b = stuff_card(i);
        int selected = g->stuff_chosen[unit] == i;
        int hovered = pt_in(b, mx, my);
        if (!p->name) continue;
        SDL_SetRenderDrawColor(r, selected ? 60 : (hovered ? 58 : 42),
                               selected ? 140 : (hovered ? 62 : 44),
                               selected ? 70 : (hovered ? 78 : 58), 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, selected ? gold.r : 150,
                               selected ? gold.g : 150,
                               selected ? gold.b : 150, 255);
        SDL_RenderDrawRect(r, &b);
        snprintf(line, sizeof line, "%d %s", i+1, p->name);
        if ((int)strlen(line) > 20) line[20] = 0;
        draw_text(r, font, line, b.x + 6, b.y + 3, white);
        /* resume court des bonus */
        {
            char bonus[128];
            int n = 0;
            bonus[0] = 0;
            if (p->hp_bonus)   n += snprintf(bonus+n, sizeof bonus-n, "PV+%d ", p->hp_bonus);
            if (p->dmg_pct)    n += snprintf(bonus+n, sizeof bonus-n, "DMG+%d%% ", p->dmg_pct);
            if (p->heal_pct)   n += snprintf(bonus+n, sizeof bonus-n, "SOIN+%d%% ", p->heal_pct);
            if (p->puissance)  n += snprintf(bonus+n, sizeof bonus-n, "PUI+%d ", p->puissance);
            if (p->range_bonus)n += snprintf(bonus+n, sizeof bonus-n, "POR+%d ", p->range_bonus);
            if (p->pa_bonus)   n += snprintf(bonus+n, sizeof bonus-n, "PA+%d ", p->pa_bonus);
            if (p->pm_bonus)   n += snprintf(bonus+n, sizeof bonus-n, "PM+%d ", p->pm_bonus);
            if (p->ret_pa)     n += snprintf(bonus+n, sizeof bonus-n, "RETPA-%d ", p->ret_pa);
            if (p->ret_pm)     n += snprintf(bonus+n, sizeof bonus-n, "RETPM-%d ", p->ret_pm);
            if (p->reflect)    n += snprintf(bonus+n, sizeof bonus-n, "RENVOI-%d", p->reflect);
            if (!bonus[0]) snprintf(bonus, sizeof bonus, "aucun bonus");
            if ((int)strlen(bonus) > 22) bonus[22] = 0;
            draw_text(r, font, bonus, b.x + 6, b.y + 21,
                      (SDL_Color){210, 205, 190, 255});
        }
        if (hovered) {
            tip_begin(p->name);
            tip_add("%s", p->desc);
            tip_add("PV %+d  DMG %+d%%  SOIN %+d%%  PUI %+d",
                    p->hp_bonus, p->dmg_pct, p->heal_pct, p->puissance);
            tip_add("Portee %+d  PA %+d  PM %+d",
                    p->range_bonus, p->pa_bonus, p->pm_bonus);
            tip_add("Retrait PA %+d  PM %+d  Renvoi %d",
                    p->ret_pa, p->ret_pm, p->reflect);
        }
    }
    if (g->stuff_chosen[unit] >= 0) {
        snprintf(line, sizeof line, "Votre panoplie : %s",
                 PANOPLIES[g->stuff_chosen[unit]].name);
        draw_text(r, font, line, 400, 20, green);
    }
    if (g->menu_status[0])
        draw_text(r, font, g->menu_status, 20, 38, green);
    {
        int i2;
        for (i2 = 0; i2 < MAX_FAVORITES; i2++) {
            SDL_Rect b = stuff_favorite_btn(i2);
            SDL_SetRenderDrawColor(r, 55, 75, 65, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
            SDL_RenderDrawRect(r, &b);
            snprintf(line, sizeof line, "SAUVER F%d", i2 + 1);
            draw_text(r, font, line, b.x + 8, b.y + 7, white);
            if (pt_in(b, mx, my)) {
                tip_begin("Enregistrer un favori");
                tip_add("Memorise la classe et la panoplie en F%d.",
                        i2 + 1);
            }
        }
        {
            SDL_Rect b = {520, 556, 136, 32};
            SDL_SetRenderDrawColor(r, 60, 140, 70, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
            SDL_RenderDrawRect(r, &b);
            draw_text(r, font, "CONTINUER", b.x + 14, b.y + 6, white);
            if (pt_in(b, mx, my)) {
                tip_begin("Continuer");
                tip_add("Valide la panoplie et passe a l'etape suivante.");
            }
        }
    }
    draw_tooltip(r, font, mx, my);
}

static void draw_dofus(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color green = {80,255,120,255};
    char line[160];
    int i;
    int team = g->songe_mode ? 0 : g->my_index;
    draw_text(r, font, "CHOISISSEZ LE DOFUS (un objet, une bentonite, l'espoir)", 20, 20, white);
    for (i = 0; i < N_DOFUS; i++) {
        Panoplie *p = &DOFUS[i];
        SDL_Rect b = {20 + (i%2)*330, 60 + (i/2)*105, 320, 100};
        if (g->team_dofus[team] == i)
            SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
        else
            SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &b);
        snprintf(line, sizeof line, "%d %s", i+1, p->name);
        draw_text(r, font, line, b.x + 6, b.y + 4, white);
        snprintf(line, sizeof line, "PV+%d DMG+%d%% SOIN+%d%% PUI+%d PORTE+%d",
                 p->hp_bonus, p->dmg_pct, p->heal_pct, p->puissance, p->range_bonus);
        draw_text(r, font, line, b.x + 6, b.y + 24, white);
        snprintf(line, sizeof line, "PA+%d PM+%d RETPA-%d RETPM-%d RENVOI-%d",
                 p->pa_bonus, p->pm_bonus, p->ret_pa, p->ret_pm, p->reflect);
        draw_text(r, font, line, b.x + 6, b.y + 44, white);
        snprintf(line, sizeof line, "(%s)", p->desc);
        draw_text(r, font, line, b.x + 6, b.y + 64, white);
    }
    if (g->team_dofus[team] >= 0) {
        snprintf(line, sizeof line, "Dofus choisi : %s",
                 DOFUS[g->team_dofus[team]].name);
        draw_text(r, font, line, 20, 440, green);
    }
    if (g->menu_status[0])
        draw_text(r, font, g->menu_status, 400, 440, green);
    {
        int mx, my, i;
        mouse_logical(&mx, &my);
        tip_reset();
        for (i = 0; i < N_DOFUS; i++) {
            SDL_Rect b = {20 + (i%2)*330, 60 + (i/2)*105, 320, 100};
            if (pt_in(b, mx, my)) {
                Panoplie *p = &DOFUS[i];
                tip_begin(p->name);
                tip_add("%s", p->desc);
                tip_add("PV %+d  DMG %+d%%  SOIN %+d%%  PUI %+d",
                        p->hp_bonus, p->dmg_pct, p->heal_pct, p->puissance);
                tip_add("Portee %+d  PA %+d  PM %+d  Renvoi %d",
                        p->range_bonus, p->pa_bonus, p->pm_bonus, p->reflect);
            }
        }
        draw_tooltip(r, font, mx, my);
    }
    {
        SDL_Rect ok = {520, 500, 130, 34};
        SDL_SetRenderDrawColor(r, g->team_dofus[team] >= 0 ? 55 : 45,
                               g->team_dofus[team] >= 0 ? 140 : 55,
                               g->team_dofus[team] >= 0 ? 65 : 55, 255);
        SDL_RenderFillRect(r, &ok);
        SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
        SDL_RenderDrawRect(r, &ok);
        draw_text(r, font, "VALIDER", ok.x + 34, ok.y + 9, white);
    }
}

static void draw_elements(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    static const char *names[ELEMENT_COUNT] = {"AIR", "FEU", "TERRE", "EAU"};
    Fighter *f = &g->f[setup_unit_index(g)];
    int i, total = 0;
    char line[128];
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color grey = {155, 155, 155, 255};
    for (i = 0; i < ELEMENT_COUNT; i++) total += f->elements[i];
    snprintf(line, sizeof line, "PERSO %d - repartissez %d points elementaires",
             g->setup_unit + 1, ELEMENT_POINTS);
    draw_text(r, font, line, 20, 20, white);
    draw_text(r, font, "Initiative = Air. Un malus d'Air ? Votre tour passe apres tout le monde.",
              20, 48, grey);
    draw_text(r, font, "Les boutons bougent la repartition par 5 points. La vie est un jeu de boutons.",
              20, 72, grey);
    draw_text(r, font, "Air (Agilite), Feu (Intelligence), Terre (Force), Eau (Chance) : 4 manieres de perdre.",
              20, 96, grey);
    for (i = 0; i < ELEMENT_COUNT; i++) {
        SDL_Rect minus = element_btn(i, 0), plus = element_btn(i, 1);
        snprintf(line, sizeof line, "%s : %d", names[i], f->elements[i]);
        draw_text(r, font, line, 100, 169 + i*60, white);
        SDL_SetRenderDrawColor(r, 110, 65, 65, 255);
        SDL_RenderFillRect(r, &minus);
        SDL_SetRenderDrawColor(r, 210, 210, 210, 255);
        SDL_RenderDrawRect(r, &minus);
        draw_text(r, font, "-5", minus.x + 18, minus.y + 8, white);
        SDL_SetRenderDrawColor(r, 55, 115, 65, 255);
        SDL_RenderFillRect(r, &plus);
        SDL_SetRenderDrawColor(r, 210, 210, 210, 255);
        SDL_RenderDrawRect(r, &plus);
        draw_text(r, font, "+5", plus.x + 18, plus.y + 8, white);
    }
    snprintf(line, sizeof line, "Points repartis : %d/%d", total, ELEMENT_POINTS);
    draw_text(r, font, line, 100, 372, white);
    {
        int mx, my;
        mouse_logical(&mx, &my);
        tip_reset();
        for (i = 0; i < ELEMENT_COUNT; i++) {
            if (pt_in(element_btn(i, 0), mx, my)) {
                tip_begin(names[i]);
                tip_add("Retirer 5 points de %s.", names[i]);
            } else if (pt_in(element_btn(i, 1), mx, my)) {
                tip_begin(names[i]);
                tip_add("Ajouter 5 points de %s.", names[i]);
            }
        }
        draw_tooltip(r, font, mx, my);
    }
    {
        SDL_Rect done = {100, 460, 250, 42};
        SDL_SetRenderDrawColor(r, total == ELEMENT_POINTS ? 55 : 45,
                               total == ELEMENT_POINTS ? 140 : 55,
                               total == ELEMENT_POINTS ? 65 : 55, 255);
        SDL_RenderFillRect(r, &done);
        SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
        SDL_RenderDrawRect(r, &done);
        draw_text(r, font, "VALIDER PERSONNAGE", done.x + 14, done.y + 10, white);
    }
}

static void draft_click(Game *g, int mx, int my)
{
    int i;
    int unit = setup_unit_index(g);
    int cls = g->class_chosen[unit];
    if (cls < 0) return;
    for (i = 0; i < N_CLASS_SPELLS; i++) {
        int col = i % 2, row = i / 2;
        SDL_Rect b = {10 + col * 330, 40 + row * 48, 320, 44};
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            if (!g->draft[unit][i] && g->draft_count[unit] < N_EQUIPPED) {
                g->draft[unit][i] = 1;
                g->draft_count[unit]++;
            } else if (g->draft[unit][i]) {
                g->draft[unit][i] = 0;
                g->draft_count[unit]--;
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    Game game;
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    TTF_Font *font = NULL;
    int i, j, running = 1, audio_scene = -1;

#ifdef __ANDROID__
    {
        const char *storage_path = SDL_AndroidGetInternalStoragePath();
        if (!storage_path || chdir(storage_path) != 0) {
            fprintf(stderr, "Android: stockage interne indisponible.\n");
            return 1;
        }
    }
#endif

    memset(&game, 0, sizeof game);
    game.my_index = 0;
    game.team_size = 1;
    game.team_size_choice = 1;
    game.net.sock = SOCK_INVALID;
    game.net.role = NET_NONE;
    game.sel_skill = -1;
    game.winner = -1;
    game.selected_favorite = -1;
    game.emote_fighter = -1;
    game.music_volume = 33;
    game.sfx_volume = 100;
    game.subtitles_when_muted = 1;
    game.active_challenge = -1;
    game.songe_party = 1;
    game.songe_level = 0;
    challenges_build(&game);
    challenges_load(&game);
    songe_record_load(&game);
    for (i = 0; i < MAX_FIGHTERS; i++) {
        game.class_chosen[i] = -1;
        game.draft_ready[i] = -1;
        game.draft_count[i] = 0;
        game.stuff_chosen[i] = -1;
        game.stuff_ready[i] = -1;
        for (j = 0; j < N_CLASS_SPELLS; j++) game.draft[i][j] = 0;
    }
    favorites_load(&game);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    srand((unsigned)time(NULL) ^ SDL_GetTicks() ^ (unsigned)clock());
#ifdef __ANDROID__
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) != IMG_INIT_PNG) {
#else
    if ((IMG_Init(IMG_INIT_PNG | IMG_INIT_WEBP)
     & (IMG_INIT_PNG | IMG_INIT_WEBP)) != (IMG_INIT_PNG | IMG_INIT_WEBP)) {
#endif
    fprintf(stderr, "SDL_image (PNG/WebP): %s\n", IMG_GetError());
    return 1;
    }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF\n"); return 1; }
    if (!net_init()) { fprintf(stderr, "WSA\n"); return 1; }

    win = SDL_CreateWindow("Dofus Arena - Legend (FTW°)",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WIN_W, WIN_H, 0);
    if (!win) {
        fprintf(stderr, "Fenetre SDL indisponible: %s\n", SDL_GetError());
        return 1;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        fprintf(stderr, "Rendu accelere indisponible, repli logiciel: %s\n",
                SDL_GetError());
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        fprintf(stderr, "Rendu SDL indisponible: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return 1;
    }
    /* rendu responsive : echelle automatique, interface 672x640 logique */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    g_ren = ren;
    g_window = win;
    SDL_ShowWindow(win);
    SDL_RaiseWindow(win);
    font = TTF_OpenFont("assets/font.ttf", 16);
#ifdef _WIN32
    if (!font) {
        const char *windows_dir = getenv("WINDIR");
        char font_path[512];
        if (windows_dir) {
            int path_len = snprintf(font_path, sizeof font_path,
                                    "%s\\Fonts\\segoeui.ttf", windows_dir);
            if (path_len >= 0 && (size_t)path_len < sizeof font_path)
                font = TTF_OpenFont(font_path, 16);
        }
    }
    if (!font) {
        fprintf(stderr, "Police introuvable (assets/font.ttf ou Windows Segoe UI).\n");
    }
#else
    if (!font)
        font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16);
    if (!font)
        fprintf(stderr, "Police introuvable (assets/font.ttf / DejaVu Sans).\n");
#endif

    g_tilesheet = load_asset(ren, "assets/tiles/tilesheet.png");
    load_ui_atlas(ren);
    load_visual_effects(ren);
    load_audio();
    apply_audio_settings(&game);

    /* La commande CLI est memorisee : elle ne sera executee qu'apres
     * avoir saisi le code PIN (le jeu est verrouille au lancement).   */
    {
        int cli = 0;                 /* 1 = host, 2 = join, 3 = challenge */
        unsigned cli_seed = 424242u;
        char cli_ip[64] = {0};
        int cli_size = 0;
        if (argc >= 2 && !strcmp(argv[1], "host")) {
            cli = 1;
            if (argc >= 3) {
                int size = atoi(argv[2]);
                if (size == 1 || size == 2 || size == 5) cli_size = size;
            }
        } else if (argc >= 3 && !strcmp(argv[1], "join")) {
            cli = 2;
            snprintf(cli_ip, sizeof cli_ip, "%s", argv[2]);
        } else if (argc >= 2 && !strcmp(argv[1], "challenge")) {
            cli = 3;
            if (argc >= 3) {
                char *end = NULL;
                unsigned long long parsed;
                if (argv[2][0] == '-') {
                    fprintf(stderr, "graine invalide: utilisez un entier positif.\n");
                    return 1;
                }
                errno = 0;
                parsed = strtoull(argv[2], &end, 10);
                if (!argv[2][0] || errno == ERANGE || !end || *end != '\0'
                    || parsed == 0 || parsed > UINT_MAX) {
                    fprintf(stderr, "graine invalide: utilisez un entier 1..%u.\n",
                            UINT_MAX);
                    return 1;
                }
                cli_seed = (unsigned)parsed;
            }
        }
        /* verrouillage : code PIN a definir ou a saisir                */
        pin_load(&game);
        game.pin_mode = game.pin_set ? 1 : 0;
        game.pin_attempts = PIN_MAX_TRIES;
        game.state = ST_PIN;
        pin_reset_entry(&game);
        if (cli == 1) {
            if (cli_size) game.team_size_choice = cli_size;
            game.team_size = game.team_size_choice;
        }
        /* memorise pour l'apres-deverrouillage                       */
        game.cli_pending = cli;
        game.cli_seed = cli_seed;
        snprintf(game.cli_ip, sizeof game.cli_ip, "%s", cli_ip);
    }

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
                if (game.net.sock != SOCK_INVALID
                    && game.state != ST_GAMEOVER)
                    net_send(&game, "BYE");
            } else if (ev.type == SDL_KEYDOWN) {
                /* CLAVIER : uniquement pour taper un message de chat   */
                SDL_Keycode k = ev.key.keysym.sym;
                if (game.chat_active) {
                    size_t len = strlen(game.chat_input);
                    if (k == SDLK_RETURN) {
                        if (len > 0) {
                            char msg[300];
                            process_social_message(&game, game.my_index,
                                                   game.chat_input);
                            snprintf(msg, sizeof msg, "CHAT %d %s",
                                     game.my_index, game.chat_input);
                            net_send(&game, msg);
                            {   /* affichage local immediate            */
                                char line[128];
                                snprintf(line, sizeof line, "Vous> %s",
                                         game.chat_input);
                                if (game.chat_log_n < 6) {
                                    strcpy(game.chat_log[game.chat_log_n], line);
                                    game.chat_log_n++;
                                } else {
                                    int q;
                                    for (q = 0; q < 5; q++)
                                        strcpy(game.chat_log[q], game.chat_log[q+1]);
                                    strcpy(game.chat_log[5], line);
                                }
                            }
                            game.chat_input[0] = 0;
                        }
                        game.chat_active = 0;
                    } else if (k == SDLK_ESCAPE) {
                        game.chat_active = 0;
                    } else if (k == SDLK_BACKSPACE && len > 0) {
                        game.chat_input[len - 1] = 0;
                    } else if (k >= 32 && k <= 126
                               && len < sizeof game.chat_input - 1) {
                        game.chat_input[len] = (char)k;
                        game.chat_input[len + 1] = 0;
                    }
                } else if (k == SDLK_F11) {
                    apply_fullscreen(&game, !game.fullscreen);
                } else if (game.state == ST_MENU && game.settings_open) {
                    if (k == SDLK_ESCAPE)
                        game.settings_open = 0;
                } else if ((game.state == ST_SONGE
                            || game.state == ST_CHALLENGES
                            || game.state == ST_DEFS
                            || game.state == ST_ACHIEVEMENTS)
                           && k == SDLK_ESCAPE) {
                    game.state = ST_MENU;
                    game.ip_editing = 0;
                    SDL_StopTextInput();
                } else if (game.state == ST_SONGE && game.ip_editing) {
                    if (k == SDLK_RETURN || k == SDLK_ESCAPE) {
                        game.ip_editing = 0;
                        SDL_StopTextInput();
                    } else if (k == SDLK_BACKSPACE
                               && strlen(game.ip_text) > 0) {
                        game.ip_text[strlen(game.ip_text) - 1] = 0;
                    }
                } else if (game.state == ST_MENU && game.ip_editing) {
                    size_t len = strlen(game.ip_text);
                    if (k == SDLK_RETURN || k == SDLK_ESCAPE) {
                        game.ip_editing = 0;
                        SDL_StopTextInput();
                    } else if (k == SDLK_BACKSPACE && len > 0) {
                        game.ip_text[len - 1] = 0;
                    }
                } else if (game.state == ST_BATTLE
                           && (k == SDLK_ESCAPE || game.in_game_menu_open)) {
                    if (k == SDLK_ESCAPE) {
                        if (game.surrender_confirm)
                            game.surrender_confirm = 0;
                        else if (game.in_game_menu_open)
                            game.in_game_menu_open = 0;
                        else {
                            game.in_game_menu_open = 1;
                            game.emote_menu_open = 0;
                            game.chat_active = 0;
                            SDL_StopTextInput();
                        }
                    } else if (game.surrender_confirm) {
                        if (k == SDLK_RETURN || k == SDLK_y)
                            surrender_match(&game);
                        else if (k == SDLK_n)
                            game.surrender_confirm = 0;
                    } else if (k == SDLK_1) {
                        game.in_game_menu_open = 0;
                    } else if (k == SDLK_2) {
                        return_to_main_menu(&game);
                    } else if (k == SDLK_3) {
                        game.surrender_confirm = 1;
                    } else if (k == SDLK_4) {
                        if (game.net.sock != SOCK_INVALID)
                            net_send(&game, "BYE");
                        running = 0;
                    }
                } else if (game.state == ST_BATTLE
                           && unit_is_mine(&game, game.turn)) {
                    static const int emotes[8] = {0,1,2,3,16,17,18,19};
                    if (k == SDLK_e) {
                        game.emote_menu_open = !game.emote_menu_open;
                        play_sound(&game, game.emote_menu_open
                                   ? SFX_MENU_SELECT : SFX_MENU_CANCEL);
                    } else if (game.emote_menu_open
                               && k >= SDLK_1 && k <= SDLK_8) {
                        show_emote(&game, game.turn, emotes[k - SDLK_1]);
                        play_sound(&game, SFX_MENU_SELECT);
                    } else if (!game.emote_menu_open
                               && k >= SDLK_1 && k <= SDLK_4) {
                        /* sorts 1-4 : zone bleue puis clic sur la cible */
                        game.sel_skill = game.sel_skill == k - SDLK_1
                                       ? -1 : k - SDLK_1;
                        play_sound(&game, game.sel_skill >= 0
                                   ? SFX_MENU_SELECT : SFX_MENU_CANCEL);
                    }
                }
            } else if (ev.type == SDL_MOUSEWHEEL) {
                if (game.state == ST_DEFS) {
                    int step = ev.wheel.y > 0 ? -1 : (ev.wheel.y < 0 ? 1 : 0);
                    game.defs_scroll += step;
                }
            } else if (ev.type == SDL_TEXTINPUT
                       && (game.state == ST_MENU || game.state == ST_SONGE)
                       && game.ip_editing) {
                const char *text = ev.text.text;
                while (*text && strlen(game.ip_text) < sizeof game.ip_text - 1) {
                    size_t len = strlen(game.ip_text);
                    if ((*text >= '0' && *text <= '9') || *text == '.') {
                        game.ip_text[len] = *text;
                        game.ip_text[len + 1] = 0;
                    }
                    text++;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN
                       && ev.button.button == SDL_BUTTON_LEFT) {
                int mx, my;
                point_logical(ev.button.x, ev.button.y, &mx, &my);
                if (getenv("DAF_DEBUG"))
                    fprintf(stderr, "RAWCLICK %d %d %d %d\n",
                            ev.button.x, ev.button.y, mx, my);
                if (game.state == ST_MENU) {
                    if (game.settings_open) {
                        int row;
                        if (pt_in(settings_back_btn(), mx, my)) {
                            game.settings_open = 0;
                        } else {
                            for (row = 0; row < 2; row++) {
                                int *volume = row == 0 ? &game.music_volume
                                                       : &game.sfx_volume;
                                if (pt_in(settings_minus_btn(row), mx, my)
                                        && *volume > 0) {
                                    *volume = *volume >= 10 ? *volume - 10 : 0;
                                    apply_audio_settings(&game);
                                } else if (pt_in(settings_plus_btn(row), mx, my)
                                           && *volume < 100) {
                                    *volume = *volume <= 90
                                            ? *volume + 10 : 100;
                                    apply_audio_settings(&game);
                                }
                            }
                            for (row = 3; row < 7; row++) {
                                if (!pt_in(settings_toggle_btn(row), mx, my))
                                    continue;
                                if (row == 3) {
                                    game.colorblind_mode = !game.colorblind_mode;
                                } else if (row == 4) {
                                    game.reduce_flashing = !game.reduce_flashing;
                                } else if (row == 5) {
                                    game.subtitles_when_muted =
                                        !game.subtitles_when_muted;
                                } else {
                                    apply_fullscreen(&game, !game.fullscreen);
                                }
                            }
                        }
                    } else if (game.ip_editing) {
                        /* clavier virtuel IP                            */
                        int kb;
                        for (kb = 0; kb < 12; kb++) {
                            SDL_Rect b = menu_kbd(kb);
                            if (pt_in(b, mx, my)) {
                                size_t len = strlen(game.ip_text);
                                static const char K[12] =
                                    {'1','2','3','4','5','6','7','8','9','0','.','\b'};
                                if (K[kb] == '\b') {
                                    if (len > 0) game.ip_text[len-1] = 0;
                                } else if (len < sizeof game.ip_text - 1) {
                                    game.ip_text[len] = K[kb];
                                    game.ip_text[len+1] = 0;
                                }
                            }
                        }
                        SDL_Rect ok = {20 + 6*55, 180 + 45, 160, 40};
                        if (pt_in(ok, mx, my)) {
                            game.ip_editing = 0;
                            SDL_StopTextInput();
                        }
                    } else {
                        SDL_Rect bi = menu_btn_ipfield();
                        SDL_Rect bh = menu_btn_host();
                        SDL_Rect bj = menu_btn_join();
                        int handled = 0;
                        static const int sizes[3] = {1, 2, 5};
                        int q;
                        if (pt_in(menu_settings_btn(), mx, my)) {
                            game.settings_open = 1;
                            handled = 1;
                        }
                        {
                            SDL_Rect ab = menu_btn_achievements();
                            if (!handled && pt_in(ab, mx, my)) {
                                game.state = ST_ACHIEVEMENTS;
                                handled = 1;
                            }
                        }
                        if (!handled && pt_in(menu_btn_songe(), mx, my)) {
                            game.state = ST_SONGE;
                            game.ip_editing = 0;
                            handled = 1;
                        }
                        if (!handled && pt_in(menu_btn_challenges(), mx, my)) {
                            game.state = ST_CHALLENGES;
                            handled = 1;
                        }
                        if (!handled && pt_in(menu_btn_defs(), mx, my)) {
                            game.state = ST_DEFS;
                            game.defs_cat = 0;
                            game.defs_scroll = 0;
                            handled = 1;
                        }
                        for (q = 0; q < 3; q++) {
                            if (!handled && pt_in(menu_size_btn(q), mx, my)) {
                                game.team_size_choice = sizes[q];
                                if (game.net.role == NET_HOST
                                    && !game.net.connected)
                                    game.team_size = sizes[q];
                                handled = 1;
                                break;
                            }
                        }
                        for (q = 0; !handled && q < MAX_FAVORITES; q++) {
                            if (pt_in(favorite_btn(q), mx, my)) {
                                game.selected_favorite = q;
                                snprintf(game.menu_status,
                                         sizeof game.menu_status,
                                         game.favorite_class[q] >= 0
                                         ? "Favori selectionne : F%d."
                                         : "Emplacement de favori F%d vide.",
                                         q + 1);
                                handled = 1;
                            }
                        }
                        if (!handled && pt_in(menu_btn_solo(), mx, my)
                            && game.net.sock == SOCK_INVALID) {
                            start_solo_game(&game, 0);
                            handled = 1;
                        }
                        if (!handled && pt_in(menu_btn_bot_model(), mx, my)
                            && game.net.sock == SOCK_INVALID) {
                            game.neural_bot = !game.neural_bot;
                            snprintf(game.menu_status, sizeof game.menu_status,
                                     game.neural_bot
                                     ? "Bot neuronal embarque selectionne."
                                     : "Bot classique selectionne.");
                            handled = 1;
                        }
                        if (!handled && pt_in(bh, mx, my)
                            && game.net.sock == SOCK_INVALID) {
                            if (game.net.role == NET_JOIN) {
                                if (!net_begin_join(&game, game.ip_text)) {
                                    snprintf(game.menu_status,
                                             sizeof game.menu_status,
                                             "Reconnexion impossible : verifiez l'IP IPv4.");
                                }
                            } else {
                                game.team_size = game.team_size_choice;
                                if (!net_open(&game, NET_HOST, NULL)) {
                                    fprintf(stderr, "bind 7777 impossible\n");
                                    snprintf(game.menu_status,
                                             sizeof game.menu_status,
                                             "Impossible d'ouvrir le port UDP 7777.");
                                } else {
                                    printf("En attente sur le port %d...\n", PORT);
                                    snprintf(game.menu_status,
                                             sizeof game.menu_status,
                                             "En attente sur UDP 7777. Partagez votre IP publique.");
                                }
                            }
                        } else if (!handled && pt_in(bi, mx, my)) {
                            game.ip_editing = 1;
                            SDL_StartTextInput();
                        } else if (!handled && pt_in(bj, mx, my)
                                  && game.net.sock == SOCK_INVALID) {
                            if (!game.ip_text[0]) {
                                snprintf(game.menu_status,
                                         sizeof game.menu_status,
                                         "Saisissez l'IP IPv4 de l'hote.");
                            } else if (net_begin_join(&game, game.ip_text)) {
                                /* net_begin_join affiche l'etat de connexion. */
                            } else {
                                fprintf(stderr, "IP invalide\n");
                                snprintf(game.menu_status,
                                         sizeof game.menu_status,
                                         "IP invalide : entrez une adresse IPv4.");
                            }
                        }
                    }
                } else if (game.state == ST_PIN) {
                    int k;
                    int locked = ((Sint32)(game.pin_lock_until
                                           - SDL_GetTicks()) > 0);
                    if (pt_in(pin_quit(), mx, my)) {
                        running = 0;
                    } else if (pt_in(pin_forget(), mx, my)) {
                        /* mot de passe oublie : confirmation puis effacement */
                        if (!game.pin_forget_armed) {
                            game.pin_forget_armed = 1;
                            snprintf(game.pin_msg, sizeof game.pin_msg,
                                     "Recliquez pour effacer le code ET "
                                     "toutes les donnees.");
                        } else {
                            wipe_all_data(&game);
                            game.pin_forget_armed = 0;
                            snprintf(game.pin_msg, sizeof game.pin_msg,
                                     "Donnees effacees. Definissez un nouveau code.");
                        }
                    } else if (!locked) {
                        for (k = 0; k < 12; k++) {
                            if (!pt_in(pin_key(k), mx, my)) continue;
                            if (k == 9) {              /* C : effacer      */
                                pin_reset_entry(&game);
                            } else if (k == 11) {      /* OK : valider     */
                                pin_submit(&game);
                            } else if (game.pin_len < PIN_LENGTH) {
                                int digit = (k == 10) ? 0 : (k + 1);
                                game.pin_entry[game.pin_len++] =
                                    (char)('0' + digit);
                                game.pin_entry[game.pin_len] = 0;
                                game.pin_msg[0] = 0;
                            }
                            break;
                        }
                    }
                } else if (game.state == ST_DEFS) {
                    int q;
                    if (pt_in(defs_back_btn(), mx, my)) {
                        game.state = ST_MENU;
                    } else if (pt_in(defs_up_btn(), mx, my)) {
                        game.defs_scroll--;
                    } else if (pt_in(defs_down_btn(), mx, my)) {
                        game.defs_scroll++;
                    } else {
                        for (q = 0; q < DEF_NCAT; q++)
                            if (pt_in(defs_cat_rect(q), mx, my)) {
                                game.defs_cat = q;
                                game.defs_scroll = 0;
                                break;
                            }
                    }
                } else if (game.state == ST_SONGE) {
                    int q;
                    if (pt_in(songe_btn_back(), mx, my)) {
                        game.state = ST_MENU;
                    } else if (pt_in(songe_btn_ipfield(), mx, my)) {
                        game.ip_editing = !game.ip_editing;
                        if (game.ip_editing) SDL_StartTextInput();
                        else SDL_StopTextInput();
                    } else if (game.ip_editing) {
                        /* clavier virtuel : saisie de l'IP a la souris     */
                        int kb;
                        static const char K[12] =
                            {'1','2','3','4','5','6','7','8','9','0','.','\b'};
                        for (kb = 0; kb < 12; kb++) {
                            SDL_Rect b = songe_kbd(kb);
                            if (!pt_in(b, mx, my)) continue;
                            {
                                size_t len = strlen(game.ip_text);
                                if (K[kb] == '\b') {
                                    if (len > 0) game.ip_text[len-1] = 0;
                                } else if (len < sizeof game.ip_text - 1) {
                                    game.ip_text[len] = K[kb];
                                    game.ip_text[len+1] = 0;
                                }
                            }
                        }
                        if (pt_in(songe_kbd_ok(), mx, my)) {
                            game.ip_editing = 0;
                            SDL_StopTextInput();
                        }
                    } else {
                        for (q = 0; q < 5; q++)
                            if (pt_in(songe_party_btn(q), mx, my)) {
                                game.songe_party = q + 1;
                                break;
                            }
                        if (pt_in(songe_btn_solo(), mx, my)
                            && game.net.sock == SOCK_INVALID) {
                            songe_start_run(&game, game.songe_party, 1);
                        } else if (pt_in(songe_btn_host(), mx, my)
                                   && game.net.sock == SOCK_INVALID) {
                            if (!net_open(&game, NET_HOST, NULL)) {
                                snprintf(game.menu_status,
                                         sizeof game.menu_status,
                                         "Bind UDP 7777 impossible.");
                            } else {
                                game.team_size = game.songe_party;
                                songe_prepare_host(&game);
                                snprintf(game.menu_status,
                                         sizeof game.menu_status,
                                         "Songe heberge : en attente d'un joueur.");
                            }
                        } else if (pt_in(songe_btn_join(), mx, my)
                                   && game.net.sock == SOCK_INVALID) {
                            if (!game.ip_text[0]) {
                                snprintf(game.menu_status,
                                         sizeof game.menu_status,
                                         "Saisissez d'abord l'IP de l'hote.");
                            } else {
                                game.songe_want = 1;
                                net_begin_join(&game, game.ip_text);
                            }
                        }
                    }
                } else if (game.state == ST_CHALLENGES) {
                    int q;
                    if (pt_in(challenge_back_btn(), mx, my)) {
                        game.state = ST_MENU;
                    } else {
                        for (q = 0; q < DAILY_CHALLENGES; q++) {
                            /* toute la carte est cliquable              */
                            if (!pt_in(challenge_card(q), mx, my)
                                && !pt_in(challenge_btn(q), mx, my)) continue;
                            if (game.challenge_done[q]) break;
                            if (game.net.sock != SOCK_INVALID) break;
                            start_challenge(&game, q);
                            break;
                        }
                    }
                } else if (game.state == ST_PICK) {
                    int pick, slot = game.setup_unit;
                    int unit = setup_unit_index(&game);
                    int handled = 0;
                    for (pick = 0; pick < MAX_FAVORITES; pick++) {
                        if (game.favorite_class[pick] >= 0
                            && pt_in(pick_favorite_rect(pick), mx, my)) {
                            int msg_unit = unit;
                            game.class_chosen[unit] = game.favorite_class[pick];
                            game.stuff_chosen[unit] = game.favorite_pano[pick];
                            {
                                char msg[32];
                                snprintf(msg, sizeof msg, "CLASS %d %d",
                                         msg_unit, game.class_chosen[unit]);
                                net_send(&game, msg);
                                snprintf(msg, sizeof msg, "STUFF %d %d",
                                         msg_unit, game.stuff_chosen[unit]);
                                net_send(&game, msg);
                            }
                            advance_setup(&game);
                            handled = 1;
                            break;
                        }
                    }
                    if (handled) continue;
                    for (pick = 0; pick < N_CLASSES; pick++) {
                        if (CLASS_CHARACTER_ROW[pick] < 0) continue;
                        if (pt_in(pick_class_rect(pick), mx, my)) {
                            game.class_chosen[unit] = pick;
                            game.match_class = pick;
                            if (game.selected_favorite >= 0
                                && game.selected_favorite < MAX_FAVORITES
                                && game.favorite_class[game.selected_favorite] == pick)
                                game.stuff_chosen[unit] =
                                    game.favorite_pano[game.selected_favorite];
                            char msg[32];
                            snprintf(msg, sizeof msg, "CLASS %d %d",
                                     unit, pick);
                            net_send(&game, msg);
                            if (game.stuff_chosen[unit] >= 0) {
                                snprintf(msg, sizeof msg, "STUFF %d %d",
                                         unit, game.stuff_chosen[unit]);
                                net_send(&game, msg);
                            }
                            advance_setup(&game);
                            break;
                        }
                    }
                } else if (game.state == ST_DRAFT) {
                    SDL_Rect vb = draft_btn();
                    int unit = setup_unit_index(&game);
                    if (pt_in(vb, mx, my)
                        && game.draft_count[unit] == N_EQUIPPED) {
                        sync_draft(&game, unit);
                    } else {
                        draft_click(&game, mx, my);
                    }
                } else if (game.state == ST_STUFF) {
                    int p, unit = setup_unit_index(&game);
                    for (p = 0; p < N_PANOPLIES; p++) {
                        if (pt_in(stuff_card(p), mx, my)) {
                            game.stuff_chosen[unit] = p;
                            break;
                        }
                    }
                    for (p = 0; p < MAX_FAVORITES; p++) {
                        if (pt_in(stuff_favorite_btn(p), mx, my)
                            && game.class_chosen[unit] >= 0
                            && game.stuff_chosen[unit] >= 0) {
                            game.favorite_class[p] = game.class_chosen[unit];
                            game.favorite_pano[p] = game.stuff_chosen[unit];
                            game.selected_favorite = p;
                            snprintf(game.menu_status, sizeof game.menu_status,
                                     favorites_save(&game)
                                     ? "Favori F%d enregistre."
                                     : "Erreur : impossible d'enregistrer le favori F%d.",
                                     p + 1);
                            break;
                        }
                    }
                    {
                        SDL_Rect done = {520, 446, 136, 28};
                        if (pt_in(done, mx, my)
                            && game.stuff_chosen[unit] >= 0)
                            sync_stuff(&game, unit);
                    }
                } else if (game.state == ST_ELEMENTS) {
                    Fighter *f = &game.f[unit_for_slot(&game, game.my_index,
                                                       game.setup_unit)];
                    int element, total = 0, i;
                    for (i = 0; i < ELEMENT_COUNT; i++)
                        total += f->elements[i];
                    for (element = 0; element < ELEMENT_COUNT; element++) {
                        if (pt_in(element_btn(element, 0), mx, my)
                            && f->elements[element] >= 5)
                            f->elements[element] -= 5;
                        else if (pt_in(element_btn(element, 1), mx, my)
                                 && total <= ELEMENT_POINTS - 5
                                 && f->elements[element] <= ELEMENT_POINTS - 5)
                            f->elements[element] += 5;
                    }
                    {
                        SDL_Rect done = {100, 418, 250, 38};
                        if (pt_in(done, mx, my)) {
                            total = 0;
                            for (i = 0; i < ELEMENT_COUNT; i++)
                                total += f->elements[i];
                            if (total == ELEMENT_POINTS) {
                                int unit = (int)(f - game.f);
                                char msg[96];
                                snprintf(msg, sizeof msg, "ELEMENTS %d %d %d %d %d",
                                         unit, f->elements[ELEM_AIR],
                                         f->elements[ELEM_FIRE],
                                         f->elements[ELEM_EARTH],
                                         f->elements[ELEM_WATER]);
                                net_send(&game, msg);
                                game.draft_ready[unit] = 2;
                                advance_setup(&game);
                            }
                        }
                    }
                } else if (game.state == ST_DOFUS) {
                    int team = game.songe_mode ? 0 : game.my_index;
                    int i;
                    for (i = 0; i < N_DOFUS; i++) {
                        SDL_Rect b = {20 + (i%2)*330, 60 + (i/2)*105, 320, 100};
                        if (pt_in(b, mx, my)) game.team_dofus[team] = i;
                    }
                    {
                        SDL_Rect ok = {520, 392, 130, 32};
                        if (pt_in(ok, mx, my) && game.team_dofus[team] >= 0) {
                            char msg[32];
                            game.dofus_ready[team] = 1;
                            snprintf(msg, sizeof msg, "DOFUS %d %d",
                                     team, game.team_dofus[team]);
                            net_send(&game, msg);
                            advance_setup(&game);
                        }
                    }
                } else if (game.state == ST_ACHIEVEMENTS) {
                    {
                        SDL_Rect up = {600, 60, 40, 30};
                        SDL_Rect down = {600, 440, 40, 30};
                        SDL_Rect back = {440, 424, 150, 32};
                        if (pt_in(up, mx, my)) game.achievement_scroll--;
                        else if (pt_in(down, mx, my)) game.achievement_scroll++;
                        else if (pt_in(back, mx, my)) game.state = ST_MENU;
                    }
                } else if (game.state == ST_BATTLE) {
                    int y0 = 0;
                    int handled = 0;
                    if (game.in_game_menu_open) {
                        if (game.surrender_confirm) {
                            if (pt_in(surrender_dialog_button(0), mx, my))
                                surrender_match(&game);
                            else if (pt_in(surrender_dialog_button(1), mx, my))
                                game.surrender_confirm = 0;
                        } else if (pt_in(ingame_menu_option(0), mx, my)) {
                            game.in_game_menu_open = 0;
                        } else if (pt_in(ingame_menu_option(1), mx, my)) {
                            return_to_main_menu(&game);
                        } else if (pt_in(ingame_menu_option(2), mx, my)) {
                            game.surrender_confirm = 1;
                        } else if (pt_in(ingame_menu_option(3), mx, my)) {
                            if (game.net.sock != SOCK_INVALID)
                                net_send(&game, "BYE");
                            running = 0;
                        } else if (!pt_in(ingame_menu_panel(), mx, my)) {
                            game.in_game_menu_open = 0;
                        }
                        handled = 1;
                    }
                    if (!handled && game.emote_menu_open) {
                        static const int emotes[8] = {0,1,2,3,16,17,18,19};
                        int option;
                        for (option = 0; option < 8; option++) {
                            if (pt_in(emote_option_rect(option), mx, my)) {
                                show_emote(&game, game.turn, emotes[option]);
                                play_sound(&game, SFX_MENU_SELECT);
                                handled = 1;
                                break;
                            }
                        }
                        if (!handled) {
                            game.emote_menu_open = 0;
                            play_sound(&game, SFX_MENU_CANCEL);
                        }
                    }
                    if (!handled && pt_in(ingame_menu_btn(y0), mx, my)) {
                        game.in_game_menu_open = 1;
                        game.emote_menu_open = 0;
                        game.chat_active = 0;
                        SDL_StopTextInput();
                        handled = 1;
                    }
                    if (!handled) {
                        int tx = -1, ty = -1;
                        iso_pick(&game, mx, my, &tx, &ty);
                        if (tx >= 0) {
                            if (game.cell[ty][tx] == CELL_GROUND) {
                                if (game.sel_skill >= 0)
                                    try_cast(&game, tx, ty);
                                else
                                    try_move(&game, tx, ty);
                            }
                        }
                    }
                    if (!handled && my < BAND_H) {
                        /* barre de sorts : clic pour selectionner      */
                        int k;
                        for (k = 0; k < N_EQUIPPED; k++) {
                            SDL_Rect b = {350 + k*165, y0 + 6, 160, 58};
                            if (pt_in(b, mx, my))
                                game.sel_skill = (game.sel_skill == k) ? -1 : k;
                        }
                        /* fin de tour                                   */
                        if (unit_is_mine(&game, game.turn)
                            && pt_in(endturn_btn(y0), mx, my))
                            end_turn(&game, 0);
                        /* chat : bouton / champ / envoyer               */
                        if (pt_in(chat_btn(y0), mx, my)
                            || pt_in(chat_field(y0), mx, my))
                            game.chat_active = !game.chat_active;
                        if (pt_in(chat_send(y0), mx, my)) {
                            if (game.chat_input[0]) {
                                char msg[300];
                                process_social_message(&game, game.my_index,
                                                       game.chat_input);
                                snprintf(msg, sizeof msg, "CHAT %d %s",
                                         game.my_index, game.chat_input);
                                net_send(&game, msg);
                                {
                                    char line[128];
                                    snprintf(line, sizeof line, "Vous> %s",
                                             game.chat_input);
                                    if (game.chat_log_n < 6) {
                                        strcpy(game.chat_log[game.chat_log_n],
                                               line);
                                        game.chat_log_n++;
                                    } else {
                                        int q;
                                        for (q = 0; q < 5; q++)
                                            strcpy(game.chat_log[q],
                                                   game.chat_log[q+1]);
                                        strcpy(game.chat_log[5], line);
                                    }
                                }
                                game.chat_input[0] = 0;
                            }
                            game.chat_active = 0;
                        }
                    }
                } else if (game.state == ST_GAMEOVER) {
                    /* retour au menu apres la partie                         */
                    return_to_main_menu(&game);
                }
            }
        }

        if (game.net.sock != SOCK_INVALID) {
            Uint32 now;
            net_poll(&game);
            now = SDL_GetTicks();
            if (game.net.sock != SOCK_INVALID) {
                if (game.net.connected) {
                    if (now - game.net.last_recv_ms >= NET_TIMEOUT_MS) {
                        net_disconnect(&game,
                            "Connexion perdue. Cliquez RECONNECTER pour relancer.");
                    } else if (now - game.net.last_keepalive_ms
                               >= NET_KEEPALIVE_MS) {
                        net_send(&game, "PING");
                        game.net.last_keepalive_ms = now;
                    }
                } else if (game.net.role == NET_JOIN) {
                    if (now - game.net.connect_started_ms
                        >= NET_CONNECT_TIMEOUT_MS) {
                        net_disconnect(&game,
                            "Delai de connexion depasse. Verifiez l'IP/UDP 7777 puis RECONNECTER.");
                    } else if (now - game.net.last_keepalive_ms
                               >= NET_KEEPALIVE_MS) {
                        net_send(&game, "HELLO client");
                        game.net.last_keepalive_ms = now;
                    }
                }
            }
        }
        ai_update(&game);
        {
            int scene = game.state == ST_BATTLE ? 1
                      : game.state == ST_GAMEOVER ? 2 : 0;
            if (scene != audio_scene) {
                play_music_for_state(&game, game.state);
                if (scene == 2)
                    play_sound(&game, game.winner == game.my_index
                               ? SFX_VICTORY : SFX_GAME_OVER);
                audio_scene = scene;
            }
        }
        anti_humil_restore(&game);
        anim_update(&game);

        SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
        SDL_RenderClear(ren);

        if (game.state == ST_PICK || game.state == ST_DRAFT
                || game.state == ST_STUFF || game.state == ST_ELEMENTS) {
            draw_ui_panel(ren, (SDL_Rect){4, 4, WIN_W - 8, WIN_H - 8});
            draw_ui_banner(ren, (SDL_Rect){10, 6, WIN_W - 20, 44});
        }
        switch (game.state) {
        case ST_MENU:     draw_menu(ren, font, &game); break;
        case ST_PIN:      draw_pin(ren, font, &game); break;
        case ST_PICK:     draw_pick(ren, font, &game); break;
        case ST_DRAFT:    draw_draft(ren, font, &game); break;
        case ST_STUFF:    draw_stuff(ren, font, &game); break;
        case ST_ELEMENTS: draw_elements(ren, font, &game); break;
        case ST_DOFUS:    draw_dofus(ren, font, &game); break;
        case ST_ACHIEVEMENTS: draw_achievements(ren, font, &game); break;
        case ST_CHALLENGES: draw_challenges(ren, font, &game); break;
        case ST_DEFS:      draw_defs(ren, font, &game); break;
        case ST_SONGE:    draw_songe(ren, font, &game); break;
        case ST_BATTLE: {
            int a, b, fighter, mx, my;
            int hover_i = -1, hover_j = -1;
            int order[MAX_FIGHTERS], count = 2 * game.team_size;
            /* survol (coordonnees logiques) avant le rendu             */
            mouse_logical(&mx, &my);
            iso_pick(&game, mx, my, &hover_i, &hover_j);
            /* tuiles en ordre de diagonales (algorithme du peintre)   */
            for (b = 0; b < game.map_width + game.map_height - 1; b++)
                for (a = 0; a < game.map_width; a++) {
                    int j = b - a;
                    if (j < 0 || j >= game.map_height) continue;
                    draw_tile(ren, &game, a, j, game.cell[j][a]);
                }
            /* zones de deplacement (vert) et de sort (bleu)           */
            draw_move_zone(ren, &game, hover_i, hover_j);
            draw_spell_zone(ren, &game, hover_i, hover_j);
            /* cibles d'attaque possibles (rouge)                      */
            draw_attack_targets(ren, &game);
            /* brume legere d'ambiance                                 */
            SDL_SetRenderDrawColor(ren, 150, 165, 190, 28);
            SDL_RenderFillRect(ren, &(SDL_Rect){0, BAND_H, WIN_W,
                                               WIN_H - BAND_H});
            /* combattants ET invocations tries par profondeur         */
            {
                int depth[MAX_FIGHTERS + MAX_SUMMONS];
                int kind[MAX_FIGHTERS + MAX_SUMMONS];
                int ref[MAX_FIGHTERS + MAX_SUMMONS];
                int nent = 0, si;
                for (fighter = 0; fighter < count; fighter++) {
                    depth[nent] = game.f[fighter].x + game.f[fighter].y;
                    kind[nent] = 0;
                    ref[nent] = fighter;
                    nent++;
                }
                for (si = 0; si < MAX_SUMMONS; si++) {
                    if (!game.summons[si].active) continue;
                    depth[nent] = game.summons[si].x + game.summons[si].y;
                    kind[nent] = 1;
                    ref[nent] = si;
                    nent++;
                }
                for (a = 1; a < nent; a++) {
                    int dd = depth[a], kk = kind[a], rr = ref[a];
                    b = a;
                    while (b > 0 && depth[b - 1] > dd) {
                        depth[b] = depth[b - 1];
                        kind[b] = kind[b - 1];
                        ref[b] = ref[b - 1];
                        b--;
                    }
                    depth[b] = dd;
                    kind[b] = kk;
                    ref[b] = rr;
                }
                for (a = 0; a < nent; a++) {
                    if (kind[a] == 0)
                        draw_fighter(ren, &game, ref[a], font);
                    else
                        draw_summon(ren, &game, ref[a]);
                }
            }
            draw_hover_mark(ren, &game, hover_i, hover_j);
            draw_battle_tooltip(ren, font, &game, hover_i, hover_j, mx, my);
            draw_vfx(&game, ren);
            /* vignette douce pour la profondeur                             */
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 90);
            SDL_RenderFillRect(ren, &(SDL_Rect){0, BAND_H, 40, WIN_H - BAND_H});
            SDL_RenderFillRect(ren, &(SDL_Rect){WIN_W - 40, BAND_H, 40,
                                               WIN_H - BAND_H});
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 60);
            SDL_RenderFillRect(ren, &(SDL_Rect){0, WIN_H - 24, WIN_W, 24});
            draw_skillbar(ren, &game, font);
            if (game.in_game_menu_open)
                draw_ingame_menu(&game, ren, font);
            else
                draw_emote_menu(&game, ren, font);
            break;
        }
        case ST_GAMEOVER: {
            SDL_Color w = {255,255,255,255};
            char msg[96];
            SDL_Rect banner = {20, WIN_H/2 - 55, WIN_W - 40,
                               (game.solo_mode || game.songe_mode) ? 86 : 58};
            SDL_SetRenderDrawColor(ren, 35, 28, 24, 235);
            SDL_RenderFillRect(ren, &banner);
            draw_ui_panel(ren, banner);
            SDL_SetRenderDrawColor(ren, 205, 170, 105, 255);
            SDL_RenderDrawRect(ren, &banner);
            if (game.songe_mode) {
                /* fin d'expedition : on Announces le niveau atteint        */
                snprintf(msg, sizeof msg,
                         "Songe termine au niveau %d ! (record %d)",
                         game.songe_level, game.songe_best);
                draw_text(ren, font, msg, WIN_W/2 - 210, banner.y + 16, w);
                draw_text(ren, font,
                          "Les monstres festoient. Vous, vous reflectissez.",
                          WIN_W/2 - 215, banner.y + 50,
                          (SDL_Color){220,210,185,255});
                break;
            }
            if (game.solo_mode)
                snprintf(msg, sizeof msg, "%s ! (clic pour rentrer)",
                         game.winner == game.my_index ? "Vous gagnez"
                                                      : "L'IA gagne (elle s'entraine la nuit, elle)");
            else
                snprintf(msg, sizeof msg, "%s gagne ! (clic pour rentrer)",
                         game.winner == game.my_index
                         ? "Votre equipe (la proverbial)" : "L'equipe adverse (et elle le sabe)");
            draw_text(ren, font, msg, WIN_W/2 - 150, banner.y + 20, w);
            if (game.solo_mode)
                draw_text(ren, font,
                          "Astuce : le vert se deplace, le bleu vise, le rouge fait mal.",
                          WIN_W/2 - 190, banner.y + 54,
                          (SDL_Color){220,210,185,255});
            break;
        }
        }

        draw_audio_subtitle(ren, font, &game);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (game.net.sock != SOCK_INVALID)
        net_close(game.net.sock);
    if (g_ui_atlas) SDL_DestroyTexture(g_ui_atlas);
    if (g_tilesheet) SDL_DestroyTexture(g_tilesheet);
    if (g_characters_sheet) SDL_DestroyTexture(g_characters_sheet);
    if (g_spell_icons) SDL_DestroyTexture(g_spell_icons);
    if (g_emote_sheet) SDL_DestroyTexture(g_emote_sheet);
    for (i = 0; i < VFX_FRAME_COUNT; i++)
        if (g_vfx_frames[i]) SDL_DestroyTexture(g_vfx_frames[i]);
    if (g_audio_ready) {
        Mix_HaltMusic();
        Mix_HaltChannel(-1);
        if (g_menu_music) Mix_FreeMusic(g_menu_music);
        for (i = 0; i < 4; i++)
            if (g_battle_music[i]) Mix_FreeMusic(g_battle_music[i]);
        for (i = 0; i < SFX_COUNT; i++)
            if (g_sounds[i]) Mix_FreeChunk(g_sounds[i]);
        Mix_CloseAudio();
    }
    Mix_Quit();
    if (font) TTF_CloseFont(font);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    TTF_Quit(); IMG_Quit();
#ifdef _WIN32
    WSACleanup();
#endif
    SDL_Quit();
    return 0;
}