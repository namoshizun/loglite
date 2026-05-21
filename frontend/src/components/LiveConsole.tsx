import { useEffect, useRef, useState } from 'react';
import { getSSEUrl } from '../api/client';
import { Play, Pause, Trash2, Search, ArrowDown, AlignLeft, Eye } from 'lucide-react';
import JsonViewer from './JsonViewer';
import { getLevelStyles } from '../logLevelStyles';
import { useTheme } from '../theme';
import { useI18n } from '../i18n/locale';

interface LogRecord {
  id: number;
  timestamp: string;
  level: string;
  service: string;
  message: string;
  [key: string]: any;
}

const TOOLBAR_INACTIVE =
  'bg-secondary text-muted-foreground border-border hover:bg-muted hover:text-foreground';

export default function LiveConsole() {
  const { theme } = useTheme();
  const { t } = useI18n();
  const levelClasses = getLevelStyles(theme);
  const serviceTagClass =
    theme === 'light'
      ? 'text-blue-700 font-semibold bg-blue-50 px-1 border border-blue-200 rounded'
      : 'text-blue-400/80 font-semibold bg-blue-950/20 px-1 border border-blue-900/20 rounded';
  const [logs, setLogs] = useState<LogRecord[]>([]);
  const [isPaused, setIsPaused] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [levelFilter, setLevelFilter] = useState<Record<string, boolean>>({
    DEBUG: true,
    INFO: true,
    WARNING: true,
    ERROR: true,
    CRITICAL: true,
  });
  const [autoScroll, setAutoScroll] = useState(true);
  const [wrapLines, setWrapLines] = useState(true);
  const [selectedLog, setSelectedLog] = useState<LogRecord | null>(null);

  const consoleEndRef = useRef<HTMLDivElement>(null);
  const eventSourceRef = useRef<EventSource | null>(null);
  const logsBufferRef = useRef<LogRecord[]>([]);
  const maxLogs = 500; // prevent memory bloat

  // Handle EventSource connection
  useEffect(() => {
    if (isPaused) {
      if (eventSourceRef.current) {
        eventSourceRef.current.close();
        eventSourceRef.current = null;
      }
      return;
    }

    const sseUrl = getSSEUrl('*');
    const es = new EventSource(sseUrl);
    eventSourceRef.current = es;

    es.onmessage = (event) => {
      try {
        const incoming: LogRecord[] = JSON.parse(event.data);
        if (incoming && incoming.length > 0) {
          // Backend returns logs in descending order (newest first).
          // We reverse them to ascending order so they append correctly to the end of the console.
          const ascending = [...incoming].reverse();
          logsBufferRef.current = [...logsBufferRef.current, ...ascending].slice(-maxLogs);
          setLogs([...logsBufferRef.current]);
        }
      } catch (err) {
        console.error('Failed to parse SSE payload:', err);
      }
    };

    es.onerror = (err) => {
      console.warn('SSE disconnected, browser will attempt reconnection:', err);
    };

    return () => {
      es.close();
    };
  }, [isPaused]);

  // Handle autoscroll
  useEffect(() => {
    if (autoScroll && consoleEndRef.current) {
      consoleEndRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [logs, autoScroll]);

  const clearLogs = () => {
    logsBufferRef.current = [];
    setLogs([]);
    setSelectedLog(null);
  };

  const toggleLevelFilter = (lvl: string) => {
    setLevelFilter((prev) => ({
      ...prev,
      [lvl]: !prev[lvl],
    }));
  };

  // Filter logs for rendering
  const filteredLogs = logs.filter((log) => {
    const matchesLevel = levelFilter[log.level?.toUpperCase()] ?? true;
    const matchesSearch = searchQuery
      ? log.message?.toLowerCase().includes(searchQuery.toLowerCase()) ||
        log.service?.toLowerCase().includes(searchQuery.toLowerCase()) ||
        log.level?.toLowerCase().includes(searchQuery.toLowerCase())
      : true;
    return matchesLevel && matchesSearch;
  });

  return (
    <div className="bg-card border border-border rounded-xl overflow-hidden flex flex-col h-[650px] shadow-sm">
      {/* Console toolbar control panel */}
      <div className="bg-muted/60 p-4 border-b border-border flex flex-col md:flex-row gap-4 items-center justify-between">
        <div className="flex flex-wrap items-center gap-3">
          {/* Pause / Play Button */}
          <button
            onClick={() => setIsPaused(!isPaused)}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-all duration-200 ${
              isPaused
                ? 'bg-green-500/10 text-green-400 border-green-500/20 hover:bg-green-500/20'
                : TOOLBAR_INACTIVE
            }`}
          >
            {isPaused ? (
              <>
                <Play size={13} fill="currentColor" />
                <span>{t('live.resume')}</span>
              </>
            ) : (
              <>
                <Pause size={13} fill="currentColor" />
                <span>{t('live.pause')}</span>
              </>
            )}
          </button>

          {/* Clear Logs */}
          <button
            onClick={clearLogs}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-colors ${TOOLBAR_INACTIVE}`}
          >
            <Trash2 size={13} />
            <span>{t('live.clear')}</span>
          </button>

          {/* Auto Scroll Toggle */}
          <button
            onClick={() => setAutoScroll(!autoScroll)}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-colors ${
              autoScroll ? 'bg-primary/10 text-primary border-primary/20' : TOOLBAR_INACTIVE
            }`}
          >
            <ArrowDown size={13} className={autoScroll ? 'animate-bounce' : ''} />
            <span>{t('live.autoScroll')}</span>
          </button>

          {/* Line Wrap Toggle */}
          <button
            onClick={() => setWrapLines(!wrapLines)}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-colors ${
              wrapLines ? 'bg-primary/10 text-primary border-primary/20' : TOOLBAR_INACTIVE
            }`}
          >
            <AlignLeft size={13} />
            <span>{t('live.wrap')}</span>
          </button>
        </div>

        {/* Search & Level Filter */}
        <div className="flex items-center gap-3 w-full md:w-auto">
          {/* Keyword Search Input */}
          <div className="relative flex-1 md:w-64">
            <span className="absolute inset-y-0 left-0 pl-3 flex items-center pointer-events-none text-muted-foreground">
              <Search size={14} />
            </span>
            <input
              type="text"
              placeholder={t('live.searchPlaceholder')}
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="bg-background border border-border text-xs rounded-md pl-9 pr-4 py-1.5 w-full text-foreground placeholder:text-muted-foreground focus:outline-none focus:border-primary transition-colors"
            />
          </div>
        </div>
      </div>

      {/* Log Levels Filter Bar */}
      <div className="bg-muted/40 px-4 py-2 border-b border-border/60 flex flex-wrap items-center gap-2">
        <span className="text-[10px] text-muted-foreground font-semibold uppercase tracking-wider mr-2">
          {t('live.toggleLevels')}
        </span>
        {Object.keys(levelClasses).map((lvl) => {
          const colors = levelClasses[lvl];
          const active = levelFilter[lvl];
          return (
            <button
              key={lvl}
              onClick={() => toggleLevelFilter(lvl)}
              className={`px-2 py-0.5 text-[10px] rounded font-mono border transition-all duration-150 ${
                active
                  ? `${colors.bg} ${colors.text} ${colors.border}`
                  : 'bg-muted/50 text-muted-foreground border-border hover:text-foreground'
              }`}
            >
              {lvl}
            </button>
          );
        })}
      </div>

      {/* Console Core Layout */}
      <div className="flex-1 flex flex-col lg:flex-row overflow-hidden">
        {/* Terminal logs list */}
        <div className="flex-1 overflow-y-auto p-4 bg-background text-foreground font-mono text-xs selection:bg-muted flex flex-col gap-1.5">
          {filteredLogs.length === 0 ? (
            <div className="flex-1 flex flex-col items-center justify-center text-muted-foreground gap-1 mt-10">
              <div className="w-1.5 h-4 bg-primary rounded animate-pulse inline-block mr-1"></div>
              <span>{isPaused ? t('live.paused') : t('live.listening')}</span>
            </div>
          ) : (
            filteredLogs.map((log) => {
              const levelStyles = levelClasses[log.level?.toUpperCase()] || levelClasses.DEBUG;
              return (
                <div
                  key={log.id}
                  onClick={() => setSelectedLog(log)}
                  className={`flex flex-col md:flex-row items-start gap-2.5 py-1 px-2.5 rounded hover:bg-muted cursor-pointer border border-transparent hover:border-border transition-colors group ${
                    selectedLog?.id === log.id ? 'bg-muted border-border' : ''
                  }`}
                >
                  {/* Timestamp */}
                  <span className="text-muted-foreground select-none whitespace-nowrap">
                    {new Date(log.timestamp).toLocaleTimeString()}
                  </span>

                  {/* Level Pill */}
                  <span
                    className={`px-1.5 py-0.5 rounded text-[10px] font-bold border leading-none ${levelStyles.bg} ${levelStyles.text} ${levelStyles.border}`}
                  >
                    {log.level || 'DEBUG'}
                  </span>

                  {/* Service tag */}
                  {log.service && (
                    <span className={`select-all whitespace-nowrap ${serviceTagClass}`}>
                      {log.service}
                    </span>
                  )}

                  {/* Message */}
                  <span
                    className={`flex-1 text-foreground select-text ${
                      wrapLines
                        ? 'whitespace-pre-wrap break-all'
                        : 'whitespace-nowrap overflow-x-hidden text-ellipsis'
                    }`}
                  >
                    {log.message}
                  </span>

                  {/* Quick Action icon */}
                  <span className="opacity-0 group-hover:opacity-100 text-muted-foreground hover:text-foreground ml-auto transition-opacity">
                    <Eye size={12} />
                  </span>
                </div>
              );
            })
          )}
          <div ref={consoleEndRef} />
        </div>

        {/* Selected Log Drawer */}
        {selectedLog && (
          <div className="w-full lg:w-[420px] border-t lg:border-t-0 lg:border-l border-border bg-card p-4 overflow-y-auto flex flex-col gap-4">
            <div className="flex items-center justify-between border-b border-border pb-2.5">
              <span className="text-xs font-bold text-foreground">{t('live.details')}</span>
              <button
                onClick={() => setSelectedLog(null)}
                className="text-xs text-muted-foreground hover:text-foreground bg-secondary border border-border px-2 py-0.5 rounded"
              >
                {t('live.close')}
              </button>
            </div>

            <div className="space-y-2 text-xs">
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">{t('live.field.id')}</span>
                <span className="text-foreground font-semibold select-all">{selectedLog.id}</span>
              </div>
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">{t('live.field.timestamp')}</span>
                <span className="text-foreground select-all">
                  {new Date(selectedLog.timestamp).toISOString()}
                </span>
              </div>
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">{t('live.field.level')}</span>
                <span
                  className={`font-semibold ${
                    levelClasses[selectedLog.level?.toUpperCase()]?.text || 'text-muted-foreground'
                  }`}
                >
                  {selectedLog.level}
                </span>
              </div>
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">{t('live.field.service')}</span>
                <span className="text-blue-400 font-semibold select-all">
                  {selectedLog.service}
                </span>
              </div>
            </div>

            {/* In-depth payload json */}
            <div className="mt-2 flex-1">
              <JsonViewer data={selectedLog} title={t('live.rawEntry')} />
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
