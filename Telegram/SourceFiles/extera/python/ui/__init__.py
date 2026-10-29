import _compat


def __getattr__(name):
    # Any SDK name a plugin imports resolves to a harmless stand-in, so one
    # missing helper does not stop the whole plugin from loading.
    if name.startswith("__") and name.endswith("__"):
        raise AttributeError(name)
    return _compat.Stub(__name__ + "." + name)
