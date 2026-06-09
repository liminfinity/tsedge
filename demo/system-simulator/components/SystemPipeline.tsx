"use client";

import { Cpu, Database, FileArchive, HardDrive, RadioTower, Shield, Waves } from "lucide-react";
import type { LiveState } from "@/lib/schema";
import { PipelineNode } from "./PipelineNode";

export function SystemPipeline({ state }: { state: LiveState }) {
  const crashed = state.agent.status === "crashed";
  const paused = state.agent.status === "paused" || state.agent.status === "stopped";
  const nodes = [
    { icon: RadioTower, title: "Датчики", status: crashed || paused ? "wait" : "active" },
    { icon: Cpu, title: "C-приложение", status: crashed ? "crash" : paused ? "wait" : "active" },
    { icon: Database, title: "TSEdge", status: crashed ? "unavailable" : "active" },
    { icon: Shield, title: "WAL", status: state.wal.last_write || state.wal.replayed ? "active" : "wait" },
    { icon: Waves, title: "Буфер", status: state.buffer.points > 0 ? "active" : "wait" },
    { icon: FileArchive, title: "Block", status: state.compression.last_block_created ? "active" : "wait" },
    { icon: HardDrive, title: "Segment-файлы", status: state.segments.length > 0 ? "active" : "wait" }
  ] as const;

  const arrows = [
    "Датчики → C-приложение",
    "C-приложение → TSEdge",
    "API → WAL",
    "WAL → Буфер",
    "Буфер → Block",
    "Block → Segment-файлы"
  ];

  return (
    <section className="panel rounded-xl p-5">
      <div className="mb-3 flex items-center justify-between gap-3">
        <h2 className="text-lg font-semibold">Состояние записи</h2>
      </div>
      <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-4 2xl:grid-cols-7">
        {nodes.map((node, index) => (
          <div key={node.title} className="relative">
            <PipelineNode {...node} />
            {index < nodes.length - 1 && (
              <div className="absolute -right-2 top-1/2 hidden h-0.5 w-2 bg-slate-300 xl:block" />
            )}
          </div>
        ))}
      </div>
      <div className="sr-only">{arrows.join(", ")}</div>
    </section>
  );
}
