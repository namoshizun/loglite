import { useCallback, useRef, useState } from 'react';
import { DayPicker } from 'react-day-picker';
import { Calendar, Clock } from 'lucide-react';
import { useI18n } from '../i18n/locale';
import { useClickOutside } from '../hooks/useClickOutside';
import { formatDateTimeMs } from '../utils/formatTimestamp';
import { formatLocalDatetimeInput, parseLocalDatetimeInput } from '../utils/logSchemaFilters';

type FilterDateTimeInputProps = {
  value: string;
  onChange: (value: string) => void;
  onInteraction?: () => void;
};

function TimeSelect({
  label,
  value,
  max,
  onChange,
}: {
  label: string;
  value: number;
  max: number;
  onChange: (n: number) => void;
}) {
  const options = Array.from({ length: max + 1 }, (_, i) => i);
  return (
    <label className="flex items-center gap-1 text-muted-foreground font-mono">
      <span className="text-[10px] w-3">{label}</span>
      <select
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        className="bg-background border border-border rounded px-1 py-0.5 text-foreground text-xs focus:outline-none focus:border-primary"
      >
        {options.map((n) => (
          <option key={n} value={n}>
            {String(n).padStart(2, '0')}
          </option>
        ))}
      </select>
    </label>
  );
}

export default function FilterDateTimeInput({
  value,
  onChange,
  onInteraction,
}: FilterDateTimeInputProps) {
  const { t } = useI18n();
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);

  const selected = parseLocalDatetimeInput(value) ?? new Date();
  const displayLabel = value.trim() ? formatDateTimeMs(selected) : t('query.datetimePick');

  const close = useCallback(() => setOpen(false), []);
  useClickOutside(rootRef, open, close);

  const commit = (d: Date) => {
    onChange(formatLocalDatetimeInput(d));
    onInteraction?.();
  };

  const setTimePart = (part: 'h' | 'm' | 's', n: number) => {
    const d = new Date(selected);
    if (part === 'h') d.setHours(n);
    if (part === 'm') d.setMinutes(n);
    if (part === 's') d.setSeconds(n);
    commit(d);
  };

  return (
    <div ref={rootRef} className="relative min-w-[220px]">
      <button
        type="button"
        onClick={() => setOpen((prev) => !prev)}
        className="w-full flex items-center gap-2 bg-background border border-border rounded px-2.5 py-1.5 text-xs text-foreground hover:border-primary font-mono text-left transition-colors"
      >
        <Calendar size={14} className="shrink-0 text-muted-foreground" />
        <span className="truncate flex-1">{displayLabel}</span>
      </button>

      {open && (
        <div className="absolute z-50 top-full left-0 mt-1 p-3 bg-popover text-popover-foreground border border-border rounded-lg shadow-xl filter-datetime-popover">
          <DayPicker
            mode="single"
            selected={selected}
            defaultMonth={selected}
            onSelect={(day) => {
              if (!day) return;
              const d = new Date(selected);
              d.setFullYear(day.getFullYear(), day.getMonth(), day.getDate());
              commit(d);
            }}
            className="filter-datetime-picker"
          />

          <div className="flex flex-wrap items-center gap-2 mt-2 pt-2 border-t border-border">
            <Clock size={13} className="text-muted-foreground shrink-0" />
            <TimeSelect
              label="H"
              max={23}
              value={selected.getHours()}
              onChange={(n) => setTimePart('h', n)}
            />
            <TimeSelect
              label="M"
              max={59}
              value={selected.getMinutes()}
              onChange={(n) => setTimePart('m', n)}
            />
            <TimeSelect
              label="S"
              max={59}
              value={selected.getSeconds()}
              onChange={(n) => setTimePart('s', n)}
            />
            <button
              type="button"
              onClick={() => commit(new Date())}
              className="ml-auto text-[10px] font-medium text-primary hover:text-primary/80 px-2 py-1 rounded border border-border hover:bg-muted transition-colors"
            >
              {t('query.datetimeNow')}
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
