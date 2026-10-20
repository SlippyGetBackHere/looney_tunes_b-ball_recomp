#!/usr/bin/env python3
"""4:3 frame baseline for Looney Tunes B-Ball (widescreen release gate, P16).

snesrecomp/docs/WIDESCREEN_PATTERNS.md, P16: with every enhancement off, the
picture must stay bit-identical while widescreen is developed. This runs a
scripted route through the built executable, hashes the frames it presents,
and records or compares the hash list. Only hashes are stored -- no images,
no ROM data.

Routes live in tools/baseline/*.route (the desktop host's --script format):

  attract     turbo; boot and the attract demo, including its court game
  match       turbo; title, random character select, a full exhibition match
              driven by a button loop, the result and stats screens
  charselect  real time; switching through the roster on character select

In turbo the host renders only frames with frame % 16 == 1, so turbo routes
sample exactly those; real-time routes sample every frame of their window.

The mod state beside the executable (mods/preloaded/state.toml) is swapped
for an empty one for the run, so no mod or coverage capture is active, and is
always restored afterwards.

  python tools/ws_baseline.py record  attract [--rom PATH] [--build build]
  python tools/ws_baseline.py compare attract [--rom PATH] [--build build]
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, 'tools', 'baseline')

# name: (turbo, first frame, last frame, step)
ROUTES = {
    'attract':    (True, 1, 7000, 16),
    'match':      (True, 3201, 46000, 16),
    'charselect': (False, 3200, 3800, 1),
}


def default_rom():
    cfg = os.path.join(ROOT, 'rom.cfg')
    if os.path.exists(cfg):
        return open(cfg, encoding='utf-8').read().strip()
    return os.environ.get('SNESRECOMP_ROM', '')


def run_route(name, rom, build):
    turbo, first, last, step = ROUTES[name]
    exe_dir = os.path.join(ROOT, build)
    exe = os.path.join(exe_dir, 'LooneyTunesBBallSNESRecomp.exe')
    state = os.path.join(exe_dir, 'mods', 'preloaded', 'state.toml')
    saved = state + '.ws_baseline_saved'
    frames_dir = tempfile.mkdtemp(prefix='ltbb_baseline_')
    had_state = os.path.exists(state)
    if had_state:
        shutil.copy2(state, saved)
    try:
        os.makedirs(os.path.dirname(state), exist_ok=True)
        with open(state, 'w', encoding='utf-8') as f:
            f.write('format_version = 1\n')
        env = dict(os.environ)
        for k in ('SNESRECOMP_TIER2_CAPTURE', 'SNESRECOMP_AUDIO_STATS',
                  'SNESRECOMP_FRAME_TIMING', 'SNESRECOMP_FORCE_FLOOR'):
            env.pop(k, None)
        env.update(SNESRECOMP_FRAME_BMP_DIR=frames_dir,
                   SNESRECOMP_FRAME_BMP_START=str(first),
                   SNESRECOMP_FRAME_BMP_END=str(last),
                   SNESRECOMP_FRAME_BMP_STEP=str(step))
        subprocess.run([exe, '--no-launcher',
                        '--config', os.path.join(BASE, 'config.ini'),
                        '--script', os.path.join(BASE, name + '.route'), rom],
                       cwd=exe_dir, env=env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        hashes = {}
        for fn in sorted(os.listdir(frames_dir)):
            if fn.startswith('frame_') and fn.endswith('.bmp'):
                with open(os.path.join(frames_dir, fn), 'rb') as f:
                    hashes[int(fn[6:-4])] = hashlib.sha1(f.read()).hexdigest()
        return hashes
    finally:
        if had_state:
            shutil.copy2(saved, state)
            os.remove(saved)
        else:
            os.remove(state)
        shutil.rmtree(frames_dir, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('mode', choices=('record', 'compare'))
    ap.add_argument('route', choices=sorted(ROUTES))
    ap.add_argument('--rom', default=default_rom())
    ap.add_argument('--build', default='build')
    a = ap.parse_args()
    if not a.rom or not os.path.exists(a.rom):
        sys.exit('ws_baseline: no ROM (pass --rom, set SNESRECOMP_ROM, or record it in rom.cfg)')
    path = os.path.join(BASE, a.route + '.sha1')
    hashes = run_route(a.route, a.rom, a.build)
    if not hashes:
        sys.exit('ws_baseline: the run produced no frames')
    if a.mode == 'record':
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            for fr in sorted(hashes):
                f.write('%d %s\n' % (fr, hashes[fr]))
        print('%s: recorded %d frames (%d..%d)' % (a.route, len(hashes), min(hashes), max(hashes)))
        return
    ref = {}
    for line in open(path, encoding='utf-8'):
        fr, h = line.split()
        ref[int(fr)] = h
    missing = sorted(set(ref) - set(hashes))
    extra = sorted(set(hashes) - set(ref))
    diff = sorted(fr for fr in set(ref) & set(hashes) if ref[fr] != hashes[fr])
    print('%s: %d frames compared, %d differ, %d missing, %d extra'
          % (a.route, len(set(ref) & set(hashes)), len(diff), len(missing), len(extra)))
    if diff:
        print('  first differing frames:', diff[:10])
    if missing or extra:
        print('  missing:', missing[:5], 'extra:', extra[:5])
    sys.exit(1 if diff or missing or extra else 0)


if __name__ == '__main__':
    main()
