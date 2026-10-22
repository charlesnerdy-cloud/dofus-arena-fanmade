#!/usr/bin/env python3
"""Import d'un pack d'interface (ex: Humble Pixel "Pocket Inventory
Series 5 - Player Status") vers assets/UI/hud.png + hud.frames.json.

Usage :
    python3 tools/hud_import.py <dossier_ou_zip> [--out assets/UI]

Le script empaquete tous les PNG du pack dans une seule planche et ecrit
un manifeste JSON (nom -> rectangle).  Le jeu sait utiliser cette planche
pour la jauge de vie, le cadre de portrait et les pastilles PA/PM.

Noms attendus par le jeu (si absents, le jeu garde son dessin interne) :
    portrait_frame, hp_bar, hp_bar_fill, pa, pm, mana_bar, mana_bar_fill
"""
import argparse
import json
import os
import sys
import zipfile

from PIL import Image


def collect(sources):
    """Liste de (nom, chemin) triee par nom."""
    files = []
    for src in sources:
        if os.path.isdir(src):
            for root, _, names in os.walk(src):
                for n in sorted(names):
                    if n.lower().endswith('.png'):
                        files.append((n, os.path.join(root, n)))
        elif src.lower().endswith('.zip'):
            with zipfile.ZipFile(src) as z:
                tmp = os.path.join('/tmp', 'hud_import_zip')
                os.makedirs(tmp, exist_ok=True)
                z.extractall(tmp)
                for root, _, names in os.walk(tmp):
                    for n in sorted(names):
                        if n.lower().endswith('.png'):
                            files.append((n, os.path.join(root, n)))
        else:
            print('ignore (inexistant):', src, file=sys.stderr)
    files.sort(key=lambda x: x[0].lower())
    return files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('sources', nargs='+', help='dossier ou zip du pack')
    ap.add_argument('--out', default='assets/UI', help='dossier de sortie')
    args = ap.parse_args()

    files = collect(args.sources)
    if not files:
        print('Aucun PNG trouve.', file=sys.stderr)
        return 1

    images = []
    for name, path in files:
        try:
            im = Image.open(path).convert('RGBA')
        except Exception as exc:                      # noqa: BLE001
            print('skip', name, exc, file=sys.stderr)
            continue
        images.append((os.path.splitext(name)[0], im))
    if not images:
        print('Aucune image lisible.', file=sys.stderr)
        return 1

    # empaquetage en grille (largeur max 512 px)
    max_w = 512
    cell_w = max(im.width for _, im in images)
    cell_h = max(im.height for _, im in images)
    cols = max(1, max_w // max(1, cell_w))
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * cell_w, rows * cell_h), (0, 0, 0, 0))
    frames = []
    for i, (name, im) in enumerate(images):
        cx = (i % cols) * cell_w
        cy = (i // cols) * cell_h
        sheet.alpha_composite(im, (cx, cy))
        frames.append({'name': name, 'x': cx, 'y': cy,
                       'w': im.width, 'h': im.height})

    os.makedirs(args.out, exist_ok=True)
    out_png = os.path.join(args.out, 'hud.png')
    out_json = os.path.join(args.out, 'hud.frames.json')
    sheet.save(out_png)
    with open(out_json, 'w') as f:
        json.dump({'image': 'hud.png',
                   'image_size': {'width': sheet.width,
                                  'height': sheet.height},
                   'frames': frames}, f, indent=2)
    print(f'{len(frames)} images -> {out_png} ({sheet.size})')
    print('manifeste ->', out_json)
    print('noms disponibles :', ', '.join(f['name'] for f in frames[:20]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
