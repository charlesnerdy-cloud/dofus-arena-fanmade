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
#define WIN_W  (GRID_W * TILE)
#define WIN_H  (GRID_H * TILE + 160)   /* bandeau UI en bas            */
#define PORT   7777
#define NET_KEEPALIVE_MS 1000
#define NET_TIMEOUT_MS   8000
#define NET_CONNECT_TIMEOUT_MS 10000
#define N_CLASSES 13
#define N_PLAYABLE 13                   /* toutes les classes jouables  */
#define N_CLASS_SPELLS 16
#define N_EQUIPPED  4
#define N_PANOPLIES 8
#define MAX_TEAM_SIZE 5
#define MAX_FIGHTERS (MAX_TEAM_SIZE * 2)
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
typedef enum { SK_ATK, SK_HEAL, SK_BUFF, SK_SPECIAL } SkillKind;
typedef enum { ELEM_AIR, ELEM_FIRE, ELEM_EARTH, ELEM_WATER } Element;

typedef struct {
    const char *name;
    SkillKind   kind;
    int         pa;        /* coût en points d'action              */
    int         range;     /* portée en cases (manhattan)          */
    int         power;     /* dégâts / soin / % boost             */
    int         cooldown;  /* tours de rechargement                */
} Skill;

typedef struct {
    const char *name;
    Skill       spells[N_CLASS_SPELLS];
} ClassDef;

/* Les 13 classes de Dofus, 16 sorts chacun (grimoire fanmade).        */
static ClassDef CLASSES[N_CLASSES] = {
    {"Iop Cogneur", {
        {"Poing Fracas",       SK_ATK,     3, 1, 12, 0},
        {"Taillade Fun",       SK_ATK,     3, 1, 15, 1},
        {"Epée Géante",        SK_ATK,     4, 4, 18, 0},
        {"Epée Olympe",        SK_ATK,     5, 3, 24, 3},
        {"Courroux Turbo",     SK_ATK,     5, 2, 30, 3},
        {"Tronçonneuse",       SK_ATK,     5, 4, 26, 3},
        {"Look Sadique",       SK_ATK,     4, 3, 16, 2},
        {"Petit Rognon",       SK_ATK,     4, 1, 19, 1},
        {"Petit Saut",         SK_SPECIAL, 2, 3, 0,  2},
        {"Vitamine C",         SK_SPECIAL, 3, 2, 6,  2},
        {"Pansement",          SK_HEAL,   3, 0, 10, 3},
        {"Grand Repas",        SK_HEAL,   4, 0, 14, 3},
        {"Café Serré",         SK_BUFF,   2, 0, 20, 4},
        {"Trop de Café",       SK_BUFF,   2, 0, 15, 3},
        {"Giga Tonnerre",      SK_SPECIAL, 4, 5, 14, 3},
        {"Courant d'Air",      SK_SPECIAL, 3, 4, 9,  2},
    }},
    {"Cra Fléchard", {
        {"Flèche Géniale",     SK_ATK,     3, 8, 14, 0},
        {"Flèche BBQ",         SK_ATK,     4, 7, 18, 1},
        {"Flèche Glaçon",      SK_ATK,     3, 7, 11, 0},
        {"Flèche Vampire",     SK_ATK,     4, 6, 16, 1},
        {"Tir Énorme",         SK_ATK,     5, 7, 26, 3},
        {"Flèche Réveil",      SK_ATK,     4, 8, 20, 2},
        {"Gros Bourdon",       SK_ATK,     4, 6, 20, 2},
        {"Flèche TNT",         SK_ATK,     5, 7, 24, 3},
        {"Flèche Butoir",      SK_SPECIAL, 3, 4, 8,  2},
        {"Regard Laser",       SK_SPECIAL, 3, 6, 0,  4},
        {"Flèche Croque",      SK_SPECIAL, 3, 6, 9,  2},
        {"Colle Forte",        SK_SPECIAL, 4, 7, 0,  4},
        {"Mode Boss",          SK_BUFF,   2, 0, 18, 4},
        {"Cours d'Archer",     SK_BUFF,   3, 0, 22, 5},
        {"Télécommande",       SK_BUFF,   2, 0, 14, 3},
        {"Recette Fuite",      SK_SPECIAL, 2, 3, 0,  2},
    }},
    {"Ecaflip Casino", {
        {"Lance Pièce",        SK_ATK,     3, 3, 16, 0},
        {"Pari Gagnant",       SK_ATK,     4, 5, 22, 2},
        {"Rekop Serré",        SK_ATK,     5, 1, 32, 4},
        {"Griffe Chatte",      SK_ATK,     3, 1, 14, 0},
        {"Fausse Barbe",       SK_ATK,     3, 4, 17, 1},
        {"Roue Bazar",         SK_ATK,     4, 3, 21, 2},
        {"Neuf Vies",          SK_ATK,     4, 2, 18, 1},
        {"Griffe Ouragan",     SK_ATK,     5, 3, 26, 3},
        {"Bond Chaton",        SK_SPECIAL, 2, 4, 0,  2},
        {"Jackpot",            SK_SPECIAL, 2, 0, 25, 5},
        {"Pied Ninja",         SK_SPECIAL, 3, 4, 8,  2},
        {"Cagnotte",           SK_SPECIAL, 4, 5, 12, 3},
        {"Chat Chanceux",      SK_BUFF,   2, 0, 18, 4},
        {"Triple Trèfle",      SK_BUFF,   3, 0, 22, 5},
        {"Nez de Chien",       SK_BUFF,   2, 0, 14, 3},
        {"6ème Sens",          SK_BUFF,   3, 0, 25, 5},
    }},
    {"Eniripsa Blouse", {
        {"Mot Pansement",      SK_HEAL,   3, 5, 20, 0},
        {"Mot Câlin",          SK_HEAL,   4, 6, 28, 2},
        {"Mot Soupe",          SK_HEAL,   4, 0, 12, 3},
        {"Mot BFF",            SK_HEAL,   3, 7, 16, 2},
        {"Mot Héros",          SK_HEAL,   5, 0, 34, 4},
        {"Mot Gilet Jaune",    SK_SPECIAL, 3, 0, 15, 3},
        {"Mot Vulgaire",       SK_ATK,    4, 4, 15, 0},
        {"Mot Sucette",        SK_ATK,    4, 5, 13, 1},
        {"Mot Grizzli",        SK_ATK,    5, 3, 25, 3},
        {"Mot Boo",            SK_SPECIAL, 2, 4, 8,  2},
        {"Mot Frein",          SK_SPECIAL, 4, 5, 0,  4},
        {"Mot Chut",           SK_SPECIAL, 3, 6, 0,  3},
        {"Mot Courage",        SK_BUFF,   2, 0, 15, 3},
        {"Mot Éclair",         SK_BUFF,   3, 0, 20, 4},
        {"Mot Kiwi",           SK_SPECIAL, 3, 0, 12, 3},
        {"Mot Bisou",          SK_HEAL,   4, 3, 22, 3},
    }},
    {"Enutrof Magot", {
        {"Lancer Chrome",      SK_ATK,     3, 6, 13, 0},
        {"Pelle Polterge",     SK_ATK,     4, 3, 17, 0},
        {"Pelleteuse XL",      SK_ATK,     5, 2, 28, 3},
        {"Pelle Arbitre",      SK_ATK,     4, 4, 20, 2},
        {"Doigt Trapu",        SK_SPECIAL, 3, 5, 9,  2},
        {"Sac TrucMuch",       SK_SPECIAL, 3, 2, 10, 3},
        {"Age de Bronze",      SK_SPECIAL, 4, 4, 12, 3},
        {"Sous Verre",         SK_SPECIAL, 2, 5, 0,  5},
        {"Pelle Roulo",        SK_ATK,     5, 1, 24, 2},
        {"Turbo Bateau",       SK_SPECIAL, 3, 3, 0,  3},
        {"Jet Karcher",        SK_ATK,     4, 5, 19, 2},
        {"Pelle Cachée",       SK_ATK,     3, 4, 15, 0},
        {"Bon Karma",          SK_BUFF,   2, 0, 18, 4},
        {"Or en Barre",        SK_BUFF,   3, 0, 20, 4},
        {"Main Rouillée",      SK_SPECIAL, 3, 3, 0,  3},
        {"Coup de Pot",        SK_SPECIAL, 4, 0, 14, 4},
    }},
    {"Sram Fourbe", {
        {"Coup Fantôme",       SK_ATK,     4, 1, 22, 0},
        {"Sale Astuce",        SK_ATK,     3, 1, 14, 0},
        {"Piège Géant",        SK_ATK,     4, 4, 19, 2},
        {"Piège Sardine",      SK_ATK,     5, 5, 24, 3},
        {"Faux Coup",          SK_ATK,     3, 2, 16, 1},
        {"Frappe Ratou",       SK_ATK,     4, 3, 20, 2},
        {"Lame Traître",       SK_ATK,     5, 2, 27, 3},
        {"Poignard Star",      SK_ATK,     4, 1, 21, 2},
        {"Bouh Spécial",       SK_SPECIAL, 2, 3, 8,  2},
        {"Caméléon",           SK_SPECIAL, 3, 0, 0,  5},
        {"Carton Double",      SK_SPECIAL, 4, 0, 0,  6},
        {"Piège Vespa",        SK_SPECIAL, 3, 4, 11, 3},
        {"Mode Vilain",        SK_BUFF,   2, 0, 20, 4},
        {"Refus Sauvage",      SK_BUFF,   3, 0, 18, 4},
        {"Piège Zip",          SK_SPECIAL, 4, 6, 0,  4},
        {"Zoom Digital",       SK_BUFF,   2, 0, 16, 3},
    }},
    {"Sacrieur Maso", {
        {"Coup de Genoux",     SK_ATK,     3, 1, 15, 0},
        {"Roulade Rageuse",    SK_ATK,     4, 1, 20, 1},
        {"Taloches XL",        SK_ATK,     5, 1, 28, 2},
        {"Frappe Turbiñe",     SK_ATK,     4, 2, 21, 2},
        {"Morsure Oufti",      SK_ATK,     3, 1, 16, 1},
        {"Tonnerre Colère",    SK_ATK,     5, 3, 26, 3},
        {"Fou Furieux",        SK_ATK,     5, 1, 30, 4},
        {"Coup Piquant",       SK_ATK,     4, 3, 19, 2},
        {"Adrénaline",         SK_BUFF,   2, 0, 25, 5},
        {"Contre Plaque",      SK_BUFF,   2, 0, 22, 5},
        {"Aimant Kédoz",       SK_SPECIAL, 2, 6, 0,  2},
        {"Coucou Swap",        SK_SPECIAL, 3, 5, 0,  3},
        {"Râle de Soin",       SK_HEAL,   4, 0, 15, 4},
        {"Recharge Vie",       SK_HEAL,   5, 0, 30, 5},
        {"Non Contact",        SK_SPECIAL, 2, 3, 0,  2},
        {"Roquettes Air",      SK_BUFF,   3, 0, 28, 5},
    }},
    {"Sadida Ronceux", {
        {"Ronce CureDent",     SK_ATK,     3, 5, 16, 0},
        {"Ronce Brutale",      SK_ATK,     4, 5, 21, 2},
        {"Ronce Vanneuse",     SK_ATK,     5, 4, 27, 3},
        {"Ronce Gentle",       SK_ATK,     4, 4, 18, 1},
        {"BBQ Brousse",        SK_ATK,     4, 3, 19, 2},
        {"Bouquet Guérit",     SK_HEAL,   4, 3, 18, 3},
        {"Herbe Rigolote",     SK_SPECIAL, 2, 4, 8,  2},
        {"Envahisseur",        SK_BUFF,   3, 0, 15, 4},
        {"Arbre Ami",          SK_SPECIAL, 3, 0, 12, 4},
        {"Vent Pollen",        SK_SPECIAL, 3, 3, 6,  2},
        {"Feu de Greg",        SK_ATK,     5, 5, 25, 3},
        {"Ronce Bulldozer",    SK_ATK,     5, 3, 29, 4},
        {"Trempe Plot",        SK_SPECIAL, 4, 5, 13, 3},
        {"Pousse-Vous",        SK_SPECIAL, 3, 3, 8,  2},
        {"Ronce Glourbo",      SK_ATK,     3, 3, 17, 1},
        {"Sève Revive",        SK_HEAL,   3, 0, 12, 2},
    }},
    {"Osamodas Zoo", {
        {"Patou Patte",        SK_ATK,     3, 4, 15, 0},
        {"Bwork Boxing",       SK_ATK,     5, 3, 27, 3},
        {"Kebab Volant",       SK_ATK,     2, 5,  8, 0},
        {"Fouet Crème",        SK_ATK,     4, 2, 18, 1},
        {"Patte Fantôme",      SK_ATK,     4, 4, 20, 2},
        {"Beee!",              SK_ATK,     3, 3, 15, 0},
        {"Corbeau Rock",       SK_ATK,     5, 6, 26, 3},
        {"Crocs Crocro",       SK_ATK,     4, 1, 22, 2},
        {"Bien-être Zoo",      SK_BUFF,   3, 0, 20, 4},
        {"Piqûre Café",        SK_BUFF,   2, 0, 15, 3},
        {"Grrr Précieux",      SK_SPECIAL, 2, 0, 10, 3},
        {"Mouette Ruse",       SK_SPECIAL, 3, 4, 9,  2},
        {"Lâche Tofu !",       SK_SPECIAL, 3, 2, 0,  3},
        {"Coque Dure",         SK_SPECIAL, 4, 0, 16, 4},
        {"Imperméable",        SK_BUFF,   3, 0, 22, 5},
        {"Sanglier Fête",      SK_ATK,     5, 4, 24, 3},
    }},
    {"Xelor Retard", {
        {"Aiguille Heure",     SK_ATK,     3, 4, 14, 0},
        {"Frigo Portatif",     SK_ATK,     4, 4, 17, 1},
        {"Toc Tic Boum",       SK_ATK,     5, 3, 25, 3},
        {"Sablier Cassé",      SK_SPECIAL, 3, 3, 0,  4},
        {"Escargot Net",       SK_SPECIAL,  2, 6, 0,  3},
        {"Réveil Musclé",      SK_BUFF,   3, 0, 22, 5},
        {"Parapluvie",         SK_BUFF,    2, 0, 15, 3},
        {"Souris Fuit",        SK_SPECIAL,  2, 3, 0,  2},
        {"Poussière Pouf",     SK_ATK,     4, 5, 19, 2},
        {"Laser Obscur",       SK_ATK,     4, 4, 18, 1},
        {"Aiguille Radar",     SK_ATK,     5, 6, 24, 3},
        {"Frappe Chrono",      SK_ATK,     4, 2, 20, 2},
        {"Porte Secrète",      SK_SPECIAL, 3, 7, 0,  4},
        {"Pas le Moral",       SK_SPECIAL, 3, 5, 0,  3},
        {"Replay Diva",        SK_SPECIAL, 4, 0, 18, 4},
        {"Mini Montre",        SK_SPECIAL, 4, 4, 12, 3},
    }},
    {"Feca Boucliné", {
        {"Baffe Naturelle",    SK_ATK,     3, 4, 13, 0},
        {"Glyphe Colère",      SK_ATK,     5, 3, 25, 3},
        {"Glyphe Friteuse",    SK_ATK,     4, 2, 18, 2},
        {"Petit Plastron",     SK_SPECIAL, 2, 0, 10, 2},
        {"Doudoune Terre",     SK_SPECIAL, 3, 0, 15, 3},
        {"Anti Tout",          SK_SPECIAL, 4, 0, 20, 5},
        {"Poignet Serre",      SK_SPECIAL, 3, 3, 8,  3},
        {"Pause Clope",        SK_SPECIAL, 3, 0, 12, 4},
        {"Nuage Punaises",     SK_ATK,     3, 5, 14, 0},
        {"Pluie Haches",       SK_ATK,     4, 4, 17, 1},
        {"Glyphe Muet",        SK_SPECIAL, 4, 4, 0,  4},
        {"Sac de Sable",       SK_BUFF,   3, 0, 20, 4},
        {"Pare Silex",         SK_SPECIAL, 3, 0, 14, 3},
        {"Bâton Boomerang",    SK_ATK,     4, 1, 19, 2},
        {"Séisme XXL",         SK_ATK,     5, 4, 26, 3},
        {"Bulbig Bulbe",       SK_SPECIAL, 4, 0, 18, 4},
    }},
    {"Pandawa Bourre", {
        {"Lame Imbibée",       SK_ATK,     4, 2, 18, 0},
        {"Haleine Turbo",      SK_ATK,     4, 4, 20, 2},
        {"Rage de Crâne",      SK_ATK,     5, 1, 26, 3},
        {"Café au Lait",       SK_HEAL,    3, 3, 18, 2},
        {"Point Faible",       SK_SPECIAL, 2, 4, 0,  3},
        {"Frisbee Pote",       SK_SPECIAL, 3, 1, 0,  3},
        {"Canon de Poulpe",    SK_BUFF,    2, 0, 18, 4},
        {"Cocktail Folie",     SK_BUFF,    2, 0, 18, 4},
        {"Bond Bambou",        SK_SPECIAL, 2, 3, 0,  2},
        {"Macadam Fracas",     SK_ATK,     4, 3, 21, 2},
        {"Punch Coco",         SK_ATK,     3, 1, 16, 0},
        {"Boum Haleine",       SK_ATK,     5, 3, 25, 3},
        {"Tai Chi Pote",       SK_SPECIAL, 4, 2, 0,  4},
        {"Bambou Boudeur",     SK_ATK,     4, 4, 19, 2},
        {"Grande Gorgée",      SK_SPECIAL, 3, 0, 12, 3},
        {"Pôle Nord XXL",      SK_SPECIAL, 4, 5, 14, 3},
    }},
    {"Roublard Boom", {
        {"Plomb Chocolat",     SK_ATK,     3, 5, 15, 0},
        {"Trépan Surprise",    SK_ATK,     4, 6, 19, 1},
        {"Pièce Montée",       SK_ATK,     4, 6, 24, 2},
        {"Superchute",         SK_ATK,     5, 2, 30, 4},
        {"Lasso Internet",     SK_SPECIAL, 2, 3, 0,  2},
        {"Croche Pater",       SK_SPECIAL, 3, 5, 0,  4},
        {"Classe Mulot",       SK_BUFF,    2, 0, 20, 4},
        {"Ponzi Partout",      SK_SPECIAL, 3, 4, 10, 3},
        {"Fumigène Disco",     SK_SPECIAL, 3, 0, 0,  3},
        {"Poudre Rasta",       SK_ATK,     5, 5, 27, 3},
        {"Lunette Sniper",     SK_ATK,     4, 7, 21, 2},
        {"Colis Suspect",      SK_ATK,     5, 4, 26, 3},
        {"Boum Grillot",       SK_ATK,     4, 3, 22, 2},
        {"Carabine Noël",      SK_ATK,     3, 6, 17, 1},
        {"Sortie Crous",       SK_SPECIAL, 2, 4, 0,  2},
        {"Fiole Mystère",      SK_SPECIAL, 4, 5, 13, 3},
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
};

/* ------------------------------------------------------------------ */
/* état du combattant                                                  */
/* ------------------------------------------------------------------ */
typedef enum { ANIM_IDLE, ANIM_MOVE, ANIM_ATTACK } AnimState;

typedef struct {
    int  cls;
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
/* état global du jeu                                                  */
/* ------------------------------------------------------------------ */
typedef enum { ST_MENU, ST_PICK, ST_DRAFT, ST_STUFF, ST_ELEMENTS,
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
    int       vfx_active;
    int       vfx_x;
    int       vfx_y;
    Uint32    vfx_started;
} Game;

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
static void set_anim(Game *g, int idx, AnimState st);
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
    int first = unit_for_slot(g, g->my_index, 0);
    int slot;
    g->setup_unit = 0;
    if (g->state == ST_PICK) {
        for (slot = 0; slot < g->team_size; slot++)
            if (g->class_chosen[first + slot] < 0) {
                g->setup_unit = slot;
                return;
            }
        g->state = ST_DRAFT;
    }
    if (g->state == ST_DRAFT) {
        for (slot = 0; slot < g->team_size; slot++)
            if (g->draft_ready[first + slot] != 1) {
                g->setup_unit = slot;
                return;
            }
        g->state = ST_STUFF;
    }
    if (g->state == ST_STUFF) {
        for (slot = 0; slot < g->team_size; slot++)
            if (g->stuff_ready[first + slot] != 1) {
                g->setup_unit = slot;
                return;
            }
        g->state = ST_ELEMENTS;
    }
    if (g->state == ST_ELEMENTS) {
        for (slot = 0; slot < g->team_size; slot++)
            if (g->draft_ready[first + slot] != 2) {
                g->setup_unit = slot;
                return;
            }
        check_stuff_start(g);
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
            if (g->state == ST_ELEMENTS || g->state == ST_STUFF)
                start_battle(g, sd);
        } else if (!strcmp(cmd, "CLASS") && sscanf(buf, "%*s %d %d", &who, &a) == 2) {
            if (who >= 0 && who < 2 * g->team_size
                && who / g->team_size != g->my_index
                && a >= 0 && a < N_CLASSES) {
                g->class_chosen[who] = a;
                advance_setup(g);
            }
        } else if (!strcmp(cmd, "SPELLS") &&
                   sscanf(buf, "%*s %d %d %d %d %d", &who, &a, &b, &c, &d) == 5) {
            if (who >= 0 && who < 2 * g->team_size
                && who / g->team_size != g->my_index
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
                && who / g->team_size != g->my_index
                && a >= 0 && a < N_PANOPLIES) {
                g->stuff_chosen[who] = a;
                g->stuff_ready[who] = 1;
                advance_setup(g);
            }
        } else if (!strcmp(cmd, "ELEMENTS")
                   && sscanf(buf, "%*s %d %d %d %d %d",
                             &who, &a, &b, &c, &d) == 5) {
            if (who >= 0 && who < 2 * g->team_size
                && who / g->team_size != g->my_index
                && a >= 0 && b >= 0 && c >= 0 && d >= 0
                && a + b + c + d == ELEMENT_POINTS) {
                g->f[who].elements[ELEM_AIR] = a;
                g->f[who].elements[ELEM_FIRE] = b;
                g->f[who].elements[ELEM_EARTH] = c;
                g->f[who].elements[ELEM_WATER] = d;
                g->draft_ready[who] = 2;
                advance_setup(g);
            }
        } else if (!strcmp(cmd, "MOVE") && sscanf(buf, "%*s %d %d %d", &who, &a, &b) == 3) {
            if (who >= 0 && who < 2 * g->team_size
                && who == g->turn
                && who / g->team_size != g->my_index
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
                && who / g->team_size != g->my_index)
                apply_skill_remote(g, who, a, b, c);
        } else if (!strcmp(cmd, "END") && sscanf(buf, "%*s %d", &who) == 1) {
            if (who == g->turn && who >= 0 && who < 2 * g->team_size
                && who / g->team_size != g->my_index)
                end_turn(g, 1);
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
    f->pano = g->stuff_chosen[idx];
    f->hp_max = MAX_HP;
    if (f->pano >= 0 && f->pano < N_PANOPLIES)
        f->hp_max += PANOPLIES[f->pano].hp_bonus;
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
    return ((GRID_H * TILE) - map_pixel_h(g)) / 2;
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

static int ai_draft_score(const Skill *skill)
{
    int score = skill->power * 4 + skill->range * 3 - skill->pa * 3;
    if (skill->kind == SK_ATK)
        return 100 + score;
    if (skill->kind == SK_SPECIAL && skill->power > 0)
        return 80 + score;
    if (skill->kind == SK_HEAL)
        return 50 + skill->power * 2 + skill->range - skill->pa;
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
    for (enemy = 0; enemy < g->team_size; enemy++) {
        opponent = &g->f[(1 - f->team) * g->team_size + enemy];
        if (opponent->alive && opponent->pano >= 0) {
            ret_pa += PANOPLIES[opponent->pano].ret_pa;
            ret_pm += PANOPLIES[opponent->pano].ret_pm;
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
    } else if (!team_has_living(g, 1)) {
        g->winner = 0;
        g->state = ST_GAMEOVER;
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
    if (g->state != ST_BATTLE || me->team != g->my_index) return;
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
    int range = s->range + (pf ? pf->range_bonus : 0);
    f->pa -= s->pa;
    f->cd_left[idx] = s->cooldown;
    play_sound(g, SFX_CAST);
    start_vfx(g, f->x, f->y);

    switch (s->kind) {
        case SK_ATK:
            set_anim(g, who, ANIM_ATTACK);
            if (manhattan(f->x, f->y, tx, ty) <= range
                && o && o->team != f->team
                && los_clear(g, f->x, f->y, tx, ty)) {
                start_vfx(g, tx, ty);
                play_sound(g, SFX_HIT);
                /* degats deterministes + panoplie (dmg% + puissance)     */
                int dmg = ((s->power * (100 + f->atk_mod)) / 100)
                          * (100 + (pf ? pf->dmg_pct : 0)) / 100
                          + (pf ? pf->puissance : 0);
                o->hp -= dmg;
                /* renvoi de dommages (Prespic) : l'attaquant subit     */
                if (po && po->reflect > 0) {
                    f->hp -= po->reflect;
                    play_sound(g, SFX_HIT);
                    if (f->hp <= 0) {
                        f->hp = 0; f->alive = 0;
                        play_sound(g, SFX_DEATH);
                        check_winner(g);
                    }
                }
                if (o->hp <= 0) {
                    o->hp = 0; o->alive = 0;
                    play_sound(g, SFX_DEATH);
                    check_winner(g);
                }
            }
            break;
        case SK_HEAL:
            set_anim(g, who, ANIM_ATTACK);
            if (o && o->team == f->team
                && (range == 0 || manhattan(f->x, f->y, tx, ty) <= range)) {
                start_vfx(g, tx, ty);
                int heal = (s->power * (100 + (pf ? pf->heal_pct : 0))) / 100;
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
                int dmg = s->power + (pf ? pf->puissance : 0);
                o->hp -= dmg;
                if (po && po->reflect > 0) {
                    f->hp -= po->reflect;
                    play_sound(g, SFX_HIT);
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
    }
}

static void apply_skill_remote(Game *g, int who, int idx, int tx, int ty)
{
    if (who < 0 || who >= 2 * g->team_size
        || g->f[who].team == g->my_index)
        return;
    cast_skill(g, who, idx, tx, ty);
}

static void try_cast(Game *g, int tx, int ty)
{
    Fighter *me = &g->f[g->turn];
    if (me->team != g->my_index || g->state != ST_BATTLE) return;
    if (g->sel_skill < 0) return;
    Skill *s = &me->spells[g->sel_skill];
    Panoplie *pf = (me->pano >= 0 && me->pano < N_PANOPLIES)
                   ? &PANOPLIES[me->pano] : NULL;
    int range = s->range + (pf ? pf->range_bonus : 0);
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
    next_turn(g);
    g->sel_skill = -1;
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
            cast_skill(g, g->turn, heal_spell, ally->x, ally->y);
            return;
        }
    }

    for (spell = 0; spell < N_EQUIPPED; spell++) {
        Skill *skill = &me->spells[spell];
        int range = skill->range + (pano ? pano->range_bonus : 0);
        if ((skill->kind != SK_ATK
             && !(skill->kind == SK_SPECIAL && skill->power > 0))
            || skill->pa > me->pa || me->cd_left[spell] > 0)
            continue;
        for (target = 0; target < 2 * g->team_size; target++) {
            Fighter *enemy = &g->f[target];
            int distance, damage, score;
            if (!enemy->alive || enemy->team == 1) continue;
            distance = manhattan(me->x, me->y, enemy->x, enemy->y);
            if (distance > range
                || !los_clear(g, me->x, me->y, enemy->x, enemy->y))
                continue;
            if (skill->kind == SK_ATK) {
                damage = skill->power * (100 + me->atk_mod) / 100;
                damage = damage * (100 + (pano ? pano->dmg_pct : 0)) / 100
                       + (pano ? pano->puissance : 0);
            } else {
                damage = skill->power + (pano ? pano->puissance : 0);
            }
            if (g->neural_bot)
                score = (int)(neural_attack_score(enemy, skill, damage)
                              * 1000.0f);
            else
                score = damage * 10 + (damage >= enemy->hp ? 1000 : 0)
                      - skill->pa;
            if (score > best_score) {
                best_score = score;
                best_spell = spell;
                best_target = target;
            }
        }
    }
    if (best_spell >= 0) {
        Fighter *enemy = &g->f[best_target];
        cast_skill(g, g->turn, best_spell, enemy->x, enemy->y);
        return;
    }

    if (me->atk_mod < 25) {
        for (spell = 0; spell < N_EQUIPPED; spell++) {
            Skill *skill = &me->spells[spell];
            if (skill->kind == SK_BUFF && skill->pa <= me->pa
                && me->cd_left[spell] == 0) {
                cast_skill(g, g->turn, spell, me->x, me->y);
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
            int distance = manhattan(me->x, me->y, best_x, best_y);
            me->x = best_x;
            me->y = best_y;
            me->pm -= distance;
            set_anim(g, g->turn, ANIM_MOVE);
            play_sound(g, SFX_MOVE);
            return;
        }
    }

    end_turn(g, 0);
}

static void ai_update(Game *g)
{
    Uint32 now;
    if (!g->solo_mode || g->state != ST_BATTLE
        || g->f[g->turn].team != 1)
        return;
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

static void draw_ui_banner(SDL_Renderer *renderer, SDL_Rect destination)
{
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

static void load_visual_effects(SDL_Renderer *renderer)
{
    int i;
    char path[160];
    g_characters_sheet =
        load_asset(renderer, "assets/character/Characters.png");
#ifndef __ANDROID__
    g_spell_icons = load_asset(renderer, "assets/character/spells_icon/spell4.webp");
#endif
    g_emote_sheet = load_asset(renderer, "assets/bubble_emotes.png");
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
    draw_text(renderer, font, "EMOTES - 1 a 8 ou clic", bg.x + 46,
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
        draw_text(renderer, font, "Surrender this match?", panel.x + 78,
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
    draw_text(renderer, font, "GAME MENU", panel.x + 104, panel.y + 16, white);
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
    if (!f) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(f, txt, c);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect d = {x, y, s->w, s->h};
    SDL_RenderCopy(r, t, NULL, &d);
    SDL_FreeSurface(s);
    SDL_DestroyTexture(t);
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
    if (g->state != ST_BATTLE || me->team != g->my_index) return;
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
    if (me->team != g->my_index) return;
    s = &me->spells[g->sel_skill];
    pf = (me->pano >= 0 && me->pano < N_PANOPLIES) ? &PANOPLIES[me->pano]
                                                   : NULL;
    range = s->range + (pf ? pf->range_bonus : 0);
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
        int enemy = g->f[idx].team != g->my_index;
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

/* dessine le combattant : frame courante du spritesheet ou repli
 * (position isometrique : les pieds sur le centre du losange)          */
static void draw_fighter(SDL_Renderer *r, Game *g, int idx, TTF_Font *font)
{
    Fighter *f = &g->f[idx];
    int sx, sy;
    SDL_Rect d;
    if (!f->alive) return;
    iso_center(g, f->x, f->y, &sx, &sy);
    d.x = sx - TILE / 2;
    d.y = sy - TILE + 6;
    d.w = TILE;
    d.h = TILE - 6;

    if (g_characters_sheet) {
        draw_character_sprite(r, CLASS_CHARACTER_ROW[f->cls],
                              character_sequence_for_anim(f->anim),
                              f->anim_frame, d,
                              f->team == g->my_index ? 0
                              : g->colorblind_mode ? 2 : 1);
    } else {
        iso_fill_diamond(r, g, f->x, f->y,
                         (SDL_Color){f->team == g->my_index ? 40 : 200,
                                     f->team == g->my_index ? 120 : 40,
                                     f->team == g->my_index ? 240 : 40,
                                     255});
    }
    /* marqueur du joueur actif : losange autour de sa case            */
    if (g->state == ST_BATTLE && g->turn == idx) {
        SDL_Color mark;
        if (g->colorblind_mode)
            mark = f->team == g->my_index
                 ? (SDL_Color){45, 155, 255, 255}
                 : (SDL_Color){255, 205, 45, 255};
        else
            mark = f->team == g->my_index
                 ? (SDL_Color){40, 255, 80, 255}
                 : (SDL_Color){255, 55, 55, 255};
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
    draw_text(r, font, CLASSES[f->cls].name, sx - 26, sy - TILE - 18, c);
    draw_text(r, font, hp, sx - 10, sy - 20, c);
    if (g->emote_fighter == idx && g->emote_until > SDL_GetTicks())
        draw_emote(g, r);
}

/* rectangles cliquables (partages rendu / clics)                      */
static SDL_Rect menu_btn_host(void)  { SDL_Rect r = {20, 70, 320, 40};  return r; }
static SDL_Rect menu_btn_join(void)  { SDL_Rect r = {20, 120, 320, 40}; return r; }
static SDL_Rect menu_btn_solo(void)  { SDL_Rect r = {350, 170, 200, 40}; return r; }
static SDL_Rect menu_btn_bot_model(void) { SDL_Rect r = {350, 216, 200, 34}; return r; }
static SDL_Rect menu_btn_ipfield(void){SDL_Rect r = {350, 120, 200, 40};return r; }
static SDL_Rect menu_size_btn(int i) { SDL_Rect r = {350 + i*66, 70, 60, 40}; return r; }
static SDL_Rect menu_settings_btn(void) { SDL_Rect r = {550, 70, 112, 40}; return r; }
static SDL_Rect settings_panel(void) { SDL_Rect r = {72, 64, 528, 512}; return r; }
static SDL_Rect settings_row(int row)
{ SDL_Rect r = {96, 144 + row * 52, 480, 42}; return r; }
static SDL_Rect settings_minus_btn(int row)
{ SDL_Rect r = {454, 149 + row * 52, 36, 32}; return r; }
static SDL_Rect settings_plus_btn(int row)
{ SDL_Rect r = {500, 149 + row * 52, 36, 32}; return r; }
static SDL_Rect settings_toggle_btn(int row)
{ SDL_Rect r = {444, 149 + row * 52, 120, 32}; return r; }
static SDL_Rect settings_back_btn(void)
{ SDL_Rect r = {264, 510, 144, 38}; return r; }
static SDL_Rect favorite_btn(int i)  { SDL_Rect r = {20 + i*126, 445, 118, 34}; return r; }
/* clavier virtuel pour l'IP (12 touches : 123456 / 7890.⌫ / OK)        */
static SDL_Rect menu_kbd(int i)     { SDL_Rect r = {20 + (i%6)*55, 180 + (i/6)*45, 50, 40}; return r; }
/* autres rectangles cliquables                                      */
static SDL_Rect pick_class_rect(int i){SDL_Rect r = {15 + (i%3)*220, 55 + (i/3)*30, 212, 28}; return r; }
static SDL_Rect pick_favorite_rect(int i){SDL_Rect r = {20 + (i%3)*220, 340 + (i/3)*34, 210, 30}; return r; }
static SDL_Rect draft_btn(void)     { SDL_Rect r = {20, 470, 260, 34};  return r; }
static SDL_Rect stuff_card(int i)    { SDL_Rect r = {20 + (i%2)*330, 60 + (i/2)*105, 320, 100}; return r; }
static SDL_Rect stuff_favorite_btn(int i){SDL_Rect r = {20 + i*98, 500, 90, 34}; return r;}
static SDL_Rect element_btn(int element, int add) { SDL_Rect r = {460 + add*70, 160 + element*60, 60, 36}; return r; }
static SDL_Rect endturn_btn(int y0)  { SDL_Rect r = {10, y0 + 92, 150, 50}; return r; }
static SDL_Rect chat_btn(int y0)     { SDL_Rect r = {170, y0 + 92, 90, 30}; return r; }
static SDL_Rect chat_field(int y0)   { SDL_Rect r = {270, y0 + 92, 300, 30}; return r; }
static SDL_Rect chat_send(int y0)    { SDL_Rect r = {580, y0 + 92, 80, 30}; return r; }
static SDL_Rect ingame_menu_btn(int y0) { SDL_Rect r = {550, y0 + 3, 112, 22}; return r; }
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
        "Sous-titres si effets muets"
    };
    SDL_Rect panel = settings_panel();
    SDL_Color white = {245, 235, 215, 255};
    SDL_Color muted = {175, 165, 150, 255};
    int i, mx, my;
    mouse_logical(&mx, &my);

    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderFillRect(r, &panel);
    draw_ui_panel(r, panel);
    draw_ui_banner(r, (SDL_Rect){88, 75, 496, 52});
    draw_text(r, font, "PARAMETRES", 270, 91, white);
    for (i = 0; i < 6; i++) {
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
            draw_text(r, font, "672 x 640 (fixe)", 448, row.y + 12, white);
        } else {
            SDL_Rect toggle = settings_toggle_btn(i);
            int enabled = i == 3 ? g->colorblind_mode
                        : i == 4 ? g->reduce_flashing
                                 : g->subtitles_when_muted;
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
        draw_text(r, font, "RETOUR", back.x + 42, back.y + 11, white);
    }
    draw_text(r, font, "ECHAP pour revenir au menu", 236, 558, muted);
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

static void draw_skillbar(SDL_Renderer *r, Game *g, TTF_Font *font)
{
    Fighter *me = &g->f[g->turn];
    int y0 = GRID_H * TILE;
    SDL_Rect panel = {0, y0, WIN_W, 160};
    SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
    SDL_RenderFillRect(r, &panel);
    draw_ui_panel(r, (SDL_Rect){2, y0 + 2, WIN_W - 4, 156});
    char line[128];
    SDL_Color white = {255,255,255,255};
    snprintf(line, sizeof line,
             "Tour %d T%dP%d | PV%d/%d PA%d PM%d INIT%d",
             g->turn_count + 1, me->team + 1,
             g->turn % g->team_size + 1,
             me->hp, me->hp_max, me->pa, me->pm, me->initiative);
    draw_ui_sprite(r, (SDL_Rect){4, 2, 29, 31},
                   (SDL_Rect){8, y0 + 5, 24, 25});
    draw_text(r, font, line, 38, y0 + 6, white);

    int k;
    for (k = 0; k < N_EQUIPPED; k++) {
        Skill *s = &me->spells[k];
        SDL_Rect b = {10 + k * 165, y0 + 30, 160, 56};
        SDL_Rect icon = {b.x + 4, b.y + 10, 30, 36};
        if (g->sel_skill == k)
            SDL_SetRenderDrawColor(r, 112, 82, 38, 255);
        else
            SDL_SetRenderDrawColor(r, 55, 48, 42, 255);
        SDL_RenderFillRect(r, &b);
        draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, b);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &b);
        if (g_spell_icons) {
            SDL_Rect icon_src = spell_icon_rect(me->spell_icon[k]);
            SDL_RenderCopy(r, g_spell_icons, &icon_src, &icon);
        }
        char t[96];
        snprintf(t, sizeof t, "%d %.15s", k+1, s->name);
        draw_text(r, font, t, b.x + 38, b.y + 7, white);
        snprintf(t, sizeof t, "%dPA P%d", s->pa, s->range);
        draw_text(r, font, t, b.x + 38, b.y + 30, white);
        snprintf(t, sizeof t, "effet %d  CD %d/%d",
                 s->power, me->cd_left[k], s->cooldown);
        draw_text(r, font, t, b.x + 38, b.y + 42, white);
    }
    /* bouton FIN DE TOUR (clic)                                      */
    SDL_Rect be = endturn_btn(y0);
    int my_turn = me->team == g->my_index;
    if (my_turn) SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
    else         SDL_SetRenderDrawColor(r, 50, 50, 60, 255);
    SDL_RenderFillRect(r, &be);
    draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, be);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &be);
    draw_text(r, font, my_turn ? "FIN DE TOUR" : "attendez...",
              be.x + 12, be.y + 6, white);
    draw_text(r, font, "clic case = se deplacer / cibler",
              be.x + 12, be.y + 26, white);
    if (me->pano >= 0)
        /* ligne du bas, a droite de l'aide : pas de chevauchement      */
        draw_text(r, font, PANOPLIES[me->pano].name, 340, be.y + 26, white);
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



static void draw_menu(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color grey  = {130,130,130,255};
    SDL_Color hover = {200,200,240,255};
    char line[96];
    int mx, my;
    mouse_logical(&mx, &my);

    if (g->settings_open) {
        draw_settings(r, font, g);
        return;
    }
    SDL_SetRenderDrawColor(r, 20, 17, 18, 255);
    SDL_RenderClear(r);
    draw_ui_panel(r, (SDL_Rect){8, 6, WIN_W - 16, 508});
    draw_ui_banner(r, (SDL_Rect){14, 8, WIN_W - 28, 48});
    {
        draw_ui_sprite(r, (SDL_Rect){4, 2, 29, 31},
                       (SDL_Rect){22, 15, 24, 25});
        draw_text(r, font, "DOFUS ARENA FANMADE", 54, 13, white);
        draw_text(r, font, "Solo contre l'IA ou duel en P2P", 54, 31, grey);
        draw_character_sprite(r, CLASS_CHARACTER_ROW[0], 0, 0,
                              (SDL_Rect){548, 152, 60, 60}, 0);
        draw_character_sprite(r, CLASS_CHARACTER_ROW[1], 0, 0,
                              (SDL_Rect){600, 152, 60, 60}, 1);
    }
    {
        SDL_Rect settings = menu_settings_btn();
        int hovered = pt_in(settings, mx, my);
        SDL_SetRenderDrawColor(r, hovered ? 94 : 64,
                               hovered ? 82 : 55,
                               hovered ? 59 : 43, 255);
        SDL_RenderFillRect(r, &settings);
        draw_ui_sprite(r, (SDL_Rect){252, 31, 20, 18}, settings);
        SDL_SetRenderDrawColor(r, 205, 170, 105, 255);
        SDL_RenderDrawRect(r, &settings);
        draw_text(r, font, "OPTIONS", settings.x + 24,
                  settings.y + 12, white);
    }
    draw_text(r, font, "Format :", 350, 48, grey);
    {
        static const int sizes[3] = {1, 2, 5};
        int i;
        for (i = 0; i < 3; i++) {
            SDL_Rect b = menu_size_btn(i);
            int selected = g->team_size_choice == sizes[i];
            SDL_SetRenderDrawColor(r, selected ? 70 : 40,
                                   selected ? 115 : 50,
                                   selected ? 75 : 65, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
            SDL_RenderDrawRect(r, &b);
            snprintf(line, sizeof line, "%dv%d", sizes[i], sizes[i]);
            draw_text(r, font, line, b.x + 12, b.y + 12, white);
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
    SDL_SetRenderDrawColor(r, can_connect ? (in_h ? 90 : 70) : 45,
                           can_connect ? (in_h ? 90 : 70) : 45,
                           can_connect ? (in_h ? 130 : 110) : 55, 255);
    SDL_RenderFillRect(r, &bh);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &bh);
    draw_text(r, font, host_label, bh.x + 10, bh.y + 12,
              can_connect && in_h ? hover : white);

    /* bouton REJOINDRE + champ IP */
    SDL_Rect bj = menu_btn_join();
    int in_j = pt_in(bj, mx, my);
    SDL_SetRenderDrawColor(r, can_connect ? (in_j ? 90 : 70) : 45,
                           can_connect ? (in_j ? 90 : 70) : 45,
                           can_connect ? (in_j ? 130 : 110) : 55, 255);
    SDL_RenderFillRect(r, &bj);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &bj);
    draw_text(r, font, "REJOINDRE UNE PARTIE", bj.x + 10, bj.y + 12,
              can_connect && in_j ? hover : white);

    SDL_Rect bi = menu_btn_ipfield();
    SDL_SetRenderDrawColor(r, 25, 25, 25, 255);
    SDL_RenderFillRect(r, &bi);
    if (g->ip_editing)
        SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
    else
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &bi);
    snprintf(line, sizeof line, "%s%s", g->ip_text,
              g->ip_editing ? "_" : "");
    draw_text(r, font, line[0] ? line : "IP IPv4 de l'hote", bi.x + 8, bi.y + 12,
              g->ip_editing ? (SDL_Color){255,220,80,255} : grey);
    draw_text(r, font, "UDP : 7777", 560, 132, white);

    {
        SDL_Rect solo = menu_btn_solo();
        int hovered = pt_in(solo, mx, my);
        int enabled = g->net.sock == SOCK_INVALID;
        SDL_SetRenderDrawColor(r, enabled ? (hovered ? 80 : 60) : 42,
                               enabled ? (hovered ? 125 : 95) : 42,
                               enabled ? (hovered ? 75 : 55) : 42, 255);
        SDL_RenderFillRect(r, &solo);
        SDL_SetRenderDrawColor(r, 225, 215, 175, 255);
        SDL_RenderDrawRect(r, &solo);
        draw_text(r, font, "JOUER SEUL (IA)", solo.x + 22,
                  solo.y + 12, enabled ? white : grey);
    }
    {
        SDL_Rect model = menu_btn_bot_model();
        int hovered = pt_in(model, mx, my);
        SDL_SetRenderDrawColor(r, hovered ? 75 : 52, hovered ? 78 : 55,
                               hovered ? 112 : 78, 255);
        SDL_RenderFillRect(r, &model);
        SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
        SDL_RenderDrawRect(r, &model);
        draw_text(r, font, g->neural_bot ? "BOT : NEURONAL"
                                         : "BOT : CLASSIQUE",
                  model.x + 8, model.y + 9, white);
    }

    if (g->menu_status[0])
        draw_text(r, font, g->menu_status, 20, 280, white);

    draw_text(r, font, "LAN : saisissez l'IP locale de l'hote.", 20, 320, grey);
    draw_text(r, font, "Internet : IP publique + redirection UDP 7777 sur la box",
              20, 344, grey);
    draw_text(r, font, "et autorisation du jeu dans le pare-feu de l'hote.",
              20, 368, grey);
    draw_text(r, font, "L'hote communique son IP publique au joueur distant.",
              20, 392, grey);
    draw_text(r, font, "FAVORIS (classe + panoplie) :", 20, 422, white);
    {
        int i;
        for (i = 0; i < MAX_FAVORITES; i++) {
            SDL_Rect b = favorite_btn(i);
            int selected = g->selected_favorite == i;
            SDL_SetRenderDrawColor(r, selected ? 75 : 45,
                                   selected ? 105 : 55, 75, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
            SDL_RenderDrawRect(r, &b);
            if (g->favorite_class[i] >= 0) {
                snprintf(line, sizeof line, "F%d %s + P%d", i + 1,
                         CLASSES[g->favorite_class[i]].name,
                         g->favorite_pano[i] + 1);
            } else {
                snprintf(line, sizeof line, "F%d - vide -", i + 1);
            }
            draw_text(r, font, line, b.x + 4, b.y + 8, white);
        }
    }

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

static void draw_pick(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color grey  = {130,130,130,255};
    char line[96];
    int unit = unit_for_slot(g, g->my_index, g->setup_unit);
    snprintf(line, sizeof line, "T%d - PERSONNAGE %d : choisissez la classe",
             g->my_index + 1, g->setup_unit + 1);
    draw_text(r, font, line, 20, 20, white);
    if (g->challenge_seed) {
        snprintf(line, sizeof line, "Defi %u - bot neuronal",
                 g->challenge_seed);
        draw_text(r, font, line, 330, 20, grey);
    } else if (g->solo_mode) {
        draw_text(r, font, "Votre adversaire IA prepare son equipe.",
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
            }
        }
    }
}

/* ecran de draft : 16 sorts de la classe, en choisir 4                 */
static void draw_draft(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color green = {80,255,120,255};
    char line[128];
    int unit = unit_for_slot(g, g->my_index, g->setup_unit);
    int cls = g->class_chosen[unit];
    int i;
    if (cls < 0 || cls >= N_CLASSES) { g->state = ST_PICK; return; }

    snprintf(line, sizeof line, "PERSO %d - DRAFT %s : choisissez 4 sorts (%d/4)",
             g->setup_unit + 1, CLASSES[cls].name, g->draft_count[unit]);
    draw_text(r, font, line, 20, 20, white);
    for (i = 0; i < N_CLASS_SPELLS; i++) {
        Skill *s = &CLASSES[cls].spells[i];
        int col = i % 4, row = i / 4;
        SDL_Rect b = {10 + col * 175, 60 + row * 100, 170, 96};
        SDL_Rect icon = {b.x + 5, b.y + 32, 42, 48};
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
        draw_text(r, font, line, b.x + 4, b.y + 4, white);
        const char *kind = s->kind == SK_ATK ? "attaque" :
                           s->kind == SK_HEAL ? "soin"   :
                           s->kind == SK_BUFF ? "buff"   : "special";
        snprintf(line, sizeof line, "%s %d, %dPA, P%d, CD%d",
                 kind, s->power, s->pa, s->range, s->cooldown);
        draw_text(r, font, line, b.x + 52, b.y + 30, white);
        snprintf(line, sizeof line, "%s",
                 g->draft[unit][i] ? "dans le deck" : "clic");
        draw_text(r, font, line, b.x + 52, b.y + 60, white);
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
                draw_text(r, font, line, 300, 505 + slot*20, green);
            }
    }
}

/* ecran de choix de panoplie (apres le draft, avant le combat)          */
static void draw_stuff(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color green = {80,255,120,255};
    char line[160];
    int i;
    int unit = unit_for_slot(g, g->my_index, g->setup_unit);
    snprintf(line, sizeof line, "PERSO %d - choisissez votre panoplie",
             g->setup_unit + 1);
    draw_text(r, font, line, 20, 20, white);
    for (i = 0; i < N_PANOPLIES; i++) {
        Panoplie *p = &PANOPLIES[i];
        SDL_Rect b = stuff_card(i);
        if (g->stuff_chosen[unit] == i)
            SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
        else
            SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &b);
        snprintf(line, sizeof line, "%d %s", i+1, p->name);
        draw_text(r, font, line, b.x + 6, b.y + 4, white);
        /* resume des bonus */
        snprintf(line, sizeof line, "PV+%d DMG+%d%% SOIN+%d%% PUI+%d PORTE+%d",
                 p->hp_bonus, p->dmg_pct, p->heal_pct, p->puissance, p->range_bonus);
        draw_text(r, font, line, b.x + 6, b.y + 24, white);
        snprintf(line, sizeof line, "PA+%d PM+%d RETPA-%d RETPM-%d RENVOI-%d",
                 p->pa_bonus, p->pm_bonus, p->ret_pa, p->ret_pm, p->reflect);
        draw_text(r, font, line, b.x + 6, b.y + 44, white);
        snprintf(line, sizeof line, "(%s)", p->desc);
        draw_text(r, font, line, b.x + 6, b.y + 64, white);
    }
    if (g->stuff_chosen[unit] >= 0) {
        snprintf(line, sizeof line, "Votre panoplie : %s",
                 PANOPLIES[g->stuff_chosen[unit]].name);
        draw_text(r, font, line, 20, 480, green);
    }
    if (g->menu_status[0])
        draw_text(r, font, g->menu_status, 300, 480, green);
    {
        int i;
        for (i = 0; i < MAX_FAVORITES; i++) {
            SDL_Rect b = stuff_favorite_btn(i);
            SDL_SetRenderDrawColor(r, 55, 75, 65, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
            SDL_RenderDrawRect(r, &b);
            snprintf(line, sizeof line, "SAUVER F%d", i + 1);
            draw_text(r, font, line, b.x + 5, b.y + 10, white);
        }
        {
            SDL_Rect b = {520, 500, 130, 34};
            SDL_SetRenderDrawColor(r, 60, 140, 70, 255);
            SDL_RenderFillRect(r, &b);
            SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
            SDL_RenderDrawRect(r, &b);
            draw_text(r, font, "CONTINUER", b.x + 8, b.y + 8, white);
        }
    }
}

static void draw_elements(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    static const char *names[ELEMENT_COUNT] = {"AIR", "FEU", "TERRE", "EAU"};
    Fighter *f = &g->f[unit_for_slot(g, g->my_index, g->setup_unit)];
    int i, total = 0;
    char line[128];
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color grey = {155, 155, 155, 255};
    for (i = 0; i < ELEMENT_COUNT; i++) total += f->elements[i];
    snprintf(line, sizeof line, "PERSO %d - repartissez %d points elementaires",
             g->setup_unit + 1, ELEMENT_POINTS);
    draw_text(r, font, line, 20, 20, white);
    draw_text(r, font, "Initiative = Air (un malus Air reduit temporairement l'initiative).",
              20, 50, grey);
    draw_text(r, font, "Les boutons modifient la repartition par pas de 5 points.",
              20, 75, grey);
    draw_text(r, font, "Elements : Air (Agilite), Feu (Intelligence), Terre (Force), Eau (Chance).",
              20, 100, grey);
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
    draw_text(r, font, line, 100, 410, white);
    {
        SDL_Rect done = {100, 460, 250, 42};
        SDL_SetRenderDrawColor(r, total == ELEMENT_POINTS ? 55 : 45,
                               total == ELEMENT_POINTS ? 140 : 55,
                               total == ELEMENT_POINTS ? 65 : 55, 255);
        SDL_RenderFillRect(r, &done);
        SDL_SetRenderDrawColor(r, 220, 220, 220, 255);
        SDL_RenderDrawRect(r, &done);
        draw_text(r, font, "VALIDER PERSONNAGE", done.x + 14, done.y + 12, white);
    }
}

static void draft_click(Game *g, int mx, int my)
{
    int i;
    int unit = unit_for_slot(g, g->my_index, g->setup_unit);
    int cls = g->class_chosen[unit];
    if (cls < 0) return;
    for (i = 0; i < N_CLASS_SPELLS; i++) {
        int col = i % 4, row = i / 4;
        SDL_Rect b = {10 + col * 175, 60 + row * 100, 170, 96};
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

    win = SDL_CreateWindow("Dofus Arena P2P - Fanmade",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WIN_W, WIN_H, SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "Fenetre SDL indisponible: %s\n", SDL_GetError());
        return 1;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
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
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    g_ren = ren;
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

    if (argc >= 2 && !strcmp(argv[1], "host")) {
        if (argc >= 3) {
            int size = atoi(argv[2]);
            if (size == 1 || size == 2 || size == 5)
                game.team_size_choice = size;
        }
        game.team_size = game.team_size_choice;
        if (!net_open(&game, NET_HOST, NULL)) { fprintf(stderr, "bind 7777 impossible\n"); return 1; }
        game.state = ST_MENU;
        snprintf(game.menu_status, sizeof game.menu_status,
                 "En attente sur UDP 7777. Partagez votre IP publique.");
        printf("En attente sur le port %d...\n", PORT);
    } else if (argc >= 3 && !strcmp(argv[1], "join")) {
        snprintf(game.ip_text, sizeof game.ip_text, "%s", argv[2]);
        if (!net_begin_join(&game, game.ip_text)) { fprintf(stderr, "ip invalide\n"); return 1; }
    } else if (argc >= 2 && !strcmp(argv[1], "challenge")) {
        unsigned seed = 424242u;
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
            seed = (unsigned)parsed;
        }
        start_solo_game(&game, seed);
        printf("Defi local contre le bot neuronal, graine %u.\n",
               game.challenge_seed);
    } else {
        game.state = ST_MENU;
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
                } else if (game.state == ST_MENU && game.settings_open) {
                    if (k == SDLK_ESCAPE)
                        game.settings_open = 0;
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
                           && game.f[game.turn].team == game.my_index) {
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
            } else if (ev.type == SDL_TEXTINPUT
                       && game.state == ST_MENU && game.ip_editing) {
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
                            for (row = 3; row < 6; row++) {
                                if (!pt_in(settings_toggle_btn(row), mx, my))
                                    continue;
                                if (row == 3) {
                                    game.colorblind_mode = !game.colorblind_mode;
                                } else if (row == 4) {
                                    game.reduce_flashing = !game.reduce_flashing;
                                } else {
                                    game.subtitles_when_muted =
                                        !game.subtitles_when_muted;
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
                } else if (game.state == ST_PICK) {
                    int pick, slot = game.setup_unit;
                    int unit = unit_for_slot(&game, game.my_index, slot);
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
                    int unit = unit_for_slot(&game, game.my_index,
                                             game.setup_unit);
                    if (pt_in(vb, mx, my)
                        && game.draft_count[unit] == N_EQUIPPED) {
                        sync_draft(&game, unit);
                    } else {
                        draft_click(&game, mx, my);
                    }
                } else if (game.state == ST_STUFF) {
                    int p, unit = unit_for_slot(&game, game.my_index,
                                                game.setup_unit);
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
                        SDL_Rect done = {520, 500, 130, 34};
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
                        SDL_Rect done = {100, 460, 250, 42};
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
                } else if (game.state == ST_BATTLE) {
                    int y0 = GRID_H * TILE;
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
                    if (!handled && my >= GRID_H * TILE) {
                        /* barre de sorts : clic pour selectionner      */
                        int k;
                        for (k = 0; k < N_EQUIPPED; k++) {
                            SDL_Rect b = {10 + k*165, y0 + 30, 160, 56};
                            if (pt_in(b, mx, my))
                                game.sel_skill = (game.sel_skill == k) ? -1 : k;
                        }
                        /* fin de tour                                   */
                        if (game.f[game.turn].team == game.my_index
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
                    /* clic pour quitter                                  */
                    running = 0;
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
        case ST_PICK:     draw_pick(ren, font, &game); break;
        case ST_DRAFT:    draw_draft(ren, font, &game); break;
        case ST_STUFF:    draw_stuff(ren, font, &game); break;
        case ST_ELEMENTS: draw_elements(ren, font, &game); break;
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
            /* combattants tries par profondeur (i + j croissant)      */
            for (fighter = 0; fighter < count; fighter++)
                order[fighter] = fighter;
            for (a = 1; a < count; a++) {
                int cur = order[a];
                b = a;
                while (b > 0 && game.f[order[b - 1]].x
                                     + game.f[order[b - 1]].y
                              > game.f[cur].x + game.f[cur].y) {
                    order[b] = order[b - 1];
                    b--;
                }
                order[b] = cur;
            }
            for (a = 0; a < count; a++)
                draw_fighter(ren, &game, order[a], font);
            draw_hover_mark(ren, &game, hover_i, hover_j);
            draw_vfx(&game, ren);
            draw_skillbar(ren, &game, font);
            if (game.in_game_menu_open)
                draw_ingame_menu(&game, ren, font);
            else
                draw_emote_menu(&game, ren, font);
            break;
        }
        case ST_GAMEOVER: {
            SDL_Color w = {255,255,255,255};
            char msg[64];
            SDL_Rect banner = {20, WIN_H/2 - 55, WIN_W - 40,
                               game.solo_mode ? 86 : 58};
            SDL_SetRenderDrawColor(ren, 35, 28, 24, 235);
            SDL_RenderFillRect(ren, &banner);
            draw_ui_panel(ren, banner);
            SDL_SetRenderDrawColor(ren, 205, 170, 105, 255);
            SDL_RenderDrawRect(ren, &banner);
            if (game.solo_mode)
                snprintf(msg, sizeof msg, "%s ! (clic pour quitter)",
                         game.winner == game.my_index ? "Vous gagnez"
                                                      : "L'IA gagne");
            else
                snprintf(msg, sizeof msg, "%s gagne ! (clic pour quitter)",
                         game.winner == game.my_index
                         ? "Votre equipe" : "Equipe adverse");
            draw_text(ren, font, msg, WIN_W/2 - 150, banner.y + 20, w);
            if (game.solo_mode)
                draw_text(ren, font,
                          "Astuce : surveille la portee et la ligne de vue.",
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