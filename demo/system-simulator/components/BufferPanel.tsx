"use client";

import { motion } from "framer-motion";
import type { LiveState } from "@/lib/schema";
import { formatNumber, formatPercent } from "@/lib/format";

export function BufferPanel({ state }: { state: LiveState }) {
  const percent = Math.max(0, Math.min(100, state.buffer.percent));
  const blockSaved = state.compression.last_block_created;
  return (
    <section className="panel rounded-xl p-5">
      <div className="flex items-center justify-between gap-3">
        <h2 className="text-base font-semibold">Буфер</h2>
        <span className="text-sm text-slate-500">{formatNumber(state.buffer.points)} / {formatNumber(state.buffer.capacity)}</span>
      </div>
      <div
        className="mt-4 rounded-full bg-slate-100 p-1"
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
      <div className="mt-3 flex justify-between gap-3 text-sm">
        <span className="min-w-0 truncate">Последнее: {blockSaved ? "block сохранён" : "точки добавлены"}</span>
        <span className="font-medium">{formatPercent(percent)}</span>
      </div>
    </section>
  );
}
