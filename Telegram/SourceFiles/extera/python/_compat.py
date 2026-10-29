import html
import importlib.abc
import importlib.machinery
import importlib.util
import json
import os
import re
import sys
from html.parser import HTMLParser

import _extera
import _media


# Stub

class Stub:
    def __init__(self, name="stub"):
        object.__setattr__(self, "_name", name)

    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        return Stub(self._name + "." + item)

    def __setattr__(self, key, value):
        object.__setattr__(self, key, value)

    def __call__(self, *args, **kwargs):
        return Stub(self._name + "()")

    def __bool__(self):
        return True

    def __iter__(self):
        return iter(())

    def __len__(self):
        return 0

    def __int__(self):
        return 0

    def __float__(self):
        return 0.0

    def __index__(self):
        return 0

    def _zero(self, *args):
        return 0

    __add__ = __radd__ = __sub__ = __rsub__ = _zero
    __mul__ = __rmul__ = __truediv__ = __rtruediv__ = _zero
    __floordiv__ = __rfloordiv__ = __mod__ = __rmod__ = _zero
    __lt__ = __le__ = __gt__ = __ge__ = lambda self, other: False

    def __str__(self):
        return ""

    def __repr__(self):
        return f"<stub {self._name}>"

    def __eq__(self, other):
        return isinstance(other, Stub)

    def __hash__(self):
        return hash(self._name)

    def __mro_entries__(self, bases):
        return (object,)

    def __or__(self, other):
        return Stub(self._name + "|")

    def __ror__(self, other):
        return Stub("|" + self._name)

    def __getitem__(self, item):
        return Stub(self._name + "[]")

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def __contains__(self, item):
        return False


# Shims

def _open_url(url):
    _extera.open_url(str(url))
    return True


class _Toast:
    LENGTH_SHORT = 0
    LENGTH_LONG = 1

    def __init__(self, text):
        self._text = str(text)

    @staticmethod
    def makeText(context, text, duration=0):
        return _Toast(text)

    def show(self):
        _extera.toast(self._text)


class _Uri:
    def __init__(self, value):
        self._value = str(value)

    @staticmethod
    def parse(value):
        return _Uri(value)

    def toString(self):
        return self._value

    def __str__(self):
        return self._value


class _Intent(Stub):
    ACTION_VIEW = "android.intent.action.VIEW"
    ACTION_SEND = "android.intent.action.SEND"
    FLAG_ACTIVITY_NEW_TASK = 0x10000000

    def __init__(self, action=None, uri=None, *args):
        Stub.__init__(self, "Intent")
        object.__setattr__(self, "action", action)
        object.__setattr__(self, "uri", uri)

    def addFlags(self, flags):
        return self

    def setData(self, uri):
        object.__setattr__(self, "uri", uri)
        return self


class _Context(Stub):
    def startActivity(self, intent, *args):
        uri = getattr(intent, "uri", None)
        if uri is not None:
            _open_url(uri)

    def getParentActivity(self):
        return self


class _Browser:
    @staticmethod
    def openUrl(context, url, *args):
        return _open_url(url)


class _ApplicationLoader:
    applicationContext = _Context("applicationContext")

    @staticmethod
    def getFilesDirFixed():
        result = _files_dir()
        result.mkdirs()
        return result

    getCacheDirFixed = getFilesDirFixed


class _LaunchActivity:
    instance = _Context("LaunchActivity")


class _AndroidUtilities(Stub):
    @staticmethod
    def addToClipboard(text):
        _extera.copy(str(text))
        return True

    @staticmethod
    def runOnUIThread(runnable, delay=0):
        run = getattr(runnable, "run", runnable)
        _extera.run_on_main(run, int(delay or 0))

    @staticmethod
    def dp(value):
        return int(value)


class _JavaLocale:
    def __init__(self, language, country=""):
        self._language = language
        self._country = country

    @staticmethod
    def getDefault():
        import locale
        code = ""
        try:
            code = (locale.getlocale()[0] or locale.getdefaultlocale()[0] or "")
        except Exception:
            code = ""
        language, _, country = code.replace("-", "_").partition("_")
        return _JavaLocale((language or "en")[:2].lower(), country[:2].upper())

    def getLanguage(self):
        return self._language

    def getCountry(self):
        return self._country

    def toString(self):
        return self._language + ("_" + self._country if self._country else "")

    def getDisplayLanguage(self):
        return self._language


class _BuildVars:
    BUILD_VERSION = 6100
    BUILD_VERSION_STRING = "12.3.0"
    APP_ID = 4
    DEBUG_VERSION = False
    DEBUG_PRIVATE_VERSION = False
    LOGS_ENABLED = False
    STANDALONE_APP = True
    CHECK_UPDATES = False


class _PluginsController:
    @staticmethod
    def getInstance():
        import extera_host
        controller = Stub("PluginsController")
        object.__setattr__(controller, "pluginsDir", _File(extera_host.PLUGINS_DIR))
        return controller


class _LocaleController(Stub):
    @staticmethod
    def getString(*args):
        for value in args:
            if isinstance(value, str):
                return value
        return ""

    @staticmethod
    def formatString(*args):
        return ""


def _dynamic_proxy(*interfaces):
    return object


# Media: the replied message as the Android plugins see it. The client puts
# the file at reply_media["path"] once it is downloaded.

class _TLObject:
    def __init__(self, **values):
        self.__dict__.update(values)

    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        return None


class _TLRPCNamespace:
    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        cls = type(item, (_TLObject,), {})
        setattr(self, item, cls)
        return cls


TLRPC = _TLRPCNamespace()
TLRPC.TL_photoSizeProgressive = type(
    "TL_photoSizeProgressive", (TLRPC.TL_photoSize,), {})


class _JList(list):
    def size(self):
        return len(self)

    def get(self, index):
        return self[index]

    def isEmpty(self):
        return not self


class _File:
    def __init__(self, base, child=None):
        base = getattr(base, "_path", base)
        self._path = os.path.join(str(base), str(child)) if child else str(base)

    def getAbsolutePath(self):
        return os.path.abspath(self._path)

    def getPath(self):
        return self._path

    def getName(self):
        return os.path.basename(self._path)

    def exists(self):
        return os.path.exists(self._path)

    def isDirectory(self):
        return os.path.isdir(self._path)

    def length(self):
        return os.path.getsize(self._path) if os.path.exists(self._path) else 0

    def mkdirs(self):
        os.makedirs(self._path, exist_ok=True)
        return True

    mkdir = mkdirs

    def delete(self):
        try:
            os.remove(self._path)
            return True
        except OSError:
            return False

    def listFiles(self):
        if not os.path.isdir(self._path):
            return []
        return [_File(self._path, name) for name in os.listdir(self._path)]

    def __str__(self):
        return self._path

    def toString(self):
        return self._path


def _files_dir():
    return _File(os.path.join(os.environ.get("TEMP", "."), "HeartGram", "plugins"))


def _media_from_info(info):
    path = info.get("path") or ""
    kind = info.get("type") or ""
    w, h = int(info.get("w") or 0), int(info.get("h") or 0)
    if kind == "photo":
        size = TLRPC.TL_photoSize(w=w, h=h, type="y", _path=path)
        photo = TLRPC.TL_photo(sizes=_JList([size]), _path=path)
        return TLRPC.TL_messageMediaPhoto(photo=photo)
    attributes = _JList()
    if kind.startswith("sticker"):
        attributes.append(TLRPC.TL_documentAttributeSticker(alt=""))
        attributes.append(TLRPC.TL_documentAttributeImageSize(w=w, h=h))
    if kind in ("gif", "video", "sticker_video"):
        attributes.append(TLRPC.TL_documentAttributeVideo(
            w=w, h=h, duration=float(info.get("duration_ms") or 0) / 1000.0))
    if kind == "gif":
        attributes.append(TLRPC.TL_documentAttributeAnimated())
    document = TLRPC.TL_document(
        mime_type=info.get("mime") or "", attributes=attributes,
        _path=path, _gif=(kind == "gif"), _kind=kind)
    return TLRPC.TL_messageMediaDocument(document=document)


_known_users = {}


class _UsersRegistry:
    """MessagesController.getInstance(a).getUser(id) for people seen in menus."""

    @staticmethod
    def getInstance(*args):
        return _UsersRegistry

    @staticmethod
    def getUser(user_id):
        return _known_users.get(int(user_id or 0))

    @staticmethod
    def getChat(chat_id):
        return None

    @staticmethod
    def getUserFull(user_id):
        return None


class MessageObject:
    def __init__(self, peer=0, msg_id=0, media_info=None, text="", out=False,
                 date=0, sender_id=0):
        media = _media_from_info(media_info) if media_info else None
        self.messageOwner = TLRPC.TL_message(
            id=msg_id, dialog_id=peer, media=media, message=text,
            _path=(media_info or {}).get("path"), out=bool(out), date=int(date),
            from_id=TLRPC.TL_peerUser(user_id=int(sender_id)),
            entities=_JList(), reply_to=None, fwd_from=None)
        self.messageText = text
        self.id = msg_id
        self._out = bool(out)
        self._sender_id = int(sender_id)

    @staticmethod
    def from_context(context):
        sender = int(context.get("sender_id") or 0)
        name = str(context.get("sender_name") or "")
        first, _, last = name.partition(" ")
        if sender:
            _known_users[sender] = TLRPC.TL_user(
                id=sender, first_name=first, last_name=last,
                username=str(context.get("sender_username") or ""),
                photo=None, deleted=False, bot=False, premium=False)
        return MessageObject(
            int(context.get("peer") or 0), int(context.get("message_id") or 0),
            context.get("reply_media") or None,
            str(context.get("message_text") or ""),
            context.get("out"), context.get("date") or 0, sender)

    def isOutOwner(self):
        return self._out

    isOut = isOutOwner

    def getFromChatId(self):
        return self._sender_id

    def getSenderId(self):
        return self._sender_id

    def isForwarded(self):
        return False

    def isReply(self):
        return False

    def isVoice(self):
        return False

    def isSticker(self):
        return str(getattr((self.messageOwner.media or object()).__dict__.get("document"), "_kind", "")).startswith("sticker") if self.messageOwner.media else False

    def isPhoto(self):
        return bool(self.messageOwner.media) and hasattr(self.messageOwner.media, "photo")

    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        if item.startswith(("is", "has", "can", "need")):
            return lambda *a, **k: False
        if item.startswith("get"):
            return lambda *a, **k: None
        return Stub("MessageObject." + item)

    def getId(self):
        return self.id

    def getDialogId(self):
        return self.messageOwner.dialog_id

    @staticmethod
    def getDocument(owner):
        media = getattr(owner, "media", None)
        return getattr(media, "document", None) if media else None

    @staticmethod
    def getPhoto(owner):
        media = getattr(owner, "media", None)
        return getattr(media, "photo", None) if media else None

    @staticmethod
    def isGifDocument(document, *args):
        return bool(getattr(document, "_gif", False))

    @staticmethod
    def isStickerDocument(document):
        return str(getattr(document, "_kind", "") or "").startswith("sticker")

    @staticmethod
    def isAnimatedStickerDocument(document, *args):
        return getattr(document, "_kind", "") == "sticker_animated"

    @staticmethod
    def isVideoStickerDocument(document):
        return getattr(document, "_kind", "") == "sticker_video"

    @staticmethod
    def isVideoDocument(document):
        return getattr(document, "_kind", "") in ("video", "gif")


class _FileLoader:
    PRIORITY_LOW = 0
    PRIORITY_NORMAL = 1
    PRIORITY_HIGH = 3
    PRIORITY_STREAM = 4

    @staticmethod
    def getInstance(*args):
        return _FileLoader

    @staticmethod
    def getPathToMessage(owner, *args):
        path = getattr(owner, "_path", None)
        return _File(path) if path else None

    @staticmethod
    def getPathToAttach(obj, *args):
        path = getattr(obj, "_path", None)
        return _File(path) if path else None

    @staticmethod
    def loadFile(*args, **kwargs):
        return None


# Bitmaps backed by Pillow, enough for plugins that decode a file, convert
# it and write it back out.

class _CompressFormat:
    PNG = "PNG"
    JPEG = "JPEG"
    WEBP = "WEBP"
    WEBP_LOSSLESS = "WEBP"
    WEBP_LOSSY = "WEBP"


class _Config:
    ARGB_8888 = "RGBA"
    RGB_565 = "RGB"
    ALPHA_8 = "L"


class _Bitmap:
    CompressFormat = _CompressFormat
    Config = _Config

    def __init__(self, image):
        self._image = image

    @staticmethod
    def createBitmap(*args):
        from PIL import Image
        if args and isinstance(args[0], _Bitmap):
            return _Bitmap(args[0]._image.copy())
        width, height = int(args[0]), int(args[1])
        return _Bitmap(Image.new("RGBA", (width, height), (0, 0, 0, 0)))

    @staticmethod
    def createScaledBitmap(bitmap, width, height, *args):
        return _Bitmap(bitmap._image.resize((int(width), int(height))))

    def getWidth(self):
        return self._image.width

    def getHeight(self):
        return self._image.height

    def hasAlpha(self):
        return "A" in self._image.getbands()

    def isRecycled(self):
        return False

    def recycle(self):
        pass

    def compress(self, fmt, quality, stream):
        import io
        image = self._image
        if fmt == "JPEG" and image.mode not in ("RGB", "L"):
            image = image.convert("RGB")
        buffer = io.BytesIO()
        image.save(buffer, fmt or "PNG", quality=int(quality or 100))
        stream.write(buffer.getvalue())
        return True


class _BitmapFactory:
    @staticmethod
    def decodeFile(path, *args):
        try:
            from PIL import Image
            image = Image.open(str(getattr(path, "_path", path)))
            image.load()
            return _Bitmap(image.convert("RGBA"))
        except Exception:
            return None

    @staticmethod
    def decodeByteArray(data, offset=0, length=None, *args):
        try:
            import io
            from PIL import Image
            data = bytes(data)
            end = None if length is None else offset + length
            image = Image.open(io.BytesIO(data[offset:end]))
            image.load()
            return _Bitmap(image.convert("RGBA"))
        except Exception:
            return None


class _FileOutputStream:
    def __init__(self, path, append=False):
        path = str(getattr(path, "_path", path))
        self._file = open(path, "ab" if append else "wb")

    def write(self, data, offset=0, length=None):
        if isinstance(data, int):
            self._file.write(bytes([data & 0xFF]))
            return
        data = bytes(data)
        end = None if length is None else offset + length
        self._file.write(data[offset:end])

    def flush(self):
        self._file.flush()

    def close(self):
        self._file.close()


class _ByteArrayOutputStream:
    def __init__(self, *args):
        self._data = bytearray()

    def write(self, data, offset=0, length=None):
        if isinstance(data, int):
            self._data.append(data & 0xFF)
            return
        data = bytes(data)
        end = None if length is None else offset + length
        self._data.extend(data[offset:end])

    def toByteArray(self):
        return bytes(self._data)

    def size(self):
        return len(self._data)

    def flush(self):
        pass

    def close(self):
        pass


class _ImageLocation:
    @staticmethod
    def getForPhoto(size, photo=None):
        return size

    @staticmethod
    def getForDocument(*args):
        return args[0] if args else None


class _ArrayList(list):
    """java.util.ArrayList: plugins build entity lists with it and check
    isinstance, so it has to be a real class."""

    def __init__(self, values=None):
        list.__init__(self, list(values) if values is not None and not isinstance(values, int) else [])

    def add(self, *args):
        if len(args) == 2:
            self.insert(int(args[0]), args[1])
        else:
            self.append(args[0])
        return True

    def addAll(self, values):
        self.extend(list(values))
        return True

    def get(self, index):
        return self[int(index)]

    def set(self, index, value):
        old = self[int(index)]
        self[int(index)] = value
        return old

    def size(self):
        return len(self)

    def isEmpty(self):
        return not self

    def contains(self, value):
        return value in self

    def clear(self):
        del self[:]

    def toArray(self, *args):
        return list(self)


class _HashMap(dict):
    def put(self, key, value):
        old = self.get(key)
        self[key] = value
        return old

    def containsKey(self, key):
        return key in self

    def size(self):
        return len(self)

    def isEmpty(self):
        return not self

    def keySet(self):
        return list(self.keys())

    def values(self):
        return list(dict.values(self))


class _HashSet(set):
    def add(self, value):
        had = value in self
        set.add(self, value)
        return not had

    def contains(self, value):
        return value in self

    def size(self):
        return len(self)

    def isEmpty(self):
        return not self


_ENTITY_KINDS = {
    "TL_messageEntityBold": "bold",
    "TL_messageEntityItalic": "italic",
    "TL_messageEntityUnderline": "underline",
    "TL_messageEntityStrike": "strike",
    "TL_messageEntityCode": "code",
    "TL_messageEntityPre": "pre",
    "TL_messageEntityTextUrl": "url",
    "TL_messageEntitySpoiler": "spoiler",
    "TL_messageEntityBlockquote": "blockquote",
}


def entities_to_list(entities):
    """TLRPC entity objects of a plugin as [kind, offset, length, data]."""
    result = []
    for entity in list(entities or []):
        if isinstance(entity, (list, tuple)):
            result.append(list(entity))
            continue
        kind = _ENTITY_KINDS.get(type(entity).__name__)
        if not kind:
            continue
        data = ""
        if kind == "url":
            data = str(getattr(entity, "url", "") or "")
        elif kind == "pre":
            data = str(getattr(entity, "language", "") or "")
        elif kind == "blockquote" and getattr(entity, "collapsed", False):
            kind = "blockquote_collapsed"
        result.append([
            kind,
            int(getattr(entity, "offset", 0) or 0),
            int(getattr(entity, "length", 0) or 0),
            data,
        ])
    return result


SHIMS = {
    "android.widget": {"Toast": _Toast},
    "android.net": {"Uri": _Uri},
    "android.content": {"Intent": _Intent},
    "org.telegram.messenger.browser": {"Browser": _Browser},
    "org.telegram.messenger": {
        "ApplicationLoader": _ApplicationLoader,
        "BuildVars": _BuildVars,
        "MessageObject": MessageObject,
        "MessagesController": _UsersRegistry,
        "FileLoader": _FileLoader,
        "ImageLocation": _ImageLocation,
        "ImageLoader": _media.ImageLoader,
        "AndroidUtilities": _AndroidUtilities("AndroidUtilities"),
        "LocaleController": _LocaleController("LocaleController"),
    },
    "org.telegram.ui": {"LaunchActivity": _LaunchActivity},
    "org.telegram.tgnet": {"TLRPC": TLRPC},
    "android.media": {
        "MediaMetadataRetriever": _media.MediaMetadataRetriever,
    },
    "org.telegram.ui.Components": {
        "RLottieDrawable": _media.RLottieDrawable,
        "AnimatedFileDrawable": _media.AnimatedFileDrawable,
    },
    "java.util": {
        "Locale": _JavaLocale,
        "ArrayList": _ArrayList,
        "List": _ArrayList,
        "HashMap": _HashMap,
        "Map": _HashMap,
        "HashSet": _HashSet,
    },
    "com.exteragram.messenger.plugins": {
        "PluginsController": _PluginsController,
    },
    "java.io": {
        "File": _File,
        "FileOutputStream": _FileOutputStream,
        "ByteArrayOutputStream": _ByteArrayOutputStream,
    },
    "android.graphics": {"Bitmap": _Bitmap, "BitmapFactory": _BitmapFactory},
    "java": {
        "jclass": lambda name: Stub(str(name)),
        "dynamic_proxy": _dynamic_proxy,
        "jarray": lambda kind: (lambda values: list(values)),
        "cast": lambda kind, value: value,
        "jint": int,
        "jlong": int,
        "jboolean": bool,
        "jfloat": float,
        "jdouble": float,
    },
}

ROOTS = ("java", "android", "androidx", "org", "com", "kotlin", "dalvik",
         "de", "me", "io")


class _StubModule(type(sys)):
    def __getattr__(self, item):
        if item.startswith("__") and item.endswith("__"):
            raise AttributeError(item)
        value = Stub(self.__name__ + "." + item)
        setattr(self, item, value)
        return value


class _Finder(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def find_spec(self, fullname, path, target=None):
        root = fullname.split(".")[0]
        if root not in ROOTS:
            return None
        if root in ("io", "me", "de") and not fullname.startswith(
                ("de.robv", "me.", "io.")):
            return None
        return importlib.machinery.ModuleSpec(fullname, self, is_package=True)

    def create_module(self, spec):
        module = _StubModule(spec.name)
        module.__path__ = []
        for key, value in SHIMS.get(spec.name, {}).items():
            setattr(module, key, value)
        return module

    def exec_module(self, module):
        pass


# Android paths: plugins keep fonts and files under /storage/emulated/0 and
# read system fonts from /system/fonts. Without a redirect they would land in
# the root of whatever drive the client runs from.

STORAGE_ROOT = ""
_ANDROID_PREFIXES = ("/storage/emulated/0", "/sdcard", "/system/fonts")


def _redirect(path):
    if not isinstance(path, str) or not STORAGE_ROOT:
        return path
    normal = path.replace("\\", "/")
    if normal.startswith("/system/fonts"):
        rest = normal[len("/system/fonts"):].lstrip("/")
        windows = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
        return os.path.join(windows, rest) if rest else windows
    for prefix in _ANDROID_PREFIXES[:2]:
        if normal == prefix or normal.startswith(prefix + "/"):
            rest = normal[len(prefix):].lstrip("/")
            return os.path.join(STORAGE_ROOT, rest) if rest else STORAGE_ROOT
    if normal.startswith("/storage/emulated/0") or normal.startswith("/storage/"):
        return os.path.join(STORAGE_ROOT, normal[len("/storage/"):])
    return path


def _patch_paths():
    import builtins
    import io
    import os as _os
    if getattr(_os, "_heartgram_patched", False):
        return
    _os._heartgram_patched = True

    def wrap(fn, count=1):
        def inner(*args, **kwargs):
            args = tuple(_redirect(a) if i < count else a
                         for i, a in enumerate(args))
            return fn(*args, **kwargs)
        inner.__name__ = getattr(fn, "__name__", "wrapped")
        return inner

    builtins.open = wrap(builtins.open)
    io.open = builtins.open
    for name in ("makedirs", "mkdir", "listdir", "remove", "unlink",
                 "scandir", "stat", "rmdir", "walk"):
        if hasattr(_os, name):
            setattr(_os, name, wrap(getattr(_os, name)))
    for name in ("exists", "isfile", "isdir", "getsize", "getmtime"):
        setattr(_os.path, name, wrap(getattr(_os.path, name)))
    _os.rename = wrap(_os.rename, 2)
    _os.replace = wrap(_os.replace, 2)


class _ExistingModule(importlib.abc.Loader):
    def __init__(self, module):
        self._module = module

    def create_module(self, spec):
        return self._module

    def exec_module(self, module):
        pass


class _PluginFinder(importlib.abc.MetaPathFinder):
    """`import zwylib` finds the plugin zwylib, as on the phone."""

    def find_spec(self, fullname, path, target=None):
        if "." in fullname:
            return None
        try:
            import extera_host
            plugins = extera_host.PLUGINS_DIR
        except Exception:
            return None
        module = extera_host.module_for(fullname)
        if module is not None:
            return importlib.machinery.ModuleSpec(
                fullname, _ExistingModule(module))
        candidate = os.path.join(plugins, fullname + ".plugin")
        if not plugins or not os.path.isfile(candidate):
            return None
        loader = importlib.machinery.SourceFileLoader(fullname, candidate)
        return importlib.util.spec_from_file_location(
            fullname, candidate, loader=loader)


def install(storage=""):
    global STORAGE_ROOT
    if storage:
        STORAGE_ROOT = storage
        os.makedirs(storage, exist_ok=True)
        _patch_paths()
    if not any(isinstance(f, _PluginFinder) for f in sys.meta_path):
        sys.meta_path.insert(0, _PluginFinder())
    if not any(isinstance(f, _Finder) for f in sys.meta_path):
        sys.meta_path.append(_Finder())


# Send params

class SendMessageParams:
    def __init__(self, peer=None, message="", reply_to=0, reply_media=None):
        self.peer = peer
        self.message = message
        self.reply_media = dict(reply_media) if reply_media else None
        self.caption = None
        self.entities = []
        if reply_to and reply_media:
            self.replyToMsg = MessageObject(peer, reply_to, reply_media)
        else:
            self.replyToMsg = _ReplyStub(reply_to) if reply_to else None
        self.replyToTopMsg = None
        self.replyMarkup = None
        self.params = None
        self.notify = True
        self.scheduleDate = 0
        self.searchLinks = True
        self.photo = None
        self.document = None
        self.path = None


class _ReplyStub(Stub):
    def __init__(self, msg_id):
        Stub.__init__(self, "replyToMsg")
        object.__setattr__(self, "id", msg_id)
        object.__setattr__(self, "messageText", "")


# Formatting

def _utf16_len(text):
    return len(text.encode("utf-16-le")) // 2


class _HtmlToEntities(HTMLParser):
    TAGS = {
        "b": "bold", "strong": "bold", "i": "italic", "em": "italic",
        "u": "underline", "ins": "underline", "s": "strike",
        "strike": "strike", "del": "strike", "code": "code", "pre": "pre",
        "blockquote": "blockquote", "tg-spoiler": "spoiler", "a": "url",
        "span": "span",
    }

    def __init__(self):
        HTMLParser.__init__(self, convert_charrefs=True)
        self.text = ""
        self.stack = []
        self.entities = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "br":
            self.text += "\n"
            return
        kind = self.TAGS.get(tag)
        if kind == "span" and "tg-spoiler" not in (attrs.get("class") or ""):
            kind = None
        if kind == "span":
            kind = "spoiler"
        if kind == "blockquote" and "expandable" in attrs:
            kind = "blockquote_collapsed"
        self.stack.append((tag, kind, _utf16_len(self.text), attrs))

    def handle_endtag(self, tag):
        for index in range(len(self.stack) - 1, -1, -1):
            if self.stack[index][0] == tag:
                _, kind, start, attrs = self.stack.pop(index)
                length = _utf16_len(self.text) - start
                if kind and length > 0:
                    data = attrs.get("href", "") if kind == "url" else ""
                    if kind == "pre":
                        data = attrs.get("language", "")
                    self.entities.append([kind, start, length, data])
                return

    def handle_data(self, data):
        self.text += data


def parse_html(text):
    parser = _HtmlToEntities()
    parser.feed(text)
    parser.close()
    return parser.text, parser.entities


_MD = [
    (re.compile(r"\*\*(.+?)\*\*", re.S), "bold"),
    (re.compile(r"__(.+?)__", re.S), "italic"),
    (re.compile(r"~~(.+?)~~", re.S), "strike"),
    (re.compile(r"\|\|(.+?)\|\|", re.S), "spoiler"),
    (re.compile(r"`([^`]+?)`", re.S), "code"),
]


def parse_markdown(text):
    entities = []
    changed = True
    while changed:
        changed = False
        best = None
        for pattern, kind in _MD:
            match = pattern.search(text)
            if match and (best is None or match.start() < best[0].start()):
                best = (match, kind)
        if best:
            match, kind = best
            inner = match.group(1)
            start = _utf16_len(text[:match.start()])
            shift = _utf16_len(match.group(0)) - _utf16_len(inner)
            for entity in entities:
                if entity[1] > start:
                    entity[1] -= shift
            entities.append([kind, start, _utf16_len(inner), ""])
            text = text[:match.start()] + inner + text[match.end():]
            changed = True
    link = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
    while True:
        match = link.search(text)
        if not match:
            break
        start = _utf16_len(text[:match.start()])
        entities.append(["url", start, _utf16_len(match.group(1)), match.group(2)])
        text = text[:match.start()] + match.group(1) + text[match.end():]
    return text, entities


def format_text(text, parse_mode):
    mode = (parse_mode or "").lower()
    if mode == "html":
        return parse_html(text)
    if mode == "markdown":
        return parse_markdown(text)
    return text, []


def to_dialog_id(peer):
    if isinstance(peer, Stub):
        return 0
    try:
        return int(peer)
    except Exception:
        value = getattr(peer, "user_id", None) or getattr(peer, "channel_id", None) \
            or getattr(peer, "chat_id", None)
        return int(value or 0)


def _gif_to_mp4(path):
    """A GIF re-encoded as a silent H.264 mp4: Telegram shows it as the same
    GIF, but the file is many times smaller and uploads at once."""
    try:
        import av
        from PIL import Image, ImageSequence
    except Exception:
        return None
    target = os.path.splitext(path)[0] + ".mp4"
    output = None
    try:
        with Image.open(path) as image:
            if not getattr(image, "is_animated", False):
                return None
            width, height = image.size
            scale = min(1.0, 720.0 / max(width, height))
            width = max(2, int(width * scale) // 2 * 2)
            height = max(2, int(height * scale) // 2 * 2)
            frames = []
            for frame in ImageSequence.Iterator(image):
                duration = frame.info.get("duration", image.info.get("duration", 80))
                rgba = frame.convert("RGBA")
                flat = Image.new("RGB", rgba.size, (255, 255, 255))
                flat.paste(rgba, mask=rgba.split()[3])
                if flat.size != (width, height):
                    flat = flat.resize((width, height), Image.LANCZOS)
                frames.append((flat, max(20, int(duration or 80))))
        if not frames:
            return None
        durations = sorted(d for _, d in frames)
        fps = max(5, min(50, round(1000.0 / durations[len(durations) // 2])))
        output = av.open(target, "w", options={"movflags": "+faststart"})
        stream = output.add_stream("libx264", rate=fps)
        stream.width = width
        stream.height = height
        stream.pix_fmt = "yuv420p"
        stream.options = {"crf": "26", "preset": "veryfast"}
        clock = 0.0
        index = 0
        for flat, duration in frames:
            clock += duration
            while index * 1000.0 / fps < clock:
                video = av.VideoFrame.from_image(flat)
                video.pts = index
                for packet in stream.encode(video):
                    output.mux(packet)
                index += 1
        for packet in stream.encode(None):
            output.mux(packet)
        output.close()
        output = None
        if os.path.getsize(target) <= 0:
            return None
        return target
    except Exception as e:
        _extera.log("gif to mp4 failed: " + repr(e))
        try:
            if output is not None:
                output.close()
            os.remove(target)
        except Exception:
            pass
        return None


def send_file(peer, path, reply_to=0, as_document=False):
    path = str(getattr(path, "_path", path) or "")
    if not path or not os.path.exists(path):
        _extera.log("send_file: no such file " + repr(path))
        return
    if path.lower().endswith(".gif"):
        converted = _gif_to_mp4(path)
        if converted:
            path = converted
            as_document = False
    _extera.send_file(
        to_dialog_id(peer), path, int(reply_to or 0), bool(as_document))


def send(peer, text, parse_mode=None, reply_to=0, entities=None):
    if entities:
        _extera.send_message(
            to_dialog_id(peer),
            str(text or ""),
            json.dumps(entities_to_list(entities)),
            int(reply_to or 0))
        return
    plain, entities = format_text(str(text or ""), parse_mode)
    _extera.send_message(
        to_dialog_id(peer), plain, json.dumps(entities), int(reply_to or 0))
