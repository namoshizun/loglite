import { BarChart3, LineChart as ChartIcon } from 'lucide-react';
import { Chart } from 'react-chartjs-2';
import type { ChartData, ChartOptions } from 'chart.js';
import { useMemo, type ReactNode } from 'react';
import type { DatabaseStatRecord } from '../../api/client';
import { useI18n } from '../../i18n/locale';
import { useTheme } from '../../theme';
import type { StatsChartWindow } from './constants';
import { baseChartPlugins, chartLayout, databaseScaleOptions, readChartColors } from './chartTheme';

type DatabaseGrowthChartsProps = {
  rows: DatabaseStatRecord[];
  chartWindow: StatsChartWindow;
};

function formatDbRows(rows: DatabaseStatRecord[]) {
  return rows.map((d) => ({
    ...d,
    db_size_mb: Number((d.db_size / (1024 * 1024)).toFixed(2)),
  }));
}

function DbAreaChart({
  title,
  titleIcon,
  points,
  datasetLabel,
  strokeColor,
  chartWindow,
  formatTooltipValue,
}: {
  title: string;
  titleIcon: ReactNode;
  points: { x: number; y: number }[];
  datasetLabel: string;
  strokeColor: string;
  chartWindow: StatsChartWindow;
  formatTooltipValue: (value: number) => [string, string];
}) {
  const { theme } = useTheme();

  const { data, options } = useMemo(() => {
    const colors = readChartColors(theme);
    const fill = strokeColor.startsWith('#') ? `${strokeColor}1a` : 'rgba(59, 130, 246, 0.1)';

    const data: ChartData<'line'> = {
      datasets: [
        {
          label: datasetLabel,
          data: points,
          borderColor: strokeColor,
          backgroundColor: fill,
          fill: true,
          tension: 0.35,
          pointRadius: 0,
          pointHoverRadius: 3,
          borderWidth: 2,
        },
      ],
    };

    const options: ChartOptions<'line'> = {
      ...chartLayout,
      plugins: {
        ...baseChartPlugins(colors),
        legend: { display: false },
        tooltip: {
          ...baseChartPlugins(colors)?.tooltip,
          callbacks: {
            ...baseChartPlugins(colors)?.tooltip?.callbacks,
            label: (ctx) => {
              const v = Number(ctx.parsed.y);
              const [formatted, name] = formatTooltipValue(v);
              return `${name}: ${formatted}`;
            },
          },
        },
      },
      scales: databaseScaleOptions(colors, chartWindow),
    };

    return { data, options };
  }, [points, datasetLabel, strokeColor, chartWindow, theme, formatTooltipValue]);

  return (
    <div className="h-[280px] bg-muted/50 p-3 rounded-lg border border-border flex flex-col">
      <h4 className="text-xs text-muted-foreground font-semibold mb-2 flex items-center gap-1.5 shrink-0">
        {titleIcon} {title}
      </h4>
      <div className="flex-1 min-h-0">
        <Chart type="line" data={data} options={options} />
      </div>
    </div>
  );
}

export default function DatabaseGrowthCharts({ rows, chartWindow }: DatabaseGrowthChartsProps) {
  const { t } = useI18n();
  const chartData = formatDbRows(rows);
  const points = (field: 'db_size_mb' | 'rows_count') =>
    chartData.map((d) => ({
      x: new Date(d.timestamp).getTime(),
      y: d[field],
    }));

  return (
    <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
      <DbAreaChart
        title={t('stats.dbSizeChart')}
        titleIcon={<ChartIcon size={12} className="text-blue-400" />}
        points={points('db_size_mb')}
        datasetLabel="Database Size"
        strokeColor="#3b82f6"
        chartWindow={chartWindow}
        formatTooltipValue={(value) => [`${value} MB`, 'Database Size']}
      />
      <DbAreaChart
        title={t('stats.rowsChart')}
        titleIcon={<BarChart3 size={12} className="text-purple-400" />}
        points={points('rows_count')}
        datasetLabel="Row Count"
        strokeColor="#8b5cf6"
        chartWindow={chartWindow}
        formatTooltipValue={(value) => [value.toLocaleString(), 'Row Count']}
      />
    </div>
  );
}
