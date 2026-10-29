"""Desktop stand-ins for the Android decoders plugins use on replied media.

MediaMetadataRetriever decodes video / GIF-as-mp4 / webm through PyAV, and
RLottieDrawable renders .tgs through rlottie-python. Both return the same
Bitmap objects as the rest of _compat, so a plugin's own code keeps working.
"""
import gzip
import json
import os


def _bitmap(image):
    import _compat
    return _compat._Bitmap(image.convert("RGBA"))


def _path(value):
    return str(getattr(value, "_path", value))


class MediaMetadataRetriever:
    METADATA_KEY_DURATION = 9
    METADATA_KEY_VIDEO_FRAME_COUNT = 32
    METADATA_KEY_VIDEO_WIDTH = 18
    METADATA_KEY_VIDEO_HEIGHT = 19
    OPTION_CLOSEST = 3
    OPTION_CLOSEST_SYNC = 2
    OPTION_NEXT_SYNC = 1
    OPTION_PREVIOUS_SYNC = 0

    def __init__(self):
        self._path = None
        self._frames = None

    def setDataSource(self, path, *args):
        self._path = _path(path)
        self._frames = None

    def _decode(self, limit=None):
        import av
        frames = []
        with av.open(self._path) as container:
            for frame in container.decode(video=0):
                frames.append(frame.to_image())
                if limit and len(frames) >= limit:
                    break
        return frames

    def _all(self):
        if self._frames is None:
            self._frames = self._decode()
        return self._frames

    def extractMetadata(self, key):
        import av
        with av.open(self._path) as container:
            stream = container.streams.video[0]
            if key == self.METADATA_KEY_DURATION:
                duration = container.duration
                if duration:
                    return str(int(duration / 1000))
                frames = len(self._all())
                rate = float(stream.average_rate or 25)
                return str(int(frames * 1000 / rate))
            if key == self.METADATA_KEY_VIDEO_FRAME_COUNT:
                return str(len(self._all()))
            if key == self.METADATA_KEY_VIDEO_WIDTH:
                return str(stream.codec_context.width)
            if key == self.METADATA_KEY_VIDEO_HEIGHT:
                return str(stream.codec_context.height)
        return None

    def getFrameAtIndex(self, index):
        frames = self._all()
        if not frames:
            return None
        return _bitmap(frames[max(0, min(int(index), len(frames) - 1))])

    def getFramesAtIndex(self, start, count):
        import _compat
        frames = self._all()[int(start):int(start) + int(count)]
        return _compat._JList([_bitmap(frame) for frame in frames])

    def getFrameAtTime(self, time_us=0, option=0):
        import av
        frames = self._all()
        if not frames:
            return None
        with av.open(self._path) as container:
            rate = float(container.streams.video[0].average_rate or 25)
        index = int(float(time_us) / 1e6 * rate)
        return _bitmap(frames[max(0, min(index, len(frames) - 1))])

    def release(self):
        self._frames = None


class AnimatedFileDrawable:
    """Telegram's own decoder; plugins catch this and use the retriever."""

    def __init__(self, *args, **kwargs):
        raise RuntimeError("AnimatedFileDrawable is not available on desktop")


class RLottieDrawable:
    def __init__(self, file, *args, **kwargs):
        from rlottie_python import LottieAnimation
        numbers = [a for a in args if isinstance(a, int)]
        self._size = (numbers[0], numbers[1]) if len(numbers) >= 2 else (512, 512)
        path = _path(file)
        self._anim = None
        data = None
        try:
            with gzip.open(path, "rb") as stream:
                data = stream.read().decode("utf-8")
        except OSError:
            with open(path, "rb") as stream:
                data = stream.read().decode("utf-8", "ignore")
        self._anim = LottieAnimation.from_data(data)
        self._frames = int(self._anim.lottie_animation_get_totalframe())
        self._duration = int(float(self._anim.lottie_animation_get_duration()) * 1000)
        self._index = 0
        self._image = None

    def getFramesCount(self):
        return self._frames

    def getDuration(self):
        return self._duration

    def getDurationMs(self):
        return self._duration

    def setAllowDecodeSingleFrame(self, *args):
        pass

    def setInvalidateOnProgressSet(self, *args):
        pass

    def setAutoRepeat(self, *args):
        pass

    def start(self):
        pass

    def stop(self):
        pass

    def updateCurrentFrame(self, *args):
        pass

    def setCurrentFrame(self, index, async_=False, reset=False):
        self._index = max(0, min(int(index), self._frames - 1))
        self._image = self._anim.render_pillow_frame(
            frame_num=self._index, width=self._size[0], height=self._size[1])

    def getAnimatedBitmap(self):
        if self._image is None:
            self.setCurrentFrame(0)
        return _bitmap(self._image)

    def recycle(self):
        self._anim = None


class ImageLoader:
    @staticmethod
    def decompressGzip(file):
        try:
            with gzip.open(_path(file), "rb") as stream:
                return stream.read().decode("utf-8")
        except OSError:
            return None
