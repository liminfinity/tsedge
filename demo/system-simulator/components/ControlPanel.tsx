"use client";

import { Download, Pause, Play, Power, RefreshCcw, RotateCcw, ShieldCheck, Trash2, Wifi, WifiOff, CloudAlert } from "lucide-react";
import type { LucideIcon } from "lucide-react";
import type { DemoCommand, LiveState } from "@/lib/schema";

const groups: Array<{
  title: string;
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
    title: "Сеть",
    actions: [
      { command: "network_offline", label: "Потерять связь", icon: WifiOff, tone: "warn" },
      { command: "network_online", label: "Восстановить связь", icon: Wifi }
    ]
  },
  {
    title: "Сценарии",
    actions: [
      { command: "simulate_pollution", label: "Всплеск PM2.5", icon: CloudAlert, tone: "warn" },
      { command: "simulate_crash", label: "Сбой", icon: Power, tone: "danger" },
      { command: "recover_from_wal", label: "Восстановить из WAL", icon: ShieldCheck }
    ]
  },
  {
    title: "Обслуживание базы",
    actions: [
      { command: "run_retention", label: "Очистить старые данные", icon: Trash2, tone: "warn" },
      { command: "export_csv", label: "Выгрузить CSV", icon: Download }
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
  onCommand: (command: DemoCommand) => Promise<void>;
  pendingCommand: DemoCommand | null;
  message: string;
  error: string;
}) {
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
    if (command === "export_csv") {
      return !state.network.online;
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
