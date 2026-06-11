"use client";

import { useMemo, useState } from "react";
import {
  Calculator,
  CloudAlert,
  Database,
  Layers,
  ListPlus,
  Pause,
  Play,
  Plus,
  Power,
  RefreshCcw,
  RotateCcw,
  Save,
  Search,
  SearchCheck,
  ShieldCheck,
  Trash2,
  Wifi,
  WifiOff
} from "lucide-react";
import type { LucideIcon } from "lucide-react";
import type { DemoCommand, LiveState } from "@/lib/schema";

const groups: Array<{
  title: string;
  note?: string;
  actions: Array<{ command: DemoCommand; label: string; icon: LucideIcon; tone?: "danger" | "warn" }>;
}> = [
  {
    title: "Управление",
    actions: [
      { command: "pause", label: "Пауза", icon: Pause },
      { command: "resume", label: "Продолжить", icon: Play },
      { command: "reset_demo", label: "Сбросить demo", icon: RotateCcw, tone: "danger" }
    ]
  },
  {
    title: "Запись",
    actions: [
      { command: "append_one", label: "Добавить 1 точку", icon: Plus },
      { command: "append_100", label: "Добавить 100", icon: ListPlus },
      { command: "append_1000", label: "Добавить 1000", icon: Layers },
      { command: "append_batch", label: "Batch-запись", icon: Database }
    ]
  },
  {
    title: "Буфер",
    actions: [
      { command: "flush_all", label: "Сбросить буфер", icon: Save }
    ]
  },
  {
    title: "Запросы",
    actions: [
      { command: "read_last_range", label: "Прочитать диапазон", icon: Search }
    ]
  },
  {
    title: "Обслуживание",
    note: "debug.temp — отдельный тестовый ряд. Его можно создать, записывать в него точки и удалить.",
    actions: [
      { command: "run_retention", label: "Очистить старые данные", icon: Trash2, tone: "warn" },
      { command: "verify_db", label: "Проверить базу", icon: SearchCheck },
      { command: "create_debug_series", label: "Создать тестовый ряд", icon: Database },
      { command: "delete_debug_series", label: "Удалить тестовый ряд", icon: Trash2, tone: "warn" }
    ]
  },
  {
    title: "Сбой",
    actions: [
      { command: "simulate_crash", label: "Сбой", icon: Power, tone: "danger" },
      { command: "recover_from_wal", label: "WAL replay", icon: ShieldCheck }
    ]
  },
  {
    title: "Связь и датчики",
    actions: [
      { command: "network_offline", label: "Потерять связь", icon: WifiOff, tone: "warn" },
      { command: "network_online", label: "Восстановить связь", icon: Wifi },
      { command: "simulate_pollution", label: "Всплеск PM2.5", icon: CloudAlert, tone: "warn" }
    ]
  }
];

export function ControlPanel({
  state,
  onCommand,
  pendingCommand,
  message,
  error
}: {
  state: LiveState;
  onCommand: (command: DemoCommand, options?: { series?: string; window_size?: number; points?: number }) => Promise<void>;
  pendingCommand: DemoCommand | null;
  message: string;
  error: string;
}) {
  const seriesOptions = useMemo(() => {
    const names = state.series_list.items.map((item) => item.name);
    return names.length > 0
      ? names
      : ["air.temperature", "air.humidity", "air.pressure", "wind.speed", "pm25.concentration", "battery.voltage"];
  }, [state.series_list.items]);
  const [customPoints, setCustomPoints] = useState("10000");
  const [querySeries, setQuerySeries] = useState(seriesOptions[0] ?? "air.temperature");
  const [windowSize, setWindowSize] = useState("500000");
  const selectedQuerySeries = seriesOptions.includes(querySeries) ? querySeries : seriesOptions[0] ?? "air.temperature";
  const customPointCount = clampInteger(customPoints, 1, 1000000);
  const selectedWindowSize = clampInteger(windowSize, 1, 86400000);

  const disabled = (command: DemoCommand) => {
    if (pendingCommand) {
      return true;
    }
    if (command === "resume") {
      return state.agent.status === "running" || state.agent.status === "crashed";
    }
    if (command === "pause") {
      return state.agent.status === "paused" || state.agent.status === "crashed";
    }
    if (command === "network_offline") {
      return !state.network.online;
    }
    if (command === "network_online") {
      return state.network.online;
    }
    if (command === "recover_from_wal") {
      return state.agent.status !== "crashed" && !state.recovery.crash_simulated;
    }
    if (command === "simulate_crash") {
      return state.agent.status === "crashed";
    }
    if (command === "append_one" || command === "append_100" || command === "append_1000" || command === "append_custom" || command === "append_batch") {
      return state.agent.status === "crashed";
    }
    if (command === "read_last_range" || command === "aggregate_avg" || command === "aggregate_min_max" || command === "window_aggregate") {
      return state.agent.total_points_written === 0;
    }
    if (command === "flush_all") {
      return state.buffer.points === 0 || state.agent.status === "crashed";
    }
    if (command === "export_csv") {
      return !state.network.online;
    }
    if (command === "verify_db") {
      return !state.verify.available;
    }
    if (command === "create_debug_series" || command === "delete_debug_series") {
      return state.agent.status === "crashed";
    }
    return false;
  };

  return (
    <section className="panel rounded-xl p-5">
      <div className="flex items-center justify-between gap-3">
        <h2 className="text-base font-semibold">Команды</h2>
        <RefreshCcw size={18} className={state.running ? "text-sky-700" : "text-slate-400"} />
      </div>
      <div className="mt-4 space-y-4">
        {groups.map((group) => (
          <div key={group.title}>
            <div className="mb-1.5 text-xs font-semibold uppercase tracking-wide text-slate-500">{group.title}</div>
            {group.note && <div className="mb-2 text-sm leading-5 text-slate-600">{group.note}</div>}
            <div className="grid gap-2 sm:grid-cols-2">
              {group.actions.map(({ command, label, icon: Icon, tone }) => {
                const isDisabled = disabled(command);
                const isPending = pendingCommand === command;
                const toneClass =
                  tone === "danger"
                    ? "hover:border-rose-300 hover:bg-rose-50"
                    : tone === "warn"
                      ? "hover:border-amber-300 hover:bg-amber-50"
                      : "hover:border-sky-300 hover:bg-sky-50";
                return (
                  <button
                    key={command}
                    type="button"
                    disabled={isDisabled}
                    aria-disabled={isDisabled}
                    onClick={() => onCommand(command)}
                    className={`flex min-h-10 items-center gap-2 rounded-lg border border-slate-200 bg-white px-3 py-2 text-left text-sm font-medium text-slate-800 outline-none transition focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:opacity-45 ${toneClass}`}
                  >
                    <Icon className="shrink-0" size={16} />
                    <span className="min-w-0 leading-5">{isPending ? "Отправка..." : label}</span>
                  </button>
                );
              })}
            </div>
            {group.title === "Запись" && (
              <div className="mt-2 rounded-lg border border-slate-200 bg-white p-3">
                <div className="grid gap-2 sm:grid-cols-[1fr_auto]">
                  <label className="text-sm text-slate-600">
                    <span className="mb-1 block">Точек</span>
                    <input
                      type="number"
                      min={1}
                      max={1000000}
                      step={1000}
                      value={customPoints}
                      onChange={(event) => setCustomPoints(event.target.value)}
                      className="h-10 w-full rounded-lg border border-slate-200 px-3 font-medium text-slate-950 outline-none focus:border-sky-400 focus:ring-2 focus:ring-sky-100"
                    />
                  </label>
                  <button
                    type="button"
                    disabled={disabled("append_custom")}
                    aria-disabled={disabled("append_custom")}
                    onClick={() => onCommand("append_custom", { points: customPointCount })}
                    className="flex min-h-10 items-center justify-center gap-2 rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition hover:border-sky-300 hover:bg-sky-50 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:opacity-45 sm:self-end"
                  >
                    <ListPlus size={16} />
                    <span>{pendingCommand === "append_custom" ? "Отправка..." : "Добавить"}</span>
                  </button>
                </div>
              </div>
            )}
            {group.title === "Запросы" && (
              <div className="mt-2 rounded-lg border border-slate-200 bg-white p-3">
                <div className="grid gap-2 lg:grid-cols-[1fr_auto_0.7fr_auto]">
                  <label className="text-sm text-slate-600">
                    <span className="mb-1 block">Ряд</span>
                    <select
                      value={selectedQuerySeries}
                      onChange={(event) => setQuerySeries(event.target.value)}
                      disabled={seriesOptions.length === 0}
                      className="h-10 w-full rounded-lg border border-slate-200 bg-white px-3 font-medium text-slate-950 outline-none focus:border-sky-400 focus:ring-2 focus:ring-sky-100 disabled:opacity-50"
                    >
                      {seriesOptions.map((series) => (
                        <option key={series} value={series}>
                          {series}
                        </option>
                      ))}
                    </select>
                  </label>
                  <div className="grid grid-cols-2 gap-2 lg:self-end">
                    <button
                      type="button"
                      disabled={disabled("aggregate_avg") || seriesOptions.length === 0}
                      aria-disabled={disabled("aggregate_avg") || seriesOptions.length === 0}
                      onClick={() => onCommand("aggregate_avg", { series: selectedQuerySeries })}
                      className="flex min-h-10 items-center justify-center gap-2 rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition hover:border-sky-300 hover:bg-sky-50 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:opacity-45"
                    >
                      <Calculator size={16} />
                      <span>{pendingCommand === "aggregate_avg" ? "..." : "AVG"}</span>
                    </button>
                    <button
                      type="button"
                      disabled={disabled("aggregate_min_max") || seriesOptions.length === 0}
                      aria-disabled={disabled("aggregate_min_max") || seriesOptions.length === 0}
                      onClick={() => onCommand("aggregate_min_max", { series: selectedQuerySeries })}
                      className="flex min-h-10 items-center justify-center gap-2 rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition hover:border-sky-300 hover:bg-sky-50 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:opacity-45"
                    >
                      <Calculator size={16} />
                      <span>{pendingCommand === "aggregate_min_max" ? "..." : "MIN/MAX"}</span>
                    </button>
                  </div>
                  <label className="text-sm text-slate-600">
                    <span className="mb-1 block">Окно, мс</span>
                    <input
                      type="number"
                      min={1}
                      step={100000}
                      value={windowSize}
                      onChange={(event) => setWindowSize(event.target.value)}
                      className="h-10 w-full rounded-lg border border-slate-200 px-3 font-medium text-slate-950 outline-none focus:border-sky-400 focus:ring-2 focus:ring-sky-100"
                    />
                  </label>
                  <button
                    type="button"
                    disabled={disabled("window_aggregate") || seriesOptions.length === 0}
                    aria-disabled={disabled("window_aggregate") || seriesOptions.length === 0}
                    onClick={() => onCommand("window_aggregate", { series: selectedQuerySeries, window_size: selectedWindowSize })}
                    className="flex min-h-10 items-center justify-center gap-2 rounded-lg border border-slate-200 bg-white px-3 py-2 text-sm font-medium text-slate-800 outline-none transition hover:border-sky-300 hover:bg-sky-50 focus-visible:ring-2 focus-visible:ring-sky-400 focus-visible:ring-offset-2 disabled:cursor-not-allowed disabled:opacity-45 lg:self-end"
                  >
                    <Calculator size={16} />
                    <span>{pendingCommand === "window_aggregate" ? "Отправка..." : "По окнам"}</span>
                  </button>
                </div>
              </div>
            )}
          </div>
        ))}
      </div>
      {(message || error) && (
        <div className={`mt-4 rounded-lg px-4 py-2 text-sm ${error ? "bg-rose-50 text-rose-800" : "bg-emerald-50 text-emerald-800"}`}>
          {error || message}
        </div>
      )}
    </section>
  );
}

function clampInteger(value: string, min: number, max: number) {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isFinite(parsed)) {
    return min;
  }
  return Math.max(min, Math.min(max, parsed));
}
