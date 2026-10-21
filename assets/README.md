# Assets du jeu

Les ressources sont facultatives, sauf indication contraire. En l'absence
d'un atlas ou d'une image de sort, le jeu conserve ses éléments de rendu de
secours.

## Interface et combat

| Fichier | Utilisation |
|---|---|
| `UI/OIG4.webp` | Atlas d'interface mis à jour. Le fond périphérique blanc est converti en transparence. L'en-tête et les barres de vie en sont découpés. |
| `tiles/tilesheet.png` | Sols, murs et vides des arènes procédurales. Les coordonnées sont dans `tiles/tilesheet.json`. |
| `character/character_cut_sheet.png` | 30 sprites de personnages, cellules 80×80, utilisés pour les aperçus de classe et les combattants. |
| `character/class_sprite_retrieval.json` | Descriptions visuelles et associations sémantiques locales classe-personnage avec niveau de confiance. |
| `character/spells_icon/spell4.webp` | Planche d'icônes de sorts. |
| `bubble_emotes.png` | Grille 16×16 d'emotes, cellules 12×13 pixels. |
| `VFX/Effect_BloodImpact/30fps/Frames/Effect_BloodImpact_1/` | 30 images PNG de l'effet d'impact joué au lancement d'un sort. |
| `BGM/` et `Sounds/` | Musiques de menu/combat et effets sonores. SDL_mixer doit pouvoir décoder les MP3. |

`SDL_image` doit inclure le support PNG et WebP. SDL_mixer doit inclure le
support MP3 pour lire les ressources audio. Les erreurs de chargement sont
affichées dans stderr; les ressources absentes ne font pas planter le jeu.

## Personnages ajoutés

Les captions de personnages sont indexées localement dans
`class_sprite_retrieval.json`, puis comparées aux rôles des classes et revues
visuellement. C'est une recherche sémantique curatée, pas un modèle d'embeddings
ni une identification officielle des classes DOFUS.

## Planche des personnages découpés

`character/character_cut_sheet.png` regroupe les 30 PNG de `character/cut/`
dans des cellules transparentes de 80×80 pixels. Les coordonnées et chemins
d'origine sont dans `character/character_cut_sheet.json`.
