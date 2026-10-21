# Dofus Arena P2P - Fanmade (1v1, 2v2, 5v5)

Jeu de combat tactique 1v1 en Peer-to-Peer inspiré de Dofus Arena, en C / SDL2.

- P2P en UDP (port 7777) : `./dofus-arena host` / `./dofus-arena join <ip>`
- Modes 1v1, 2v2 et 5v5 : chaque joueur controle une equipe T1/T2
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

## Ressources audio et interface
- L'interface utilise l'atlas `assets/UI/OIG4.webp` (fond blanc périphérique
  rendu transparent), notamment pour l'en-tête et les barres de vie.
- La planche demande le support WebP de SDL_image. Le mixage audio utilise
  SDL_mixer et ses décodeurs MP3; si un fichier audio n'est pas pris en charge,
  le jeu affiche le chemin et l'erreur dans la sortie standard d'erreur.
- Les personnages en combat et dans la sélection de classes sont extraits de
  `assets/character/character_cut_sheet.png`. Les associations de classes sont
  documentées dans `assets/character/class_sprite_retrieval.json` : elles
  utilisent une recherche sémantique locale sur des descriptions visuelles
  curatées, puis une vérification manuelle. Elles ne sont ni des résultats d'un
  modèle vectoriel ni des identifications officielles DOFUS.
- Pour produire un exécutable Windows natif, utilisez MinGW-w64 et les paquets
  SDL2, SDL2_image (PNG/WebP), SDL2_ttf et SDL2_mixer avec les codecs MP3. Sous
  Windows, `make` produit `dofus-arena.exe`; les DLL SDL doivent se trouver à
  côté de l'exécutable ou dans le `PATH`.