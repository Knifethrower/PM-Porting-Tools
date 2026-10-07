#!/usr/bin/env python3
"""Differential run of gt: every test on the reference (Mesa desktop GL) and on gl4es variants, then compare.

  gtrun.py [-v ship,ship-asan] [-m SUBSTR] [-j JOBS] [-c CORPUSDIR] [-o OUTDIR] [--rerun-ref]

OUTDIR (default ~/gl4es-test/results/<date>) gets <variant>/ (raw test output), report.md, results.json
and diff/ (ref | variant | difference pictures of failing image tests). The reference run is reused when
present unless --rerun-ref. Each job has its own Xvfb; a crashing test is isolated and the batch resumes.
"""
import argparse, json, os, re, struct, subprocess, sys, time, zlib, collections, datetime
from concurrent.futures import ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GT = os.environ.get('GT', os.path.expanduser('~/gl4es-test'))
BIN = os.path.join(GT, 'bin', 'gt')
# own copy of Xvfb: other scripts' "pkill -x Xvfb" must not take the test servers down
XVFB = os.path.join(GT, 'bin', 'gtXvfb')
BATCH = 40
TIMEOUT = 600


def variant_env(v):
    out = subprocess.run(['bash', '-c', f'. "{HERE}/env.sh"; gl4es_env {v}'], capture_output=True, text=True).stdout.split()
    env = dict(os.environ)
    for kv in out:
        k, _, val = kv.partition('=')
        env[k] = val
    return env


def start_xvfb(disp):
    # first free display from disp on, so parallel gtrun.py calls never share (and later kill) an X server
    while os.path.exists(f'/tmp/.X{disp}-lock') or os.path.exists(f'/tmp/.X11-unix/X{disp}'):
        disp += 1
    p = subprocess.Popen([XVFB, f':{disp}', '-screen', '0', '1280x1024x24', '-nolisten', 'tcp'],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        if subprocess.run(['xdpyinfo', '-display', f':{disp}'], capture_output=True).returncode == 0:
            break
        time.sleep(0.1)
    return p, f':{disp}'


def fname(name):
    return re.sub(r'[/ ]', '_', name)


def display_ok(display):
    return subprocess.run(['xdpyinfo', '-display', display], capture_output=True).returncode == 0


def run_batch(names, outdir, env, x, corpus, log):
    """Runs names; returns {name: 'done' | 'crash' | 'timeout' | 'not-run'}; stderr per test goes to
    <outdir>/<test>.err. x = [Popen, display] of this job's Xvfb, restarted if it died (another script's pkill)."""
    status = {}
    todo = list(names)
    stuck = 0
    while todo:
        if not display_ok(x[1]):
            log(f'  Xvfb {x[1]} gone, restarting')
            x[0].kill(); x[0].wait()
            x[0], x[1] = start_xvfb(int(x[1][1:]))
        e = dict(env); e['DISPLAY'] = x[1]
        cmd = [BIN, '-o', outdir] + (['-c', corpus] if corpus else []) + todo
        try:
            p = subprocess.run(cmd, env=e, capture_output=True, timeout=TIMEOUT)
            rc, err = p.returncode, p.stderr.decode('utf-8', 'replace')
        except subprocess.TimeoutExpired as ex:
            rc, err = 'timeout', (ex.stderr or b'').decode('utf-8', 'replace')
        # split stderr by test markers
        cur = None; chunks = collections.defaultdict(list)
        for line in err.splitlines():
            if line.startswith('### '):
                cur = line[4:].strip(); continue
            if cur:
                chunks[cur].append(line)
        for n, lines in chunks.items():
            with open(os.path.join(outdir, fname(n) + '.err'), 'w') as f:
                f.write('\n'.join(lines))
        # which ran to the end
        rest = []
        for i, n in enumerate(todo):
            tf = os.path.join(outdir, fname(n) + '.txt')
            txt = open(tf, errors='replace').read() if os.path.exists(tf) else ''
            if txt.rstrip().endswith('done'):
                status[n] = 'done'
            elif txt.startswith('test '):
                status[n] = 'timeout' if rc == 'timeout' else 'crash'
                log(f'  {status[n]}: {n} (rc {rc})')
                rest = todo[i + 1:]
                break
            else:
                rest = todo[i:]
                if rc == 0:   # not run although the binary ended normally: unknown test name
                    status[n] = 'missing'; rest = todo[i + 1:]
                break
        if rest and rest[0] == todo[0]:   # the batch did not even start its first test
            stuck += 1
            if stuck >= 3:
                log(f'  batch not starting (rc {rc}): {todo[0]} ... marked not-run; {err.strip()[-200:]}')
                for n in rest:
                    status[n] = 'not-run'
                break
        else:
            stuck = 0
        todo = rest
    return status


def run_variant(v, tests, out, jobs, corpus, log):
    vdir = os.path.join(out, v)
    import shutil; shutil.rmtree(vdir, ignore_errors=True)   # no stale .err/.txt from an earlier run
    os.makedirs(vdir, exist_ok=True)
    env = variant_env(v)
    batches = [tests[i:i + BATCH] for i in range(0, len(tests), BATCH)]
    xs = [list(start_xvfb(100 + 8 * i)) for i in range(jobs)]
    status = {}
    done = [0]
    t0 = time.time()

    def work(ib):
        i, b = ib
        st = run_batch(b, vdir, env, xs[i % jobs], corpus, log)
        done[0] += 1
        if done[0] % 20 == 0 or done[0] == len(batches):
            log(f'  {v}: {done[0]}/{len(batches)} batches, {time.time() - t0:.0f} s')
        return st

    try:
        with ThreadPoolExecutor(jobs) as ex:
            for st in ex.map(work, enumerate(batches)):
                status.update(st)
    finally:
        for p, _ in xs:
            p.kill(); p.wait()
    json.dump(status, open(os.path.join(vdir, 'status.json'), 'w'))
    return status


def parse(path):
    r = {'vals': {}, 'imgs': [], 'info': [], 'skip': None}
    if not os.path.exists(path):
        return r
    for line in open(path, errors='replace'):
        p = line.rstrip('\n').split(' ')
        if p[0] == 'val':
            r['vals'][p[1]] = (float(p[2]), [float(x) for x in p[3:]])
        elif p[0] == 'img':
            r['imgs'].append((p[1], int(p[2]), int(p[3]), int(p[4]), float(p[5])))
        elif p[0] == 'info':
            r['info'].append(' '.join(p[1:]))
        elif p[0] == 'skip':
            r['skip'] = ' '.join(p[1:])
    return r


def png(path, rgb):
    h, w, _ = rgb.shape
    raw = b''.join(b'\0' + rgb[y].tobytes() for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def img_compare(a, b, w, h, tol):
    A = np.frombuffer(open(a, 'rb').read(), np.uint8).reshape(h, w, 4).astype(int)
    B = np.frombuffer(open(b, 'rb').read(), np.uint8).reshape(h, w, 4).astype(int)
    d = np.abs(A - B).max(axis=2)
    # a pixel also passes when it matches a reference pixel within its 3x3 neighbourhood: rasterisation edges
    # one pixel off (rounding in gl4es's transforms) are noise; wrong colours over areas still fail
    best = d.copy()
    P = np.pad(A, ((1, 1), (1, 1), (0, 0)), mode='edge')
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            best = np.minimum(best, np.abs(P[dy:dy + h, dx:dx + w] - B).max(axis=2))
    bad = best > tol
    return bad.mean(), int(d.max()), A, B, bad


def diff_picture(path, A, B, bad):
    k = max(1, 192 // A.shape[1])   # small test pictures enlarged (nearest) so they can be read
    if k > 1:
        A, B, bad = (X.repeat(k, 0).repeat(k, 1) for X in (A, B, bad))
    def rgb(X):   # colour over a checkerboard showing alpha
        h, w, _ = X.shape
        cb = ((np.indices((h, w)).sum(0) // 8) % 2 * 60 + 100)[..., None]
        a = X[..., 3:4] / 255.0
        return (X[..., :3] * a + cb * (1 - a)).astype(np.uint8)
    D = np.zeros(A.shape[:2] + (3,), np.uint8); D[bad] = (255, 0, 255)
    D[~bad] = (np.abs(A - B)[~bad][:, :3] * 20).clip(0, 255)
    sep = np.full((A.shape[0], 4, 3), 255, np.uint8)
    pic = np.concatenate([rgb(A), sep, rgb(B), sep, D], axis=1)[::-1]   # GL rows are bottom-up
    png(path, np.ascontiguousarray(pic))


SAN = re.compile(r'(ERROR: AddressSanitizer: [\w-]+|runtime error: .*|LeakSanitizer)')


def compare(v, tests, out, ref_status, st):
    rdir, vdir = os.path.join(out, 'ref'), os.path.join(out, v)
    import shutil; shutil.rmtree(os.path.join(out, 'diff', v), ignore_errors=True)
    os.makedirs(os.path.join(out, 'diff', v), exist_ok=True)
    res = {}
    ndiff = 0
    for n in tests:
        f = fname(n)
        rs, vs = ref_status.get(n), st.get(n)
        R, V = parse(os.path.join(rdir, f + '.txt')), parse(os.path.join(vdir, f + '.txt'))
        e = {'status': 'pass', 'why': []}
        err = open(os.path.join(vdir, f + '.err'), errors='replace').read() if os.path.exists(os.path.join(vdir, f + '.err')) else ''
        san = sorted(set(m.group(1)[:120] for m in SAN.finditer(err)))
        if rs != 'done':
            e['status'] = 'ref-broken'; e['why'].append(f'reference {rs}')
        elif R['skip']:
            e['status'] = 'skip'; e['why'].append(R['skip'])
        elif any(k.endswith('-accepted') and vals[1] and vals[1][0] == 0 for k, vals in R['vals'].items()):
            e['status'] = 'ref-rejected'
        elif vs != 'done':
            e['status'] = vs or 'not-run'
            e['why'].append(err.strip().splitlines()[-1][:200] if err.strip() else '')
        else:
            for k, (tol, rv) in R['vals'].items():
                if k not in V['vals']:
                    e['status'] = 'fail'; e['why'].append(f'{k}: missing'); continue
                vv = V['vals'][k][1]
                if len(vv) != len(rv):
                    e['status'] = 'fail'; e['why'].append(f'{k}: {len(vv)} values vs {len(rv)}'); continue
                a, b = np.array(rv), np.array(vv)
                both_nan = np.isnan(a) & np.isnan(b)
                bad = ~both_nan & ~(np.abs(a - b) <= tol + 1e-6)
                if bad.any():
                    i = int(np.argmax(bad))
                    e['status'] = 'fail'
                    e['why'].append(f'{k}: {int(bad.sum())}/{len(a)} differ, e.g. [{i}] ref {a[i]:.4g} got {b[i]:.4g}')
            for (rf, w, h, tol, mf), vi in zip(R['imgs'], V['imgs'] + [None] * len(R['imgs'])):
                if vi is None:
                    e['status'] = 'fail'; e['why'].append(f'{rf}: no picture'); continue
                frac, mx, A, B, bad = img_compare(os.path.join(rdir, rf), os.path.join(vdir, vi[0]), w, h, tol)
                if frac > mf:
                    e['status'] = 'fail'
                    e['why'].append(f'{rf}: {frac * 100:.2f}% of pixels differ (max {mx}, tol {tol})')
                    if ndiff < 3000:
                        diff_picture(os.path.join(out, 'diff', v, rf.replace('.rgba', '.png')), A, B, bad); ndiff += 1
        if san:
            e['sanitizer'] = san
            if e['status'] == 'pass':
                e['status'] = 'sanitizer'
        ri = set(x for x in R['info'] if x.startswith('glerror')); vi_ = set(x for x in V['info'] if x.startswith('glerror'))
        if ri != vi_:
            e['glerrors'] = {'ref': sorted(ri), 'got': sorted(vi_)}
        prog = [x for x in V['info'] if x.startswith(('progerr', 'compile-log', 'link-log'))]
        if prog:
            e['progerr'] = prog[:3]
        e['feat'] = [x[5:] for x in V['info'] if x.startswith('feat ')] or [x[5:] for x in R['info'] if x.startswith('feat ')]
        res[n] = e
    return res


def attribution(res, prefix):
    """Fuzz seeds: which features are over-represented among failures"""
    seeds = {n: e for n, e in res.items() if n.startswith(prefix) and e['status'] in ('pass', 'fail', 'crash', 'timeout', 'sanitizer')}
    if not seeds:
        return []
    fail = {n for n, e in seeds.items() if e['status'] != 'pass'}
    base = len(fail) / len(seeds)
    cnt = collections.Counter(); fcnt = collections.Counter()
    for n, e in seeds.items():
        for f in set(e['feat']):
            cnt[f] += 1
            if n in fail:
                fcnt[f] += 1
    rows = []
    for f, c in cnt.items():
        if c >= 5 and fcnt[f]:
            rate = fcnt[f] / c
            if rate > base * 1.5 or rate > 0.8:
                rows.append((rate, f, fcnt[f], c))
    rows.sort(reverse=True)
    return [(f, k, c, r) for r, f, k, c in rows[:25]], base, len(seeds)


def group_of(n):
    p = n.split('/')
    return '/'.join(p[:2]) if p[0] in ('arb', 'state', 'corpus') else p[0]


def report(out, variants, allres, tests):
    L = [f'# gl4es differential test report', '', f'{datetime.datetime.now():%Y-%m-%d %H:%M}, {len(tests)} tests, '
         f'reference = Mesa llvmpipe desktop GL, gl4es on Mesa llvmpipe GLES 2 with the Mali capability profile.', '']
    for v in variants:
        res = allres[v]
        st = collections.Counter(e['status'] for e in res.values())
        L += [f'## {v}', '', '| status | tests |', '|--|--|'] + [f'| {k} | {c} |' for k, c in st.most_common()] + ['']
        groups = collections.defaultdict(collections.Counter)
        for n, e in res.items():
            groups[group_of(n)][e['status']] += 1
        L += ['| group | pass | fail | crash | sanitizer | other |', '|--|--|--|--|--|--|']
        for g in sorted(groups):
            c = groups[g]
            L.append(f"| {g} | {c['pass']} | {c['fail']} | {c['crash'] + c['timeout']} | {c['sanitizer']} | "
                     f"{sum(c.values()) - c['pass'] - c['fail'] - c['crash'] - c['timeout'] - c['sanitizer']} |")
        L.append('')
        for pfx in ('ffp/', 'arb/fp/', 'arb/vp/', 'arb/pair/'):
            a = attribution(res, pfx)
            if a and a[0]:
                rows, base, n = a
                L += [f'### {pfx}: features over-represented in failures (base fail rate {base * 100:.1f}% of {n})', '',
                      '| feature | failing | with feature | fail rate |', '|--|--|--|--|']
                L += [f'| {f} | {k} | {c} | {k / c * 100:.0f}% |' for f, k, c, r in rows] + ['']
        L += ['### Failures', '']
        for n in sorted(res):
            e = res[n]
            if e['status'] in ('pass', 'skip', 'ref-rejected'):
                continue
            extra = ''
            if e.get('sanitizer'): extra += ' | ' + '; '.join(e['sanitizer'])
            if e.get('progerr'): extra += ' | ' + e['progerr'][0][:150]
            L.append(f"- `{n}` **{e['status']}** {'; '.join(e['why'])[:300]}{extra}")
        L.append('')
    open(os.path.join(out, 'report.md'), 'w').write('\n'.join(L))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', default='ship')
    ap.add_argument('-m', default=None)
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-c', default=None)
    ap.add_argument('-o', default=None)
    ap.add_argument('--rerun-ref', action='store_true')
    ap.add_argument('--compare-only', action='store_true', help='re-score the outputs already in -o (new tolerances), run nothing')
    a = ap.parse_args()
    out = a.o or os.path.join(GT, 'results', datetime.datetime.now().strftime('%Y%m%d-%H%M'))
    os.makedirs(out, exist_ok=True)
    logf = open(os.path.join(out, 'run.log'), 'a')
    def log(s):
        print(s, flush=True); logf.write(s + '\n'); logf.flush()
    cmd = [BIN, '-l'] + (['-m', a.m] if a.m else []) + (['-c', a.c] if a.c else [])
    tests = subprocess.run(cmd, capture_output=True, text=True).stdout.split('\n')
    tests = [t for t in tests if t]
    log(f'{len(tests)} tests -> {out}')
    variants = a.v.split(',')
    rs_path = os.path.join(out, 'ref', 'status.json')
    if a.compare_only:
        ref_status = json.load(open(rs_path))
        tests = [t for t in tests if t in ref_status]
        allres = {}
        for v in variants:
            st = json.load(open(os.path.join(out, v, 'status.json')))
            allres[v] = compare(v, tests, out, ref_status, st)
            json.dump(allres[v], open(os.path.join(out, f'results-{v}.json'), 'w'), indent=0)
        report(out, variants, allres, tests)
        log(f'report (re-scored): {out}/report.md')
        return
    if os.path.exists(rs_path) and not a.rerun_ref:
        ref_status = json.load(open(rs_path))
        missing = [t for t in tests if t not in ref_status]
        if missing:
            ref_status.update(run_variant('ref', missing, out, a.j, a.c, log))
            json.dump(ref_status, open(rs_path, 'w'))
    else:
        ref_status = run_variant('ref', tests, out, a.j, a.c, log)
    allres = {}
    for v in variants:
        st = run_variant(v, tests, out, a.j, a.c, log)
        allres[v] = compare(v, tests, out, ref_status, st)
        json.dump(allres[v], open(os.path.join(out, f'results-{v}.json'), 'w'), indent=0)
    report(out, variants, allres, tests)
    log(f'report: {out}/report.md')


if __name__ == '__main__':
    main()
