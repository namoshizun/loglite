import { useEffect, useState } from 'react';

/** Returns current time (ms), refreshed on an interval (default 30s). */
export function useUptimeClock(intervalMs = 30_000): number {
  const [nowMs, setNowMs] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setNowMs(Date.now()), intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);
  return nowMs;
}
