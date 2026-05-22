import type { LogColumnKind, LogSchemaColumn } from '../api/client';
import { datetimeLocalToFilterValue, formatLocalDatetimeInput } from './logSchemaFilters';

export type FieldValues = Record<string, string>;

export function editableSchemaColumns(columns: LogSchemaColumn[]): LogSchemaColumn[] {
  return columns.filter((c) => !c.primary_key);
}

export function buildDefaultFieldValues(columns: LogSchemaColumn[]): FieldValues {
  const values: FieldValues = {};
  const nowIso = formatLocalDatetimeInput();
  for (const col of editableSchemaColumns(columns)) {
    if (col.kind === 'datetime') {
      values[col.name] = nowIso;
    } else if (col.name === 'level') {
      values[col.name] = 'INFO';
    } else if (col.name === 'service') {
      values[col.name] = 'test';
    } else if (col.name === 'message') {
      values[col.name] = 'Debug log entry';
    } else {
      values[col.name] = '';
    }
  }
  return values;
}

function parseJsonField(raw: string): unknown | null {
  const trimmed = raw.trim();
  if (!trimmed) return null;
  try {
    return JSON.parse(trimmed);
  } catch {
    return undefined;
  }
}

/** Coerce form string to a JSON-serializable value; `undefined` means invalid. */
export function coerceFieldToJsonValue(
  kind: LogColumnKind,
  raw: string,
  optional: boolean,
): unknown | null | undefined {
  const trimmed = raw.trim();
  if (!trimmed) {
    return optional ? null : undefined;
  }

  switch (kind) {
    case 'boolean': {
      const lower = trimmed.toLowerCase();
      if (lower === 'true' || lower === '1') return true;
      if (lower === 'false' || lower === '0') return false;
      return undefined;
    }
    case 'integer': {
      const n = Number(trimmed);
      if (!Number.isFinite(n) || !Number.isInteger(n)) return undefined;
      return n;
    }
    case 'number': {
      const n = Number(trimmed);
      if (!Number.isFinite(n)) return undefined;
      return n;
    }
    case 'datetime': {
      const iso = datetimeLocalToFilterValue(trimmed);
      if (iso) return iso;
      return Number.isNaN(Date.parse(trimmed)) ? undefined : trimmed;
    }
    case 'json': {
      const parsed = parseJsonField(trimmed);
      return parsed === undefined ? undefined : parsed;
    }
    case 'text':
    case 'blob':
    default:
      return trimmed;
  }
}

export function isRequiredFieldValid(kind: LogColumnKind, raw: string): boolean {
  return coerceFieldToJsonValue(kind, raw, false) !== undefined;
}

export function buildLogPayload(
  columns: LogSchemaColumn[],
  values: FieldValues,
): { payload: Record<string, unknown> | null; errorField?: string } {
  const payload: Record<string, unknown> = {};

  for (const col of editableSchemaColumns(columns)) {
    const raw = values[col.name] ?? '';
    const optional = !col.not_null;
    const coerced = coerceFieldToJsonValue(col.kind, raw, optional);

    if (coerced === undefined) {
      return { payload: null, errorField: col.name };
    }
    if (coerced !== null) {
      payload[col.name] = coerced;
    }
  }

  return { payload };
}

export function canSubmitLog(columns: LogSchemaColumn[], values: FieldValues): boolean {
  for (const col of editableSchemaColumns(columns)) {
    if (!col.not_null) continue;
    if (!isRequiredFieldValid(col.kind, values[col.name] ?? '')) return false;
  }
  return editableSchemaColumns(columns).length > 0;
}
