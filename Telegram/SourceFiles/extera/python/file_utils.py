import os

import _compat
import extera_host as _host


def get_plugins_dir():
    return _compat._File(_host.PLUGINS_DIR)


def get_files_dir():
    return _compat._files_dir()


def get_cache_dir():
    return _compat._files_dir()


def ensure_dir_exists(path):
    os.makedirs(str(getattr(path, "_path", path)), exist_ok=True)
    return True


def read_file(path, mode="r"):
    with open(str(getattr(path, "_path", path)), mode,
              **({} if "b" in mode else {"encoding": "utf-8"})) as f:
        return f.read()


def write_file(path, data, mode="w"):
    path = str(getattr(path, "_path", path))
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, mode, **({} if "b" in mode else {"encoding": "utf-8"})) as f:
        f.write(data)
    return True


def exists(path):
    return os.path.exists(str(getattr(path, "_path", path)))


def delete_file(path):
    try:
        os.remove(str(getattr(path, "_path", path)))
        return True
    except OSError:
        return False


def list_dir(path):
    path = str(getattr(path, "_path", path))
    return os.listdir(path) if os.path.isdir(path) else []


def __getattr__(name):
    # Any SDK name a plugin imports resolves to a harmless stand-in, so one
    # missing helper does not stop the whole plugin from loading.
    if name.startswith("__") and name.endswith("__"):
        raise AttributeError(name)
    return _compat.Stub(__name__ + "." + name)
