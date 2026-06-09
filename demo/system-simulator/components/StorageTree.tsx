"use client";

import { useState } from "react";

export function StorageTree({ text }: { text: string }) {
  const [open, setOpen] = useState(false);

  return (
    <section className="panel rounded-xl p-5">
      <button
        type="button"
        onClick={() => setOpen((value) => !value)}
        className="flex w-full items-center justify-between gap-3 rounded-lg text-left outline-none focus-visible:ring-2 focus-visible:ring-sky-400"
      >
        <span className="text-base font-semibold">Файлы базы</span>
        <span className="text-sm text-slate-500">{open ? "Скрыть" : "Показать"}</span>
      </button>
      {open && (
        <pre className="mt-4 max-h-[300px] overflow-auto whitespace-pre rounded-lg bg-slate-950 p-4 text-xs leading-5 text-slate-100">{text || "Файлы пока не созданы."}</pre>
      )}
    </section>
  );
}
