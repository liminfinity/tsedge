from __future__ import annotations

import ctypes
import os
import sys
from importlib import resources
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import List, Optional, Sequence, Tuple, Union


TSEDGE_OK = 0
TSEDGE_ERR_CORRUPT = -5


class TSEdgeError(Exception):
    """Raised when a TSEdge C API call returns a negative status code."""

    def __init__(self, code: int, message: str):
        self.code = code
        self.message = message
        super().__init__(f"{message} ({code})")


class Aggregate(Enum):
    MIN = 0
    MAX = 1
    SUM = 2
    AVG = 3
    COUNT = 4


class Durability(Enum):
    FAST = 0
    BALANCED = 1
    STRICT = 2


@dataclass(frozen=True)
class Point:
    timestamp: int
    value: float


@dataclass(frozen=True)
class WindowAggregate:
    window_start: int
    window_end: int
    count: int
    min: float
    max: float
    avg: float


@dataclass(frozen=True)
class SeriesStats:
    block_count: int
    buffered_points: int
    total_indexed_points: int
    segment_count: int
    has_time_range: bool
    min_timestamp: int
    max_timestamp: int
    active_segment_id: int
    segment_size_bytes: int
    total_segment_size_bytes: int
    raw_size_estimate_bytes: Optional[int] = None
    compressed_size_bytes: Optional[int] = None
    compression_ratio: Optional[float] = None
    bytes_per_point: Optional[float] = None


@dataclass(frozen=True)
class SeriesInfo:
    name: str
    total_points: int
    segment_count: int
    block_count: int
    compressed_size_bytes: int


@dataclass(frozen=True)
class VerifyReport:
    series_count: int
    segment_count: int
    block_count: int
    wal_entry_count: int
    error_count: int
    first_error_path: str
    first_error_message: str


class SeriesHandle:
    """Database-owned opaque handle returned by TSEdge.get_series_handle."""

    def __init__(self, db: "TSEdge", ptr: ctypes.c_void_p, series_name: str):
        self._db = db
        self._ptr = ptr
        self.series_name = series_name


class _CPoint(ctypes.Structure):
    _fields_ = [
        ("timestamp", ctypes.c_int64),
        ("value", ctypes.c_double),
    ]


class _CWindowAggregate(ctypes.Structure):
    _fields_ = [
        ("window_start", ctypes.c_int64),
        ("window_end", ctypes.c_int64),
        ("count", ctypes.c_uint64),
        ("min", ctypes.c_double),
        ("max", ctypes.c_double),
        ("avg", ctypes.c_double),
    ]


class _CSeriesStats(ctypes.Structure):
    _fields_ = [
        ("block_count", ctypes.c_size_t),
        ("buffered_points", ctypes.c_size_t),
        ("total_indexed_points", ctypes.c_size_t),
        ("segment_count", ctypes.c_size_t),
        ("has_time_range", ctypes.c_int),
        ("min_timestamp", ctypes.c_int64),
        ("max_timestamp", ctypes.c_int64),
        ("active_segment_id", ctypes.c_uint32),
        ("segment_size_bytes", ctypes.c_uint64),
        ("total_segment_size_bytes", ctypes.c_uint64),
        ("raw_size_estimate_bytes", ctypes.c_uint64),
        ("compressed_size_bytes", ctypes.c_uint64),
        ("compression_ratio", ctypes.c_double),
        ("bytes_per_point", ctypes.c_double),
    ]


class _CVerifyReport(ctypes.Structure):
    _fields_ = [
        ("series_count", ctypes.c_size_t),
        ("segment_count", ctypes.c_size_t),
        ("block_count", ctypes.c_size_t),
        ("wal_entry_count", ctypes.c_size_t),
        ("error_count", ctypes.c_size_t),
        ("first_error_path", ctypes.c_char * 256),
        ("first_error_message", ctypes.c_char * 256),
    ]


class _CSeriesInfo(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char * 256),
        ("total_points", ctypes.c_uint64),
        ("segment_count", ctypes.c_uint32),
        ("block_count", ctypes.c_uint32),
        ("compressed_size_bytes", ctypes.c_uint64),
    ]


_PointCallback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(_CPoint), ctypes.c_void_p)


def _encode(text: Union[str, os.PathLike[str]]) -> bytes:
    return os.fsencode(os.fspath(text))


def _decode(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def _parse_aggregate(value: Union[Aggregate, str, int]) -> int:
    if isinstance(value, Aggregate):
        return value.value
    if isinstance(value, int):
        return value
    name = value.strip().upper()
    try:
        return Aggregate[name].value
    except KeyError as exc:
        raise ValueError(f"unknown aggregate: {value}") from exc


def _parse_durability(value: Union[Durability, str, int]) -> int:
    if isinstance(value, Durability):
        return value.value
    if isinstance(value, int):
        return value
    name = value.strip().upper()
    try:
        return Durability[name].value
    except KeyError as exc:
        raise ValueError(f"unknown durability mode: {value}") from exc


def _library_names() -> List[str]:
    if sys.platform == "win32":
        return ["tsedge.dll"]
    if sys.platform == "darwin":
        return ["libtsedge.dylib", "libtsedge.so"]
    return ["libtsedge.so", "libtsedge.dylib"]


def _bundled_library_paths(names: Sequence[str]) -> List[Path]:
    paths: List[Path] = []
    try:
        native_root = resources.files(__package__).joinpath("native")
    except (AttributeError, ModuleNotFoundError, TypeError):
        native_root = Path(__file__).resolve().parent / "native"

    for name in names:
        candidate = native_root / name
        try:
            if candidate.is_file():
                paths.append(Path(str(candidate)))
        except OSError:
            continue
    return paths


def _candidate_library_paths(explicit: Optional[Union[str, os.PathLike[str]]]) -> List[Union[str, Path]]:
    names = _library_names()
    paths: List[Union[str, Path]] = []

    if explicit:
        paths.append(Path(explicit))
    env_path = os.environ.get("TSEDGE_LIBRARY")
    if env_path:
        paths.append(Path(env_path))

    paths.extend(_bundled_library_paths(names))

    package_dir = Path(__file__).resolve().parent
    repo_root = package_dir.parents[1]
    for base in [
        repo_root / "build",
        repo_root / "build" / "src",
        repo_root / "build" / "lib",
        repo_root.parent / "build",
        Path.cwd() / "build",
        Path.cwd() / "build" / "lib",
    ]:
        for name in names:
            paths.append(base / name)

    paths.extend(names)
    return paths


def _load_library(lib_path: Optional[Union[str, os.PathLike[str]]] = None) -> ctypes.CDLL:
    errors = []
    for candidate in _candidate_library_paths(lib_path):
        try:
            if isinstance(candidate, Path):
                if not candidate.exists():
                    continue
                return ctypes.CDLL(str(candidate))
            return ctypes.CDLL(candidate)
        except OSError as exc:
            errors.append(f"{candidate}: {exc}")
    detail = "\n".join(errors[-5:])
    hint = "Set TSEDGE_LIBRARY or pass lib_path to TSEdge.open()."
    raise TSEdgeError(-1, f"could not load TSEdge library. {hint}" + (f"\n{detail}" if detail else ""))


def _bind(lib: ctypes.CDLL) -> None:
    db_pp = ctypes.POINTER(ctypes.c_void_p)
    point_p = ctypes.POINTER(_CPoint)
    window_pp = ctypes.POINTER(ctypes.POINTER(_CWindowAggregate))
    series_info_pp = ctypes.POINTER(ctypes.POINTER(_CSeriesInfo))

    lib.tsedge_open.argtypes = [ctypes.c_char_p, db_pp]
    lib.tsedge_open.restype = ctypes.c_int
    lib.tsedge_close.argtypes = [ctypes.c_void_p]
    lib.tsedge_close.restype = ctypes.c_int
    lib.tsedge_create_series.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.tsedge_create_series.restype = ctypes.c_int
    lib.tsedge_delete_series.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.tsedge_delete_series.restype = ctypes.c_int
    lib.tsedge_append.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int64, ctypes.c_double]
    lib.tsedge_append.restype = ctypes.c_int
    lib.tsedge_append_batch.argtypes = [ctypes.c_void_p, ctypes.c_char_p, point_p, ctypes.c_size_t]
    lib.tsedge_append_batch.restype = ctypes.c_int
    lib.tsedge_get_series_handle.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.tsedge_get_series_handle.restype = ctypes.c_int
    lib.tsedge_append_handle.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int64, ctypes.c_double]
    lib.tsedge_append_handle.restype = ctypes.c_int
    lib.tsedge_append_batch_handle.argtypes = [ctypes.c_void_p, ctypes.c_void_p, point_p, ctypes.c_size_t]
    lib.tsedge_append_batch_handle.restype = ctypes.c_int
    lib.tsedge_flush.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.tsedge_flush.restype = ctypes.c_int
    lib.tsedge_flush_all.argtypes = [ctypes.c_void_p]
    lib.tsedge_flush_all.restype = ctypes.c_int
    lib.tsedge_read_range.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int64,
        ctypes.c_int64,
        _PointCallback,
        ctypes.c_void_p,
    ]
    lib.tsedge_read_range.restype = ctypes.c_int
    lib.tsedge_aggregate.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int64,
        ctypes.c_int64,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_double),
    ]
    lib.tsedge_aggregate.restype = ctypes.c_int
    lib.tsedge_aggregate_windowed.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_int64,
        ctypes.c_int64,
        ctypes.c_int64,
        window_pp,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    lib.tsedge_aggregate_windowed.restype = ctypes.c_int
    lib.tsedge_free_window_aggregates.argtypes = [ctypes.POINTER(_CWindowAggregate)]
    lib.tsedge_free_window_aggregates.restype = None
    lib.tsedge_get_series_stats.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(_CSeriesStats)]
    lib.tsedge_get_series_stats.restype = ctypes.c_int
    lib.tsedge_list_series.argtypes = [ctypes.c_void_p, series_info_pp, ctypes.POINTER(ctypes.c_size_t)]
    lib.tsedge_list_series.restype = ctypes.c_int
    lib.tsedge_free_series_list.argtypes = [ctypes.POINTER(_CSeriesInfo)]
    lib.tsedge_free_series_list.restype = None
    lib.tsedge_delete_before.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int64]
    lib.tsedge_delete_before.restype = ctypes.c_int
    lib.tsedge_verify.argtypes = [ctypes.c_char_p, ctypes.POINTER(_CVerifyReport)]
    lib.tsedge_verify.restype = ctypes.c_int
    lib.tsedge_export_csv.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int64, ctypes.c_int64, ctypes.c_char_p]
    lib.tsedge_export_csv.restype = ctypes.c_int
    lib.tsedge_set_durability.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.tsedge_set_durability.restype = ctypes.c_int
    lib.tsedge_strerror.argtypes = [ctypes.c_int]
    lib.tsedge_strerror.restype = ctypes.c_char_p

    for name, argtypes in {
        "tsedge_set_disk_quota": [ctypes.c_void_p, ctypes.c_uint64],
        "tsedge_get_disk_quota": [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64)],
        "tsedge_enforce_disk_quota": [ctypes.c_void_p],
    }.items():
        try:
            func = getattr(lib, name)
        except AttributeError:
            continue
        func.argtypes = argtypes
        func.restype = ctypes.c_int


def _copy_stats(stats: _CSeriesStats) -> SeriesStats:
    return SeriesStats(
        block_count=int(stats.block_count),
        buffered_points=int(stats.buffered_points),
        total_indexed_points=int(stats.total_indexed_points),
        segment_count=int(stats.segment_count),
        has_time_range=bool(stats.has_time_range),
        min_timestamp=int(stats.min_timestamp),
        max_timestamp=int(stats.max_timestamp),
        active_segment_id=int(stats.active_segment_id),
        segment_size_bytes=int(stats.segment_size_bytes),
        total_segment_size_bytes=int(stats.total_segment_size_bytes),
        raw_size_estimate_bytes=int(stats.raw_size_estimate_bytes),
        compressed_size_bytes=int(stats.compressed_size_bytes),
        compression_ratio=float(stats.compression_ratio),
        bytes_per_point=float(stats.bytes_per_point),
    )


def _copy_report(report: _CVerifyReport) -> VerifyReport:
    return VerifyReport(
        series_count=int(report.series_count),
        segment_count=int(report.segment_count),
        block_count=int(report.block_count),
        wal_entry_count=int(report.wal_entry_count),
        error_count=int(report.error_count),
        first_error_path=_decode(bytes(report.first_error_path)),
        first_error_message=_decode(bytes(report.first_error_message)),
    )


def _points_array(points: Sequence[Union[Point, Tuple[int, float]]]) -> Tuple[Optional[ctypes.Array], int]:
    if not points:
        return None, 0
    array_type = _CPoint * len(points)
    array = array_type()
    for i, point in enumerate(points):
        if isinstance(point, Point):
            timestamp, value = point.timestamp, point.value
        else:
            timestamp, value = point
        array[i].timestamp = int(timestamp)
        array[i].value = float(value)
    return array, len(points)


class TSEdge:
    """Object-oriented ctypes wrapper around the embedded TSEdge C API."""

    def __init__(self, lib: ctypes.CDLL, db: ctypes.c_void_p, path: str):
        self._lib = lib
        self._db = db
        self._path = path
        self._closed = False

    @classmethod
    def open(cls, path: Union[str, os.PathLike[str]], lib_path: Optional[Union[str, os.PathLike[str]]] = None) -> "TSEdge":
        lib = _load_library(lib_path)
        _bind(lib)
        db = ctypes.c_void_p()
        rc = lib.tsedge_open(_encode(path), ctypes.byref(db))
        if rc != TSEDGE_OK:
            raise TSEdgeError(rc, cls._message_from_lib(lib, rc))
        return cls(lib, db, os.fspath(path))

    @classmethod
    def verify_path(
        cls,
        path: Union[str, os.PathLike[str]],
        lib_path: Optional[Union[str, os.PathLike[str]]] = None,
        raise_on_error: bool = False,
    ) -> VerifyReport:
        lib = _load_library(lib_path)
        _bind(lib)
        report = _CVerifyReport()
        rc = lib.tsedge_verify(_encode(path), ctypes.byref(report))
        if rc != TSEDGE_OK and (raise_on_error or rc != TSEDGE_ERR_CORRUPT):
            raise TSEdgeError(rc, cls._message_from_lib(lib, rc))
        return _copy_report(report)

    def __enter__(self) -> "TSEdge":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    @staticmethod
    def _message_from_lib(lib: ctypes.CDLL, code: int) -> str:
        raw = lib.tsedge_strerror(code)
        return raw.decode("utf-8", errors="replace") if raw else f"error {code}"

    def _check(self, rc: int) -> None:
        if rc != TSEDGE_OK:
            raise TSEdgeError(rc, self._message_from_lib(self._lib, rc))

    def _require_open(self) -> ctypes.c_void_p:
        if self._closed or not self._db:
            raise TSEdgeError(-1, "database is closed")
        return self._db

    def create_series(self, name: str) -> None:
        self._check(self._lib.tsedge_create_series(self._require_open(), _encode(name)))

    def delete_series(self, name: str) -> None:
        self._check(self._lib.tsedge_delete_series(self._require_open(), _encode(name)))

    def append(self, series: str, timestamp: int, value: float) -> None:
        self._check(self._lib.tsedge_append(self._require_open(), _encode(series), int(timestamp), float(value)))

    def append_batch(self, series: str, points: Sequence[Union[Point, Tuple[int, float]]]) -> None:
        array, count = _points_array(points)
        if count == 0:
            self._check(self._lib.tsedge_append_batch(self._require_open(), _encode(series), None, 0))
            return
        self._check(self._lib.tsedge_append_batch(self._require_open(), _encode(series), array, count))

    def get_series_handle(self, series: str) -> SeriesHandle:
        handle = ctypes.c_void_p()
        self._check(self._lib.tsedge_get_series_handle(self._require_open(), _encode(series), ctypes.byref(handle)))
        return SeriesHandle(self, handle, series)

    def append_handle(self, handle: SeriesHandle, timestamp: int, value: float) -> None:
        self._check(self._lib.tsedge_append_handle(self._require_open(), handle._ptr, int(timestamp), float(value)))

    def append_batch_handle(self, handle: SeriesHandle, points: Sequence[Union[Point, Tuple[int, float]]]) -> None:
        array, count = _points_array(points)
        if count == 0:
            self._check(self._lib.tsedge_append_batch_handle(self._require_open(), handle._ptr, None, 0))
            return
        self._check(self._lib.tsedge_append_batch_handle(self._require_open(), handle._ptr, array, count))

    def flush(self, series: str) -> None:
        self._check(self._lib.tsedge_flush(self._require_open(), _encode(series)))

    def flush_all(self) -> None:
        self._check(self._lib.tsedge_flush_all(self._require_open()))

    def read_range(self, series: str, start: int, end: int) -> List[Point]:
        points: List[Point] = []
        callback_error: List[BaseException] = []

        @_PointCallback
        def callback(point_ptr, _user_data):
            try:
                point = point_ptr.contents
                points.append(Point(int(point.timestamp), float(point.value)))
                return 0
            except BaseException as exc:  # pragma: no cover - defensive path.
                callback_error.append(exc)
                return 1

        rc = self._lib.tsedge_read_range(self._require_open(), _encode(series), int(start), int(end), callback, None)
        if callback_error:
            raise callback_error[0]
        self._check(rc)
        return points

    def aggregate(self, series: str, start: int, end: int, agg: Union[Aggregate, str, int]) -> float:
        result = ctypes.c_double()
        self._check(
            self._lib.tsedge_aggregate(
                self._require_open(),
                _encode(series),
                int(start),
                int(end),
                _parse_aggregate(agg),
                ctypes.byref(result),
            )
        )
        return float(result.value)

    def aggregate_windowed(self, series: str, start_time: int, end_time: int, window_size: int) -> List[WindowAggregate]:
        out_windows = ctypes.POINTER(_CWindowAggregate)()
        out_count = ctypes.c_size_t()
        rc = self._lib.tsedge_aggregate_windowed(
            self._require_open(),
            _encode(series),
            int(start_time),
            int(end_time),
            int(window_size),
            ctypes.byref(out_windows),
            ctypes.byref(out_count),
        )
        self._check(rc)
        if not out_windows or out_count.value == 0:
            return []
        try:
            result = []
            for i in range(out_count.value):
                item = out_windows[i]
                result.append(
                    WindowAggregate(
                        window_start=int(item.window_start),
                        window_end=int(item.window_end),
                        count=int(item.count),
                        min=float(item.min),
                        max=float(item.max),
                        avg=float(item.avg),
                    )
                )
            return result
        finally:
            self._lib.tsedge_free_window_aggregates(out_windows)

    def get_series_stats(self, series: str) -> SeriesStats:
        stats = _CSeriesStats()
        self._check(self._lib.tsedge_get_series_stats(self._require_open(), _encode(series), ctypes.byref(stats)))
        return _copy_stats(stats)

    def list_series(self) -> List[SeriesInfo]:
        out_series = ctypes.POINTER(_CSeriesInfo)()
        out_count = ctypes.c_size_t()
        self._check(self._lib.tsedge_list_series(self._require_open(), ctypes.byref(out_series), ctypes.byref(out_count)))
        if not out_series or out_count.value == 0:
            return []
        try:
            return [
                SeriesInfo(
                    name=_decode(bytes(out_series[i].name)),
                    total_points=int(out_series[i].total_points),
                    segment_count=int(out_series[i].segment_count),
                    block_count=int(out_series[i].block_count),
                    compressed_size_bytes=int(out_series[i].compressed_size_bytes),
                )
                for i in range(out_count.value)
            ]
        finally:
            self._lib.tsedge_free_series_list(out_series)

    def delete_before(self, series: str, older_than_timestamp: int) -> None:
        self._check(self._lib.tsedge_delete_before(self._require_open(), _encode(series), int(older_than_timestamp)))

    def verify(self, db_path: Optional[Union[str, os.PathLike[str]]] = None, raise_on_error: bool = False) -> VerifyReport:
        path = self._path if db_path is None else os.fspath(db_path)
        report = _CVerifyReport()
        rc = self._lib.tsedge_verify(_encode(path), ctypes.byref(report))
        if rc != TSEDGE_OK and (raise_on_error or rc != TSEDGE_ERR_CORRUPT):
            raise TSEdgeError(rc, self._message_from_lib(self._lib, rc))
        return _copy_report(report)

    def export_csv(self, series: str, start: int, end: int, output_path: Union[str, os.PathLike[str]]) -> None:
        self._check(
            self._lib.tsedge_export_csv(
                self._require_open(),
                _encode(series),
                int(start),
                int(end),
                _encode(output_path),
            )
        )

    def set_durability(self, mode: Union[Durability, str, int]) -> None:
        self._check(self._lib.tsedge_set_durability(self._require_open(), _parse_durability(mode)))

    def set_disk_quota(self, max_bytes: int) -> None:
        func = getattr(self._lib, "tsedge_set_disk_quota", None)
        if func is None:
            raise TSEdgeError(-1, "disk quota API is not available in this TSEdge library")
        self._check(func(self._require_open(), int(max_bytes)))

    def get_disk_quota(self) -> int:
        func = getattr(self._lib, "tsedge_get_disk_quota", None)
        if func is None:
            raise TSEdgeError(-1, "disk quota API is not available in this TSEdge library")
        value = ctypes.c_uint64()
        self._check(func(self._require_open(), ctypes.byref(value)))
        return int(value.value)

    def enforce_disk_quota(self) -> None:
        func = getattr(self._lib, "tsedge_enforce_disk_quota", None)
        if func is None:
            raise TSEdgeError(-1, "disk quota API is not available in this TSEdge library")
        self._check(func(self._require_open()))

    def close(self) -> None:
        if self._closed or not self._db:
            return
        db = self._db
        self._db = ctypes.c_void_p()
        self._closed = True
        self._check(self._lib.tsedge_close(db))


def verify_database(path: Union[str, os.PathLike[str]], lib_path: Optional[Union[str, os.PathLike[str]]] = None) -> VerifyReport:
    return TSEdge.verify_path(path, lib_path=lib_path)
