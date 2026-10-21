# Assets du jeu

Les ressources sont facultatives, sauf indication contraire. En l'absence
d'un atlas ou d'une image de sort, le jeu conserve ses éléments de rendu de
secours.

## Interface et combat

| Fichier | Utilisation |
|---|---|
| `UI/UI.png` | Atlas d'interface transparent utilisé pour les cadres du menu, des écrans de préparation et du HUD de combat, ainsi que pour les barres de vie. |
| `tiles/tilesheet.png` | Sols, murs et vides des arènes procédurales. Les coordonnées sont dans `tiles/tilesheet.json`. |
| `character/Characters.png` | Planche transparente de 12 personnages animés, avec cinq séquences de quatre frames par ligne ; utilisée pour les aperçus de classe et les combattants. |
| `character/character_cut_sheet.png` | Ancienne planche de 30 sprites découpés, conservée comme ressource mais non utilisée par le jeu. |
| `character/class_sprite_retrieval.json` | Associations de l'ancienne planche découpée ; elles ne décrivent pas les lignes de `Characters.png`. |
| `character/spells_icon/spell4.webp` | Planche d'icônes de sorts. |
| `bubble_emotes.png` | Grille 16×16 d'emotes, cellules 12×13 pixels. |
| `VFX/Effect_BloodImpact/30fps/Frames/Effect_BloodImpact_1/` | 30 images PNG de l'effet d'impact joué au lancement d'un sort. |
| `BGM/` et `Sounds/` | Musiques de menu/combat et effets sonores. SDL_mixer doit pouvoir décoder les MP3. |

`SDL_image` doit inclure le support PNG et WebP. SDL_mixer doit inclure le
support MP3 pour lire les ressources audio. Les erreurs de chargement sont
affichées dans stderr; les ressources absentes ne font pas planter le jeu.

Deux sons originaux et discrets soutiennent un climat de jeu équilibré :
`Sounds/Balance_Calm.wav` joue un court signal de recentrage après des insultes
reconnues dans le chat ou un spam rapide de messages/emotes ; `Sounds/Balance_Encourage.wav`
accompagne les messages explicitement encourageants et les soins effectifs d'un
combattant allié. Le déclenchement est probabiliste (70 % pour un message
hostile reconnu, 65 % pour un message de soutien ou un soin réussi, 45 % pour
un spam détecté), avec un délai anti-répétition et un volume atténué. Les indices
sont locaux et non punitifs ; les actions de combat normales ne sont pas
évaluées comme toxiques.

Le bot solo peut utiliser un petit réseau neuronal embarqué (`tools/bot_model.h`).
Ses poids sont préentraînés avec des exemples tactiques synthétiques ; il
reclasse les attaques selon les dégâts, les mises KO et le coût en PA. Il
fonctionne hors ligne et ne s'entraîne pas sur les données des joueurs.

## Personnages ajoutés

Les captions de personnages sont indexées localement dans
`class_sprite_retrieval.json`, puis comparées aux rôles des classes et revues
visuellement. C'est une recherche sémantique curatée, pas un modèle d'embeddings
ni une identification officielle des classes DOFUS.

## Planche des personnages découpés

`character/Characters.png` contient 12 lignes de personnages et cinq séquences
de frames par ligne, mais leurs alignements et dimensions ne sont pas uniformes.
Utilisez `tools/sprite_frame_selector.py` pour découper les sprites à la main,
par composants connectés ou par grille espacée. Dans la liste des frames,
Ctrl-clic ajoute ou retire une découpe et Maj-clic sélectionne une plage ;
la suppression et la labellisation IA peuvent alors agir sur plusieurs frames.
Le seuil alpha et le filtre de pixels minimaux permettent d'ignorer les pixels
parasites ; le padding conserve les marges transparentes. En mode automatique,
l'analyse utilise le canal alpha
des planches transparentes ou estime la couleur de fond dominante sur les bords
des planches opaques ; un clic droit permet d'échantillonner une autre couleur
de fond. Vérifiez les aperçus et les emprises visibles avant d'exporter un
manifeste ou les PNG découpés. Les classes partagent parfois une ligne car la
planche contient moins de personnages que les 13 classes jouables.

`character/character_cut_sheet.png` regroupe les 30 PNG de `character/cut/`
dans des cellules transparentes de 80×80 pixels. Cette ancienne planche n'est
plus chargée par le jeu.

### Labellisation automatique des sprites

`Analyze & slice` propose aussi un nom descriptif pour chaque découpe avec le
modèle local BLIP (`Salesforce/blip-image-captioning-base`). Le modèle est
téléchargé depuis Hugging Face lors de la première labellisation ; les images
restent sur la machine pendant l'inférence. `AI label generic frames` remplit
les noms de découpe génériques ; les noms saisis manuellement sont conservés
par défaut. Cochez `Replace existing labels` pour relabelliser toutes les
découpes. La progression est affichée ; l'annulation ignore les suggestions
du lot en cours. BLIP est un modèle général, pas un modèle spécialisé en pixel
art : vérifiez ses suggestions, qui peuvent être imprécises sur les petites
icônes et les fragments de personnages.

Installez les dépendances optionnelles avec
`python -m pip install -r tools/requirements-ai.txt`. La détection et la
labellisation manuelle restent disponibles sans ces dépendances.
