import { formatDateTimeMs } from '../../utils/formatTimestamp';

type LogTableCellProps = {
  column: string;
  value: unknown;
  levelColors: Record<string, string>;
  serviceTagClass: string;
};

export default function LogTableCell({
  column,
  value,
  levelColors,
  serviceTagClass,
}: LogTableCellProps) {
  if (column === 'level') {
    const levelStr = String(value).toUpperCase();
    const levelColor = levelColors[levelStr] || levelColors.DEBUG;
    return (
      <td className="py-2 px-4 whitespace-nowrap">
        <span
          className={`px-1.5 py-0.5 rounded text-[10px] font-mono font-bold border ${levelColor}`}
        >
          {levelStr}
        </span>
      </td>
    );
  }

  if (column === 'timestamp') {
    return (
      <td className="py-2 px-4 whitespace-nowrap text-muted-foreground font-mono">
        {formatDateTimeMs(value as string | number | Date)}
      </td>
    );
  }

  if (column === 'service') {
    return (
      <td className="py-2 px-4 whitespace-nowrap">
        {value ? <span className={serviceTagClass}>{String(value)}</span> : '-'}
      </td>
    );
  }

  const isObject = typeof value === 'object' && value !== null;
  const displayVal = isObject ? JSON.stringify(value) : String(value ?? '');

  return (
    <td
      className={`py-2 px-4 font-mono ${
        column === 'message' ? 'text-foreground' : 'text-muted-foreground'
      } max-w-xs md:max-w-md lg:max-w-lg truncate`}
      title={displayVal}
    >
      {displayVal}
    </td>
  );
}
