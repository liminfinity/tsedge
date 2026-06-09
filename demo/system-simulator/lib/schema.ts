import { z } from "zod";

export const commandSchema = z.object({
  command: z.enum([
    "network_offline",
    "network_online",
    "simulate_pollution",
    "simulate_crash",
    "recover_from_wal",
    "run_retention",
    "export_csv",
    "reset_demo",
    "pause",
    "resume"
  ])
});

export const liveStateSchema = z.object({
  running: z.boolean(),
  tick: z.number(),
  mode: z.string(),
  scenario: z.object({
    title: z.string(),
    short_description: z.string()
  }),
  network: z.object({
    online: z.boolean(),
    status: z.string()
  }),
  agent: z.object({
    status: z.string(),
    last_update_ms: z.number(),
    points_written_per_series: z.number(),
    total_points_written: z.number()
  }),
  last_batch: z.object({
    point_count: z.number(),
    timestamp: z.number(),
    series: z.array(
      z.object({
        name: z.string(),
        title: z.string(),
        value: z.number(),
        unit: z.string()
      })
    )
  }),
  wal: z.object({
    status: z.string(),
    last_write: z.boolean(),
    entries_since_flush: z.number(),
    replayed: z.boolean()
  }),
  buffer: z.object({
    points: z.number(),
    capacity: z.number(),
    percent: z.number()
  }),
  compression: z.object({
    active: z.boolean(),
    last_block_points: z.number(),
    last_block_created: z.boolean(),
    algorithm: z.string()
  }),
  segments: z.array(
    z.object({
      name: z.string(),
      status: z.string(),
      blocks: z.number(),
      old: z.boolean(),
      deleted: z.boolean()
    })
  ),
  retention: z.object({
    enabled: z.boolean(),
    last_run: z.boolean(),
    last_deleted: z.string().nullable(),
    deleted_count: z.number()
  }),
  recovery: z.object({
    crash_simulated: z.boolean(),
    wal_replayed: z.boolean(),
    recovered_points: z.number()
  }),
  export: z.object({
    csv_ready: z.boolean(),
    last_file: z.string().nullable()
  }),
  events: z.array(
    z.object({
      time: z.string(),
      type: z.string(),
      message: z.string()
    })
  )
});

export type LiveState = z.infer<typeof liveStateSchema>;
export type DemoCommand = z.infer<typeof commandSchema>["command"];
