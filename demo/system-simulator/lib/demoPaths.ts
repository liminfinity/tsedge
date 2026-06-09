import path from "node:path";

export function liveOutputDir() {
  return process.env.TSEDGE_LIVE_OUTPUT
    ? path.resolve(process.env.TSEDGE_LIVE_OUTPUT)
    : path.resolve(process.cwd(), "../../build/ecopost_live_output");
}
