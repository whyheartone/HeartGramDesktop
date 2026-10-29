import threading
import time
from typing import Any, Callable, Optional

import _compat
import _extera

from android_utils import run_on_ui_thread, log  # noqa: F401


class RequestCallback:
    """Callback for send_request: a plain function or an object with run()."""

    def __init__(self, fn=None):
        self._fn = fn

    def run(self, response=None, error=None):
        if self._fn:
            return self._fn(response, error)

    def __call__(self, response=None, error=None):
        return self.run(response, error)


PLUGINS_QUEUE = "pluginsQueue"
GLOBAL_QUEUE = "globalQueue"
STAGE_QUEUE = "stageQueue"
EXTERNAL_NETWORK_QUEUE = "externalNetworkQueue"


# Queues

def run_on_queue(fn: Callable, queue_name: str = PLUGINS_QUEUE, delay: int = 0):
    def runner():
        if delay:
            time.sleep(delay / 1000)
        try:
            fn()
        except Exception as e:
            _extera.log(f"run_on_queue task failed: {e}")
    threading.Thread(target=runner, daemon=True).start()


def get_queue_by_name(queue_name: str):
    return _compat.Stub(queue_name)


# Sending

def _reply_id(value):
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    return int(getattr(value, "id", 0) or 0)


def send_message(params: Any, parse_mode: Optional[str] = None,
                 on_sent: Optional[Callable] = None):
    if isinstance(params, dict):
        peer = params.get("peer")
        text = params.get("message") or params.get("caption") or ""
        reply = params.get("replyToMsg")
        path = params.get("path")
        document = params.get("document")
    else:
        peer = getattr(params, "peer", None)
        text = getattr(params, "message", "") or ""
        reply = getattr(params, "replyToMsg", None)
        path = getattr(params, "path", None)
        document = getattr(params, "document", None)
    entities = None if parse_mode else (
        params.get("entities") if isinstance(params, dict)
        else getattr(params, "entities", None))
    if path:
        _compat.send_file(peer, path, _reply_id(reply),
                          as_document=bool(document))
        return
    _compat.send(peer, text, parse_mode, _reply_id(reply), entities)


def get_reply_media(params: Any):
    """Desktop only: the media of the replied message as a dict with
    type (photo, sticker_static, sticker_animated, sticker_video, gif, video,
    document), path (downloaded file), mime, w, h, duration_ms. None when the
    reply has no media or the hook was not given a reply."""
    info = getattr(params, "reply_media", None)
    if not info:
        return None
    # The client writes the file as soon as the download finishes.
    import os
    import time
    path = info.get("path")
    deadline = time.time() + 30
    while path and not os.path.exists(path) and time.time() < deadline:
        time.sleep(0.2)
    return info


def is_desktop() -> bool:
    return True


def send_text(peer_id: int, text: str, parse_mode: Optional[str] = None,
              replyToMsg: Any = None, **kwargs):
    _compat.send(peer_id, text, parse_mode, _reply_id(replyToMsg))


def send_photo(peer_id, path, caption=None, **kwargs):
    _compat.send_file(peer_id, path, _reply_id(kwargs.get("replyToMsg")),
                      as_document=False)


def send_document(peer_id, path, caption=None, **kwargs):
    _compat.send_file(peer_id, path, _reply_id(kwargs.get("replyToMsg")),
                      as_document=True)


def send_video(peer_id, path, caption=None, **kwargs):
    _compat.send_file(peer_id, path, _reply_id(kwargs.get("replyToMsg")),
                      as_document=True)


def send_audio(peer_id, path, caption=None, **kwargs):
    _compat.send_file(peer_id, path, _reply_id(kwargs.get("replyToMsg")),
                      as_document=True)


def edit_message(message_object, text, parse_mode=None, **kwargs):
    _extera.log("edit_message is not available on desktop yet")


def send_request(request: Any, callback: Optional[Callable] = None):
    _extera.log("send_request is not available on desktop yet")
    return 0


# Controllers

def get_last_fragment():
    return _compat._Context("fragment")


def get_account_instance():
    return _compat.Stub("AccountInstance")


def get_messages_controller():
    return _compat.Stub("MessagesController")


def get_contacts_controller():
    return _compat.Stub("ContactsController")


def get_media_data_controller():
    return _compat.Stub("MediaDataController")


def get_notification_center():
    return _compat.Stub("NotificationCenter")


def get_locale_controller():
    return _compat.Stub("LocaleController")


def get_user_config():
    return _compat.Stub("UserConfig")


def get_connections_manager():
    return _compat.Stub("ConnectionsManager")


class _SendMessagesHelper:
    """SendMessagesHelper.sendMessage(params) of the Android client."""

    def sendMessage(self, params, *args, **kwargs):
        send_message(params)

    def getInstance(self, *args):
        return self

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        return _compat.Stub("SendMessagesHelper." + name)


_send_messages_helper = _SendMessagesHelper()


def get_send_messages_helper():
    return _send_messages_helper


def get_file_loader():
    return _compat._FileLoader


def get_messages_storage():
    return _compat.Stub("MessagesStorage")


def get_current_dialog_id() -> int:
    return int(_extera.current_peer())


class NotificationCenterDelegate:
    def didReceivedNotification(self, id, account, args):
        pass


def __getattr__(name):
    # Any SDK name a plugin imports resolves to a harmless stand-in, so one
    # missing helper does not stop the whole plugin from loading.
    if name.startswith("__") and name.endswith("__"):
        raise AttributeError(name)
    return _compat.Stub(__name__ + "." + name)
