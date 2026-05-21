import { useState, useEffect } from 'react';
import type { FormEvent } from 'react';
import { useQuery } from '@tanstack/react-query';
import { fetchLogs } from '../api/client';
import type { QueryFilter } from '../api/client';
import { Search, Plus, X, ChevronLeft, ChevronRight, Settings2, Eye } from 'lucide-react';
import JsonViewer from './JsonViewer';
import { getLevelTableClasses } from '../logLevelStyles';
import { useTheme } from '../theme';
import { useI18n } from '../i18n/locale';
import { formatDateTimeMs } from '../utils/formatTimestamp';

const DEFAULT_COLUMNS = ['id', 'timestamp', 'level', 'service', 'message'];

export default function HistoricalQuery() {
  const { theme } = useTheme();
  const { t } = useI18n();
  const levelColors = getLevelTableClasses(theme);
  const serviceTagClass =
    theme === 'light'
      ? 'text-blue-700 font-semibold font-mono bg-blue-50 px-1 border border-blue-200 rounded'
      : 'text-blue-400/90 font-semibold font-mono bg-blue-950/20 px-1 border border-blue-900/10 rounded';
  // Query States
  const [limit, setLimit] = useState(20);
  const [offset, setOffset] = useState(0);

  // Dynamic filter state
  const [activeFilters, setActiveFilters] = useState<QueryFilter[]>([]);

  // Pending filter inputs (for adding)
  const [newField, setNewField] = useState('level');
  const [newOp, setNewOp] = useState<'=' | '!=' | '>' | '>=' | '<' | '<=' | '~='>('=');
  const [newValue, setNewValue] = useState('');

  // Selected fields / columns state
  const [allAvailableColumns, setAllAvailableColumns] = useState<string[]>(DEFAULT_COLUMNS);
  const [visibleColumns, setVisibleColumns] = useState<string[]>(DEFAULT_COLUMNS);
  const [showColumnSettings, setShowColumnSettings] = useState(false);

  // Expanded log view
  const [selectedLog, setSelectedLog] = useState<Record<string, any> | null>(null);

  // TanStack Query to fetch logs
  const { data, isLoading, isError, error } = useQuery({
    queryKey: ['logs', limit, offset, activeFilters],
    queryFn: () =>
      fetchLogs({
        fields: '*',
        limit,
        offset,
        filters: activeFilters,
      }),
  });

  // Extract all available columns from retrieved log entries
  useEffect(() => {
    if (data && data.results && data.results.length > 0) {
      const keys = Object.keys(data.results[0]);
      // Ensure all unique keys are added, maintaining defaults first
      const uniqueKeys = Array.from(new Set([...DEFAULT_COLUMNS, ...keys]));
      setAllAvailableColumns(uniqueKeys);
    }
  }, [data]);

  const handleAddFilter = (e: FormEvent) => {
    e.preventDefault();
    if (!newValue.trim()) return;

    const filter: QueryFilter = {
      field: newField,
      op: newOp,
      value: newValue.trim(),
    };

    setActiveFilters([...activeFilters, filter]);
    setNewValue('');
    setOffset(0); // Reset page to first
  };

  const handleRemoveFilter = (idx: number) => {
    const next = activeFilters.filter((_, i) => i !== idx);
    setActiveFilters(next);
    setOffset(0);
  };

  const handlePageChange = (direction: 'prev' | 'next') => {
    if (direction === 'prev') {
      setOffset(Math.max(0, offset - limit));
    } else {
      if (data && offset + limit < data.total) {
        setOffset(offset + limit);
      }
    }
  };

  const toggleColumn = (col: string) => {
    setVisibleColumns((prev) =>
      prev.includes(col) ? prev.filter((c) => c !== col) : [...prev, col],
    );
  };

  const currentPage = Math.floor(offset / limit) + 1;
  const totalPages = data ? Math.ceil(data.total / limit) : 1;

  return (
    <div className="bg-card border border-border rounded-xl p-5 shadow-sm space-y-6">
      {/* 1. Filter Builder Panel */}
      <div className="bg-muted/40 p-4 border border-border/80 rounded-lg space-y-4">
        <h3 className="text-xs font-semibold text-muted-foreground uppercase tracking-wider flex items-center gap-1.5">
          <Search size={13} /> {t('query.builder')}
        </h3>

        {/* Filter input fields */}
        <form onSubmit={handleAddFilter} className="flex flex-wrap items-center gap-3">
          {/* Field selection */}
          <div className="flex flex-col gap-1">
            <span className="text-[10px] text-muted-foreground font-mono">{t('query.field')}</span>
            <select
              value={newField}
              onChange={(e) => setNewField(e.target.value)}
              className="bg-background border border-border rounded px-2.5 py-1.5 text-xs text-foreground focus:outline-none focus:border-primary font-mono"
            >
              {allAvailableColumns.map((col) => (
                <option key={col} value={col}>
                  {col}
                </option>
              ))}
            </select>
          </div>

          {/* Operator Selection */}
          <div className="flex flex-col gap-1">
            <span className="text-[10px] text-muted-foreground font-mono">
              {t('query.operator')}
            </span>
            <select
              value={newOp}
              onChange={(e) => setNewOp(e.target.value as any)}
              className="bg-background border border-border rounded px-2.5 py-1.5 text-xs text-foreground focus:outline-none focus:border-primary font-mono font-bold"
            >
              <option value="=">=</option>
              <option value="!=">!=</option>
              <option value="~=">~= (LIKE)</option>
              <option value=">">&gt;</option>
              <option value=">=">&gt;=</option>
              <option value="<">&lt;</option>
              <option value="<=">&lt;=</option>
            </select>
          </div>

          {/* Value entry */}
          <div className="flex flex-col gap-1 flex-1 min-w-[150px]">
            <span className="text-[10px] text-muted-foreground font-mono">{t('query.value')}</span>
            <input
              type="text"
              placeholder={t('query.valuePlaceholder')}
              value={newValue}
              onChange={(e) => setNewValue(e.target.value)}
              className="bg-background border border-border rounded px-2.5 py-1.5 text-xs text-foreground placeholder:text-muted-foreground focus:outline-none focus:border-primary"
            />
          </div>

          {/* Add button */}
          <div className="flex flex-col gap-1 self-end">
            <button
              type="submit"
              className="bg-primary hover:bg-primary/95 text-primary-foreground font-bold px-3 py-1.5 rounded text-xs flex items-center gap-1 transition-colors cursor-pointer"
            >
              <Plus size={14} /> {t('query.addFilter')}
            </button>
          </div>
        </form>

        {/* List of active filters */}
        {activeFilters.length > 0 && (
          <div className="flex flex-wrap gap-2 pt-2 border-t border-border">
            {activeFilters.map((filter, idx) => (
              <span
                key={idx}
                className="bg-background border border-border text-[11px] font-mono pl-2.5 pr-1 py-1 rounded-md flex items-center gap-1 text-foreground"
              >
                <span className="text-muted-foreground">{filter.field}</span>
                <span className="text-primary font-bold">{filter.op}</span>
                <span className="text-foreground">{filter.value}</span>
                <button
                  onClick={() => handleRemoveFilter(idx)}
                  className="text-muted-foreground hover:text-foreground p-0.5 rounded transition-colors"
                >
                  <X size={12} />
                </button>
              </span>
            ))}
          </div>
        )}
      </div>

      {/* 2. Controls Row: Columns Toggle & Page Limits */}
      <div className="flex justify-between items-center bg-muted/30 p-2.5 border border-border rounded-lg">
        {/* Toggle Column settings drawer */}
        <div className="relative">
          <button
            onClick={() => setShowColumnSettings(!showColumnSettings)}
            className="flex items-center gap-1.5 px-3 py-1 text-xs font-medium bg-card border border-border hover:bg-muted text-muted-foreground hover:text-foreground rounded transition-colors"
          >
            <Settings2 size={13} /> {t('query.visibleColumns')}
          </button>

          {showColumnSettings && (
            <div className="absolute z-10 top-8 left-0 bg-card border border-border p-3 rounded-lg shadow-lg w-48 text-xs flex flex-col gap-1.5">
              <span className="text-[10px] text-muted-foreground font-bold uppercase tracking-wider mb-1">
                {t('query.selectColumns')}
              </span>
              {allAvailableColumns.map((col) => (
                <label
                  key={col}
                  className="flex items-center gap-2 cursor-pointer text-muted-foreground hover:text-foreground"
                >
                  <input
                    type="checkbox"
                    checked={visibleColumns.includes(col)}
                    onChange={() => toggleColumn(col)}
                    className="rounded bg-background border-border text-primary focus:ring-primary"
                  />
                  <span className="font-mono">{col}</span>
                </label>
              ))}
            </div>
          )}
        </div>

        {/* Total rows counter and page settings */}
        <div className="flex items-center gap-3 text-xs">
          {data && (
            <span className="text-muted-foreground font-mono">
              {t('query.totalMatch')}{' '}
              <strong className="text-foreground font-semibold">
                {data.total.toLocaleString()}
              </strong>{' '}
              rows
            </span>
          )}
          <span className="text-muted-foreground border-l border-border pl-3">
            {t('query.rows')}
          </span>
          <select
            value={limit}
            onChange={(e) => {
              setLimit(Number(e.target.value));
              setOffset(0);
            }}
            className="bg-background border border-border text-xs px-2 py-0.5 rounded text-foreground font-mono"
          >
            {[20, 50, 100].map((num) => (
              <option key={num} value={num}>
                {num}
              </option>
            ))}
          </select>
        </div>
      </div>

      {/* 3. Core Database Logs Grid */}
      <div className="flex flex-col lg:flex-row gap-5 overflow-hidden min-h-[400px]">
        {/* Main Logs Table */}
        <div className="flex-1 overflow-x-auto border border-border rounded-lg bg-background">
          <table className="w-full text-left text-xs border-collapse">
            <thead>
              <tr className="bg-muted border-b border-border">
                {visibleColumns.map((col) => (
                  <th
                    key={col}
                    className="py-2.5 px-4 font-mono font-bold text-muted-foreground capitalize select-none"
                  >
                    {col}
                  </th>
                ))}
                <th className="py-2.5 px-4 text-right"></th>
              </tr>
            </thead>
            <tbody className="divide-y divide-border/60">
              {isLoading ? (
                <tr>
                  <td
                    colSpan={visibleColumns.length + 1}
                    className="py-20 text-center text-muted-foreground"
                  >
                    <div className="flex flex-col items-center gap-2">
                      <div className="w-6 h-6 border-2 border-primary border-t-transparent rounded-full animate-spin"></div>
                      <span>{t('query.loading')}</span>
                    </div>
                  </td>
                </tr>
              ) : isError ? (
                <tr>
                  <td
                    colSpan={visibleColumns.length + 1}
                    className="py-20 text-center text-destructive"
                  >
                    <p className="font-semibold">{t('query.failed')}</p>
                    <p className="text-xs text-muted-foreground mt-1">
                      {(error as any)?.message || 'Unknown error'}
                    </p>
                  </td>
                </tr>
              ) : !data || data.results.length === 0 ? (
                <tr>
                  <td
                    colSpan={visibleColumns.length + 1}
                    className="py-20 text-center text-muted-foreground italic"
                  >
                    {t('query.noResults')}
                  </td>
                </tr>
              ) : (
                data.results.map((row, idx) => (
                  <tr
                    key={row.id ?? idx}
                    onClick={() => setSelectedLog(row)}
                    className={`hover:bg-muted cursor-pointer border-l-2 border-transparent transition-colors ${
                      selectedLog && selectedLog.id === row.id ? 'bg-muted border-l-primary' : ''
                    }`}
                  >
                    {visibleColumns.map((col) => {
                      const val = row[col];
                      // Format specific fields for visualization
                      if (col === 'level') {
                        const levelStr = String(val).toUpperCase();
                        const levelColor = levelColors[levelStr] || levelColors.DEBUG;
                        return (
                          <td key={col} className="py-2 px-4 whitespace-nowrap">
                            <span
                              className={`px-1.5 py-0.5 rounded text-[10px] font-mono font-bold border ${levelColor}`}
                            >
                              {levelStr}
                            </span>
                          </td>
                        );
                      }

                      if (col === 'timestamp') {
                        return (
                          <td
                            key={col}
                            className="py-2 px-4 whitespace-nowrap text-muted-foreground font-mono"
                          >
                            {formatDateTimeMs(val)}
                          </td>
                        );
                      }

                      if (col === 'service') {
                        return (
                          <td key={col} className="py-2 px-4 whitespace-nowrap">
                            {val ? <span className={serviceTagClass}>{val}</span> : '-'}
                          </td>
                        );
                      }

                      // Dynamic fields or extra json
                      const isObject = typeof val === 'object' && val !== null;
                      const displayVal = isObject ? JSON.stringify(val) : String(val ?? '');

                      return (
                        <td
                          key={col}
                          className={`py-2 px-4 font-mono ${
                            col === 'message' ? 'text-foreground' : 'text-muted-foreground'
                          } max-w-xs md:max-w-md lg:max-w-lg truncate`}
                          title={displayVal}
                        >
                          {displayVal}
                        </td>
                      );
                    })}
                    <td className="py-2 px-4 text-right text-muted-foreground">
                      <Eye
                        size={13}
                        className="inline opacity-0 group-hover:opacity-100 hover:text-foreground transition-opacity"
                      />
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>

        {/* Side Details Drawer */}
        {selectedLog && (
          <div className="w-full lg:w-[420px] bg-muted/30 border border-border rounded-lg p-4 flex flex-col gap-4 overflow-y-auto max-h-[600px]">
            <div className="flex items-center justify-between border-b border-border pb-2">
              <span className="text-xs font-bold text-foreground">{t('query.details')}</span>
              <button
                onClick={() => setSelectedLog(null)}
                className="text-xs text-muted-foreground hover:text-foreground bg-secondary border border-border px-2 py-0.5 rounded transition-colors"
              >
                {t('query.close')}
              </button>
            </div>

            <div className="space-y-2 text-xs font-mono">
              {allAvailableColumns.map(
                (col) =>
                  selectedLog[col] !== undefined &&
                  typeof selectedLog[col] !== 'object' && (
                    <div key={col} className="flex justify-between border-b border-border/30 py-1">
                      <span className="text-muted-foreground select-none">{col}:</span>
                      <span className="text-foreground break-all select-all ml-4 text-right">
                        {String(selectedLog[col])}
                      </span>
                    </div>
                  ),
              )}
            </div>

            {/* Structured payload */}
            <div className="mt-2 flex-1">
              <JsonViewer data={selectedLog} title={t('query.fullMatrix')} />
            </div>
          </div>
        )}
      </div>

      {/* 4. Pagination Controller Footer */}
      {!isLoading && !isError && data && data.total > 0 && (
        <div className="flex items-center justify-between border-t border-border pt-4">
          <div className="text-xs text-muted-foreground font-mono">
            {t('query.pagination.showing')}{' '}
            <span className="text-foreground font-medium">{offset + 1}</span>{' '}
            {t('query.pagination.to')}{' '}
            <span className="text-foreground font-medium">
              {Math.min(offset + limit, data.total)}
            </span>{' '}
            {t('query.pagination.of')}{' '}
            <span className="text-foreground font-semibold">{data.total.toLocaleString()}</span>{' '}
            {t('query.pagination.entries')}
          </div>

          <div className="flex items-center gap-1.5">
            <button
              onClick={() => handlePageChange('prev')}
              disabled={offset === 0}
              className="p-1 rounded bg-secondary border border-border text-muted-foreground hover:text-foreground disabled:opacity-30 hover:bg-muted transition-all cursor-pointer"
            >
              <ChevronLeft size={16} />
            </button>
            <span className="text-xs font-mono text-muted-foreground px-2">
              {t('query.pagination.page')}{' '}
              <strong className="text-foreground font-semibold">{currentPage}</strong>{' '}
              {t('query.pagination.ofPages')}{' '}
              <strong className="text-foreground font-semibold">{totalPages}</strong>
            </span>
            <button
              onClick={() => handlePageChange('next')}
              disabled={offset + limit >= data.total}
              className="p-1 rounded bg-secondary border border-border text-muted-foreground hover:text-foreground disabled:opacity-30 hover:bg-muted transition-all cursor-pointer"
            >
              <ChevronRight size={16} />
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
