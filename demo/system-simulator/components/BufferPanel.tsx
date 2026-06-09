"use client";

import { motion } from "framer-motion";
import type { LiveState } from "@/lib/schema";
import { formatNumber, formatPercent } from "@/lib/format";

export function BufferPanel({ state }: { state: LiveState }) {
  const percent = Math.max(0, Math.min(100, state.buffer.percent));
  const blockSaved = state.compression.last_block_created;
  return (
    <section className="panel rounded-xl p-6">
      <div className="flex items-center justify-between gap-3">
        <h2 className="text-lg font-semibold">Буфер</h2>
        <span className="rounded-full bg-slate-100 px-3 py-1 text-sm font-medium text-slate-700">
          {formatNumber(state.buffer.points)} / {formatNumber(state.buffer.capacity)}
        </span>
      </div>
      <p className="mt-2 text-sm leading-6 text-slate-600">
        Буфер копит точки в памяти. Когда он сбрасывается, база создаёт block.
      </p>
      <div
        className="mt-5 rounded-full bg-slate-100 p-1"
        role="progressbar"
        aria-label="Заполнение буфера"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(percent)}
      >
        <motion.div
          animate={{ width: `${percent}%` }}
          className={`h-4 rounded-full ${state.compression.last_block_created ? "bg-emerald-500" : "bg-sky-500"}`}
        />
      </div>
      <div className="mt-4 rounded-lg bg-slate-50 px-4 py-3 text-sm">
        <div className="flex items-start justify-between gap-4">
          <span className="text-slate-600">Последняя операция</span>
          <span className="text-right font-semibold text-slate-950">
            {blockSaved ? "block сохранён" : "точки добавлены"}
          </span>
        </div>
        <div className="mt-1 text-xs leading-5 text-slate-500">
          Заполнено {formatPercent(percent)} от размера буфера.
        </div>
      </div>
    </section>
  );
}
