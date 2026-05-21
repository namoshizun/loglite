import type { MessageKey } from '../i18n/messages/en';

type TranslateFn = (key: MessageKey, vars?: Record<string, string | number>) => string;

const BYTE_KEYS = ['bytes.0', 'bytes.kb', 'bytes.mb', 'bytes.gb', 'bytes.tb'] as const;

export function formatBytes(bytes: number, t: TranslateFn, decimals = 2): string {
  if (!bytes || bytes === 0) {
    return `0 ${t('bytes.0')}`;
  }
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  const unit = t(BYTE_KEYS[Math.min(i, BYTE_KEYS.length - 1)]);
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${unit}`;
}
