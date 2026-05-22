import { useUptimeClock } from './useUptimeClock';
import type { MessageKey } from '../i18n/messages/en';
import {
  effectiveUptimeSeconds,
  formatUpSinceTimestamp,
  formatUptimeHoursMinutes,
} from '../utils/serverUptime';

type Translate = (key: MessageKey, vars?: Record<string, string | number>) => string;

export function useServerUpSince(
  uptime: number | undefined,
  fetchedAtMs: number | undefined,
  t: Translate,
): string | null {
  const nowMs = useUptimeClock();

  if (uptime == null || !fetchedAtMs) return null;

  const seconds = effectiveUptimeSeconds(uptime, fetchedAtMs, nowMs);
  const start = new Date(nowMs - seconds * 1000);
  const { hrs, mins } = formatUptimeHoursMinutes(seconds);

  return t('header.upSince', {
    start: formatUpSinceTimestamp(start),
    hrs,
    mins,
  });
}
