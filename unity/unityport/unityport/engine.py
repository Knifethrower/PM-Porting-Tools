"""The installer engine: runs the configured steps in order, each one at most once.

Everything is recorded in the state file (<data>/<state_file>): finished steps, finished files
per step, and the texture sizes that were scaled (for the sprites step). Every rewritten file is
written as <file>.tmp first and moved into place by commit(), which records the move in the
state file before doing it, so an interrupted run (power off, patcher killed) resumes where it
stopped and never leaves a half-written game file behind.
"""
import glob, importlib, json, os, re, shutil, sys, time

T0 = time.time()


def log(msg):
    m, s = divmod(int(time.time() - T0), 60)
    print(f'[{m:02d}:{s:02d}] {msg}', flush=True)


class Installer:
    def __init__(self, gamedir, cfg):
        self.gamedir = gamedir
        self.cfg = cfg
        self.data = os.path.join(gamedir, cfg['game']['data'])
        self.state_path = os.path.join(self.data, cfg['game']['state_file'])
        self.state = {}
        self.log = log

    # ------------------------------------------------------------------ state
    def load_state(self):
        if os.path.exists(self.state_path):
            with open(self.state_path) as f:
                self.state = json.load(f)
        self.state.setdefault('version', self.cfg['game']['installed_version'])
        self.state.setdefault('steps', [])
        self.state.setdefault('files', {})       # step -> list of finished files
        self.state.setdefault('scaled', {})      # file -> {path_id: [old_w, old_h, new_w, new_h]}

    def save_state(self):
        tmp = self.state_path + '.tmp'
        with open(tmp, 'w') as f:
            json.dump(self.state, f)
            f.flush(); os.fsync(f.fileno())
        os.replace(tmp, self.state_path)

    def done(self, step):
        return step in self.state['steps']

    def finish(self, step):
        self.state['steps'].append(step)
        self.save_state()

    def commit(self, step, path, extra_tmp=()):
        """Move <path>.tmp (and extra .tmp files) into place, then record the file as done.
        A crash between the renames is finished by recover() on the next run."""
        self.state['committing'] = {'step': step, 'files': [path, *extra_tmp]}
        self.save_state()
        self._finish_commit()

    def _finish_commit(self):
        c = self.state.pop('committing', None)
        if not c:
            return
        for p in reversed(c['files']):           # .resS / .resource before .assets
            if os.path.exists(p + '.tmp'):
                os.replace(p + '.tmp', p)
        self.state['files'].setdefault(c['step'], []).append(os.path.basename(c['files'][0]))
        self.save_state()

    def recover(self):
        if 'committing' in self.state:
            log(f"finishing interrupted write of {os.path.basename(self.state['committing']['files'][0])}")
            self._finish_commit()
        for p in glob.glob(os.path.join(self.data, '*.tmp')) + glob.glob(os.path.join(self.data, '*', '*.tmp')):
            if p != self.state_path + '.tmp':
                os.remove(p)                       # half-written output of an interrupted step

    def file_done(self, step, path):
        return os.path.basename(path) in self.state['files'].get(step, [])

    def skip_file(self, step, path, save=True):
        """Record a file as finished without writing it (nothing to change)."""
        self.state['files'].setdefault(step, []).append(os.path.basename(path))
        if save:
            self.save_state()

    def write_tmp(self, path, data):
        with open(path + '.tmp', 'wb') as f:
            f.write(data); f.flush(); os.fsync(f.fileno())

    # ------------------------------------------------------------------ helpers
    def files(self, patterns, sort='name'):
        """Files in the data folder matching the globs, in pattern order (each pattern sorted),
        without duplicates."""
        out = []
        for pat in patterns:
            found = glob.glob(os.path.join(self.data, pat))
            if sort == 'number':
                found.sort(key=lambda p: (int(''.join(c for c in os.path.basename(p) if c.isdigit()) or 0), p))
            else:
                found.sort()
            out += [p for p in found if p not in out and os.path.isfile(p)]
        return out

    def need_space(self, nbytes, what):
        free = shutil.disk_usage(self.data).free
        if free < nbytes:
            sys.exit(f'Not enough free space to {what}: {free >> 20} MB free, '
                     f'need about {nbytes >> 20} MB.')

    # ------------------------------------------------------------------ main
    def run(self):
        from . import layout
        name = self.cfg['game']['name']
        log(f'{name}: first-run setup')
        layout.run(self)
        self.load_state()
        self.recover()
        for step in self.cfg['steps']:
            if self.done(step):
                continue
            log(f'--- {step} ---')
            importlib.import_module(f'.{step}', __package__).run(self)
            self.finish(step)
        with open(os.path.join(self.gamedir, '.installed'), 'w') as f:
            f.write(f"{self.cfg['game']['installed_version']}\n")
        log('setup complete')
