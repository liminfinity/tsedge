import type { LiveState } from "@/lib/schema";
import { formatBytes, formatNumber, formatPercent } from "@/lib/format";

export type LoadSample = {
  receivedAt: number;
  totalPoints: number;
  bufferPercent: number;
  walEntries: number;
  segmentCount: number;
  blockCount: number;
};

export function makeLoadSample(state: LiveState): LoadSample {
  return {
    receivedAt: Date.now(),
    totalPoints: state.agent.total_points_written,
    bufferPercent: Math.max(0, Math.min(100, state.buffer.percent)),
    walEntries: state.wal.entries_since_flush,
    segmentCount: state.segments.length,
    blockCount: state.segments.reduce((sum, segment) => sum + segment.blocks, 0)
  };
}

export function LoadPanel({ state, history }: { state: LiveState; history: LoadSample[] }) {
  const last = history.at(-1);
  const prev = history.length > 1 ? history.at(-2) : undefined;
  const elapsedSeconds = last && prev ? Math.max(1, (last.receivedAt - prev.receivedAt) / 1000) : 1;
  const pointsPerSecond = last && prev ? Math.max(0, (last.totalPoints - prev.totalPoints) / elapsedSeconds) : 0;
  const bufferBytes = state.buffer.points * state.last_batch.point_count * 16;
  const blockCount = state.segments.reduce((sum, segment) => sum + segment.blocks, 0);

  return (
    <section className="panel rounded-xl p-5">
      <div className="flex items-center justify-between gap-3">
        <h2 className="text-base font-semibold">Нагрузка</h2>
      </div>

      <div className="mt-4 grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
        <Metric label="Скорость записи" value={`${formatNumber(Math.round(pointsPerSecond))} точек/с`} />
        <Metric label="Буфер в памяти" value={formatBytes(bufferBytes)} />
        <Metric label="Ждут записи на диск" value={formatNumber(state.wal.entries_since_flush)} />
        <Metric label="Blocks / segments" value={`${formatNumber(blockCount)} / ${formatNumber(state.segments.length)}`} />
      </div>

      <div className="mt-4 grid gap-4 lg:grid-cols-3">
        <MiniChart title="Буфер" unit="%" values={history.map((sample) => sample.bufferPercent)} format={(value) => formatPercent(value)} />
        <MiniChart title="WAL" unit="записей" values={history.map((sample) => sample.walEntries)} format={(value) => formatNumber(Math.round(value))} />
        <MiniChart title="Segment-файлы" unit="шт." values={history.map((sample) => sample.segmentCount)} format={(value) => formatNumber(Math.round(value))} />
      </div>
    </section>
  );
}

function Metric({ label, value }: { label: string; value: string }) {
  return (
    <div className="rounded-lg border border-slate-200 bg-white px-4 py-3">
      <div className="text-xs text-slate-500">{label}</div>
      <div className="mt-1 text-lg font-semibold text-slate-950">{value}</div>
    </div>
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
