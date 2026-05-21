import { createContext, useContext, useState, type ReactNode } from 'react';
import { en, type MessageKey } from './messages/en';
import { zh } from './messages/zh';

export type Locale = 'en' | 'zh';

const STORAGE_KEY = 'loglite-locale';

const dictionaries: Record<Locale, Record<MessageKey, string>> = { en, zh };

export function getStoredLocale(): Locale {
  const stored = localStorage.getItem(STORAGE_KEY);
  return stored === 'zh' ? 'zh' : 'en';
}

export function applyLocale(locale: Locale): void {
  document.documentElement.lang = locale === 'zh' ? 'zh-CN' : 'en';
  localStorage.setItem(STORAGE_KEY, locale);
}

function interpolate(template: string, vars?: Record<string, string | number>): string {
  if (!vars) {
    return template;
  }
  return template.replace(/\{\{(\w+)\}\}/g, (_, key: string) => String(vars[key] ?? ''));
}

type LocaleContextValue = {
  locale: Locale;
  setLocale: (locale: Locale) => void;
  t: (key: MessageKey, vars?: Record<string, string | number>) => string;
};

const LocaleContext = createContext<LocaleContextValue | null>(null);

export function LocaleProvider({ children }: { children: ReactNode }) {
  const [locale, setLocaleState] = useState<Locale>(() => getStoredLocale());

  const setLocale = (next: Locale) => {
    applyLocale(next);
    setLocaleState(next);
  };

  const t = (key: MessageKey, vars?: Record<string, string | number>) =>
    interpolate(dictionaries[locale][key] ?? dictionaries.en[key] ?? key, vars);

  return (
    <LocaleContext.Provider value={{ locale, setLocale, t }}>{children}</LocaleContext.Provider>
  );
}

export function useI18n(): LocaleContextValue {
  const ctx = useContext(LocaleContext);
  if (!ctx) {
    throw new Error('useI18n must be used within LocaleProvider');
  }
  return ctx;
}
