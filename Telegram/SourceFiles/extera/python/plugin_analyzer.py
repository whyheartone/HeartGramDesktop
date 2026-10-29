"""Local safety review of a plugin: what it can do, with the lines that show it.

The finding names and groups follow exteraGram's analysis catalog. The checks
are heuristics over the plugin's syntax tree, run on the user's own machine:
nothing is uploaded, and a plugin that cannot be parsed is reported as such.
"""
import ast
import base64
import hashlib
import json
import re
import zlib

DANGER, WARN, INFO = "danger", "warn", "info"

# id: (category, level, ru name, ru text, en name, en text)
CATALOG = {
    "dynamic_code": ("code", DANGER, "Динамический код", "Выполняет код, которого нет в файле плагина", "Dynamic code", "Runs code that isn't in the plugin file"),
    "embedded_code": ("code", WARN, "Упакованный код", "Содержит упакованный код", "Embedded code", "Contains packed code"),
    "loads_code": ("code", DANGER, "Скомпилированный код", "Загружает дополнительный скомпилированный код", "Compiled code", "Loads extra compiled code"),
    "reflection_dynamic": ("code", WARN, "Динамические вызовы", "Выбирает, что вызвать, прямо во время работы", "Dynamic calls", "Chooses what to call while running"),
    "native_code": ("code", DANGER, "Нативный код", "Запускает нативный код, который нельзя проверить", "Native code", "Runs native code that can't be checked"),
    "anti_analysis": ("code", DANGER, "Защита от разбора", "Пытается скрыть, что делает", "Anti-analysis", "Tries to hide what it does"),
    "hidden_parts": ("code", WARN, "Нечитаемые части", "Есть части, которые не удалось прочитать", "Hidden parts", "Has parts that couldn't be read"),
    "remote_code": ("code", DANGER, "Удалённый код", "Может скачать и выполнить код, который вы не видели", "Remote code", "Can download and run code you haven't seen"),
    "remote_control": ("code", DANGER, "Удалённое управление", "Может управляться удалённо", "Remote control", "Can be controlled remotely"),
    "hides_behavior": ("code", WARN, "Скрытое поведение", "Скрывает часть своего поведения", "Hides behavior", "Hides part of its behavior"),
    "network": ("network", WARN, "Доступ в интернет", "Подключается к интернету", "Internet access", "Connects to the internet"),
    "exfiltrate_messages": ("network", DANGER, "Утечка сообщений", "Может отправить ваши сообщения или контакты в интернет", "Message leak", "Can send your messages or contacts to the internet"),
    "exfiltrate_files": ("network", DANGER, "Утечка файлов", "Может отправить ваши файлы в интернет", "File leak", "Can send your files to the internet"),
    "device_id_upload": ("network", DANGER, "Идентификаторы устройства", "Отправляет идентификаторы устройства в интернет", "Device identifiers", "Sends device identifiers to the internet"),
    "files": ("files", INFO, "Файлы", "Читает и записывает файлы", "Files", "Reads and writes files"),
    "telegram_files": ("files", INFO, "Загрузки через Telegram", "Скачивает файлы через Telegram", "Telegram downloads", "Downloads files through Telegram"),
    "processes": ("app", DANGER, "Системные процессы", "Запускает системные процессы", "System processes", "Starts system processes"),
    "app_exit": ("app", WARN, "Закрытие приложения", "Может закрыть или перезапустить приложение", "Closing the app", "Can close or restart the app"),
    "host_tampering": ("app", DANGER, "Вмешательство в приложение", "Меняет работу приложения или других плагинов", "App tampering", "Changes how the app or other plugins work"),
    "java_hooks": ("app", WARN, "Перехват методов", "Меняет поведение приложения через перехват методов", "Method hooks", "Changes app behavior with hooks"),
    "ui_hooks": ("app", DANGER, "Перехват важных экранов", "Меняет экраны входа, оплаты или безопасности", "Sensitive screen hooks", "Changes login, payment or security screens"),
    "persistence": ("app", WARN, "Управление плагинами", "Может устанавливать и менять плагины", "Plugin management", "Can install or change plugins"),
    "app_settings": ("app", WARN, "Настройки приложения", "Меняет настройки приложения", "App settings", "Changes app settings"),
    "background": ("app", INFO, "Работа в фоне", "Работает в фоне или реагирует на события системы", "Background work", "Runs in the background or reacts to system events"),
    "notifications": ("device", WARN, "Уведомления", "Читает уведомления приложения", "Notifications", "Reads the app's notifications"),
    "intents": ("device", WARN, "Другие приложения", "Открывает другие приложения или передаёт им данные", "Other apps", "Opens other apps or sends them data"),
    "privacy": ("device", WARN, "Личные данные", "Обращается к личным данным устройства", "Private data", "Accesses private device data"),
    "system_overlay": ("device", DANGER, "Рисование поверх окон", "Может показывать свои окна поверх других приложений", "Drawing over other apps", "Can show its own windows on top of other apps"),
    "sms": ("device", DANGER, "SMS", "Может читать и отправлять SMS", "SMS", "Can read or send SMS from your phone"),
    "screen_capture": ("device", DANGER, "Запись экрана", "Может снимать экран или отключать защиту от скриншотов", "Screen capture", "Can capture the screen or turn off screenshot protection"),
    "installs_apps": ("device", DANGER, "Установка приложений", "Может устанавливать другие приложения", "App installs", "Can install other apps"),
    "media_library": ("device", WARN, "Фото и медиа", "Читает ваши фото и медиа", "Photos and media", "Reads your photos and media"),
    "webview_bridge": ("device", WARN, "Мост для веб-страниц", "Позволяет веб-страницам вызывать приложение", "Web page bridge", "Lets web pages call into the app"),
    "contacts": ("account", WARN, "Контакты", "Читает ваши контакты", "Contacts", "Reads your contacts"),
    "third_party_code": ("packages", WARN, "Непроверенные пакеты", "Устанавливает пакеты, которые не проверялись", "Unchecked packages", "Installs packages that weren't checked"),
    "telegram_api": ("account", INFO, "Запросы Telegram", "Отправляет запросы от вашего имени", "Telegram requests", "Sends requests on your behalf"),
    "sensitive_requests": ("account", DANGER, "Операции с аккаунтом", "Может переводить деньги, подарки или управление аккаунтом", "Account operations", "Can move money, gifts or control of your account"),
    "intercepts_requests": ("account", WARN, "Перехват запросов", "Читает и меняет запросы Telegram", "Request interception", "Reads and changes Telegram requests"),
    "session_data": ("account", DANGER, "Данные сессии", "Обращается к данным сессии аккаунта", "Session data", "Touches account session data"),
    "session_via_telegram": ("account", DANGER, "Утечка сессии", "Может отправить сессию аккаунта через Telegram", "Session leak", "Can send your account session through Telegram"),
    "telegram_leak": ("account", DANGER, "Утечка через Telegram", "Отправляет ваши личные данные сообщениями Telegram", "Data leak via Telegram", "Sends your private data through Telegram messages"),
    "hidden_account_actions": ("account", DANGER, "Скрытые действия", "Может действовать от вашего имени и скрывать это", "Hidden actions", "Can act on your account and hide it"),
    "account_takeover": ("account", DANGER, "Захват аккаунта", "Может захватить ваш аккаунт", "Account takeover", "Can take over your account"),
    "wallet_access": ("account", DANGER, "Криптокошелёк", "Обращается к вашему криптокошельку", "Crypto wallet", "Accesses your crypto wallet"),
    "messages_read": ("messages", WARN, "Чтение сообщений", "Читает ваши сообщения и чаты", "Reading messages", "Reads your messages and chats"),
    "messages_send": ("messages", WARN, "Отправка сообщений", "Отправляет, пересылает или меняет сообщения", "Sending messages", "Sends, forwards or edits messages"),
    "messages_delete": ("messages", WARN, "Удаление сообщений", "Удаляет сообщения", "Deleting messages", "Deletes messages"),
    "forward_messages": ("messages", WARN, "Пересылка", "Может пересылать ваши сообщения внутри Telegram", "Forwarding", "Can forward your messages inside Telegram"),
}

CATEGORIES = {
    "code": ("Код", "Code"),
    "account": ("Аккаунт", "Account"),
    "messages": ("Сообщения", "Messages"),
    "network": ("Сеть", "Network"),
    "files": ("Файлы", "Files"),
    "device": ("Устройство", "Device"),
    "app": ("Приложение", "App"),
    "packages": ("Пакеты", "Packages"),
}

NETWORK_ROOTS = {"requests", "urllib", "urllib3", "http", "httpx", "aiohttp",
                 "socket", "websocket", "websockets", "ftplib", "smtplib",
                 "telnetlib", "ssl", "grpc", "okhttp3"}
PROCESS_NAMES = {"os.system", "os.popen", "os.spawnl", "os.spawnv", "os.execv",
                 "os.execl", "pty.spawn", "ProcessBuilder", "Runtime"}
SEND_NAMES = {"send_message", "send_text", "send_photo", "send_document",
              "send_video", "send_audio", "edit_message", "sendMessage",
              "sendMessages", "editMessage"}
LEAK_WORDS = re.compile(r"(session|token|auth_?key|password|passcode|phone|dc_?key|secret)", re.I)
DEVICE_WORDS = re.compile(r"(android_id|device_?id|imei|serial|mac_?addr|getDeviceId|Build\.)", re.I)
MESSAGE_WORDS = re.compile(r"(message|messages|contact|contacts|history|dialog)", re.I)
SENSITIVE_TL = re.compile(r"(payments_|account_delete|account_changePhone|account_updatePassword|auth_resetAuthorizations|account_resetAuthorization|auth_exportAuthorization|auth_importAuthorization|stars|gift|messages_deleteHistory|channels_deleteChannel|account_setAuthorizationTTL)", re.I)
UI_HOOK_WORDS = re.compile(r"(Login|Passcode|Payment|Wallet|TwoStep|Password|AuthCode|SecurityIssues|Stars|Gift)", re.I)
DANGEROUS_BLOB = (b"dex\n", b"PK\x03\x04", b"\x7fELF", b"MZ")


def _dotted(node):
    parts = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if isinstance(node, ast.Name):
        parts.append(node.id)
    elif isinstance(node, ast.Call):
        inner = _dotted(node.func)
        if inner:
            parts.append(inner + "()")
    return ".".join(reversed(parts))


def _text_of(node):
    try:
        return ast.unparse(node)
    except Exception:
        return ""


class _Scan(ast.NodeVisitor):
    def __init__(self, lines):
        self.lines = lines
        self.found = {}
        self.functions = []
        self.try_pass = 0

    def add(self, finding, line):
        entry = self.found.setdefault(finding, [])
        if line not in [l for l, _ in entry]:
            code = self.lines[line - 1].strip() if 0 < line <= len(self.lines) else ""
            entry.append((line, code[:160]))

    # -- imports ----------------------------------------------------------
    def visit_Import(self, node):
        for alias in node.names:
            self._module(alias.name, node.lineno)
        self.generic_visit(node)

    def visit_ImportFrom(self, node):
        self._module(node.module or "", node.lineno)
        for alias in node.names:
            self._module((node.module or "") + "." + alias.name, node.lineno)
        self.generic_visit(node)

    def _module(self, name, line):
        root = name.split(".")[0]
        low = name.lower()
        if root in NETWORK_ROOTS:
            self.add("network", line)
        if root in ("subprocess", "pty"):
            self.add("processes", line)
        if root in ("ctypes", "cffi"):
            self.add("native_code", line)
        if root in ("marshal", "pyarmor", "pyarmor_runtime"):
            self.add("anti_analysis", line)
        if root in ("pip", "ensurepip"):
            self.add("third_party_code", line)
        if "dexclassloader" in low or "inmemorydexclassloader" in low or "pathclassloader" in low:
            self.add("loads_code", line)
        if "smsmanager" in low or "telephony.sms" in low:
            self.add("sms", line)
        if "mediaprojection" in low:
            self.add("screen_capture", line)
        if "contactscontract" in low:
            self.add("contacts", line)
        if "provider.mediastore" in low:
            self.add("media_library", line)
        if low.endswith("content.intent") or low.endswith("net.uri") and False:
            self.add("intents", line)
        if "notificationlistener" in low or "notificationmanager" in low:
            self.add("notifications", line)
        if "javascriptinterface" in low:
            self.add("webview_bridge", line)
        if "telephonymanager" in low or "locationmanager" in low or "accountmanager" in low or "settings.secure" in low:
            self.add("privacy", line)
        if "packageinstaller" in low:
            self.add("installs_apps", line)
        if "broadcastreceiver" in low or ("android.app.service" in low):
            self.add("background", line)
        if "tonutils" in low or "toncontroller" in low or "walletconfig" in low:
            self.add("wallet_access", line)
        if "pluginscontroller" in low:
            self.add("persistence", line)

    # -- calls ------------------------------------------------------------
    def visit_FunctionDef(self, node):
        self.functions.append(node)
        self.generic_visit(node)

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_Try(self, node):
        silent = any(
            len(handler.body) == 1 and isinstance(handler.body[0], ast.Pass)
            for handler in node.handlers)
        if silent:
            for inner in ast.walk(ast.Module(body=node.body, type_ignores=[])):
                if not isinstance(inner, ast.Call):
                    continue
                name = _dotted(inner.func)
                if name.split(".")[0] in NETWORK_ROOTS or name in ("exec", "eval"):
                    self.try_pass += 1
                    break
        self.generic_visit(node)

    def visit_While(self, node):
        if isinstance(node.test, ast.Constant) and node.test.value is True:
            self.add("background", node.lineno)
        self.generic_visit(node)

    def visit_Call(self, node):
        name = _dotted(node.func)
        last = name.split(".")[-1]
        line = node.lineno
        argtext = " ".join(_text_of(a) for a in list(node.args) + [k.value for k in node.keywords])
        if last in ("exec", "eval") and name in ("exec", "eval", "builtins.exec", "builtins.eval"):
            self.add("dynamic_code", line)
        elif name == "compile":
            self.add("dynamic_code", line)
        elif last in ("__import__", "import_module") or name in ("importlib.import_module",):
            self.add("reflection_dynamic", line)
        elif last in ("getattr", "setattr") and len(node.args) >= 2 and not isinstance(node.args[1], ast.Constant):
            self.add("reflection_dynamic", line)
        if name in PROCESS_NAMES or name.startswith("subprocess.") or last in ("Popen", "check_output", "check_call") and "subprocess" in name:
            self.add("processes", line)
            if "pip" in argtext and "install" in argtext:
                self.add("third_party_code", line)
        if last in ("marshal", "loads") and name.startswith("marshal"):
            self.add("anti_analysis", line)
        if name in ("sys.gettrace", "sys.settrace", "sys.setprofile"):
            self.add("anti_analysis", line)
        if last in ("loadLibrary", "load", "CDLL", "WinDLL", "LoadLibrary") and ("System" in name or "ctypes" in name or "cdll" in name.lower()):
            self.add("native_code", line)
        if "DexClassLoader" in name or "InMemoryDexClassLoader" in name or "PathClassLoader" in name:
            self.add("loads_code", line)
        if last in ("hook_method", "hook_all_methods", "hook_all_constructors", "hook_constructor"):
            self.add("java_hooks", line)
            if UI_HOOK_WORDS.search(argtext):
                self.add("ui_hooks", line)
        if last in ("add_hook", "add_post_request_hook", "add_request_hook", "hook_request"):
            self.add("intercepts_requests", line)
        if last in ("send_request", "sendRequest"):
            self.add("telegram_api", line)
            if SENSITIVE_TL.search(argtext):
                self.add("sensitive_requests", line)
            if re.search(r"(forwardMessages|messages_forward)", argtext, re.I):
                self.add("forward_messages", line)
            if re.search(r"(deleteMessages|messages_delete)", argtext, re.I):
                self.add("messages_delete", line)
            if re.search(r"(getHistory|messages_search|getMessages|getDialogs|contacts_get)", argtext, re.I):
                self.add("messages_read", line)
            if re.search(r"(auth_|resetAuthorization|exportAuthorization)", argtext, re.I):
                self.add("session_data", line)
        if last in SEND_NAMES:
            self.add("messages_send", line)
            if LEAK_WORDS.search(argtext):
                self.add("telegram_leak", line)
        if last in ("deleteMessages", "delete_messages"):
            self.add("messages_delete", line)
        if last in ("getMessages", "get_messages", "loadMessages", "getMessage", "get_message_history", "getDialogs"):
            self.add("messages_read", line)
        if last in ("forwardMessages", "forward_messages"):
            self.add("forward_messages", line)
        if last in ("get_file_loader", "loadFile", "load_file", "download_file"):
            self.add("telegram_files", line)
        if last in ("open", "remove", "unlink", "rmtree", "copyfile", "move", "rename", "makedirs", "write_file", "read_file", "delete_file") and not name.startswith("self."):
            self.add("files", line)
        if last in ("exit", "_exit", "killProcess", "restart", "kill", "quit") and (
                name in ("sys.exit", "os._exit", "exit", "quit") or "Process" in name or "System" in name or "restart" in name.lower()):
            self.add("app_exit", line)
        if last in ("loadPluginFromFile", "deletePlugin", "setPluginEnabled", "installPlugin", "uninstallPlugin"):
            self.add("persistence", line)
        if last == "setApplicationLabel" or ("edit" in last.lower() and "Settings" in name) or "SharedConfig" in name and last.startswith("set"):
            self.add("app_settings", line)
        if last in ("getLastKnownLocation", "getLine1Number", "getDeviceId", "getImei"):
            self.add("privacy", line)
        if last in ("sendTextMessage", "sendMultipartTextMessage"):
            self.add("sms", line)
        if last in ("addJavascriptInterface",):
            self.add("webview_bridge", line)
        if last in ("startActivity", "startActivityForResult", "sendBroadcast"):
            self.add("intents", line)
        if last == "setFlags" and "FLAG_SECURE" in argtext or last == "clearFlags" and "FLAG_SECURE" in argtext:
            self.add("screen_capture", line)
        if last in ("registerReceiver", "startService", "startForegroundService", "JobScheduler"):
            self.add("background", line)
        if "getInstalledPackages" in last or "installPackage" in last:
            self.add("installs_apps", line)
        if last in ("add_on_send_message_hook",):
            self.add("intercepts_requests", line)
        # the network
        root = name.split(".")[0]
        if root in NETWORK_ROOTS or last in ("urlopen", "create_connection", "getaddrinfo") or name.startswith("requests."):
            self.add("network", line)
            if DEVICE_WORDS.search(argtext):
                self.add("device_id_upload", line)
            if LEAK_WORDS.search(argtext):
                self.add("session_data", line)
            if MESSAGE_WORDS.search(argtext) and any(k.arg in ("json", "data", "params", "files") for k in node.keywords):
                self.add("exfiltrate_messages", line)
            if any(k.arg == "files" for k in node.keywords):
                self.add("exfiltrate_files", line)
        self.generic_visit(node)

    def visit_Constant(self, node):
        value = node.value
        if isinstance(value, str) and len(value) > 2000 and re.fullmatch(r"[A-Za-z0-9+/=\s\\]+", value):
            blob = self._decode(value)
            if blob is not None:
                if blob.startswith(DANGEROUS_BLOB):
                    self.add("embedded_code", node.lineno)
                elif b"import " in blob[:4000] or b"def " in blob[:4000]:
                    self.add("embedded_code", node.lineno)
        elif isinstance(value, str):
            if re.search(r"(InMemoryDexClassLoader|DexClassLoader|PathClassLoader)", value):
                self.add("loads_code", node.lineno)
            if re.search(r"(seed phrase|mnemonic|private key|wallet\.dat)", value, re.I):
                self.add("wallet_access", node.lineno)
            if "FLAG_SECURE" in value:
                self.add("screen_capture", node.lineno)
        self.generic_visit(node)

    @staticmethod
    def _decode(text):
        try:
            raw = base64.b64decode(re.sub(r"[\s\\]", "", text), validate=False)
        except Exception:
            return None
        for data in (raw,):
            try:
                return zlib.decompress(data)[:8000]
            except Exception:
                pass
        return raw[:8000]


def _function_flags(tree, scan):
    """Combinations that are worse than their parts."""
    for fn in scan.functions:
        names = set()
        for node in ast.walk(fn):
            if isinstance(node, ast.Call):
                names.add(_dotted(node.func))
        texts = " ".join(sorted(names))
        fetches = bool(re.search(r"(requests\.|urlopen|urllib|httpx|aiohttp)", texts))
        runs = bool(re.search(r"(^|\.)(exec|eval|compile|loadClass|DexClassLoader|InMemoryDexClassLoader)", texts))
        if fetches and runs:
            scan.add("remote_code", fn.lineno)
        serves = bool(re.search(r"(\.bind|\.listen|\.accept|websocket|WebSocketApp|create_server)", texts))
        acts = bool(re.search(r"(send_message|send_request|send_text|exec|eval|subprocess)", texts))
        if serves and acts:
            scan.add("remote_control", fn.lineno)


def analyze(path, lang="ru"):
    with open(path, "rb") as f:
        data = f.read()
    text = data.decode("utf-8", "replace")
    lines = text.splitlines()
    scan = _Scan(lines)
    tree = None
    try:
        tree = ast.parse(text)
    except SyntaxError as error:
        scan.add("hidden_parts", error.lineno or 1)
    if tree is not None:
        scan.visit(tree)
        _function_flags(tree, scan)
    found = scan.found
    longest = max((len(l) for l in lines), default=0)
    if longest > 6000 and "embedded_code" not in found and "anti_analysis" not in found:
        index = next(i for i, l in enumerate(lines) if len(l) == longest) + 1
        if not re.fullmatch(r'[\s\w+/=\\"\']*', lines[index - 1][:200]):
            scan.add("hidden_parts", index)
    if scan.try_pass >= 3:
        scan.add("hides_behavior", 1)
    if "session_data" in found and "network" in found and ("exfiltrate_messages" in found or "telegram_leak" in found):
        scan.add("account_takeover", found["session_data"][0][0])
    if "telegram_leak" in found and re.search(r"(session|auth_?key)", text, re.I):
        scan.add("session_via_telegram", found["telegram_leak"][0][0])
    if "sensitive_requests" in found and "hides_behavior" in found:
        scan.add("hidden_account_actions", found["sensitive_requests"][0][0])
    packed = [i for i, l in enumerate(lines, 1) if re.fullmatch(r"#\s*[A-Za-z0-9+/=]{60,}", l.strip())]
    if len(packed) >= 20:
        scan.add("embedded_code", packed[0])
    index = 0 if lang == "ru" else 1
    result = []
    for fid, entries in found.items():
        category, level, rn, rt, en, et = CATALOG[fid]
        result.append({
            "id": fid,
            "category": category,
            "level": level,
            "name": (rn, en)[index],
            "text": (rt, et)[index],
            "snippets": [{"line": l, "code": c} for l, c in sorted(entries)[:4]],
            "count": len(entries),
        })
    order = {DANGER: 0, WARN: 1, INFO: 2}
    result.sort(key=lambda r: (order[r["level"]], r["category"], r["id"]))
    counts = {level: sum(1 for r in result if r["level"] == level) for level in (DANGER, WARN, INFO)}
    if counts[DANGER]:
        verdict = "danger"
    elif counts[WARN]:
        verdict = "caution"
    else:
        verdict = "safe"
    return {
        "verdict": verdict,
        "counts": counts,
        "findings": result,
        "categories": {k: v[index] for k, v in CATEGORIES.items()},
        "lines": len(lines),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def main(path):
    print(json.dumps(analyze(path), ensure_ascii=False, indent=1))


if __name__ == "__main__":
    import sys
    main(sys.argv[1])
