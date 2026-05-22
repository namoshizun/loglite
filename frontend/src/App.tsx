import { QueryClient, QueryClientProvider } from '@tanstack/react-query';

import {
  createRootRoute,
  createRoute,
  createRouter,
  RouterProvider,
  useSearch,
  useNavigate,
  Outlet,
} from '@tanstack/react-router';
import Header from './components/Header';
import StatsDashboard from './components/StatsDashboard';
import LiveConsole from './components/LiveConsole';
import HistoricalQuery from './components/HistoricalQuery';
import SettingsPanel from './components/SettingsPanel';
import TestPanel from './components/TestPanel';
import { Activity, Radio, Search, Settings, FlaskConical } from 'lucide-react';
import { useI18n } from './i18n/locale';

// Setup TanStack Query client
const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: false,
    },
  },
});

// Setup TanStack Router route schema
interface DashboardSearch {
  tab?: 'analytics' | 'live' | 'search' | 'test' | 'settings';
}

const rootRoute = createRootRoute({
  component: () => (
    <div className="min-h-screen flex flex-col bg-background text-foreground">
      <Header />
      <main className="flex-1 max-w-7xl w-full mx-auto p-4 md:p-6 space-y-6">
        <Outlet />
      </main>
    </div>
  ),
});

const dashboardRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/',
  validateSearch: (search: Record<string, unknown>): DashboardSearch => {
    const tab = search.tab as string | undefined;
    return {
      tab:
        tab === 'analytics' ||
        tab === 'live' ||
        tab === 'search' ||
        tab === 'test' ||
        tab === 'settings'
          ? tab
          : 'analytics',
    };
  },
  component: DashboardContent,
});

const routeTree = rootRoute.addChildren([dashboardRoute]);
const router = createRouter({
  routeTree,
  defaultPreload: 'intent',
});

// Register router types for type safety
declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router;
  }
}

function DashboardContent() {
  const { t } = useI18n();
  const search = useSearch({ from: '/' });
  const navigate = useNavigate({ from: '/' });
  const activeTab = search.tab || 'analytics';

  const handleTabChange = (tab: 'analytics' | 'live' | 'search' | 'test' | 'settings') => {
    navigate({
      search: (prev) => ({ ...prev, tab }),
    });
  };

  return (
    <div className="space-y-6">
      {/* Navigation Sub-header tabs */}
      <div className="flex border-b border-border">
        <button
          onClick={() => handleTabChange('analytics')}
          className={`flex items-center gap-2 px-4 py-3 text-sm font-semibold border-b-2 transition-all duration-150 cursor-pointer ${
            activeTab === 'analytics'
              ? 'border-primary text-primary'
              : 'border-transparent text-muted-foreground hover:text-foreground'
          }`}
        >
          <Activity size={16} />
          <span>{t('tabs.analytics')}</span>
        </button>
        <button
          onClick={() => handleTabChange('live')}
          className={`flex items-center gap-2 px-4 py-3 text-sm font-semibold border-b-2 transition-all duration-150 cursor-pointer ${
            activeTab === 'live'
              ? 'border-primary text-primary'
              : 'border-transparent text-muted-foreground hover:text-foreground'
          }`}
        >
          <Radio size={16} />
          <span>{t('tabs.live')}</span>
        </button>
        <button
          onClick={() => handleTabChange('search')}
          className={`flex items-center gap-2 px-4 py-3 text-sm font-semibold border-b-2 transition-all duration-150 cursor-pointer ${
            activeTab === 'search'
              ? 'border-primary text-primary'
              : 'border-transparent text-muted-foreground hover:text-foreground'
          }`}
        >
          <Search size={16} />
          <span>{t('tabs.search')}</span>
        </button>
        <button
          onClick={() => handleTabChange('test')}
          className={`flex items-center gap-2 px-4 py-3 text-sm font-semibold border-b-2 transition-all duration-150 cursor-pointer ${
            activeTab === 'test'
              ? 'border-primary text-primary'
              : 'border-transparent text-muted-foreground hover:text-foreground'
          }`}
        >
          <FlaskConical size={16} />
          <span>{t('tabs.test')}</span>
        </button>
        <button
          onClick={() => handleTabChange('settings')}
          className={`flex items-center gap-2 px-4 py-3 text-sm font-semibold border-b-2 transition-all duration-150 cursor-pointer ${
            activeTab === 'settings'
              ? 'border-primary text-primary'
              : 'border-transparent text-muted-foreground hover:text-foreground'
          }`}
        >
          <Settings size={16} />
          <span>{t('tabs.settings')}</span>
        </button>
      </div>

      {/* Tab components */}
      <div>
        {activeTab === 'analytics' && <StatsDashboard />}
        {activeTab === 'live' && <LiveConsole />}
        {activeTab === 'search' && <HistoricalQuery />}
        {activeTab === 'test' && <TestPanel />}
        {activeTab === 'settings' && <SettingsPanel />}
      </div>
    </div>
  );
}

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <RouterProvider router={router} />
    </QueryClientProvider>
  );
}
