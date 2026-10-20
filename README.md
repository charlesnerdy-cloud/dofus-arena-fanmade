# Dofus Arena P2P - Fanmade

Jeu de combat tactique 1v1 en Peer-to-Peer inspiré de Dofus Arena, en C / SDL2.

- P2P en UDP (port 7777) : `./dofus-arena host` / `./dofus-arena join <ip>`
- 12 classes jouables, 16 sorts chacune, draft de 4 sorts par partie
- Combat au tour par tour : grille 14×10, PA/PM, cooldowns, buffs
- Dégâts déterministes = zéro désynchronisation entre pairs
- Jouable sans aucun asset (rendu de secours)

## Compilation
    sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
    make

## Contrôles
- Déplacement : clic sur une case (coûte des PM)
- Sort : touche 1-4 puis clic sur la cible
- Fin de tour : Espace