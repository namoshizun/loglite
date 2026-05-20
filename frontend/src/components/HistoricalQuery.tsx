import { useState, useEffect } from 'react';
import type { FormEvent } from 'react';
import { useQuery } from '@tanstack/react-query';
import { fetchLogs } from '../api/client';
import type { QueryFilter } from '../api/client';
import { Search, Plus, X, ChevronLeft, ChevronRight, Settings2, Eye } from 'lucide-react';
import JsonViewer from './JsonViewer';

const LEVEL_COLORS: Record<string, string> = {
  DEBUG: 'text-zinc-400 bg-zinc-800/40 border-zinc-700/60',
  INFO: 'text-green-400 bg-green-950/20 border-green-800/30',
  WARNING: 'text-amber-400 bg-amber-950/20 border-amber-800/30',
  ERROR: 'text-red-400 bg-red-950/20 border-red-800/30',
  CRITICAL: 'text-purple-400 bg-purple-950/20 border-purple-800/30',
};

const DEFAULT_COLUMNS = ['id', 'timestamp', 'level', 'service', 'message'];

export default function HistoricalQuery() {
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
    queryFn: () => fetchLogs({
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
      prev.includes(col) ? prev.filter((c) => c !== col) : [...prev, col]
    );
  };

  const currentPage = Math.floor(offset / limit) + 1;
  const totalPages = data ? Math.ceil(data.total / limit) : 1;

  return (
    <div className="bg-card border border-border rounded-xl p-5 shadow-sm space-y-6">
      {/* 1. Filter Builder Panel */}
      <div className="bg-zinc-950 p-4 border border-border/80 rounded-lg space-y-4">
        <h3 className="text-xs font-semibold text-muted-foreground uppercase tracking-wider flex items-center gap-1.5">
          <Search size={13} /> Query Builder
        </h3>

        {/* Filter input fields */}
        <form onSubmit={handleAddFilter} className="flex flex-wrap items-center gap-3">
          {/* Field selection */}
          <div className="flex flex-col gap-1">
            <span className="text-[10px] text-zinc-500 font-mono">Field</span>
            <select
              value={newField}
              onChange={(e) => setNewField(e.target.value)}
              className="bg-zinc-900 border border-zinc-800 rounded px-2.5 py-1.5 text-xs text-foreground focus:outline-none focus:border-primary font-mono"
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
            <span className="text-[10px] text-zinc-500 font-mono">Operator</span>
            <select
              value={newOp}
              onChange={(e) => setNewOp(e.target.value as any)}
              className="bg-zinc-900 border border-zinc-800 rounded px-2.5 py-1.5 text-xs text-foreground focus:outline-none focus:border-primary font-mono font-bold"
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
            <span className="text-[10px] text-zinc-500 font-mono">Value</span>
            <input
              type="text"
              placeholder="e.g. ERROR, AuthServer..."
              value={newValue}
              onChange={(e) => setNewValue(e.target.value)}
              className="bg-zinc-900 border border-zinc-800 rounded px-2.5 py-1.5 text-xs text-foreground placeholder:text-zinc-600 focus:outline-none focus:border-primary"
            />
          </div>

          {/* Add button */}
          <div className="flex flex-col gap-1 self-end">
            <button
              type="submit"
              className="bg-primary hover:bg-primary/95 text-primary-foreground font-bold px-3 py-1.5 rounded text-xs flex items-center gap-1 transition-colors cursor-pointer"
            >
              <Plus size={14} /> Add Filter
            </button>
          </div>
        </form>

        {/* List of active filters */}
        {activeFilters.length > 0 && (
          <div className="flex flex-wrap gap-2 pt-2 border-t border-zinc-900">
            {activeFilters.map((filter, idx) => (
              <span
                key={idx}
                className="bg-zinc-900 border border-zinc-800 text-[11px] font-mono pl-2.5 pr-1 py-1 rounded-md flex items-center gap-1 text-zinc-300"
              >
                <span className="text-zinc-500">{filter.field}</span>
                <span className="text-primary font-bold">{filter.op}</span>
                <span className="text-foreground">{filter.value}</span>
                <button
                  onClick={() => handleRemoveFilter(idx)}
                  className="text-zinc-500 hover:text-zinc-300 p-0.5 rounded transition-colors"
                >
                  <X size={12} />
                </button>
              </span>
            ))}
          </div>
        )}
      </div>

      {/* 2. Controls Row: Columns Toggle & Page Limits */}
      <div className="flex justify-between items-center bg-zinc-900/20 p-2.5 border border-border rounded-lg">
        {/* Toggle Column settings drawer */}
        <div className="relative">
          <button
            onClick={() => setShowColumnSettings(!showColumnSettings)}
            className="flex items-center gap-1.5 px-3 py-1 text-xs font-medium bg-zinc-900 border border-border hover:bg-zinc-800 text-zinc-400 hover:text-foreground rounded transition-colors"
          >
            <Settings2 size={13} /> Visible Columns
          </button>

          {showColumnSettings && (
            <div className="absolute z-10 top-8 left-0 bg-zinc-900 border border-border p-3 rounded-lg shadow-lg w-48 text-xs flex flex-col gap-1.5">
              <span className="text-[10px] text-zinc-500 font-bold uppercase tracking-wider mb-1">
                Select columns:
              </span>
              {allAvailableColumns.map((col) => (
                <label key={col} className="flex items-center gap-2 cursor-pointer text-zinc-350 hover:text-foreground">
                  <input
                    type="checkbox"
                    checked={visibleColumns.includes(col)}
                    onChange={() => toggleColumn(col)}
                    className="rounded bg-zinc-950 border-zinc-800 text-primary focus:ring-primary"
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
              Total match: <strong className="text-foreground font-semibold">{data.total.toLocaleString()}</strong> rows
            </span>
          )}
          <span className="text-zinc-500 border-l border-zinc-800 pl-3">Rows:</span>
          <select
            value={limit}
            onChange={(e) => {
              setLimit(Number(e.target.value));
              setOffset(0);
            }}
            className="bg-zinc-900 border border-zinc-800 text-xs px-2 py-0.5 rounded text-foreground font-mono"
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
        <div className="flex-1 overflow-x-auto border border-border rounded-lg bg-zinc-950/20">
          <table className="w-full text-left text-xs border-collapse">
            <thead>
              <tr className="bg-zinc-900 border-b border-border">
                {visibleColumns.map((col) => (
                  <th key={col} className="py-2.5 px-4 font-mono font-bold text-muted-foreground capitalize select-none">
                    {col}
                  </th>
                ))}
                <th className="py-2.5 px-4 text-right"></th>
              </tr>
            </thead>
            <tbody className="divide-y divide-border/60">
              {isLoading ? (
                <tr>
                  <td colSpan={visibleColumns.length + 1} className="py-20 text-center text-muted-foreground">
                    <div className="flex flex-col items-center gap-2">
                      <div className="w-6 h-6 border-2 border-primary border-t-transparent rounded-full animate-spin"></div>
                      <span>Querying database logs...</span>
                    </div>
                  </td>
                </tr>
              ) : isError ? (
                <tr>
                  <td colSpan={visibleColumns.length + 1} className="py-20 text-center text-destructive">
                    <p className="font-semibold">Query failed</p>
                    <p className="text-xs text-muted-foreground mt-1">{(error as any)?.message || 'Unknown error'}</p>
                  </td>
                </tr>
              ) : !data || data.results.length === 0 ? (
                <tr>
                  <td colSpan={visibleColumns.length + 1} className="py-20 text-center text-zinc-500 italic">
                    No log records match the selected filters.
                  </td>
                </tr>
              ) : (
                data.results.map((row, idx) => (
                  <tr
                    key={row.id ?? idx}
                    onClick={() => setSelectedLog(row)}
                    className={`hover:bg-zinc-900/40 cursor-pointer border-l-2 border-transparent transition-colors ${
                      selectedLog && selectedLog.id === row.id ? 'bg-zinc-900 border-l-primary' : ''
                    }`}
                  >
                    {visibleColumns.map((col) => {
                      const val = row[col];
                      // Format specific fields for visualization
                      if (col === 'level') {
                        const levelStr = String(val).toUpperCase();
                        const levelColor = LEVEL_COLORS[levelStr] || LEVEL_COLORS.DEBUG;
                        return (
                          <td key={col} className="py-2 px-4 whitespace-nowrap">
                            <span className={`px-1.5 py-0.5 rounded text-[10px] font-mono font-bold border ${levelColor}`}>
                              {levelStr}
                            </span>
                          </td>
                        );
                      }
                      
                      if (col === 'timestamp') {
                        return (
                          <td key={col} className="py-2 px-4 whitespace-nowrap text-zinc-500 font-mono">
                            {new Date(val).toLocaleString()}
                          </td>
                        );
                      }

                      if (col === 'service') {
                        return (
                          <td key={col} className="py-2 px-4 whitespace-nowrap">
                            {val ? (
                              <span className="text-blue-400/90 font-semibold font-mono bg-blue-950/20 px-1 border border-blue-900/10 rounded">
                                {val}
                              </span>
                            ) : '-'}
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
                            col === 'message' ? 'text-zinc-200' : 'text-zinc-400'
                          } max-w-xs md:max-w-md lg:max-w-lg truncate`}
                          title={displayVal}
                        >
                          {displayVal}
                        </td>
                      );
                    })}
                    <td className="py-2 px-4 text-right text-zinc-650">
                      <Eye size={13} className="inline opacity-0 group-hover:opacity-100 hover:text-zinc-200 transition-opacity" />
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>

        {/* Side Details Drawer */}
        {selectedLog && (
          <div className="w-full lg:w-[420px] bg-zinc-900/30 border border-border rounded-lg p-4 flex flex-col gap-4 overflow-y-auto max-h-[600px]">
            <div className="flex items-center justify-between border-b border-border pb-2">
              <span className="text-xs font-bold text-foreground">Log Details</span>
              <button
                onClick={() => setSelectedLog(null)}
                className="text-xs text-zinc-400 hover:text-zinc-250 bg-zinc-800 border border-zinc-700 px-2 py-0.5 rounded transition-colors"
              >
                Close
              </button>
            </div>

            <div className="space-y-2 text-xs font-mono">
              {allAvailableColumns.map(
                (col) =>
                  selectedLog[col] !== undefined &&
                  typeof selectedLog[col] !== 'object' && (
                    <div key={col} className="flex justify-between border-b border-border/30 py-1">
                      <span className="text-muted-foreground select-none">{col}:</span>
                      <span className="text-zinc-300 break-all select-all ml-4 text-right">
                        {String(selectedLog[col])}
                      </span>
                    </div>
                  )
              )}
            </div>

            {/* Structured payload */}
            <div className="mt-2 flex-1">
              <JsonViewer data={selectedLog} title="Full Log Matrix" />
            </div>
          </div>
        )}
      </div>

      {/* 4. Pagination Controller Footer */}
      {!isLoading && !isError && data && data.total > 0 && (
        <div className="flex items-center justify-between border-t border-border pt-4">
          <div className="text-xs text-muted-foreground font-mono">
            Showing <span className="text-foreground font-medium">{offset + 1}</span> to{' '}
            <span className="text-foreground font-medium">
              {Math.min(offset + limit, data.total)}
            </span>{' '}
            of <span className="text-foreground font-semibold">{data.total.toLocaleString()}</span> entries
          </div>

          <div className="flex items-center gap-1.5">
            <button
              onClick={() => handlePageChange('prev')}
              disabled={offset === 0}
              className="p-1 rounded bg-zinc-900 border border-border text-zinc-400 hover:text-foreground disabled:opacity-30 disabled:hover:text-zinc-450 hover:bg-zinc-800 transition-all cursor-pointer"
            >
              <ChevronLeft size={16} />
            </button>
            <span className="text-xs font-mono text-zinc-400 px-2">
              Page <strong className="text-foreground font-semibold">{currentPage}</strong> of{' '}
              <strong className="text-foreground font-semibold">{totalPages}</strong>
            </span>
            <button
              onClick={() => handlePageChange('next')}
              disabled={offset + limit >= data.total}
              className="p-1 rounded bg-zinc-900 border border-border text-zinc-400 hover:text-foreground disabled:opacity-30 disabled:hover:text-zinc-450 hover:bg-zinc-800 transition-all cursor-pointer"
            >
              <ChevronRight size={16} />
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
