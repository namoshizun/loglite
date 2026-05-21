import type { LogSchemaColumn } from '../../api/client';

export const DEFAULT_COLUMNS = ['id', 'timestamp', 'level', 'service', 'message'];

export const filterInputClassName =
  'bg-background border border-border rounded px-2.5 py-1.5 text-xs text-foreground placeholder:text-muted-foreground focus:outline-none focus:border-primary';

export const filterSelectClassName =
  'bg-background border border-border rounded px-2.5 py-1.5 text-xs text-foreground focus:outline-none focus:border-primary font-mono';

export function pickDefaultField(columns: LogSchemaColumn[]): string {
  if (columns.some((c) => c.name === 'level')) return 'level';
  const nonPk = columns.find((c) => !c.primary_key);
  return nonPk?.name ?? columns[0]?.name ?? 'level';
}

export function serviceTagClassForTheme(theme: 'light' | 'dark'): string {
  return theme === 'light'
    ? 'text-blue-700 font-semibold font-mono bg-blue-50 px-1 border border-blue-200 rounded'
    : 'text-blue-400/90 font-semibold font-mono bg-blue-950/20 px-1 border border-blue-900/10 rounded';
}
