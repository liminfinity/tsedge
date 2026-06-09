import type { LiveState } from "@/lib/schema";
import { formatBytes, formatNumber } from "@/lib/format";

export type LoadSample = {
  receivedAt: number;
  totalPoints: number;
  bufferBytes: number;
  walEntries: number;
  diskBytes: number;
};

export function makeLoadSample(state: LiveState): LoadSample {
  const bufferBytes = state.buffer.points * state.last_batch.point_count * 16;
  return {
    receivedAt: Date.now(),
    totalPoints: state.agent.total_points_written,
    bufferBytes,
    walEntries: state.wal.entries_since_flush,
    diskBytes: state.storage.compressed_size_bytes
  };
}

export function LoadPanel({ history }: { history: LoadSample[] }) {
  const last = history.at(-1);
  const prev = history.length > 1 ? history.at(-2) : undefined;
  const elapsedSeconds = last && prev ? Math.max(1, (last.receivedAt - prev.receivedAt) / 1000) : 1;
  const pointsPerSecond = last && prev ? Math.max(0, (last.totalPoints - prev.totalPoints) / elapsedSeconds) : 0;
  const speedHistory = history.map((sample, index) => {
    if (index === 0) {
      return 0;
    }
    const previous = history[index - 1];
    const seconds = Math.max(1, (sample.receivedAt - previous.receivedAt) / 1000);
    return Math.max(0, (sample.totalPoints - previous.totalPoints) / seconds);
  });

  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center justify-between gap-3">
        <div>
          <h2 className="text-lg font-semibold">Нагрузка на устройство</h2>
          <p className="mt-1 text-sm text-slate-600">
            Как запись в TSEdge влияет на память, диск и очередь WAL.
          </p>
        </div>
        <div className="rounded-full bg-slate-100 px-3 py-1 text-sm font-medium text-slate-700">
          {formatNumber(Math.round(pointsPerSecond))} точек/с
        </div>
      </div>

      <div className="mt-5 grid gap-4 md:grid-cols-2 xl:grid-cols-4">
        <MiniChart title="Память буфера" unit="RAM" values={history.map((sample) => sample.bufferBytes)} format={formatBytes} />
        <MiniChart title="Очередь WAL" unit="записей" values={history.map((sample) => sample.walEntries)} format={(value) => formatNumber(Math.round(value))} />
        <MiniChart title="Размер на диске" unit="segment-файлы" values={history.map((sample) => sample.diskBytes)} format={formatBytes} />
        <MiniChart title="Скорость записи" unit="точек/с" values={speedHistory} format={(value) => formatNumber(Math.round(value))} />
      </div>

    </section>
  );
}

function MiniChart({
  title,
  unit,
  values,
  format
}: {
  title: string;
  unit: string;
  values: number[];
  format: (value: number) => string;
}) {
  const data = values.length ? values : [0];
  const min = Math.min(...data);
  const max = Math.max(...data);
  const range = Math.max(1, max - min);
  const width = 320;
  const height = 92;
  const points = data
    .map((value, index) => {
      const x = data.length === 1 ? width : (index / (data.length - 1)) * width;
      const y = height - ((value - min) / range) * (height - 12) - 6;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");
  const current = data[data.length - 1] ?? 0;

  return (
    <div className="rounded-lg border border-slate-200 bg-white p-4">
      <div className="flex items-center justify-between gap-3">
        <div>
          <div className="font-medium text-slate-900">{title}</div>
          <div className="text-xs text-slate-500">{unit}</div>
        </div>
        <div className="text-sm font-semibold text-slate-950">{format(current)}</div>
      </div>
      <svg viewBox={`0 0 ${width} ${height}`} className="mt-3 h-24 w-full overflow-visible" role="img" aria-label={`${title}: ${format(current)}`}>
        <line x1="0" y1={height - 6} x2={width} y2={height - 6} stroke="#e2e8f0" strokeWidth="2" />
        <polyline points={points} fill="none" stroke="#0284c7" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
    </div>
  );
}
