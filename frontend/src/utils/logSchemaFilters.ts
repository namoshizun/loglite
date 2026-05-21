import type { LogColumnKind } from '../api/client';
import type { QueryFilter } from '../api/client';

export type FilterOperator = QueryFilter['op'];

const TEXT_OPS: FilterOperator[] = ['=', '!=', '~='];
const ORDER_OPS: FilterOperator[] = ['=', '!=', '>', '>=', '<', '<='];
const BOOL_OPS: FilterOperator[] = ['=', '!='];

export function operatorsForKind(kind: LogColumnKind): FilterOperator[] {
  switch (kind) {
    case 'integer':
    case 'number':
    case 'datetime':
      return ORDER_OPS;
    case 'boolean':
      return BOOL_OPS;
    case 'text':
    case 'json':
    case 'blob':
    default:
      return TEXT_OPS;
  }
}

export function defaultOperator(kind: LogColumnKind): FilterOperator {
  return operatorsForKind(kind)[0];
}

function pad2(n: number): string {
  return String(n).padStart(2, '0');
}

/** Local wall-clock value for the datetime picker (YYYY-MM-DDTHH:mm:ss). */
export function formatLocalDatetimeInput(date: Date = new Date()): string {
  return (
    `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())}T` +
    `${pad2(date.getHours())}:${pad2(date.getMinutes())}:${pad2(date.getSeconds())}`
  );
}

export function parseLocalDatetimeInput(raw: string): Date | null {
  const coerced = datetimeLocalToFilterValue(raw);
  if (!coerced) return null;
  const ms = Date.parse(coerced);
  return Number.isNaN(ms) ? null : new Date(ms);
}

/** Convert datetime-local value (YYYY-MM-DDTHH:mm or with seconds) to API ISO fragment. */
export function datetimeLocalToFilterValue(raw: string): string | null {
  const trimmed = raw.trim();
  if (!trimmed) return null;
  const normalized = trimmed.length === 16 ? `${trimmed}:00` : trimmed;
  const ms = Date.parse(normalized);
  if (Number.isNaN(ms)) return null;
  const d = new Date(ms);
  return (
    `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}T` +
    `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`
  );
}

export function coerceFilterValue(kind: LogColumnKind, raw: string): string | null {
  const trimmed = raw.trim();
  if (!trimmed) return null;

  switch (kind) {
    case 'boolean': {
      const lower = trimmed.toLowerCase();
      if (lower === 'true' || lower === '1') return 'true';
      if (lower === 'false' || lower === '0') return 'false';
      return null;
    }
    case 'integer': {
      const n = Number(trimmed);
      if (!Number.isFinite(n) || !Number.isInteger(n)) return null;
      return String(n);
    }
    case 'number': {
      const n = Number(trimmed);
      if (!Number.isFinite(n)) return null;
      return String(n);
    }
    case 'datetime': {
      const fromLocal = datetimeLocalToFilterValue(trimmed);
      if (fromLocal) return fromLocal;
      return Number.isNaN(Date.parse(trimmed)) ? null : trimmed;
    }
    case 'text':
    case 'json':
    case 'blob':
      return trimmed;
    default:
      return trimmed;
  }
}

export function isValidFilterInput(kind: LogColumnKind, raw: string): boolean {
  return coerceFilterValue(kind, raw) !== null;
}
