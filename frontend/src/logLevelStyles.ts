import type { Theme } from './theme';

export type LevelStyle = { bg: string; text: string; border: string };

const LEVEL_DARK: Record<string, LevelStyle> = {
  DEBUG: { bg: 'bg-zinc-800/60', text: 'text-zinc-400', border: 'border-zinc-700' },
  INFO: { bg: 'bg-green-950/30', text: 'text-green-400', border: 'border-green-800/30' },
  WARNING: { bg: 'bg-amber-950/30', text: 'text-amber-400', border: 'border-amber-800/30' },
  ERROR: { bg: 'bg-red-950/30', text: 'text-red-400', border: 'border-red-800/30' },
  CRITICAL: { bg: 'bg-purple-950/30', text: 'text-purple-400', border: 'border-purple-800/30' },
};

const LEVEL_LIGHT: Record<string, LevelStyle> = {
  DEBUG: { bg: 'bg-zinc-200', text: 'text-zinc-600', border: 'border-zinc-300' },
  INFO: { bg: 'bg-green-100', text: 'text-green-700', border: 'border-green-300' },
  WARNING: { bg: 'bg-amber-100', text: 'text-amber-800', border: 'border-amber-300' },
  ERROR: { bg: 'bg-red-100', text: 'text-red-700', border: 'border-red-300' },
  CRITICAL: { bg: 'bg-purple-100', text: 'text-purple-700', border: 'border-purple-300' },
};

export function getLevelStyles(theme: Theme): Record<string, LevelStyle> {
  return theme === 'light' ? LEVEL_LIGHT : LEVEL_DARK;
}

export function getLevelTableClasses(theme: Theme): Record<string, string> {
  const s = getLevelStyles(theme);
  return Object.fromEntries(
    Object.entries(s).map(([k, v]) => [k, `${v.text} ${v.bg} ${v.border}`]),
  );
}
