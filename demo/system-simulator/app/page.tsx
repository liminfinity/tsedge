"use client";

import { useCallback, useEffect, useState } from "react";
import { AlertTriangle } from "lucide-react";
import { StationDashboard } from "@/components/station/StationDashboard";
import { makeSample } from "@/components/station/stationData";
import type { StationSample } from "@/components/station/stationTypes";
import type { DemoCommand, LiveState } from "@/lib/schema";

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

  async function sendCommand(command: DemoCommand, series?: string) {
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
        body: JSON.stringify(series ? { command, series } : { command })
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
    return <NoStatePanel error={error} />;
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

function NoStatePanel({ error }: { error: string }) {
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
