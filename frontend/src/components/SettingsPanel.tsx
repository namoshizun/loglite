import { useQuery } from '@tanstack/react-query';
import { fetchSettings, type SettingEntry } from '../api/client';

function isSqliteParamsRecord(value: SettingEntry['value']): value is Record<string, string> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function SqliteParamsValue({ params }: { params: Record<string, string> }) {
  const entries = Object.entries(params);
  if (entries.length === 0) {
    return <span className="text-muted-foreground">(none)</span>;
  }
  return (
    <div className="flex flex-col gap-1">
      {entries.map(([key, val]) => (
        <div key={key} className="leading-snug">
          <span className="text-muted-foreground">{key}</span>
          <span className="text-muted-foreground mx-1">=</span>
          <span className="text-foreground">{val}</span>
        </div>
      ))}
    </div>
  );
}

function formatSettingValue(value: SettingEntry['value']): string {
  if (value === null || value === undefined) {
    return '—';
  }
  if (typeof value === 'boolean') {
    return value ? 'true' : 'false';
  }
  if (Array.isArray(value)) {
    return value.length === 0 ? '(none)' : value.join(', ');
  }
  return String(value);
}

function renderSettingValue(row: SettingEntry) {
  if (row.key === 'sqlite_params' && isSqliteParamsRecord(row.value)) {
    return <SqliteParamsValue params={row.value} />;
  }
  return formatSettingValue(row.value);
}

export default function SettingsPanel() {
  const { data, isLoading, isError, error } = useQuery({
    queryKey: ['settings'],
    queryFn: fetchSettings,
    staleTime: 60_000,
  });

  if (isLoading) {
    return (
      <div className="bg-card border border-border rounded-xl p-8 flex items-center justify-center min-h-[320px]">
        <div className="flex flex-col items-center gap-2 text-muted-foreground">
          <div className="w-8 h-8 border-4 border-primary border-t-transparent rounded-full animate-spin" />
          <span className="text-sm">Loading instance settings...</span>
        </div>
      </div>
    );
  }

  if (isError) {
    return (
      <div className="bg-card border border-destructive/30 rounded-xl p-8 text-center text-destructive min-h-[200px] flex items-center justify-center">
        <div>
          <p className="font-semibold">Failed to load settings</p>
          <p className="text-xs text-muted-foreground mt-1">{(error as Error).message}</p>
        </div>
      </div>
    );
  }

  const rows = data?.settings ?? [];

  return (
    <div className="bg-card border border-border rounded-xl shadow-sm overflow-hidden">
      <div className="overflow-x-auto">
        <table className="w-full text-left text-sm border-collapse">
          <thead>
            <tr className="bg-muted border-b border-border">
              <th className="py-3 px-5 font-mono font-semibold text-muted-foreground w-[220px]">
                Setting
              </th>
              <th className="py-3 px-5 font-semibold text-muted-foreground w-[280px]">Value</th>
              <th className="py-3 px-5 font-semibold text-muted-foreground">Description</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-border/60">
            {rows.map((row) => (
              <tr key={row.key} className="hover:bg-muted/40 align-top">
                <td className="py-3 px-5 font-mono text-xs text-foreground font-semibold whitespace-nowrap">
                  {row.key}
                </td>
                <td className="py-3 px-5 font-mono text-xs text-foreground break-all">
                  {renderSettingValue(row)}
                </td>
                <td className="py-3 px-5 text-xs text-muted-foreground leading-relaxed">
                  {row.description}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
