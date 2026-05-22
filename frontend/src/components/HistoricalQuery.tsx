import { useState, useEffect, useMemo } from 'react';
import type { FormEvent } from 'react';
import { useQuery } from '@tanstack/react-query';
import { fetchLogs } from '../api/client';
import type { QueryFilter } from '../api/client';
import { useLogSchema } from '../hooks/useLogSchema';
import { getLevelTableClasses } from '../logLevelStyles';
import { useTheme } from '../theme';
import {
  coerceFilterValue,
  defaultOperator,
  formatLocalDatetimeInput,
  isValidFilterInput,
  operatorsForKind,
  type FilterOperator,
} from '../utils/logSchemaFilters';
import QueryFilterBuilder from './historical-query/QueryFilterBuilder';
import QueryResultsToolbar from './historical-query/QueryResultsToolbar';
import HistoricalLogsTable from './historical-query/HistoricalLogsTable';
import HistoricalLogDetailPanel from './historical-query/HistoricalLogDetailPanel';
import QueryResultsPagination from './historical-query/QueryResultsPagination';
import {
  DEFAULT_COLUMNS,
  pickDefaultField,
  serviceTagClassForTheme,
} from './historical-query/constants';

export default function HistoricalQuery() {
  const { theme } = useTheme();
  const levelColors = getLevelTableClasses(theme);
  const serviceTagClass = serviceTagClassForTheme(theme);

  const [limit, setLimit] = useState(20);
  const [offset, setOffset] = useState(0);
  const [activeFilters, setActiveFilters] = useState<QueryFilter[]>([]);

  const [newField, setNewField] = useState('level');
  const [newOp, setNewOp] = useState<FilterOperator>('=');
  const [newValue, setNewValue] = useState('');
  const [valueTouched, setValueTouched] = useState(false);

  const [visibleColumns, setVisibleColumns] = useState<string[]>(DEFAULT_COLUMNS);
  const [showColumnSettings, setShowColumnSettings] = useState(false);
  const [selectedLog, setSelectedLog] = useState<Record<string, unknown> | null>(null);

  const {
    schemaColumns,
    columnByName,
    isLoading: schemaLoading,
    isError: schemaError,
  } = useLogSchema();

  const selectedKind = columnByName.get(newField)?.kind ?? 'text';
  const allowedOps = operatorsForKind(selectedKind);

  useEffect(() => {
    if (schemaColumns.length === 0) return;
    setNewField((prev) => (columnByName.has(prev) ? prev : pickDefaultField(schemaColumns)));
    const names = schemaColumns.map((c) => c.name);
    setVisibleColumns((prev) => {
      const kept = prev.filter((c) => names.includes(c));
      return kept.length > 0 ? kept : names;
    });
  }, [schemaColumns, columnByName]);

  useEffect(() => {
    const ops = operatorsForKind(selectedKind);
    setNewOp((op) => (ops.includes(op) ? op : defaultOperator(selectedKind)));
  }, [newField, selectedKind]);

  const allAvailableColumns = useMemo(() => {
    const fromSchema = schemaColumns.map((c) => c.name);
    if (fromSchema.length > 0) return fromSchema;
    return DEFAULT_COLUMNS;
  }, [schemaColumns]);

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

  const valueInvalid =
    valueTouched && newValue.trim() !== '' && !isValidFilterInput(selectedKind, newValue);
  const canAddFilter = newValue.trim() !== '' && isValidFilterInput(selectedKind, newValue);

  const handleFieldChange = (field: string) => {
    setNewField(field);
    const kind = columnByName.get(field)?.kind ?? 'text';
    setNewOp(defaultOperator(kind));
    setNewValue(kind === 'datetime' ? formatLocalDatetimeInput() : '');
    setValueTouched(false);
  };

  const handleAddFilter = (e: FormEvent) => {
    e.preventDefault();
    setValueTouched(true);
    const coerced = coerceFilterValue(selectedKind, newValue);
    if (!coerced) return;

    setActiveFilters([...activeFilters, { field: newField, op: newOp, value: coerced }]);
    setNewValue(selectedKind === 'datetime' ? formatLocalDatetimeInput() : '');
    setValueTouched(false);
    setOffset(0);
  };

  const handleRemoveFilter = (idx: number) => {
    setActiveFilters(activeFilters.filter((_, i) => i !== idx));
    setOffset(0);
  };

  const handlePageChange = (direction: 'prev' | 'next') => {
    if (direction === 'prev') {
      setOffset(Math.max(0, offset - limit));
    } else if (data && offset + limit < data.total) {
      setOffset(offset + limit);
    }
  };

  const toggleColumn = (col: string) => {
    setVisibleColumns((prev) =>
      prev.includes(col) ? prev.filter((c) => c !== col) : [...prev, col],
    );
  };

  return (
    <div className="bg-card border border-border rounded-xl p-5 shadow-sm space-y-6">
      <QueryFilterBuilder
        schemaError={schemaError}
        schemaLoading={schemaLoading}
        hasSchemaColumns={schemaColumns.length > 0}
        fieldOptions={allAvailableColumns}
        field={newField}
        operator={newOp}
        value={newValue}
        kind={selectedKind}
        allowedOperators={allowedOps}
        activeFilters={activeFilters}
        valueInvalid={valueInvalid}
        canAdd={canAddFilter}
        onFieldChange={handleFieldChange}
        onOperatorChange={setNewOp}
        onValueChange={setNewValue}
        onValueTouched={() => setValueTouched(true)}
        onSubmit={handleAddFilter}
        onRemoveFilter={handleRemoveFilter}
      />

      <QueryResultsToolbar
        columns={allAvailableColumns}
        visibleColumns={visibleColumns}
        showColumnSettings={showColumnSettings}
        onToggleColumnSettings={() => setShowColumnSettings((v) => !v)}
        onToggleColumn={toggleColumn}
        total={data?.total}
        limit={limit}
        onLimitChange={(next) => {
          setLimit(next);
          setOffset(0);
        }}
      />

      <div className="flex flex-col lg:flex-row gap-5 overflow-hidden min-h-[400px]">
        <HistoricalLogsTable
          visibleColumns={visibleColumns}
          rows={data?.results}
          isLoading={isLoading}
          isError={isError}
          errorMessage={(error as Error | undefined)?.message}
          selectedLogId={selectedLog?.id}
          levelColors={levelColors}
          serviceTagClass={serviceTagClass}
          onSelectRow={setSelectedLog}
        />

        {selectedLog && (
          <HistoricalLogDetailPanel
            log={selectedLog}
            scalarColumns={allAvailableColumns}
            onClose={() => setSelectedLog(null)}
          />
        )}
      </div>

      {!isLoading && !isError && data && data.total > 0 && (
        <QueryResultsPagination
          offset={offset}
          limit={limit}
          total={data.total}
          onPageChange={handlePageChange}
        />
      )}
    </div>
  );
}
