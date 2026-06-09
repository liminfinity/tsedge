"use client";

import { useCallback, useEffect, useState } from "react";
import { AlertTriangle } from "lucide-react";
import { CompressionPanel } from "@/components/CompressionPanel";
import { ControlPanel } from "@/components/ControlPanel";
import { DatabaseStatePanel } from "@/components/DatabaseStatePanel";
import { EventLog } from "@/components/EventLog";
import { LoadPanel, makeLoadSample } from "@/components/LoadPanel";
import { MaintenancePanel } from "@/components/MaintenancePanel";
import { OperationResultPanel } from "@/components/OperationResultPanel";
import { SegmentShelf } from "@/components/SegmentShelf";
import { StatusHeader } from "@/components/StatusHeader";
import { StorageTree } from "@/components/StorageTree";
import { ViewNav } from "@/components/ViewNav";
import type { DemoCommand, LiveState } from "@/lib/schema";
import type { LoadSample } from "@/components/LoadPanel";

export default function Page() {
  const [state, setState] = useState<LiveState | null>(null);
  const [tree, setTree] = useState("");
  const [error, setError] = useState("");
  const [live, setLive] = useState(true);
  const [lastReceivedAt, setLastReceivedAt] = useState<number | null>(null);
  const [now, setNow] = useState(Date.now());
  const [commandMessage, setCommandMessage] = useState("");
  const [commandError, setCommandError] = useState("");
  const [pendingCommand, setPendingCommand] = useState<DemoCommand | null>(null);
  const [loadHistory, setLoadHistory] = useState<LoadSample[]>([]);

  const load = useCallback(async () => {
    try {
      const response = await fetch("/api/state", { cache: "no-store" });
      if (!response.ok) {
        const payload = await response.json().catch(() => null);
        throw new Error(payload?.error ?? "Состояние demo не найдено.");
      }
      const payload = (await response.json()) as LiveState;
      setState(payload);
      setLoadHistory((previous) => {
        const sample = makeLoadSample(payload);
        const last = previous[previous.length - 1];
        const next = last && sample.totalPoints < last.totalPoints ? [sample] : [...previous, sample];
        return next.slice(-60);
      });
      setLastReceivedAt(Date.now());
      setError("");
      const treeResponse = await fetch("/api/storage-tree", { cache: "no-store" });
      if (treeResponse.ok) {
        setTree(await treeResponse.text());
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : "Состояние demo не найдено.");
    }
  }, []);

  useEffect(() => {
    load();
  }, [load]);

  useEffect(() => {
    if (!live) {
      return;
    }
    const timer = window.setInterval(load, 1000);
    return () => window.clearInterval(timer);
  }, [live, load]);

  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);

  async function sendCommand(command: DemoCommand) {
    setPendingCommand(command);
    setCommandError("");
    setCommandMessage("");
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
      setTimeout(load, 300);
    } catch (err) {
      setCommandError(err instanceof Error ? err.message : "Команду не удалось отправить.");
    } finally {
      setPendingCommand(null);
    }
  }

  if (!state) {
    return (
      <main className="mx-auto flex min-h-screen max-w-4xl items-center justify-center p-6">
        <div className="panel rounded-lg p-6">
          <div className="flex items-center gap-3 text-amber-800">
            <AlertTriangle />
            <h1 className="text-xl font-semibold">Нет данных</h1>
          </div>
          <p className="mt-3 text-slate-700">{error || "Запустите агент."}</p>
          <pre className="mt-4 rounded bg-slate-950 p-4 text-sm text-slate-100">
{`cd build
./tsedge_ecopost_agent --live --interval-ms 1000`}
          </pre>
        </div>
      </main>
    );
  }

  return (
    <main className="mx-auto max-w-[1600px] space-y-4 p-4 lg:p-6">
      <div className="flex justify-end">
        <ViewNav />
      </div>
      <StatusHeader
        state={state}
        live={live}
        lastReceivedAt={lastReceivedAt}
        stale={lastReceivedAt ? now - lastReceivedAt > 5000 : false}
        onToggleLive={() => setLive((value) => !value)}
      />
      {error && (
        <div className="rounded-md border border-amber-200 bg-amber-50 px-4 py-3 text-sm text-amber-900">
          {error}. Последнее состояние оставлено на экране.
        </div>
      )}
      <div className="grid gap-5 lg:grid-cols-2 2xl:grid-cols-3">
        <DatabaseStatePanel state={state} />
        <CompressionPanel state={state} />
        <MaintenancePanel state={state} checking={pendingCommand === "verify_db"} />
      </div>
      <OperationResultPanel state={state} />
      <LoadPanel history={loadHistory} />
      <div className="grid gap-4 lg:grid-cols-[0.9fr_1.1fr]">
        <div className="space-y-4">
          <ControlPanel
            state={state}
            onCommand={sendCommand}
            pendingCommand={pendingCommand}
            message={commandMessage}
            error={commandError}
          />
          <EventLog state={state} />
        </div>
        <div className="space-y-4">
          <SegmentShelf state={state} />
          <StorageTree text={tree} />
        </div>
      </div>
      <footer className="pb-4 text-center text-xs text-slate-500">
        Данные обновляются автоматически.
      </footer>
    </main>
  );
}
