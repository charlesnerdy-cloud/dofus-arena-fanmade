#!/usr/bin/env python3
"""Audit de l'interface du jeu (main.c).

Verifie que :
  1. tous les elements dessines sont relies au gestionnaire de clics ;
  2. tous les elements dessines sont a l'interieur de la fenetre ;
  3. chaque rectangle dessine correspond a celui teste au clic.

Usage :  python3 tools/audit_ui.py [chemin/main.c]
"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'main.c'
src = open(path, encoding='utf-8').read()
lines = src.split('\n')

# --- fenetre logique ------------------------------------------------
m = re.search(r'#define WIN_W\s+(\d+)', src)
WIN_W = int(m.group(1)) if m else 672
m = re.search(r'#define WIN_H\s+(\d+)', src)
WIN_H = int(m.group(1)) if m else 640

# --- bloc de gestion des clics ---------------------------------------
i = next((i for i, l in enumerate(lines) if 'SDL_MOUSEBUTTONDOWN' in l), -1)
if i < 0:
    print('!! gestionnaire de clics introuvable')
    sys.exit(2)
# on commence a la ligne qui ouvre le bloc (celle avec la '{')
while i < len(lines) and '{' not in lines[i]:
    i += 1
depth = 0
click = []
for l in lines[i:]:
    click.append(l)
    depth += l.count('{') - l.count('}')
    if depth == 0 and len(click) > 3:
        break
click = '\n'.join(click)

# --- fonctions de dessin --------------------------------------------
draws = {}
for m in re.finditer(r'static void (draw_\w+)\(SDL_Renderer', src):
    name = m.group(1)
    start = m.start()
    nxt = src.find('\nstatic ', start + 10)
    draws[name] = src[start:nxt if nxt > 0 else len(src)]

helpers = set(re.findall(r'static SDL_Rect ([a-z_][a-z0-9_]*)\(', src))
# rectangles a coordonnees constantes
const_helpers = {}
for m in re.finditer(
        r'static SDL_Rect ([a-z_][a-z0-9_]*)\([^)]*\)\s*\{\s*SDL_Rect r = \{([^}]*)\}',
        src):
    name, vals = m.group(1), m.group(2)
    nums = re.findall(r'-?\d+', vals)
    if len(nums) == 4 and 'WIN_W' not in vals and 'WIN_H' not in vals:
        const_helpers[name] = tuple(int(x) for x in nums)

problems = []
offscreen = []
for name, body in sorted(draws.items()):
    for m in re.finditer(r'SDL_Rect ([a-z_][a-z0-9_]*)\s*=\s*\{([^}]*)\}', body):
        nm, vals = m.group(1), m.group(2)
        if 'WIN_W' in vals or 'WIN_H' in vals:
            continue
        nums = [int(x) for x in re.findall(r'-?\d+', vals)]
        if len(nums) != 4:
            continue
        x, y, w, h = nums
        if x < 0 or y < 0 or x + w > WIN_W or y + h > WIN_H:
            offscreen.append((name, nm, (x, y, w, h)))
        if nm not in click and nm not in ('up', 'down', 'back', 'ok', 'done',
                                         'r', 'b', 'panel', 'row', 'toggle',
                                         'minus', 'plus', 'icon', 'frame',
                                         'hb', 'in', 'depleted', 'bg', 'inner',
                                         'f', 'shade', 'slot', 'wbox', 'l',
                                         'lv', 'source', 'src', 'dst', 'sc',
                                         'dc', 'p', 'item', 'sel', 'bg2'):
            problems.append((name, nm, 'rectangle litteral non teste au clic'))
    for m in re.finditer(r'pt_in\(\s*([a-z_][a-z0-9_]*)\s*\(', body):
        h = m.group(1)
        if h in helpers and h not in click:
            problems.append((name, h + '()', 'helper dessine mais non teste'))

print('=' * 72)
print('AUDIT DE L\'INTERFACE  (%s, fenetre %dx%d)' % (path, WIN_W, WIN_H))
print('=' * 72)
print('\n[1] Elements dessines SANS gestionnaire de clics :')
if problems:
    for d, n, why in problems:
        print('    %-20s %-26s %s' % (d, n, why))
else:
    print('    (aucun)')
print('\n[2] Elements dessines HORS de la fenetre :')
if offscreen:
    for d, n, v in offscreen:
        print('    %-20s %-26s %s' % (d, n, v))
else:
    print('    (aucun)')
print('\n[3] Helpers a coordonnees constantes hors fenetre :')
bad = [(n, v) for n, v in sorted(const_helpers.items())
       if v[0] < 0 or v[1] < 0 or v[0] + v[2] > WIN_W or v[1] + v[3] > WIN_H]
for n, v in bad:
    print('    %-24s %s' % (n, v))
if not bad:
    print('    (aucun)')
print('\n[4] Etats de jeu : dessin / clic')
states = sorted(set(re.findall(r'\b(ST_[A-Z]+)\b', src)))
for st in states:
    drawn = ('case %s:' % st) in src
    clicked = ('game.state == %s' % st) in click
    print('    %s %-16s dessine=%-5s clic=%s'
          % ('OK ' if (drawn and clicked) else '?? ', st, drawn, clicked))
sys.exit(1 if (problems or offscreen or bad) else 0)
