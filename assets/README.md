# Assets attendus (tous optionnels, repli en code si absents)
   Fichier | Taille | Rôle |
 |---|---|---|
 | tile_grass.png | 48×48 | Tuile de sol |
 | tile_blocked.png | 48×48 | Obstacle |
 | classes_sheet.png | 10 col × 10 lig, frames 32×32 | Spritesheet classes (Idle 0-3, Move 4-7, Attack 8-9) |
 | ui_panel.png | 672×160 | Fond du bandeau |
 | ui_hpbar.png | ~40×6 | Barre de vie |
 | ui_skillslot.png | 180×56 | Case de sort |
 | ui_skillslot_sel.png | idem | Case sélectionnée |
 | ui_endturn.png | libre | Bouton fin de tour |
 | font.ttf | — | Police UI |

Constantes à adapter dans main.c : SHEET_*, ANIM_*, CLASS_SHEET_ROW[].