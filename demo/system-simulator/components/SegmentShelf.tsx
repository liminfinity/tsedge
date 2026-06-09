"use client";

import { motion } from "framer-motion";
import type { LiveState } from "@/lib/schema";

export function SegmentShelf({ state }: { state: LiveState }) {
  const segments = [...state.segments].sort((a, b) => a.name.localeCompare(b.name));
  const mostlySingleBlock = segments.length > 1 && segments.every((segment) => segment.blocks <= 1);
  return (
    <section className="panel rounded-xl p-5">
      <div className="flex items-center justify-between gap-3">
        <h2 className="text-base font-semibold">Segment-файлы</h2>
        <span className="text-sm text-slate-500">{segments.length} файлов</span>
      </div>
      <div className="mt-4 grid gap-3 sm:grid-cols-2 2xl:grid-cols-3">
        {segments.map((segment) => (
          <motion.div
            key={segment.name}
            layout
            className={`rounded-lg border px-4 py-3 ${segment.deleted ? "border-rose-200 bg-rose-50" : segment.status === "active" ? "border-sky-300 bg-sky-50" : "border-slate-200 bg-white"}`}
          >
            <div className="break-all font-mono text-sm font-semibold">{segment.name}</div>
            <div className="mt-2 flex flex-wrap gap-1 text-xs">
              <span className={`inline-flex rounded px-2 py-1 ${segment.status === "active" ? "bg-sky-100 text-sky-800" : "bg-slate-100 text-slate-700"}`}>
                {segment.status === "active" ? "активный" : "закрыт"}
              </span>
              <span className="inline-flex rounded bg-slate-100 px-2 py-1 text-slate-700">{segment.blocks} block</span>
              <span className={`inline-flex rounded px-2 py-1 ${segment.old ? "bg-amber-100 text-amber-800" : "bg-emerald-100 text-emerald-800"}`}>
                {segment.old ? "старый" : "актуальный"}
              </span>
              {segment.deleted && <span className="inline-flex rounded bg-rose-100 px-2 py-1 text-rose-800">удалён</span>}
            </div>
          </motion.div>
        ))}
        {state.segments.length === 0 && <div className="text-sm text-slate-500">Segment-файлы ещё не созданы.</div>}
      </div>
      {state.retention.last_run && (
        <div className="mt-4 rounded-lg bg-amber-50 px-4 py-2 text-sm text-amber-900">
          {state.retention.deleted_count > 0
            ? `Очистка: удалено ${state.retention.deleted_count}.`
            : "Полных старых segment-файлов пока нет."}
        </div>
      )}
      {mostlySingleBlock && (
        <div className="mt-4 rounded-lg bg-slate-50 px-4 py-2 text-sm text-slate-600">
          В demo лимит segment-файла маленький, поэтому один block часто закрывает файл.
        </div>
      )}
    </section>
  );
}
