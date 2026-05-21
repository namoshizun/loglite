import { BarChart3, LineChart as ChartIcon } from 'lucide-react';
import {
  Area,
  AreaChart,
  CartesianGrid,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import type { DatabaseStatRecord } from '../../api/client';
import { useI18n } from '../../i18n/locale';
import {
  CHART_AXIS,
  CHART_GRID,
  chartTooltipStyle,
  formatChartTime,
  formatChartTooltipLabel,
} from './constants';

type DatabaseGrowthChartsProps = {
  rows: DatabaseStatRecord[];
};

function formatDbRows(rows: DatabaseStatRecord[]) {
  return rows.map((d) => ({
    ...d,
    db_size_mb: Number((d.db_size / (1024 * 1024)).toFixed(2)),
  }));
}

export default function DatabaseGrowthCharts({ rows }: DatabaseGrowthChartsProps) {
  const { t } = useI18n();
  const chartData = formatDbRows(rows);

  return (
    <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
      <div className="h-[280px] bg-muted/50 p-3 rounded-lg border border-border">
        <h4 className="text-xs text-muted-foreground font-semibold mb-2 flex items-center gap-1.5">
          <ChartIcon size={12} className="text-blue-400" /> {t('stats.dbSizeChart')}
        </h4>
        <ResponsiveContainer width="100%" height="90%">
          <AreaChart data={chartData} margin={{ top: 10, right: 5, left: -20, bottom: 0 }}>
            <CartesianGrid strokeDasharray="3 3" stroke={CHART_GRID} />
            <XAxis
              dataKey="timestamp"
              tickFormatter={formatChartTime}
              stroke={CHART_AXIS}
              fontSize={10}
            />
            <YAxis stroke={CHART_AXIS} fontSize={10} />
            <Tooltip
              contentStyle={chartTooltipStyle}
              labelFormatter={formatChartTooltipLabel}
              formatter={(value) => [`${value} MB`, 'Database Size']}
            />
            <Area
              type="monotone"
              dataKey="db_size_mb"
              stroke="#3b82f6"
              fill="#3b82f6"
              fillOpacity={0.1}
              strokeWidth={2}
            />
          </AreaChart>
        </ResponsiveContainer>
      </div>

      <div className="h-[280px] bg-muted/50 p-3 rounded-lg border border-border">
        <h4 className="text-xs text-muted-foreground font-semibold mb-2 flex items-center gap-1.5">
          <BarChart3 size={12} className="text-purple-400" /> {t('stats.rowsChart')}
        </h4>
        <ResponsiveContainer width="100%" height="90%">
          <AreaChart data={chartData} margin={{ top: 10, right: 5, left: -20, bottom: 0 }}>
            <CartesianGrid strokeDasharray="3 3" stroke={CHART_GRID} />
            <XAxis
              dataKey="timestamp"
              tickFormatter={formatChartTime}
              stroke={CHART_AXIS}
              fontSize={10}
            />
            <YAxis stroke={CHART_AXIS} fontSize={10} />
            <Tooltip
              contentStyle={chartTooltipStyle}
              labelFormatter={formatChartTooltipLabel}
              formatter={(value) => [Number(value).toLocaleString(), 'Row Count']}
            />
            <Area
              type="monotone"
              dataKey="rows_count"
              stroke="#8b5cf6"
              fill="#8b5cf6"
              fillOpacity={0.1}
              strokeWidth={2}
            />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}
