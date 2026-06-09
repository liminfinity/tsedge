"use client";

import { useCallback, useEffect, useMemo, useState } from "react";
import { AlertTriangle, Battery, Cloud, Download, Droplets, Gauge, Radio, Thermometer, Wind, Wifi, WifiOff } from "lucide-react";
import type { LucideIcon } from "lucide-react";
import { ViewNav } from "@/components/ViewNav";
import { formatClock } from "@/lib/format";
import type { DemoCommand, LiveState } from "@/lib/schema";

type StationSample = {
  totalPoints: number;
  values: Record<string, number>;
};

const metrics: Array<{
  name: string;
  title: string;
  unit: string;
  icon: LucideIcon;
  decimals: number;
}> = [
  { name: "air.temperature", title: "Температура", unit: "°C", icon: Thermometer, decimals: 1 },
  { name: "air.humidity", title: "Влажность", unit: "%", icon: Droplets, decimals: 0 },
  { name: "air.pressure", title: "Давление", unit: "гПа", icon: Gauge, decimals: 0 },
  { name: "wind.speed", title: "Ветер", unit: "м/с", icon: Wind, decimals: 1 },
  { name: "pm25.concentration", title: "PM2.5", unit: "мкг/м³", icon: Cloud, decimals: 1 },
  { name: "battery.voltage", title: "Аккумулятор", unit: "В", icon: Battery, decimals: 2 }
];

export default function StationPage() {
  const [state, setState] = useState<LiveState | null>(null);
  const [error, setError] = useState("");
  const [lastReceivedAt, setLastReceivedAt] = useState<number | null>(null);
  const [history, setHistory] = useState<StationSample[]>([]);
  const [commandMessage, setCommandMessage] = useState("");
  const [commandError, setCommandError] = useState("");
  const [pendingCommand, setPendingCommand] = useState<DemoCommand | null>(null);

  const load = useCallback(async () => {
    try {
      const response = await fetch("/api/state", { cache: "no-store" });
      if (!response.ok) {
        const payload = await response.json().catch(() => null);
        throw new Error(payload?.error ?? "Состояние demo не найдено.");
      }
      const payload = (await response.json()) as LiveState;
      setState(payload);
      setLastReceivedAt(Date.now());
      setError("");
      setHistory((previous) => {
        const sample = makeSample(payload);
        const last = previous[previous.length - 1];
        const next = last && sample.totalPoints < last.totalPoints ? [sample] : [...previous, sample];
        return next.slice(-48);
      });
    } catch (err) {
      setError(err instanceof Error ? err.message : "Состояние demo не найдено.");
    }
  }, []);

  useEffect(() => {
    load();
    const timer = window.setInterval(load, 1000);
    return () => window.clearInterval(timer);
  }, [load]);

  async function sendCommand(command: DemoCommand) {
    if (command === "export_csv" && state && !state.network.online) {
      setCommandMessage("");
      setCommandError("Выгрузка доступна после восстановления связи.");
      return;
    }
    setPendingCommand(command);
    setCommandMessage("");
    setCommandError("");
    try {
      const response = await fetch("/api/command", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ command })
      });
      if (!response.ok) {
        const payload = await response.json().catch(() => null);
        throw new Error(payload?.error ?? "Команду не удалось отправить.");
      }
      setCommandMessage("Команда отправлена.");
      setTimeout(load, 500);
    } catch (err) {
      setCommandError(err instanceof Error ? err.message : "Команду не удалось отправить.");
    } finally {
      setPendingCommand(null);
    }
  }

  if (!state) {
    return (
      <main className="mx-auto flex min-h-screen max-w-4xl items-center justify-center p-6">
        <div className="panel rounded-xl p-6">
          <div className="flex items-center gap-3 text-amber-800">
            <AlertTriangle />
            <h1 className="text-xl font-semibold">Нет данных</h1>
          </div>
          <p className="mt-3 text-slate-700">{error || "Запустите агент метеопоста."}</p>
          <pre className="mt-4 rounded-lg bg-slate-950 p-4 text-sm text-slate-100">
{`cd build
./tsedge_ecopost_agent --live --interval-ms 1000`}
          </pre>
        </div>
      </main>
    );
  }

  return (
    <StationDashboard
      state={state}
      history={history}
      lastReceivedAt={lastReceivedAt}
      error={error}
      commandMessage={commandMessage}
      commandError={commandError}
      pendingCommand={pendingCommand}
      onCommand={sendCommand}
    />
  );
}

function StationDashboard({
  state,
  history,
  lastReceivedAt,
  error,
  commandMessage,
  commandError,
  pendingCommand,
  onCommand
}: {
  state: LiveState;
  history: StationSample[];
  lastReceivedAt: number | null;
  error: string;
  commandMessage: string;
  commandError: string;
  pendingCommand: DemoCommand | null;
  onCommand: (command: DemoCommand) => Promise<void>;
}) {
  const values = useMemo(() => Object.fromEntries(state.last_batch.series.map((item) => [item.name, item.value])), [state.last_batch.series]);
  const pm25 = values["pm25.concentration"] ?? 0;
  const battery = values["battery.voltage"] ?? 0;
  const stationStatus = getStationStatus(state, pm25, battery);

  return (
    <main className="mx-auto max-w-[1500px] space-y-5 p-4 lg:p-6">
      <div className="flex justify-end">
        <ViewNav />
      </div>

      <header className="panel rounded-2xl p-6">
        <div className="flex flex-col gap-5 xl:flex-row xl:items-start xl:justify-between">
          <div>
            <h1 className="text-3xl font-semibold tracking-tight text-slate-950">Автономный метеопост</h1>
            <p className="mt-2 max-w-2xl text-base text-slate-600">
              Локальная панель станции. Данные приходят от C-приложения, которое пишет точки в TSEdge.
            </p>
          </div>
          <div className="grid gap-3 sm:grid-cols-2 xl:min-w-[520px]">
            <StatusCard label="Статус" value={stationStatus.label} tone={stationStatus.tone} icon={stationStatus.icon} />
            <StatusCard label="Связь" value={state.network.online ? "Связь есть" : "Связи нет"} tone={state.network.online ? "good" : "bad"} icon={state.network.online ? Wifi : WifiOff} />
            <StatusCard label="Режим" value={state.network.online ? "Онлайн" : "Автономно"} tone={state.network.online ? "good" : "warn"} icon={Radio} />
            <StatusCard label="Обновлено" value={lastReceivedAt ? formatClock(lastReceivedAt) : "нет данных"} />
          </div>
        </div>
        {error && (
          <div className="mt-4 rounded-lg border border-amber-200 bg-amber-50 px-4 py-2 text-sm text-amber-900">
            {error}. Последние данные оставлены на экране.
          </div>
        )}
      </header>

      <section className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
        {metrics.map((metric) => (
          <MetricCard key={metric.name} metric={metric} value={values[metric.name]} history={history.map((sample) => sample.values[metric.name] ?? 0)} />
        ))}
      </section>

      <SituationBar values={values} pm25={pm25} />

      <section className="grid gap-4 lg:grid-cols-[0.9fr_1.1fr]">
        <RecentEvents state={state} />
        <div className="panel rounded-2xl p-5">
          <h2 className="text-lg font-semibold text-slate-950">Выгрузка данных</h2>
          <p className="mt-2 text-sm leading-6 text-slate-600">
            CSV нужен, чтобы забрать накопленные данные станции.
          </p>
          <ExportCsvAction
            state={state}
            pending={pendingCommand === "export_csv"}
            message={commandMessage}
            error={commandError}
            onExport={() => onCommand("export_csv")}
          />
        </div>
      </section>
    </main>
  );
}

function SituationBar({
  values,
  pm25
}: {
  values: Record<string, number>;
  pm25: number;
}) {
  return (
    <section className="panel rounded-2xl p-4">
      <div className="grid gap-3 md:grid-cols-2">
        <CompactStatus
          label="Воздух"
          value={airQuality(pm25)}
          detail={`${pm25.toFixed(1)} мкг/м³ PM2.5`}
          tone={pm25 > 75 ? "bad" : pm25 > 35 ? "warn" : "good"}
        />
        <CompactStatus
          label="Погода"
          value={weatherText(values)}
          detail={`${(values["wind.speed"] ?? 0).toFixed(1)} м/с, ${(values["air.temperature"] ?? 0).toFixed(1)} °C`}
          tone="neutral"
        />
      </div>
    </section>
  );
}

function CompactStatus({
  label,
  value,
  detail,
  tone
}: {
  label: string;
  value: string;
  detail: string;
  tone: Tone;
}) {
  return (
    <div className="flex items-center justify-between gap-4 rounded-xl bg-white px-4 py-3">
      <div>
        <div className="text-sm text-slate-500">{label}</div>
        <div className={`mt-1 text-lg font-semibold ${toneClass(tone)}`}>{value}</div>
      </div>
      <div className="max-w-[45%] text-right text-sm leading-5 text-slate-500">{detail}</div>
    </div>
  );
}

function ExportCsvAction({
  state,
  pending,
  message,
  error,
  onExport
}: {
  state: LiveState;
  pending: boolean;
  message: string;
  error: string;
  onExport: () => void;
}) {
  const offline = !state.network.online;
  const disabled = pending || state.agent.status === "crashed" || offline;
  return (
    <div className="mt-4 rounded-xl border border-slate-200 bg-white p-4">
      <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
        <div>
          <div className="font-medium text-slate-950">CSV-выгрузка</div>
          <div className="mt-1 text-sm text-slate-500">
            {offline
              ? "Выгрузка доступна после восстановления связи"
              : state.export.csv_ready
                ? `Готов файл: ${state.export.last_file ?? "CSV"}`
                : "Сохранить данные станции в CSV"}
          </div>
        </div>
        <button
          type="button"
          disabled={disabled}
          onClick={onExport}
          className="inline-flex min-h-10 items-center justify-center gap-2 rounded-lg bg-sky-600 px-4 py-2 text-sm font-medium text-white transition hover:bg-sky-700 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:bg-slate-300"
        >
          <Download size={16} />
          {offline ? "Нет связи" : pending ? "Выгружаю..." : "Выгрузить CSV"}
        </button>
      </div>
      {(message || error) && (
        <div className={`mt-3 rounded-lg px-3 py-2 text-sm ${error ? "bg-rose-50 text-rose-800" : "bg-emerald-50 text-emerald-800"}`}>
          {error || message}
        </div>
      )}
      {state.agent.status === "crashed" && (
        <div className="mt-3 rounded-lg bg-rose-50 px-3 py-2 text-sm text-rose-800">
          Сначала восстановите станцию.
        </div>
      )}
      {offline && (
        <div className="mt-3 rounded-lg bg-amber-50 px-3 py-2 text-sm text-amber-800">
          Восстановите связь, чтобы выгрузить CSV.
        </div>
      )}
      {state.export.csv_ready && state.export.last_file && !offline && (
        <a
          href="/api/export/csv"
          className="mt-3 inline-flex min-h-10 items-center justify-center rounded-lg border border-sky-200 bg-sky-50 px-4 py-2 text-sm font-medium text-sky-800 transition hover:bg-sky-100 focus-visible:ring-2 focus-visible:ring-sky-400"
        >
          Скачать CSV
        </a>
      )}
    </div>
  );
}

function makeSample(state: LiveState): StationSample {
  return {
    totalPoints: state.agent.total_points_written,
    values: Object.fromEntries(state.last_batch.series.map((item) => [item.name, item.value]))
  };
}

function StatusCard({ label, value, icon: Icon, tone = "neutral" }: { label: string; value: string; icon?: LucideIcon; tone?: Tone }) {
  return (
    <div className="rounded-xl border border-slate-200 bg-white px-4 py-3">
      <div className="flex items-center gap-2 text-sm text-slate-500">
        {Icon && <Icon size={16} />}
        {label}
      </div>
      <div className={`mt-1 text-lg font-semibold ${toneClass(tone)}`}>{value}</div>
    </div>
  );
}

function MetricCard({
  metric,
  value = 0,
  history
}: {
  metric: (typeof metrics)[number];
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

function RecentEvents({ state }: { state: LiveState }) {
  const events = state.events
    .filter((event) => ["append", "network", "pollution", "recovery", "export", "error"].includes(event.type))
    .slice(-8)
    .reverse();

  return (
    <section className="panel rounded-2xl p-5">
      <h2 className="text-lg font-semibold text-slate-950">Последние события</h2>
      <div className="mt-4 space-y-2">
        {events.map((event, index) => (
          <div key={`${event.time}-${index}`} className="flex gap-3 rounded-xl border border-slate-100 bg-white px-4 py-3">
            <span className="shrink-0 text-sm text-slate-500">{event.time}</span>
            <span className="text-sm font-medium text-slate-900">{stationEventText(event.message)}</span>
          </div>
        ))}
        {events.length === 0 && <div className="text-sm text-slate-500">Событий пока нет.</div>}
      </div>
    </section>
  );
}

function getStationStatus(state: LiveState, pm25: number, battery: number): { label: string; tone: Tone; icon: LucideIcon } {
  if (state.agent.status === "crashed") {
    return { label: "Сбой", tone: "bad", icon: AlertTriangle };
  }
  if (pm25 > 75 || battery < 3.4) {
    return { label: "Предупреждение", tone: "warn", icon: AlertTriangle };
  }
  if (!state.network.online) {
    return { label: "Работает автономно", tone: "warn", icon: WifiOff };
  }
  return { label: "Станция работает", tone: "good", icon: Radio };
}

function airQuality(pm25: number) {
  if (pm25 > 75) {
    return "Опасный PM2.5";
  }
  if (pm25 > 35) {
    return "Пыль повышена";
  }
  return "Воздух в норме";
}

function weatherText(values: Record<string, number>) {
  const temp = values["air.temperature"] ?? 0;
  const wind = values["wind.speed"] ?? 0;
  if (wind > 8) {
    return "Ветрено";
  }
  if (temp > 25) {
    return "Тепло";
  }
  if (temp < 5) {
    return "Холодно";
  }
  return "Спокойно";
}

function stationEventText(message: string) {
  if (message.includes("Получено 6 новых точек")) {
    return "Данные обновлены";
  }
  if (message.includes("Связь потеряна")) {
    return "Связь потеряна";
  }
  if (message.includes("Связь восстановлена")) {
    return "Связь восстановлена";
  }
  if (message.includes("PM2.5")) {
    return "Зафиксирован всплеск PM2.5";
  }
  if (message.includes("восстанов")) {
    return "Станция восстановлена";
  }
  if (message.includes("Сбой")) {
    return "Сбой питания";
  }
  if (message.includes("CSV")) {
    return "CSV готов";
  }
  return message;
}

type Tone = "good" | "warn" | "bad" | "neutral";

function toneClass(tone: Tone) {
  if (tone === "good") {
    return "text-emerald-700";
  }
  if (tone === "warn") {
    return "text-amber-700";
  }
  if (tone === "bad") {
    return "text-rose-700";
  }
  return "text-slate-950";
}
