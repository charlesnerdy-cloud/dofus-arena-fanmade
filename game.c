/* ============================================================================
 * DOFUS ARENA FANMADE - P2P 1v1 en C / SDL2
 * ---------------------------------------------------------------------------
 * - 12 classes jouables (Roublard en attente de sprite), 16 sorts chacune.
 * - Phase de DRAFT : chaque joueur choisit 4 sorts parmi les 16 de sa classe
 *   avant chaque partie. La sélection est synchronisée en P2P.
 * - Combat tactique au tour par tour sur grille (style Dofus Arena).
 * - Animations par spritesheet : Idle / Move / Attack (cosmétique, locale).
 * - Réseau Peer-To-Peer en UDP : host + join, port 7777.
 * - Assets : spritesheet "classes" (10 lignes x 10 colonnes, frames ~32x32)
 *   + tuiles. Placez-les dans ./assets/. Si absents : rendu de secours.
 *
 * Compilation :
 *   gcc -O2 -o dofus-arena main.c -lSDL2 -lSDL2_image -lSDL2_ttf -lm
 *
 * Lancement :
 *   ./dofus-arena host              (héberger, port 7777)
 *   ./dofus-arena join 192.168.x.x  (rejoindre)
 *
 * Déroulement : MENU -> PICK (classe) -> DRAFT (4 sorts) -> BATTLE
 * Contrôles : clic = sélectionner case/sort, Espace = fin de tour.
 *
 * IMPORTANT : ajustez SHEET_* / ANIM_* / CLASS_SHEET_ROW ci-dessous à la
 * structure réelle de votre spritesheet (ligne/colonne de chaque classe,
 * colonnes de début des anims idle/move/attack).
 * ==========================================================================*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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
#define GRID_W 14
#define GRID_H 10
#define TILE   48
#define WIN_W  (GRID_W * TILE)
#define WIN_H  (GRID_H * TILE + 160)   /* bandeau UI en bas            */
#define PORT   7777
#define N_CLASSES 13                   /* Roublard (idx 12) en attente */
#define N_PLAYABLE 12                   /* classes sélectionnables      */
#define N_CLASS_SPELLS 16
#define N_EQUIPPED  4
#define MAX_HP 100

/* --- Spritesheet : structure (A ADAPTER a votre feuille) ------------ */
#define SHEET_FRAME_W 32                /* largeur d'une frame          */
#define SHEET_FRAME_H 32                /* hauteur d'une frame          */
#define SHEET_COLS 10                   /* colonnes                    */
#define SHEET_ROWS 10                   /* lignes                      */

/* colonnes de début et nombre de frames par état d'animation          */
#define ANIM_IDLE_START  0
#define ANIM_IDLE_N     4
#define ANIM_MOVE_START 4
#define ANIM_MOVE_N     4
#define ANIM_ATK_START  8
#define ANIM_ATK_N      2
#define ANIM_FRAME_MS 150              /* durée d'une frame             */
#define ANIM_ATK_MS   450              /* durée totale de l'anim attaque */

/* Ligne du spritesheet par classe (5 archétypes : 0 guerrier, 1 mage,
   2 archer, 3 barbare, 4 nécromancien). A ADAPTER selon vos frames.  */
static const int CLASS_SHEET_ROW[N_CLASSES] = {
    0,  /* Iop       - guerrier  */
    2,  /* Cra       - archer    */
    3,  /* Ecaflip   - barbare   */
    1,  /* Eniripsa  - mage      */
    4,  /* Enutrof   - necro     */
    3,  /* Sram      - barbare   */
    3,  /* Sacrieur  - barbare   */
    2,  /* Sadida    - archer    */
    4,  /* Osamodas  - necro     */
    1,  /* Xelor     - mage      */
    0,  /* Feca      - guerrier  */
    3,  /* Pandawa   - barbare   */
    -1  /* Roublard  - PAS DE SPRITE (mise de cote)                    */
};

/* ------------------------------------------------------------------ */
/* Assets                                                              */
/* ------------------------------------------------------------------ */
/* Kit "Free Top-Down Roguelike Game Kit Pixel Art" (itch.io, CC0)     */
/* Les planches 15-17 contiennent : barres de vie, cases de sorts,     */
/* portraits, boutons (15-16) et icones de sorts orange (17).          */
static const char *ASSET_PATHS[] = {
    "assets/tile_grass.png",    /* 0 : sol                          */
    "assets/tile_blocked.png",  /* 1 : obstacle                     */
    "assets/classes_sheet.png", /* 2 : spritesheet des classes       */
    "assets/ui_panel.png",      /* 3 : panneau de fond du bandeau    */
    "assets/ui_hpbar.png",      /* 4 : barre de vie                 */
    "assets/ui_skillslot.png",  /* 5 : case de sort                  */
    "assets/ui_skillslot_sel.png",/* 6 : case de sort sélectionnée    */
    "assets/ui_endturn.png"     /* 7 : bouton fin de tour            */
};
#define N_ASSETS 8
static SDL_Texture *g_assets[N_ASSETS];

/* ------------------------------------------------------------------ */
/* Sorts                                                               */
/* ------------------------------------------------------------------ */
typedef enum { SK_ATK, SK_HEAL, SK_BUFF, SK_SPECIAL } SkillKind;

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

/* Les 13 classes de Dofus, 16 sorts chacune (grimoire fanmade).        */
static ClassDef CLASSES[N_CLASSES] = {
    {"Iop", {
        {"Pression",          SK_ATK,     3, 1, 12, 0},
        {"Couper",            SK_ATK,     3, 1, 15, 1},
        {"Epée Divine",       SK_ATK,     4, 4, 18, 0},
        {"Epée Céleste",      SK_ATK,     5, 3, 24, 3},
        {"Colère de Iop",     SK_ATK,     5, 2, 30, 3},
        {"Epée Destructrice", SK_ATK,     5, 4, 26, 3},
        {"Intimidation",      SK_ATK,     4, 3, 16, 2},
        {"Mutilation",        SK_ATK,     4, 1, 19, 1},
        {"Bond",              SK_SPECIAL, 2, 3, 0,  2},
        {"Amplification",     SK_SPECIAL, 3, 2, 6,  2},
        {"Cautérisation",     SK_HEAL,   3, 0, 10, 3},
        {"Vitalité",          SK_HEAL,   4, 0, 14, 3},
        {"Concentration",     SK_BUFF,   2, 0, 20, 4},
        {"Compulsion",        SK_BUFF,   2, 0, 15, 3},
        {"Foudre",            SK_SPECIAL, 4, 5, 14, 3},
        {"Bourrasque",        SK_SPECIAL, 3, 4, 9,  2},
    }},
    {"Cra", {
        {"Flèche Magique",    SK_ATK,     3, 8, 14, 0},
        {"Flèche Enflammée",  SK_ATK,     4, 7, 18, 1},
        {"Flèche Glacée",     SK_ATK,     3, 7, 11, 0},
        {"Flèche Absorbante", SK_ATK,     4, 6, 16, 1},
        {"Tir Puissant",      SK_ATK,     5, 7, 26, 3},
        {"Flèche Punitive",   SK_ATK,     4, 8, 20, 2},
        {"Arc Magique",       SK_ATK,     4, 6, 20, 2},
        {"Flèche Destructrice",SK_ATK,    5, 7, 24, 3},
        {"Flèche Recul",      SK_SPECIAL, 3, 4, 8,  2},
        {"Œil de Taupe",      SK_SPECIAL, 3, 6, 0,  4},
        {"Flèche Cinglante",  SK_SPECIAL, 3, 6, 9,  2},
        {"Tir Immobilisant",  SK_SPECIAL, 4, 7, 0,  4},
        {"Tir Critique",      SK_BUFF,   2, 0, 18, 4},
        {"Maîtrise de l'Arc", SK_BUFF,   3, 0, 22, 5},
        {"Tir Éloigné",       SK_BUFF,   2, 0, 14, 3},
        {"Fuite",             SK_SPECIAL, 2, 3, 0,  2},
    }},
    {"Ecaflip", {
        {"Pile ou Face",      SK_ATK,     3, 3, 16, 0},
        {"Tout ou Rien",      SK_ATK,     4, 5, 22, 2},
        {"Rekop",             SK_ATK,     5, 1, 32, 4},
        {"Griffe Joueuse",    SK_ATK,     3, 1, 14, 0},
        {"Bluff",             SK_ATK,     3, 4, 17, 1},
        {"Roue de la Fortune",SK_ATK,     4, 3, 21, 2},
        {"Esprit Félin",      SK_ATK,     4, 2, 18, 1},
        {"Griffe Céleste",    SK_ATK,     5, 3, 26, 3},
        {"Bond du Chat",      SK_SPECIAL, 2, 4, 0,  2},
        {"Roulette",          SK_SPECIAL, 2, 0, 25, 5},
        {"Contrepied",        SK_SPECIAL, 3, 4, 8,  2},
        {"Glyphe Chanceux",   SK_SPECIAL, 4, 5, 12, 3},
        {"Chance d'Ecaflip",  SK_BUFF,   2, 0, 18, 4},
        {"Trèfle",            SK_BUFF,   3, 0, 22, 5},
        {"Odorat",            SK_BUFF,   2, 0, 14, 3},
        {"Félintuition",      SK_BUFF,   3, 0, 25, 5},
    }},
    {"Eniripsa", {
        {"Mot Soignant",      SK_HEAL,   3, 5, 20, 0},
        {"Mot Curatif",       SK_HEAL,   4, 6, 28, 2},
        {"Mot de Régénération",SK_HEAL,  4, 0, 12, 3},
        {"Mot d'Amitié",      SK_HEAL,   3, 7, 16, 2},
        {"Mot de Sacrifice",  SK_HEAL,   5, 0, 34, 4},
        {"Mot de Prévention", SK_SPECIAL, 3, 0, 15, 3},
        {"Mot Interdit",      SK_ATK,    4, 4, 15, 0},
        {"Mot Drainant",      SK_ATK,    4, 5, 13, 1},
        {"Mot Ours",          SK_ATK,    5, 3, 25, 3},
        {"Mot de Frayeur",    SK_SPECIAL, 2, 4, 8,  2},
        {"Mot d'Impuissance", SK_SPECIAL, 4, 5, 0,  4},
        {"Mot de Silence",    SK_SPECIAL, 3, 6, 0,  3},
        {"Mot de Volonté",    SK_BUFF,   2, 0, 15, 3},
        {"Mot de Vigueur",    SK_BUFF,   3, 0, 20, 4},
        {"Mot Stimulant",     SK_SPECIAL, 3, 0, 12, 3},
        {"Mot d'Amour",       SK_HEAL,   4, 3, 22, 3},
    }},
    {"Enutrof", {
        {"Lancer de Pièces",  SK_ATK,     3, 6, 13, 0},
        {"Pelle Fantôme",     SK_ATK,     4, 3, 17, 0},
        {"Pelle Massacrante", SK_ATK,     5, 2, 28, 3},
        {"Pelle de Jugement", SK_ATK,     4, 4, 20, 2},
        {"Maladresse",        SK_SPECIAL, 3, 5, 9,  2},
        {"Sac Animé",         SK_SPECIAL, 3, 2, 10, 3},
        {"Force de l'Age",    SK_SPECIAL, 4, 4, 12, 3},
        {"Coffrage",          SK_SPECIAL, 2, 5, 0,  5},
        {"Roulage de Pelle",  SK_ATK,     5, 1, 24, 2},
        {"Accélération Poussée",SK_SPECIAL,3, 3, 0, 3},
        {"Châtiment de l'Eau",SK_ATK,     4, 5, 19, 2},
        {"Pelle Fantomatique",SK_ATK,     3, 4, 15, 0},
        {"Chance d'Eca",      SK_BUFF,   2, 0, 18, 4},
        {"Trésor",            SK_BUFF,   3, 0, 20, 4},
        {"Rétention",         SK_SPECIAL, 3, 3, 0,  3},
        {"Glyphe de Chance",  SK_SPECIAL, 4, 0, 14, 4},
    }},
    {"Sram", {
        {"Coup Sournois",     SK_ATK,     4, 1, 22, 0},
        {"Arnaque",           SK_ATK,     3, 1, 14, 0},
        {"Piège de Masse",    SK_ATK,     4, 4, 19, 2},
        {"Piège d'Emboîtement",SK_ATK,    5, 5, 24, 3},
        {"Attaque Trompeuse", SK_ATK,     3, 2, 16, 1},
        {"Frappe du Sram",    SK_ATK,     4, 3, 20, 2},
        {"Fourbe Lame",       SK_ATK,     5, 2, 27, 3},
        {"Poignard Prodige",  SK_ATK,     4, 1, 21, 2},
        {"Peur",              SK_SPECIAL, 2, 3, 8,  2},
        {"Invisibilité",     SK_SPECIAL, 3, 0, 0,  5},
        {"Double",            SK_SPECIAL, 4, 0, 0,  6},
        {"Piège Empoisonné",  SK_SPECIAL, 3, 4, 11, 3},
        {"Fourbe",            SK_BUFF,   2, 0, 20, 4},
        {"Mépris",            SK_BUFF,   3, 0, 18, 4},
        {"Piège de Silence",  SK_SPECIAL, 4, 6, 0,  4},
        {"Concentration de Sram",SK_BUFF, 2, 0, 16, 3},
    }},
    {"Sacrieur", {
        {"Pied du Sacrieur",  SK_ATK,     3, 1, 15, 0},
        {"Attaque Furieuse",  SK_ATK,     4, 1, 20, 1},
        {"Punition",          SK_ATK,     5, 1, 28, 2},
        {"Frappe du Sacrieur",SK_ATK,     4, 2, 21, 2},
        {"Morsure du Chien",  SK_ATK,     3, 1, 16, 1},
        {"Chatiment Colereux",SK_ATK,     5, 3, 26, 3},
        {"Furie Sacrée",      SK_ATK,     5, 1, 30, 4},
        {"Larcère",           SK_ATK,     4, 3, 19, 2},
        {"Furie",             SK_BUFF,   2, 0, 25, 5},
        {"Châtiment",         SK_BUFF,   2, 0, 22, 5},
        {"Attirance",         SK_SPECIAL, 2, 6, 0,  2},
        {"Transposition",     SK_SPECIAL, 3, 5, 0,  3},
        {"Sacrifice",         SK_HEAL,   4, 0, 15, 4},
        {"Châtiment Vital",   SK_HEAL,   5, 0, 30, 5},
        {"Dérobade",          SK_SPECIAL, 2, 3, 0,  2},
        {"Châtiment de l'Air",SK_BUFF,   3, 0, 28, 5},
    }},
    {"Sadida", {
        {"Ronce",             SK_ATK,     3, 5, 16, 0},
        {"Ronce Agressive",   SK_ATK,     4, 5, 21, 2},
        {"Ronce Insolente",   SK_ATK,     5, 4, 27, 3},
        {"Ronce Apaisante",   SK_ATK,     4, 4, 18, 1},
        {"Feu de Brousse",    SK_ATK,     4, 3, 19, 2},
        {"Sacrifice",         SK_HEAL,   4, 3, 18, 3},
        {"Herbe Folle",       SK_SPECIAL, 2, 4, 8,  2},
        {"Surinvasion",       SK_BUFF,   3, 0, 15, 4},
        {"Arbre",             SK_SPECIAL, 3, 0, 12, 4},
        {"Venteux",           SK_SPECIAL, 3, 3, 6,  2},
        {"Feu Grégeois",      SK_ATK,     5, 5, 25, 3},
        {"Ronce Dévastatrice",SK_ATK,     5, 3, 29, 4},
        {"Tremblement",       SK_SPECIAL, 4, 5, 13, 3},
        {"Poussée",           SK_SPECIAL, 3, 3, 8,  2},
        {"Ronce Gloursonne",  SK_ATK,     3, 3, 17, 1},
        {"Sève Vitale",       SK_HEAL,   3, 0, 12, 2},
    }},
    {"Osamodas", {
        {"Griffe",            SK_ATK,     3, 4, 15, 0},
        {"Frappe du Bwork",   SK_ATK,     5, 3, 27, 3},
        {"Tofu",              SK_ATK,     2, 5,  8, 0},
        {"Fouet",             SK_ATK,     4, 2, 18, 1},
        {"Griffe Spectrale",  SK_ATK,     4, 4, 20, 2},
        {"Cri du Bouftou",    SK_ATK,     3, 3, 15, 0},
        {"Corbeau Noir",      SK_ATK,     5, 6, 26, 3},
        {"Crocs Mulou",       SK_ATK,     4, 1, 22, 2},
        {"Bénédiction Animale",SK_BUFF,   3, 0, 20, 4},
        {"Piqûre Motivante",  SK_BUFF,   2, 0, 15, 3},
        {"Cri de l'Ours",     SK_SPECIAL, 2, 0, 10, 3},
        {"Corbeau",           SK_SPECIAL, 3, 4, 9,  2},
        {"Invocation de Tofu",SK_SPECIAL, 3, 2, 0,  3},
        {"Bouclier Animé",    SK_SPECIAL, 4, 0, 16, 4},
        {"Résistance Magique",SK_BUFF,   3, 0, 22, 5},
        {"Sanglier Déchaîné", SK_ATK,     5, 4, 24, 3},
    }},
    {"Xelor", {
        {"Aiguille",          SK_ATK,     3, 4, 14, 0},
        {"Gelure",            SK_ATK,     4, 4, 17, 1},
        {"Horloge",           SK_ATK,     5, 3, 25, 3},
        {"Sablier du Temps",  SK_SPECIAL, 3, 3, 0,  4},
        {"Ralentissement",    SK_SPECIAL,  2, 6, 0,  3},
        {"Cadran Xelor",      SK_BUFF,   3, 0, 22, 5},
        {"Contre",            SK_BUFF,    2, 0, 15, 3},
        {"Fuite de Xelor",    SK_SPECIAL,  2, 3, 0,  2},
        {"Poussière Temporelle",SK_ATK,   4, 5, 19, 2},
        {"Rayon Obscur",      SK_ATK,     4, 4, 18, 1},
        {"Aiguille Cherchuse",SK_ATK,     5, 6, 24, 3},
        {"Frappe de Xelor",   SK_ATK,     4, 2, 20, 2},
        {"Téléportation",     SK_SPECIAL, 3, 7, 0,  4},
        {"Démotivation",      SK_SPECIAL, 3, 5, 0,  3},
        {"Remise du Temps",   SK_SPECIAL, 4, 0, 18, 4},
        {"Glyphe de Xelor",   SK_SPECIAL, 4, 4, 12, 3},
    }},
    {"Feca", {
        {"Attaque Naturelle",  SK_ATK,     3, 4, 13, 0},
        {"Glyphe d'Agression",SK_ATK,     5, 3, 25, 3},
        {"Glyphe Enflammé",   SK_ATK,     4, 2, 18, 2},
        {"Bouclier Féca",     SK_SPECIAL, 2, 0, 10, 2},
        {"Armure Terrestre",  SK_SPECIAL, 3, 0, 15, 3},
        {"Immunité",          SK_SPECIAL, 4, 0, 20, 5},
        {"Rétention",         SK_SPECIAL, 3, 3, 8,  3},
        {"Trêve",             SK_SPECIAL, 3, 0, 12, 4},
        {"Nuage Moskito",     SK_ATK,     3, 5, 14, 0},
        {"Attaque Nuageuse",  SK_ATK,     4, 4, 17, 1},
        {"Glyphe de Silence", SK_SPECIAL, 4, 4, 0,  4},
        {"Bouclier Terrestre",SK_BUFF,   3, 0, 20, 4},
        {"Bouclier de Feu",   SK_SPECIAL, 3, 0, 14, 3},
        {"Retour du Bâton",   SK_ATK,     4, 1, 19, 2},
        {"Tremblement de Terre",SK_ATK,   5, 4, 26, 3},
        {"Bulbesacre",        SK_SPECIAL, 4, 0, 18, 4},
    }},
    {"Pandawa", {
        {"Poison Lame",       SK_ATK,     4, 2, 18, 0},
        {"Vent Ethyl",        SK_ATK,     4, 4, 20, 2},
        {"Gueule de Bois",    SK_ATK,     5, 1, 26, 3},
        {"Lait de Bambou",    SK_HEAL,    3, 3, 18, 2},
        {"Vulnérabilité",     SK_SPECIAL, 2, 4, 0,  3},
        {"Karcham",           SK_SPECIAL, 3, 1, 0,  3},
        {"Picole",            SK_BUFF,    2, 0, 18, 4},
        {"Boison de Potion",  SK_BUFF,    2, 0, 18, 4},
        {"Bond du Pandawa",   SK_SPECIAL, 2, 3, 0,  2},
        {"Fracasse",          SK_ATK,     4, 3, 21, 2},
        {"Punch du Pandawa",  SK_ATK,     3, 1, 16, 0},
        {"Souffle Alcoolisé", SK_ATK,     5, 3, 25, 3},
        {"Cham Chaam",        SK_SPECIAL, 4, 2, 0,  4},
        {"Bambou Malheureux", SK_ATK,     4, 4, 19, 2},
        {"Gobelet",           SK_SPECIAL, 3, 0, 12, 3},
        {"Zéro Absolu",       SK_SPECIAL, 4, 5, 14, 3},
    }},
    {"Roublard", {   /* classe en attente : non sélectionnable */
        {"Pistolet",          SK_ATK,     3, 5, 15, 0},
        {"Perforation",       SK_ATK,     4, 6, 19, 1},
        {"Bombe",             SK_ATK,     4, 6, 24, 2},
        {"Mise à Terre",      SK_ATK,     5, 2, 30, 4},
        {"Câble",             SK_SPECIAL, 2, 3, 0,  2},
        {"Ruse",              SK_SPECIAL, 3, 5, 0,  4},
        {"Roublardise",       SK_BUFF,    2, 0, 20, 4},
        {"Escroquerie",       SK_SPECIAL, 3, 4, 10, 3},
        {"Fumigation",        SK_SPECIAL, 3, 0, 0,  3},
        {"Poudre Noire",      SK_ATK,     5, 5, 27, 3},
        {"Tir de Précision",  SK_ATK,     4, 7, 21, 2},
        {"Charge Explosive",  SK_ATK,     5, 4, 26, 3},
        {"Détonation",        SK_ATK,     4, 3, 22, 2},
        {"Carabine",          SK_ATK,     3, 6, 17, 1},
        {"Retraite Estrade",  SK_SPECIAL, 2, 4, 0,  2},
        {"Poison Mortel",     SK_SPECIAL, 4, 5, 13, 3},
    }},
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
    Skill spells[N_EQUIPPED];  /* les 4 sorts choisis pour la partie  */
    int  cd_left[N_EQUIPPED];
    /* animation (cosmétique, jamais synchronisée sur le réseau)      */
    AnimState anim;
    int       anim_frame;
    Uint32    anim_until;   /* timestamp de retour a ANIM_IDLE       */
} Fighter;

/* ------------------------------------------------------------------ */
/* réseau : protocole P2P minimal (inchangé)                            */
/*   HELLO / CLASS <who> <idx> / SPELLS <who> <a> <b> <c> <d>          */
/*   MOVE <who> <x> <y> / SKILL <who> <idx> <tx> <ty> / END <who> / BYE*/
/* ------------------------------------------------------------------ */
typedef enum { NET_NONE, NET_HOST, NET_JOIN } NetRole;

typedef struct {
    Sock          sock;
    struct sockaddr_in peer;
    int           has_peer;
    NetRole       role;
    Uint32        last_recv_ms;
} Net;

/* ------------------------------------------------------------------ */
/* état global du jeu                                                  */
/* ------------------------------------------------------------------ */
typedef enum { ST_MENU, ST_PICK, ST_DRAFT, ST_BATTLE, ST_GAMEOVER } GameState;

typedef struct {
    GameState state;
    int       blocked[GRID_H][GRID_W];
    Fighter   f[2];
    int       my_index;
    int       turn;
    int       turn_count;
    int       sel_skill;
    int       winner;
    Net       net;
    int       class_chosen[2];
    int       draft[2][N_CLASS_SPELLS];
    int       draft_count[2];
    int       draft_ready[2];
} Game;

/* prototypes des fonctions moteur utilisées par net_poll */
static void apply_skill_remote(Game *g, int who, int idx, int tx, int ty);
static void end_turn(Game *g, int from_net);
static void sync_draft(Game *g, int who);
static void check_draft_start(Game *g);
static void set_anim(Game *g, int idx, AnimState st);

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
    g->net.role = role;
    if (role == NET_HOST) {
        struct sockaddr_in a;
        int yes = 1;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = INADDR_ANY;
        a.sin_port = htons(PORT);
        setsockopt(g->net.sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof yes);
        if (bind(g->net.sock, (struct sockaddr*)&a, sizeof a) < 0) return 0;
    } else {
        memset(&g->net.peer, 0, sizeof g->net.peer);
        g->net.peer.sin_family = AF_INET;
        g->net.peer.sin_port = htons(PORT);
        if (inet_pton(AF_INET, host_ip, &g->net.peer.sin_addr) != 1) return 0;
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

/* réception non bloquante : dispatch vers le moteur de jeu            */
static void net_poll(Game *g)
{
    char buf[256];
    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof from;
        int n = (int)recvfrom(g->net.sock, buf, sizeof buf - 1, MSG_DONTWAIT,
                              (struct sockaddr*)&from, &flen);
        if (n <= 0) break;
        buf[n] = 0;
        char *nl = strchr(buf, '\n'); if (nl) *nl = 0;
        if (!g->net.has_peer) { g->net.peer = from; g->net.has_peer = 1; }

        char cmd[16]; int who, a, b, c, d;
        if (sscanf(buf, "%15s", cmd) != 1) continue;
        if (!strcmp(cmd, "HELLO")) {
            net_send(g, "HELLO ok");
            g->state = ST_PICK;
        } else if (!strcmp(cmd, "CLASS") && sscanf(buf, "%*s %d %d", &who, &a) == 2) {
            if (who != g->my_index) {
                g->class_chosen[who] = a;
                if (g->state == ST_PICK) g->state = ST_DRAFT;
            }
        } else if (!strcmp(cmd, "SPELLS") &&
                   sscanf(buf, "%*s %d %d %d %d %d", &who, &a, &b, &c, &d) == 5) {
            if (who != g->my_index) {
                memset(g->draft[who], 0, sizeof g->draft[who]);
                g->draft[who][a] = g->draft[who][b] = 1;
                g->draft[who][c] = g->draft[who][d] = 1;
                g->draft_ready[who] = 1;
                check_draft_start(g);
            }
        } else if (!strcmp(cmd, "MOVE") && sscanf(buf, "%*s %d %d %d", &who, &a, &b) == 3) {
            if (who != g->my_index) {
                g->f[who].x = a; g->f[who].y = b;
                set_anim(g, who, ANIM_MOVE);  /* anim distante (cosmétique) */
            }
        } else if (!strcmp(cmd, "SKILL") && sscanf(buf, "%*s %d %d %d %d", &who, &a, &b, &c) == 4) {
            if (who != g->my_index) apply_skill_remote(g, who, a, b, c);
        } else if (!strcmp(cmd, "END") && sscanf(buf, "%*s %d", &who) == 1) {
            if (who != g->my_index) end_turn(g, 1);
        } else if (!strcmp(cmd, "BYE")) {
            g->winner = g->my_index; g->state = ST_GAMEOVER;
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
    for (i = 0; i < 2; i++) {
        Fighter *f = &g->f[i];
        if (!f->alive) continue;
        if (f->anim != ANIM_IDLE && (Sint32)(now - f->anim_until) >= 0)
            f->anim = ANIM_IDLE;   /* retour au idle après move/attack */
        if (advance) {
            int n = f->anim == ANIM_IDLE  ? ANIM_IDLE_N :
                    f->anim == ANIM_MOVE  ? ANIM_MOVE_N : ANIM_ATK_N;
            f->anim_frame = (f->anim_frame + 1) % (n > 0 ? n : 1);
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
    f->hp = MAX_HP; f->alive = 1; f->atk_mod = 0;
    f->anim = ANIM_IDLE; f->anim_frame = 0; f->anim_until = 0;
    memset(f->cd_left, 0, sizeof f->cd_left);
    for (s = 0; s < N_CLASS_SPELLS && n < N_EQUIPPED; s++) {
        if (g->draft[idx][s]) f->spells[n++] = CLASSES[f->cls].spells[s];
    }
    for (k = n; k < N_EQUIPPED; k++)
        f->spells[k] = CLASSES[f->cls].spells[0]; /* sécurité */
}

static void start_new_game(Game *g)
{
    int i, j;
    srand((unsigned)time(NULL));
    for (j = 0; j < GRID_H; j++)
        for (i = 0; i < GRID_W; i++)
            g->blocked[j][i] = 0;
    /* obstacles symétriques (équité) */
    for (i = 0; i < 10; i++) {
        int x = 2 + rand() % (GRID_W - 4);
        int y = 1 + rand() % (GRID_H - 2);
        g->blocked[y][x] = 1;
        g->blocked[GRID_H-1-y][GRID_W-1-x] = 1;
    }
    g->blocked[GRID_H/2][0] = g->blocked[GRID_H/2][GRID_W-1] = 0;
}

static void place_fighters(Game *g)
{
    int i;
    for (i = 0; i < 2; i++) build_fighter(g, i);
    g->f[0].x = 1; g->f[0].y = GRID_H/2;
    g->f[1].x = GRID_W-2; g->f[1].y = GRID_H/2;
    g->turn = 0; g->turn_count = 0; g->winner = -1;
    g->f[0].pa = 6; g->f[0].pm = 3;
    g->f[1].pa = 0; g->f[1].pm = 0;
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
    check_draft_start(g);
}

static void check_draft_start(Game *g)
{
    if (g->draft_ready[0] && g->draft_ready[1]
        && g->class_chosen[0] >= 0 && g->class_chosen[1] >= 0) {
        start_new_game(g);
        place_fighters(g);
        g->state = ST_BATTLE;
    }
}

/* ------------------------------------------------------------------ */
/* règles de jeu                                                       */
/* ------------------------------------------------------------------ */
static int manhattan(int x1, int y1, int x2, int y2)
{
    return abs(x1-x2) + abs(y1-y2);
}

static void try_move(Game *g, int tx, int ty)
{
    Fighter *me = &g->f[g->my_index];
    if (g->turn != g->my_index || g->state != ST_BATTLE) return;
    if (tx < 0 || ty < 0 || tx >= GRID_W || ty >= GRID_H) return;
    if (g->blocked[ty][tx]) return;
    Fighter *op = &g->f[1 - g->my_index];
    if (op->x == tx && op->y == ty) return;
    int d = manhattan(me->x, me->y, tx, ty);
    if (d == 0 || d > me->pm) return;
    me->x = tx; me->y = ty; me->pm -= d;
    set_anim(g, g->my_index, ANIM_MOVE);
    char msg[64];
    snprintf(msg, sizeof msg, "MOVE %d %d %d", g->my_index, tx, ty);
    net_send(g, msg);
}

static void cast_skill(Game *g, int who, int idx, int tx, int ty)
{
    Fighter *f = &g->f[who];
    Fighter *o = &g->f[1 - who];
    if (idx < 0 || idx >= N_EQUIPPED) return;
    Skill *s = &f->spells[idx];
    f->pa -= s->pa;
    f->cd_left[idx] = s->cooldown;

    switch (s->kind) {
        case SK_ATK:
            set_anim(g, who, ANIM_ATTACK);
            if (manhattan(f->x, f->y, tx, ty) <= s->range && o->x == tx && o->y == ty) {
                /* dégâts déterministes : pas de désync entre peers    */
                int dmg = (s->power * (100 + f->atk_mod)) / 100;
                o->hp -= dmg;
                if (o->hp <= 0) {
                    o->hp = 0; o->alive = 0;
                    g->winner = who; g->state = ST_GAMEOVER;
                }
            }
            break;
        case SK_HEAL:
            set_anim(g, who, ANIM_ATTACK);
            if (s->range == 0 || manhattan(f->x, f->y, tx, ty) <= s->range) {
                f->hp += s->power;
                if (f->hp > MAX_HP) f->hp = MAX_HP;
            }
            break;
        case SK_BUFF:
            set_anim(g, who, ANIM_ATTACK);
            f->atk_mod += s->power;
            break;
        case SK_SPECIAL:
            set_anim(g, who, ANIM_ATTACK);
            if (s->power > 0 && o->x == tx && o->y == ty) {
                o->hp -= s->power;
                if (o->hp <= 0) {
                    o->hp = 0; o->alive = 0;
                    g->winner = who; g->state = ST_GAMEOVER;
                }
            } else if (s->power > 0) {
                f->hp += s->power;
                if (f->hp > MAX_HP) f->hp = MAX_HP;
            }
            break;
    }
}

static void apply_skill_remote(Game *g, int who, int idx, int tx, int ty)
{
    if (who == g->my_index) return;
    cast_skill(g, who, idx, tx, ty);
}

static void try_cast(Game *g, int tx, int ty)
{
    Fighter *me = &g->f[g->my_index];
    if (g->turn != g->my_index || g->state != ST_BATTLE) return;
    if (g->sel_skill < 0) return;
    Skill *s = &me->spells[g->sel_skill];
    if (s->pa > me->pa) return;
    if (me->cd_left[g->sel_skill] > 0) return;
    if (s->range > 0 && manhattan(me->x, me->y, tx, ty) > s->range) return;
    cast_skill(g, g->my_index, g->sel_skill, tx, ty);
    char msg[64];
    snprintf(msg, sizeof msg, "SKILL %d %d %d %d",
             g->my_index, g->sel_skill, tx, ty);
    net_send(g, msg);
    g->sel_skill = -1;
}

static void end_turn(Game *g, int from_net)
{
    int k;
    Fighter *cur = &g->f[g->turn];
    for (k = 0; k < N_EQUIPPED; k++)
        if (cur->cd_left[k] > 0) cur->cd_left[k]--;
    g->turn = 1 - g->turn;
    g->turn_count++;
    g->f[g->turn].pa = 6;
    g->f[g->turn].pm = 3;
    g->sel_skill = -1;
    if (!from_net) {
        char msg[32];
        snprintf(msg, sizeof msg, "END %d", g->my_index);
        net_send(g, msg);
    }
}

/* ------------------------------------------------------------------ */
/* rendu                                                               */
/* ------------------------------------------------------------------ */
static SDL_Texture *load_asset(SDL_Renderer *r, const char *path)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
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

static void draw_tile(SDL_Renderer *r, int i, int j, int blocked)
{
    SDL_Rect d = {i * TILE, j * TILE, TILE, TILE};
    if (g_assets[blocked ? 1 : 0]) {
        SDL_RenderCopy(r, g_assets[blocked ? 1 : 0], NULL, &d);
    } else {
        if (blocked) SDL_SetRenderDrawColor(r, 90, 70, 40, 255);
        else         SDL_SetRenderDrawColor(r, 60, 140, 60, 255);
        SDL_RenderFillRect(r, &d);
    }
    SDL_SetRenderDrawColor(r, 20, 20, 20, 255);
    SDL_RenderDrawRect(r, &d);
}

/* dessine le combattant : frame courante du spritesheet ou repli       */
static void draw_fighter(SDL_Renderer *r, Game *g, int idx, TTF_Font *font)
{
    Fighter *f = &g->f[idx];
    if (!f->alive) return;
    SDL_Rect d = {f->x * TILE, f->y * TILE, TILE, TILE};

    int row = CLASS_SHEET_ROW[f->cls];
    if (g_assets[2] && row >= 0 && row < SHEET_ROWS) {
        int start, n;
        switch (f->anim) {
            case ANIM_MOVE:   start = ANIM_MOVE_START; n = ANIM_MOVE_N; break;
            case ANIM_ATTACK: start = ANIM_ATK_START;  n = ANIM_ATK_N;  break;
            default:          start = ANIM_IDLE_START; n = ANIM_IDLE_N; break;
        }
        int col = start + (f->anim_frame % (n > 0 ? n : 1));
        if (col >= SHEET_COLS) col %= SHEET_COLS;
        SDL_Rect s = { col * SHEET_FRAME_W, row * SHEET_FRAME_H,
                      SHEET_FRAME_W, SHEET_FRAME_H };
        /* teinte l'adversaire pour le distinguer                     */
        if (idx != g->my_index)
            SDL_SetTextureColorMod(g_assets[2], 255, 170, 170);
        else
            SDL_SetTextureColorMod(g_assets[2], 255, 255, 255);
        SDL_RenderCopy(r, g_assets[2], &s, &d);
    } else {
        SDL_SetRenderDrawColor(r, idx == g->my_index ? 40 : 200,
                               idx == g->my_index ? 120 : 40,
                               idx == g->my_index ? 240 : 40, 255);
        SDL_RenderFillRect(r, &d);
    }
    /* marqueur du joueur actif */
    if (g->state == ST_BATTLE && g->turn == idx) {
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
        SDL_RenderDrawRect(r, &d);
    }
    /* barre de vie du kit pixel art au-dessus du sprite, ou repli      */
    {
        SDL_Rect hb = {f->x * TILE + 4, f->y * TILE - 8, TILE - 8, 6};
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, &hb);
        int fill = (hb.w * f->hp) / MAX_HP;
        SDL_Rect in = {hb.x + 1, hb.y + 1, fill > 2 ? fill - 2 : 0, hb.h - 2};
        if (f->hp > MAX_HP/2)      SDL_SetRenderDrawColor(r, 80, 220, 80, 255);
        else if (f->hp > MAX_HP/5) SDL_SetRenderDrawColor(r, 240, 180, 40, 255);
        else                       SDL_SetRenderDrawColor(r, 220, 60, 50, 255);
        SDL_RenderFillRect(r, &in);
        if (g_assets[4]) SDL_RenderCopy(r, g_assets[4], NULL, &hb);
    }
    char hp[32];
    snprintf(hp, sizeof hp, "%d", f->hp);
    SDL_Color c = {255, 255, 255, 255};
    draw_text(r, font, CLASSES[f->cls].name, f->x*TILE + 4, f->y*TILE - 18, c);
    draw_text(r, font, hp, f->x*TILE + TILE/2 - 8, f->y*TILE + TILE/2 - 8, c);
}

static void draw_skillbar(SDL_Renderer *r, Game *g, TTF_Font *font)
{
    Fighter *me = &g->f[g->my_index];
    int y0 = GRID_H * TILE;
    SDL_Rect panel = {0, y0, WIN_W, 160};
    SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
    SDL_RenderFillRect(r, &panel);
    char line[128];
    SDL_Color white = {255,255,255,255};
    snprintf(line, sizeof line, "PV: %d  PA: %d  PM: %d  atk: %+d%%",
             me->hp, me->pa, me->pm, me->atk_mod);
    draw_text(r, font, line, 10, y0 + 6, white);

    int k;
    for (k = 0; k < N_EQUIPPED; k++) {
        Skill *s = &me->spells[k];
        SDL_Rect b = {10 + k * 190, y0 + 30, 180, 56};
        /* case de sort du kit pixel art, sinon rectangle               */
        SDL_Texture *slot = g_assets[g->sel_skill == k ? 6 : 5];
        if (slot)
            SDL_RenderCopy(r, slot, NULL, &b);
        else {
            if (g->sel_skill == k)
                SDL_SetRenderDrawColor(r, 200, 170, 60, 255);
            else
                SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
            SDL_RenderFillRect(r, &b);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &b);
        char t[96];
        snprintf(t, sizeof t, "%d %s (%dPA P%d)", k+1, s->name, s->pa, s->range);
        draw_text(r, font, t, b.x + 6, b.y + 6, white);
        snprintf(t, sizeof t, "effet %d  CD %d/%d",
                 s->power, me->cd_left[k], s->cooldown);
        draw_text(r, font, t, b.x + 6, b.y + 30, white);
    }
    draw_text(r, font, "Espace = fin de tour  |  clic = cibler case",
              10, y0 + 94, white);
    if (g->turn != g->my_index)
        draw_text(r, font, "Tour de l'adversaire...", 10, y0 + 118, white);
}

static void draw_menu(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    draw_text(r, font, "DOFUS ARENA FANMADE - P2P 1v1", 20, 20, white);
    draw_text(r, font, "H = Heberger (host)", 20, 70, white);
    draw_text(r, font, "J = Rejoindre (join 127.0.0.1)", 20, 100, white);
    if (g->net.role == NET_HOST && !g->net.has_peer)
        draw_text(r, font, "En attente d'un adversaire sur le port 7777...",
                  20, 150, white);
}

static void draw_pick(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color grey  = {130,130,130,255};
    char line[96];
    draw_text(r, font, "Choisissez votre classe (touches 1-9 puis A-C) :", 20, 20, white);
    int i;
    for (i = 0; i < N_CLASSES; i++) {
        snprintf(line, sizeof line, "%2d %-12s", i+1, CLASSES[i].name);
        int col = i % 3;
        /* Roublard (13e) en attente de sprite : grisé                */
        draw_text(r, font, line, 20 + col*220, 60 + (i/3)*30,
                  i < N_PLAYABLE ? white : grey);
    }
    draw_text(r, font, "Roublard : bientot (sprites en attente)", 20, 210, grey);
    if (g->class_chosen[g->my_index] >= 0) {
        snprintf(line, sizeof line, "Votre classe : %s",
                 CLASSES[g->class_chosen[g->my_index]].name);
        draw_text(r, font, line, 20, 240, white);
    }
    if (g->class_chosen[1 - g->my_index] >= 0) {
        snprintf(line, sizeof line, "Adversaire pret : %s",
                 CLASSES[g->class_chosen[1 - g->my_index]].name);
        draw_text(r, font, line, 20, 270, white);
    }
    if (g->class_chosen[0] >= 0 && g->class_chosen[1] >= 0 && g->net.has_peer)
        draw_text(r, font, "Passez au draft de sorts...", 20, 300, white);
}

/* écran de draft : 16 sorts de la classe, en choisir 4                 */
static void draw_draft(SDL_Renderer *r, TTF_Font *font, Game *g)
{
    SDL_Color white = {255,255,255,255};
    SDL_Color green = {80,255,120,255};
    char line[128];
    int cls = g->class_chosen[g->my_index];
    int i;
    if (cls < 0 || cls >= N_CLASSES) { g->state = ST_PICK; return; }

    snprintf(line, sizeof line, "DRAFT %s : choisissez 4 sorts parmi 16 (%d/4)",
             CLASSES[cls].name, g->draft_count[g->my_index]);
    draw_text(r, font, line, 20, 20, white);
    for (i = 0; i < N_CLASS_SPELLS; i++) {
        Skill *s = &CLASSES[cls].spells[i];
        int col = i % 4, row = i / 4;
        SDL_Rect b = {10 + col * 175, 60 + row * 100, 170, 96};
        if (g->draft[g->my_index][i])
            SDL_SetRenderDrawColor(r, 60, 160, 70, 255);
        else
            SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
        SDL_RenderFillRect(r, &b);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &b);
        snprintf(line, sizeof line, "%X %s", i+1, s->name);
        draw_text(r, font, line, b.x + 4, b.y + 4, white);
        const char *kind = s->kind == SK_ATK ? "attaque" :
                           s->kind == SK_HEAL ? "soin"   :
                           s->kind == SK_BUFF ? "buff"   : "special";
        snprintf(line, sizeof line, "%s %d, %dPA, P%d, CD%d",
                 kind, s->power, s->pa, s->range, s->cooldown);
        draw_text(r, font, line, b.x + 4, b.y + 22, white);
        snprintf(line, sizeof line, "touche %X", i+1);
        draw_text(r, font, line, b.x + 4, b.y + 74, white);
    }
    if (g->draft_count[g->my_index] >= N_EQUIPPED)
        draw_text(r, font, "Entree = valider votre deck", 20, 480, white);
    if (g->draft_ready[1 - g->my_index])
        draw_text(r, font, "L'adversaire a valide son deck.", 20, 505, green);
}

static void draft_click(Game *g, int mx, int my)
{
    int i;
    int cls = g->class_chosen[g->my_index];
    if (cls < 0) return;
    for (i = 0; i < N_CLASS_SPELLS; i++) {
        int col = i % 4, row = i / 4;
        SDL_Rect b = {10 + col * 175, 60 + row * 100, 170, 96};
        if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
            if (!g->draft[g->my_index][i] && g->draft_count[g->my_index] < N_EQUIPPED) {
                g->draft[g->my_index][i] = 1;
                g->draft_count[g->my_index]++;
            } else if (g->draft[g->my_index][i]) {
                g->draft[g->my_index][i] = 0;
                g->draft_count[g->my_index]--;
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
    int i, j, running = 1;

    memset(&game, 0, sizeof game);
    game.my_index = 0;
    game.sel_skill = -1;
    game.winner = -1;
    for (i = 0; i < 2; i++) {
        game.class_chosen[i] = -1;
        game.draft_ready[i] = 0;
        game.draft_count[i] = 0;
        for (j = 0; j < N_CLASS_SPELLS; j++) game.draft[i][j] = 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) { fprintf(stderr, "IMG\n"); return 1; }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF\n"); return 1; }
    if (!net_init()) { fprintf(stderr, "WSA\n"); return 1; }

    win = SDL_CreateWindow("Dofus Arena P2P - Fanmade",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WIN_W, WIN_H, 0);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    font = TTF_OpenFont("assets/font.ttf", 16);
    if (!font) font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16);

    for (i = 0; i < N_ASSETS; i++) g_assets[i] = load_asset(ren, ASSET_PATHS[i]);

    if (argc >= 2 && !strcmp(argv[1], "host")) {
        if (!net_open(&game, NET_HOST, NULL)) { fprintf(stderr, "bind 7777 impossible\n"); return 1; }
        game.state = ST_MENU;
        printf("En attente sur le port %d...\n", PORT);
    } else if (argc >= 3 && !strcmp(argv[1], "join")) {
        if (!net_open(&game, NET_JOIN, argv[2])) { fprintf(stderr, "ip invalide\n"); return 1; }
        game.my_index = 1;
        net_send(&game, "HELLO client");
        game.state = ST_PICK;
    } else {
        game.state = ST_MENU;
        game.net.sock = SOCK_INVALID;
    }

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
                if (game.net.sock != SOCK_INVALID) net_send(&game, "BYE");
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (game.state == ST_MENU) {
                    if (k == SDLK_h && game.net.sock == SOCK_INVALID) {
                        net_open(&game, NET_HOST, NULL);
                    } else if (k == SDLK_j && game.net.sock == SOCK_INVALID) {
                        net_open(&game, NET_JOIN, "127.0.0.1");
                        game.my_index = 1;
                        net_send(&game, "HELLO client");
                    }
                } else if (game.state == ST_PICK) {
                    int pick = -1;
                    if (k >= SDLK_1 && k <= SDLK_9) pick = (int)(k - SDLK_1);
                    else if (k >= SDLK_a && k <= SDLK_c) pick = 9 + (int)(k - SDLK_a);
                    if (pick >= 0 && pick < N_CLASSES) {
                        if (pick >= N_PLAYABLE) {
                            /* Roublard indisponible pour l'instant    */
                        } else {
                            game.class_chosen[game.my_index] = pick;
                            char msg[32];
                            snprintf(msg, sizeof msg, "CLASS %d %d",
                                     game.my_index, pick);
                            net_send(&game, msg);
                            if (game.class_chosen[0] >= 0 && game.class_chosen[1] >= 0)
                                game.state = ST_DRAFT;
                        }
                    }
                } else if (game.state == ST_DRAFT) {
                    int s = -1;
                    if (k >= SDLK_1 && k <= SDLK_9) s = (int)(k - SDLK_1);
                    else if (k >= SDLK_a && k <= SDLK_g) s = 9 + (int)(k - SDLK_a);
                    if (s >= 0 && s < N_CLASS_SPELLS) {
                        if (!game.draft[game.my_index][s]
                            && game.draft_count[game.my_index] < N_EQUIPPED) {
                            game.draft[game.my_index][s] = 1;
                            game.draft_count[game.my_index]++;
                        } else if (game.draft[game.my_index][s]) {
                            game.draft[game.my_index][s] = 0;
                            game.draft_count[game.my_index]--;
                        }
                    } else if (k == SDLK_RETURN
                               && game.draft_count[game.my_index] == N_EQUIPPED) {
                        sync_draft(&game, game.my_index);
                    }
                } else if (game.state == ST_BATTLE) {
                    if (k == SDLK_SPACE && game.turn == game.my_index)
                        end_turn(&game, 0);
                    else if (k >= SDLK_1 && k <= SDLK_4)
                        game.sel_skill = (int)(k - SDLK_1);
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                int mx = ev.button.x, my = ev.button.y;
                if (game.state == ST_DRAFT) {
                    draft_click(&game, mx, my);
                } else if (game.state == ST_BATTLE) {
                    if (mx < GRID_W * TILE && my < GRID_H * TILE) {
                        int tx = mx / TILE, ty = my / TILE;
                        if (game.sel_skill >= 0) try_cast(&game, tx, ty);
                        else                    try_move(&game, tx, ty);
                    } else if (game.turn == game.my_index) {
                        int y0 = GRID_H * TILE;
                        int k;
                        for (k = 0; k < N_EQUIPPED; k++) {
                            SDL_Rect b = {10 + k*190, y0 + 30, 180, 56};
                            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h)
                                game.sel_skill = (game.sel_skill == k) ? -1 : k;
                        }
                    }
                }
            }
        }

        if (game.net.sock != SOCK_INVALID)
            net_poll(&game);
        anim_update(&game);

        SDL_SetRenderDrawColor(ren, 15, 15, 15, 255);
        SDL_RenderClear(ren);

        switch (game.state) {
        case ST_MENU:     draw_menu(ren, font, &game); break;
        case ST_PICK:     draw_pick(ren, font, &game); break;
        case ST_DRAFT:    draw_draft(ren, font, &game); break;
        case ST_BATTLE: {
            int a, b;
            for (b = 0; b < GRID_H; b++)
                for (a = 0; a < GRID_W; a++)
                    draw_tile(ren, a, b, game.blocked[b][a]);
            draw_fighter(ren, &game, 0, font);
            draw_fighter(ren, &game, 1, font);
            draw_skillbar(ren, &game, font);
            break;
        }
        case ST_GAMEOVER: {
            SDL_Color w = {255,255,255,255};
            char msg[64];
            snprintf(msg, sizeof msg, "%s gagne ! (Echap pour quitter)",
                     game.winner == game.my_index ? "Vous" : "Adversaire");
            draw_text(ren, font, msg, WIN_W/2 - 150, WIN_H/2, w);
            break;
        }
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (game.net.sock != SOCK_INVALID)
        net_close(game.net.sock);
    for (i = 0; i < N_ASSETS; i++)
        if (g_assets[i]) SDL_DestroyTexture(g_assets[i]);
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