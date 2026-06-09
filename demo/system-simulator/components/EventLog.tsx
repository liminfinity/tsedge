import { AlertCircle, Archive, CloudAlert, Database, Download, Radio, RotateCcw, Shield, Trash2 } from "lucide-react";
import type { LucideIcon } from "lucide-react";
import type { LiveState } from "@/lib/schema";

const icons: Record<string, LucideIcon> = {
  sensor: Radio,
  append: Database,
  wal: Shield,
  buffer: Archive,
  block: Archive,
  segment: Archive,
  retention: Trash2,
  network: Radio,
  pollution: CloudAlert,
  recovery: RotateCcw,
  export: Download,
  error: AlertCircle
};

const labels: Record<string, string> = {
  sensor: "Датчики",
  append: "Запись",
  wal: "WAL",
  buffer: "Буфер",
  block: "Block",
  segment: "Segment",
  retention: "Очистка",
  network: "Связь",
  pollution: "PM2.5",
  recovery: "Восстановление",
  export: "CSV",
  error: "Ошибка"
};

export function EventLog({ state }: { state: LiveState }) {
  return (
    <section className="panel rounded-xl p-5">
      <h2 className="text-base font-semibold">События</h2>
      <div className="mt-4 max-h-[360px] space-y-2 overflow-auto pr-1">
        {[...state.events].reverse().map((event, index) => {
          const Icon = icons[event.type] ?? Database;
          return (
            <div key={`${event.time}-${index}`} className="grid grid-cols-[2rem_1fr] gap-3 rounded-lg border border-slate-100 bg-white px-3 py-2.5 text-sm">
              <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-slate-100 text-slate-600">
                <Icon size={16} strokeWidth={1.9} />
              </div>
              <div className="min-w-0 pt-0.5">
                <div className="truncate text-xs leading-4 text-slate-500">{event.time} · {labels[event.type] ?? event.type}</div>
                <div className="break-words text-sm font-medium leading-5 text-slate-900">{shortMessage(event.message)}</div>
              </div>
            </div>
          );
        })}
        {state.events.length === 0 && <div className="text-sm text-slate-500">Событий пока нет.</div>}
      </div>
    </section>
  );
}

function shortMessage(message: string) {
  if (message.includes("Получено 6 новых точек")) {
    return "6 точек записано";
  }
  if (message.includes("WAL")) {
    if (message.includes("восстанов")) {
      return "Восстановление выполнено";
    }
    return "WAL обновлён";
  }
  if (message.includes("буфер")) {
    return "Буфер пополнен";
  }
  if (message.includes("block") || message.includes("Block")) {
    return "Block создан";
  }
  if (message.includes("Связь потеряна")) {
    return "Связь потеряна";
  }
  if (message.includes("Связь восстановлена")) {
    return "Связь восстановлена";
  }
  if (message.includes("Retention") || message.includes("очист")) {
    return "Очистка выполнена";
  }
  if (message.includes("segment_")) {
    const match = message.match(/segment_\d+\.tse/);
    return match ? `Создан ${match[0]}` : "Segment обновлён";
  }
  if (message.includes("CSV")) {
    return "CSV создан";
  }
  if (message.includes("PM2.5")) {
    return "Всплеск PM2.5";
  }
  if (message.includes("Сбой")) {
    return "Сбой";
  }
  if (message.includes("Demo сброшено")) {
    return "Demo сброшено";
  }
  return message;
}
