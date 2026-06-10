import { stationMetrics, toneClass } from "./stationData";
import type { Tone } from "./stationTypes";

export function MetricCard({
  metric,
  value = 0,
  history
}: {
  metric: (typeof stationMetrics)[number];
  value?: number;
  history: number[];
}) {
  const Icon = metric.icon;
  const tone = metric.name === "pm25.concentration" ? (value > 75 ? "bad" : value > 35 ? "warn" : "good") : metric.name === "battery.voltage" ? (value < 3.4 ? "bad" : value < 3.65 ? "warn" : "good") : "neutral";

  return (
    <article className="panel rounded-2xl p-5">
      <div className="flex items-start justify-between gap-4">
        <div>
          <div className="flex items-center gap-2 text-sm text-slate-500">
            <Icon size={18} />
            {metric.title}
          </div>
          <div className={`mt-3 text-4xl font-semibold tracking-tight ${toneClass(tone)}`}>
            {value.toFixed(metric.decimals)}
            <span className="ml-2 text-lg font-medium text-slate-500">{metric.unit}</span>
          </div>
        </div>
      </div>
      <Sparkline values={history} tone={tone} />
    </article>
  );
}

function Sparkline({ values, tone }: { values: number[]; tone: Tone }) {
  const data = values.length ? values : [0];
  const min = Math.min(...data);
  const max = Math.max(...data);
  const range = Math.max(1, max - min);
  const width = 300;
  const height = 64;
  const points = data
    .map((value, index) => {
      const x = data.length === 1 ? width : (index / (data.length - 1)) * width;
      const y = height - ((value - min) / range) * (height - 10) - 5;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");
  const stroke = tone === "bad" ? "#e11d48" : tone === "warn" ? "#d97706" : tone === "good" ? "#059669" : "#0284c7";

  return (
    <svg viewBox={`0 0 ${width} ${height}`} className="mt-4 h-16 w-full" aria-hidden="true">
      <line x1="0" y1={height - 5} x2={width} y2={height - 5} stroke="#e2e8f0" strokeWidth="2" />
      <polyline points={points} fill="none" stroke={stroke} strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}
