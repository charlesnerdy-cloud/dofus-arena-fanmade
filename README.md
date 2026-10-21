# Dofus Arena P2P - Fanmade (1v1, 2v2, 5v5)

Jeu de combat tactique 1v1 en Peer-to-Peer inspiré de Dofus Arena, en C / SDL2.

- P2P en UDP (port 7777) : `./dofus-arena host` / `./dofus-arena join <ip>`
- Modes 1v1, 2v2 et 5v5 : chaque joueur controle une equipe T1/T2
- Mode solo : jouez contre une IA classique ou un bot neuronal embarqué qui affine le choix des attaques
- 13 classes, 16 sorts chacune, draft de 4 sorts par personnage
- Arènes générées aléatoirement (9–14 × 7–10 cases), contours et dimensions variables,
  décor de donjon tiré de `assets/tiles/tilesheet.png`
- PA/PM, cooldowns, buffs et choix des caracteristiques elementaires
- Ordre global d'initiative decroissante ; Air determine l'initiative, les egalites
  sont departagees par le numero de roster. Un sort special offensif applique un
  malus Air temporaire de 10 points.
- Favoris persistants de combinaisons classe + panoplie
- Dégâts déterministes = zéro désynchronisation entre pairs
- Jouable sans aucun asset (rendu de secours)

## Compilation
    sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev
    make

## Installation
    sudo make install

The executable is installed to `/usr/local/bin` by default. To choose a
different prefix, use `sudo make install PREFIX=/path/to/prefix`.

## Partie en reseau
- L'hote choisit le format **1v1**, **2v2** ou **5v5**, puis **CREER UNE PARTIE - HEBERGER** ; l'autre joueur saisit son
  adresse IPv4 puis choisit **REJOINDRE UNE PARTIE**.
- Chaque joueur prepare chaque personnage de son equipe (classe, 4 sorts,
  panoplie et 100 points a repartir entre Air, Feu, Terre et Eau). Les favoris
  se sauvegardent depuis l'ecran de panoplie et restent accessibles au menu.
- Les combattants jouent un par un, par initiative decroissante ; un tour peut
  donc etre suivi d'un autre combattant de la meme equipe. Le combattant actif
  est encadre en vert pour votre equipe et en rouge pour l'equipe adverse.
- Chaque combat génère une arène de taille et de forme variables, dans les
  limites de la zone de jeu ; les deux pairs reproduisent la même carte à partir
  de la graine envoyée par l'hôte. Les textures proviennent de la planche
  `assets/tiles/tilesheet.png`.
- Les caracteristiques elementaires sont reparties par pas de 5 (budget de
  100 points par personnage). Dans cette version, Air gouverne l'initiative ;
  les degats elementaires et passifs propres aux classes ne sont pas encore
  calcules, car les sorts du prototype ne portent pas de type elementaire.
- Le jeu utilise UDP sur le port fixe **7777**. En reseau local, utilisez
  l'adresse locale de l'hote.
- Pour jouer sur Internet, l'hote doit rediriger le port **UDP 7777** vers son
  ordinateur dans sa box et autoriser le jeu dans son pare-feu, puis partager
  son adresse IPv4 publique. Il s'agit d'une connexion directe, sans relais :
  la disponibilite depend donc de la box et du fournisseur d'acces.
- Si la connexion est perdue, les joueurs reviennent au menu. **RECONNECTER**
  relance l'attente/la connexion ; une nouvelle selection de partie est
  necessaire, car l'etat d'un combat interrompu n'est pas restaure.

## Contrôles
- Déplacement : clic sur une case (coûte des PM)
- Sort : touche 1-4 puis clic sur la cible
- Fin de tour : Espace
- Emotes : `E`, puis `1` à `8` ou clic sur une emote
- Menu en combat : `Échap` ou le bouton **MENU** du bandeau. Il permet de
  reprendre, revenir au menu principal en se déconnectant, abandonner (avec
  confirmation) ou quitter le jeu. **Abandonner** annonce immédiatement la
  victoire à l'équipe adverse ; les règles de tour et le combat continuent
  normalement tant qu'aucune de ces actions n'est choisie.

## Ressources audio et interface
- L'interface charge l'atlas transparent `assets/UI/UI.png` pour les cadres
  du menu, des ecrans de preparation et du HUD de combat, ainsi que pour les
  barres de vie. Le menu **OPTIONS** regroupe le volume de la musique et des
  effets, le mode daltonien, la reduction des flashs et les sous-titres des
  effets muets. La fenetre reste en taille fixe (672 x 640).
- Le mixage audio utilise
  SDL_mixer et ses décodeurs MP3; si un fichier audio n'est pas pris en charge,
  le jeu affiche le chemin et l'erreur dans la sortie standard d'erreur.
- Des sons discrets de pacification et d'encouragement répondent avec une
  probabilité modérée aux insultes/spams du chat, aux messages de soutien et
  aux soins réussis ; les actions de combat normales ne sont pas évaluées.
- En solo, le bouton **BOT : CLASSIQUE / RESEAU NEURONAL** permet de choisir
  l'adversaire. Le petit réseau préentraîné est embarqué dans le binaire,
  fonctionne hors ligne et classe les attaques selon les dégâts, les mises
  KO et le coût en PA. Son entraînement reproductible sur exemples tactiques
  synthétiques est disponible avec `python tools/train_bot_model.py` après
  `python -m pip install numpy`.
  Il n'enregistre ni n'apprend à partir des parties des joueurs. Après un
  combat solo, un conseil général et bienveillant s'affiche pour aider à
  réfléchir aux choix de portée et de sorts.
- Les personnages en combat et dans la sélection de classes utilisent
  `assets/character/Characters.png` : une planche transparente de 12 lignes,
  avec cinq séquences de quatre frames par ligne. Les trois premières font
  14×16 pixels, les deux dernières 16×16 ; chaque frame est découpée à sa
  position réelle et centrée sans étirement horizontal. Les classes sont
  associées aux lignes par ressemblance visuelle ; certaines partagent une ligne
  et ces associations ne sont pas des identifications officielles DOFUS.
- Pour produire un exécutable Windows natif, utilisez MinGW-w64 et les paquets
  SDL2, SDL2_image (PNG/WebP), SDL2_ttf et SDL2_mixer avec les codecs MP3. Sous
  Windows, `make` produit `dofus-arena.exe`; les DLL SDL doivent se trouver à
  côté de l'exécutable ou dans le `PATH`.

## Outil intelligent de découpe des sprites

L'outil Python permet de sélectionner des frames avec des rectangles
indépendants, d'inspecter chaque découpe et d'exporter les coordonnées JSON ou
les images PNG. Il demande Python avec Tkinter et Pillow :

    python -m pip install Pillow
    python tools/sprite_frame_selector.py

On peut aussi ouvrir une autre planche en la passant en argument. Dans l'outil,
glissez sur l'image pour sélectionner une frame, ajustez le zoom avec la
molette ou les boutons `+`/`-`, et faites un clic milieu/droit-glisser pour
déplacer la vue. Un clic droit ouvre le menu contextuel des découpes
intelligentes, des actions manuelles, de l'annulation et de l'export ; après
avoir sélectionné une zone, utilisez-le pour lancer rapidement **Smart grid**
ou la détection des objets connectés. Donnez un nom à la sélection puis cliquez
**Add selection**.
Pour découper automatiquement plusieurs frames, sélectionnez une seule bande
horizontale, réglez **Auto-slice gap** si des parties d'une frame sont séparées,
puis cliquez **Auto-slice selection**. Vérifiez les rectangles et les aperçus
avant d'exporter : la détection utilise la transparence, et chaque frame peut
avoir une taille ou un alignement différent.
Pour ajouter une découpe manuellement, saisissez ses coordonnées **X/Y** et sa
largeur/hauteur **W/H**, puis cliquez **Add manual slice**. **Lock proportion**
conserve le rapport largeur/hauteur quand vous modifiez l'une des dimensions.
**Preview** montre le rectangle sans l'ajouter ; sélectionnez une découpe
existante pour modifier ses coordonnées, puis cliquez **Apply to selected**.
**Undo** ou `Ctrl+Z` annule les dernières créations, suppressions, détections et
modifications.
**Smart grid** détecte les bandes horizontales et verticales d'une grille
espacée. **Auto-slice objects** sépare les composants connectés ; choisissez
une connectivité 4 ou 8 pour décider si les pixels en diagonale sont liés.
**Alpha threshold** ignore les pixels transparents ou très pâles, **Min pixels**
filtre les petits débris, et **Padding** conserve une marge transparente autour
des découpes détectées. L'aperçu affiche aussi l'emprise visible pour aider à
repérer un rectangle trop serré ou trop large.
**Analyze & slice** choisit automatiquement entre l'analyse de grille et les
composants connectés. Le mode **Foreground: auto** utilise la transparence
lorsqu'elle est présente, ou estime le fond dominant sur les bords pour les
planches opaques ; réglez **Color tolerance** pour inclure ou exclure davantage
de couleurs proches du fond. Vous pouvez aussi faire un clic droit sur un pixel
de fond puis choisir **Use clicked pixel as background** pour l'échantillonner.