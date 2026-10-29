import _compat


class ParsedMessage:
    def __init__(self, text, entities):
        self.text = text
        self.entities = entities


def parse_markdown(text: str) -> ParsedMessage:
    plain, entities = _compat.parse_markdown(text)
    return ParsedMessage(plain, entities)


def parse_html(text: str) -> ParsedMessage:
    plain, entities = _compat.parse_html(text)
    return ParsedMessage(plain, entities)


def __getattr__(name):
    # Any SDK name a plugin imports resolves to a harmless stand-in, so one
    # missing helper does not stop the whole plugin from loading.
    if name.startswith("__") and name.endswith("__"):
        raise AttributeError(name)
    return _compat.Stub(__name__ + "." + name)
