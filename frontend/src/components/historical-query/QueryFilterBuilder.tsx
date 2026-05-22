import type { FormEvent } from 'react';
import { Plus, Search, X } from 'lucide-react';
import type { LogColumnKind, QueryFilter } from '../../api/client';
import { useI18n } from '../../i18n/locale';
import type { FilterOperator } from '../../utils/logSchemaFilters';
import { filterSelectClassName } from './constants';
import QueryFilterValueInput from './QueryFilterValueInput';

type QueryFilterBuilderProps = {
  schemaError: boolean;
  schemaLoading: boolean;
  hasSchemaColumns: boolean;
  fieldOptions: string[];
  field: string;
  operator: FilterOperator;
  value: string;
  kind: LogColumnKind;
  allowedOperators: FilterOperator[];
  activeFilters: QueryFilter[];
  valueInvalid: boolean;
  canAdd: boolean;
  onFieldChange: (field: string) => void;
  onOperatorChange: (op: FilterOperator) => void;
  onValueChange: (value: string) => void;
  onValueTouched: () => void;
  onSubmit: (e: FormEvent) => void;
  onRemoveFilter: (index: number) => void;
};

export default function QueryFilterBuilder({
  schemaError,
  schemaLoading,
  hasSchemaColumns,
  fieldOptions,
  field,
  operator,
  value,
  kind,
  allowedOperators,
  activeFilters,
  valueInvalid,
  canAdd,
  onFieldChange,
  onOperatorChange,
  onValueChange,
  onValueTouched,
  onSubmit,
  onRemoveFilter,
}: QueryFilterBuilderProps) {
  const { t } = useI18n();

  return (
    <div className="bg-muted/40 p-4 border border-border/80 rounded-lg space-y-4">
      <h3 className="text-xs font-semibold text-muted-foreground uppercase tracking-wider flex items-center gap-1.5">
        <Search size={13} /> {t('query.builder')}
      </h3>

      {schemaError && <p className="text-xs text-destructive">{t('query.schemaLoadFailed')}</p>}

      <form onSubmit={onSubmit} className="flex flex-wrap items-center gap-3">
        <div className="flex flex-col gap-1">
          <span className="text-[10px] text-muted-foreground font-mono">{t('query.field')}</span>
          <select
            value={field}
            disabled={schemaLoading && !hasSchemaColumns}
            onChange={(e) => onFieldChange(e.target.value)}
            className={filterSelectClassName}
          >
            {fieldOptions.map((col) => (
              <option key={col} value={col}>
                {col}
              </option>
            ))}
          </select>
        </div>

        <div className="flex flex-col gap-1">
          <span className="text-[10px] text-muted-foreground font-mono">{t('query.operator')}</span>
          <select
            value={operator}
            onChange={(e) => onOperatorChange(e.target.value as FilterOperator)}
            className={`${filterSelectClassName} font-bold`}
          >
            {allowedOperators.map((op) => (
              <option key={op} value={op}>
                {op === '~=' ? '~= (LIKE)' : op}
              </option>
            ))}
          </select>
        </div>

        <div className="flex flex-col gap-1 flex-1 min-w-[150px]">
          <span className="text-[10px] text-muted-foreground font-mono">{t('query.value')}</span>
          <QueryFilterValueInput
            kind={kind}
            value={value}
            onChange={onValueChange}
            onTouched={onValueTouched}
          />
          {valueInvalid && (
            <span className="text-[10px] text-destructive">{t('query.invalidValue')}</span>
          )}
        </div>

        <div className="flex flex-col gap-1 self-end">
          <button
            type="submit"
            disabled={!canAdd}
            className="bg-primary hover:bg-primary/95 text-primary-foreground font-bold px-3 py-1.5 rounded text-xs flex items-center gap-1 transition-colors cursor-pointer disabled:opacity-40 disabled:cursor-not-allowed"
          >
            <Plus size={14} /> {t('query.addFilter')}
          </button>
        </div>
      </form>

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
                type="button"
                onClick={() => onRemoveFilter(idx)}
                className="text-muted-foreground hover:text-foreground p-0.5 rounded transition-colors"
              >
                <X size={12} />
              </button>
            </span>
          ))}
        </div>
      )}
    </div>
  );
}
