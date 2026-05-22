/** Wall-clock start time from server uptime (seconds) anchored at last stats fetch. */
export function effectiveUptimeSeconds(
  uptimeAtFetch: number,
  fetchedAtMs: number,
  nowMs = Date.now(),
): number {
  const elapsedSinceFetch = Math.max(0, (nowMs - fetchedAtMs) / 1000);
  return uptimeAtFetch + elapsedSinceFetch;
}

export function formatUpSinceTimestamp(start: Date): string {
  const pad = (n: number) => String(n).padStart(2, '0');
  return (
    `${start.getFullYear()}-${pad(start.getMonth() + 1)}-${pad(start.getDate())} ` +
    `${pad(start.getHours())}:${pad(start.getMinutes())}:${pad(start.getSeconds())}`
  );
}

export function formatUptimeHoursMinutes(totalSeconds: number): { hrs: number; mins: number } {
  const whole = Math.max(0, Math.floor(totalSeconds));
  const hrs = Math.floor(whole / 3600);
  const mins = Math.floor((whole % 3600) / 60);
  return { hrs, mins };
}
