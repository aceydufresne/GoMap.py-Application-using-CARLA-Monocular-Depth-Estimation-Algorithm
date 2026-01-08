import os
import traceback
from core import G

def load(app):
    log_path = os.path.expanduser("~/Documents/makehuman/v1py3/plugin_logs/test.txt")
    try:
        human = G.app.selectedHuman

        names = sorted(human.getModifierNames())

        out_path = os.path.expanduser("~/Downloads/VariablesList.txt")
        with open(out_path, "w", encoding="utf-8") as f:
            for n in names:
                f.write(n + "\n")

        with open(log_path, "a", encoding="utf-8") as log:
            log.write(f"wrote {len(names)} to {out_path}")

        print(f"wrote {len(names)} to {out_path}")

    except Exception:
        with open(log_path, "a", encoding="utf-8") as log:
            log.write("error: " + traceback.format_exc() + "\n")
        print("error")

def unload(app):
    pass