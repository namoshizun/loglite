import { useEffect, useRef, useState } from 'react';
import { getSSEUrl } from '../api/client';
import { Play, Pause, Trash2, Search, ArrowDown, AlignLeft, Eye } from 'lucide-react';
import JsonViewer from './JsonViewer';

interface LogRecord {
  id: number;
  timestamp: string;
  level: string;
  service: string;
  message: string;
  [key: string]: any;
}

const LEVEL_CLASSES: Record<string, { bg: string; text: string; border: string }> = {
  DEBUG: { bg: 'bg-zinc-950/60', text: 'text-zinc-400', border: 'border-zinc-800' },
  INFO: { bg: 'bg-green-950/30', text: 'text-green-400', border: 'border-green-800/30' },
  WARNING: { bg: 'bg-amber-950/30', text: 'text-amber-400', border: 'border-amber-800/30' },
  ERROR: { bg: 'bg-red-950/30', text: 'text-red-400', border: 'border-red-850/30' },
  CRITICAL: { bg: 'bg-purple-950/30', text: 'text-purple-400', border: 'border-purple-800/30' },
};

export default function LiveConsole() {
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
      <div className="bg-zinc-900/60 p-4 border-b border-border flex flex-col md:flex-row gap-4 items-center justify-between">
        <div className="flex flex-wrap items-center gap-3">
          {/* Pause / Play Button */}
          <button
            onClick={() => setIsPaused(!isPaused)}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-all duration-200 ${
              isPaused
                ? 'bg-green-500/10 text-green-400 border-green-500/20 hover:bg-green-500/20'
                : 'bg-zinc-800 text-foreground border-zinc-700 hover:bg-zinc-750'
            }`}
          >
            {isPaused ? (
              <>
                <Play size={13} fill="currentColor" />
                <span>Resume Stream</span>
              </>
            ) : (
              <>
                <Pause size={13} fill="currentColor" />
                <span>Pause Stream</span>
              </>
            )}
          </button>

          {/* Clear Logs */}
          <button
            onClick={clearLogs}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold bg-zinc-850 hover:bg-zinc-800 text-zinc-400 border border-zinc-700 hover:text-zinc-200 transition-colors"
          >
            <Trash2 size={13} />
            <span>Clear Screen</span>
          </button>

          {/* Auto Scroll Toggle */}
          <button
            onClick={() => setAutoScroll(!autoScroll)}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-colors ${
              autoScroll
                ? 'bg-primary/10 text-primary border-primary/20'
                : 'bg-zinc-850 text-zinc-400 border-zinc-700'
            }`}
          >
            <ArrowDown size={13} className={autoScroll ? 'animate-bounce' : ''} />
            <span>Auto Scroll</span>
          </button>

          {/* Line Wrap Toggle */}
          <button
            onClick={() => setWrapLines(!wrapLines)}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-semibold border transition-colors ${
              wrapLines
                ? 'bg-primary/10 text-primary border-primary/20'
                : 'bg-zinc-850 text-zinc-400 border-zinc-700'
            }`}
          >
            <AlignLeft size={13} />
            <span>Wrap Text</span>
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
              placeholder="Search stream message, service..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="bg-zinc-950 border border-zinc-800 text-xs rounded-md pl-9 pr-4 py-1.5 w-full text-foreground placeholder:text-muted-foreground focus:outline-none focus:border-primary transition-colors"
            />
          </div>
        </div>
      </div>

      {/* Log Levels Filter Bar */}
      <div className="bg-zinc-950 px-4 py-2 border-b border-border/60 flex flex-wrap items-center gap-2">
        <span className="text-[10px] text-zinc-500 font-semibold uppercase tracking-wider mr-2">
          Toggle Levels:
        </span>
        {Object.keys(LEVEL_CLASSES).map((lvl) => {
          const colors = LEVEL_CLASSES[lvl];
          const active = levelFilter[lvl];
          return (
            <button
              key={lvl}
              onClick={() => toggleLevelFilter(lvl)}
              className={`px-2 py-0.5 text-[10px] rounded font-mono border transition-all duration-150 ${
                active
                  ? `${colors.bg} ${colors.text} ${colors.border}`
                  : 'bg-zinc-900/20 text-zinc-600 border-zinc-900/60 hover:text-zinc-400'
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
        <div className="flex-1 overflow-y-auto p-4 bg-zinc-950 text-zinc-300 font-mono text-xs selection:bg-zinc-800 flex flex-col gap-1.5">
          {filteredLogs.length === 0 ? (
            <div className="flex-1 flex flex-col items-center justify-center text-zinc-600 gap-1 mt-10">
              <div className="w-1.5 h-4 bg-primary rounded animate-pulse inline-block mr-1"></div>
              <span>{isPaused ? 'Stream is paused. Resume to view incoming logs.' : 'Listening for incoming log records...'}</span>
            </div>
          ) : (
            filteredLogs.map((log) => {
              const levelStyles = LEVEL_CLASSES[log.level?.toUpperCase()] || LEVEL_CLASSES.DEBUG;
              return (
                <div
                  key={log.id}
                  onClick={() => setSelectedLog(log)}
                  className={`flex flex-col md:flex-row items-start gap-2.5 py-1 px-2.5 rounded hover:bg-zinc-900/50 cursor-pointer border border-transparent hover:border-zinc-800 transition-colors group ${
                    selectedLog?.id === log.id ? 'bg-zinc-900 border-zinc-700/80' : ''
                  }`}
                >
                  {/* Timestamp */}
                  <span className="text-zinc-500 select-none whitespace-nowrap">
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
                    <span className="text-blue-400/80 font-semibold select-all whitespace-nowrap bg-blue-950/20 px-1 border border-blue-900/20 rounded">
                      {log.service}
                    </span>
                  )}

                  {/* Message */}
                  <span
                    className={`flex-1 text-zinc-300 select-text ${
                      wrapLines ? 'whitespace-pre-wrap break-all' : 'whitespace-nowrap overflow-x-hidden text-ellipsis'
                    }`}
                  >
                    {log.message}
                  </span>

                  {/* Quick Action icon */}
                  <span className="opacity-0 group-hover:opacity-100 text-zinc-500 hover:text-zinc-300 ml-auto transition-opacity">
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
              <span className="text-xs font-bold text-foreground">Log Details</span>
              <button
                onClick={() => setSelectedLog(null)}
                className="text-xs text-zinc-400 hover:text-zinc-200 bg-zinc-800 border border-zinc-700 px-2 py-0.5 rounded"
              >
                Close
              </button>
            </div>

            <div className="space-y-2 text-xs">
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">ID:</span>
                <span className="text-zinc-300 font-semibold select-all">{selectedLog.id}</span>
              </div>
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">Timestamp:</span>
                <span className="text-zinc-300 select-all">
                  {new Date(selectedLog.timestamp).toISOString()}
                </span>
              </div>
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">Level:</span>
                <span
                  className={`font-semibold ${
                    LEVEL_CLASSES[selectedLog.level?.toUpperCase()]?.text || 'text-zinc-400'
                  }`}
                >
                  {selectedLog.level}
                </span>
              </div>
              <div className="flex justify-between border-b border-border/40 py-1 font-mono">
                <span className="text-muted-foreground">Service:</span>
                <span className="text-blue-400 font-semibold select-all">{selectedLog.service}</span>
              </div>
            </div>

            {/* In-depth payload json */}
            <div className="mt-2 flex-1">
              <JsonViewer data={selectedLog} title="Raw Log Entry" />
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
