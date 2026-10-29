import _compat
from typing import Any


def find_class(name: str) -> Any:
    return _compat.Stub(str(name))


def get_private_field(obj: Any, name: str) -> Any:
    return _compat.Stub(str(name))


def set_private_field(obj: Any, name: str, value: Any) -> bool:
    return False


def get_static_private_field(clazz: Any, name: str) -> Any:
    return _compat.Stub(str(name))


def set_static_private_field(clazz: Any, name: str, value: Any) -> bool:
    return False


def __getattr__(name):
    # Any SDK name a plugin imports resolves to a harmless stand-in, so one
    # missing helper does not stop the whole plugin from loading.
    if name.startswith("__") and name.endswith("__"):
        raise AttributeError(name)
    return _compat.Stub(__name__ + "." + name)
