import { useEffect, useState } from 'react';

export type DashboardTab = 'analytics' | 'live' | 'search' | 'test' | 'settings';

const TABS: DashboardTab[] = ['analytics', 'live', 'search', 'test', 'settings'];
const DEFAULT_TAB: DashboardTab = 'analytics';

function parseTab(search: URLSearchParams): DashboardTab {
  const raw = search.get('tab');
  if (raw && TABS.includes(raw as DashboardTab)) {
    return raw as DashboardTab;
  }
  return DEFAULT_TAB;
}

export function useDashboardTab() {
  const [tab, setTabState] = useState<DashboardTab>(() =>
    parseTab(new URLSearchParams(window.location.search)),
  );

  useEffect(() => {
    const onPopState = () => {
      setTabState(parseTab(new URLSearchParams(window.location.search)));
    };
    window.addEventListener('popstate', onPopState);
    return () => window.removeEventListener('popstate', onPopState);
  }, []);

  const setTab = (next: DashboardTab) => {
    const params = new URLSearchParams(window.location.search);
    if (next === DEFAULT_TAB) {
      params.delete('tab');
    } else {
      params.set('tab', next);
    }
    const qs = params.toString();
    const url = qs ? `${window.location.pathname}?${qs}` : window.location.pathname;
    history.replaceState(null, '', url);
    setTabState(next);
  };

  return { tab, setTab };
}
