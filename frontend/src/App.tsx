import { lazy, Suspense } from 'react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import Header from './components/Header';
import { Activity, Radio, Search, Settings, FlaskConical } from 'lucide-react';
import { useI18n } from './i18n/locale';
import { useDashboardTab, type DashboardTab } from './hooks/useDashboardTab';

const StatsDashboard = lazy(() => import('./components/StatsDashboard'));
const LiveConsole = lazy(() => import('./components/LiveConsole'));
const HistoricalQuery = lazy(() => import('./components/HistoricalQuery'));
const TestPanel = lazy(() => import('./components/TestPanel'));
const SettingsPanel = lazy(() => import('./components/SettingsPanel'));

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      refetchOnWindowFocus: false,
      retry: false,
    },
  },
});

function TabPanelFallback() {
  const { t } = useI18n();
  return (
    <div className="min-h-[300px] flex items-center justify-center text-muted-foreground">
      <div className="flex flex-col items-center gap-2">
        <div className="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin" />
        <span className="text-sm">{t('tabs.loading')}</span>
      </div>
    </div>
  );
}

function DashboardContent() {
  const { t } = useI18n();
  const { tab: activeTab, setTab } = useDashboardTab();

  const handleTabChange = (tab: DashboardTab) => {
    setTab(tab);
  };

  return (
    <div className="space-y-6">
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

      <Suspense fallback={<TabPanelFallback />}>
        {activeTab === 'analytics' && <StatsDashboard />}
        {activeTab === 'live' && <LiveConsole />}
        {activeTab === 'search' && <HistoricalQuery />}
        {activeTab === 'test' && <TestPanel />}
        {activeTab === 'settings' && <SettingsPanel />}
      </Suspense>
    </div>
  );
}

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <div className="min-h-screen flex flex-col bg-background text-foreground">
        <Header />
        <main className="flex-1 max-w-7xl w-full mx-auto p-4 md:p-6 space-y-6">
          <DashboardContent />
        </main>
      </div>
    </QueryClientProvider>
  );
}
