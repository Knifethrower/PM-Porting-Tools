"""Usage:
  python3 -m unityport install <config.toml> <gamedir>   convert the game in place (resumable)
  python3 -m unityport survey <gamedir> [data folder]     report what a port of this game needs
"""
import os, sys


def main(argv):
    if len(argv) >= 3 and argv[0] == 'install':
        from . import config, engine
        cfg = config.load(argv[1])
        engine.Installer(os.path.abspath(argv[2]), cfg).run()
    elif len(argv) >= 2 and argv[0] == 'survey':
        from . import survey
        survey.run(os.path.abspath(argv[1]), argv[2] if len(argv) > 2 else None)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
