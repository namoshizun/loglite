import { Chart } from 'react-chartjs-2';
import type { ChartData, ChartOptions } from 'chart.js';
import { useMemo } from 'react';
import { useI18n } from '../../i18n/locale';
import { useTheme } from '../../theme';
import {
  activityBucketCenterMs,
  type EnrichedActivityRow,
  type StatsChartWindow,
} from './constants';
import { activityScaleOptions, baseChartPlugins, chartLayout, readChartColors } from './chartTheme';
import type { ActivityCategory, SubMetricsState } from './types';

type ActivityCategoryChartProps = {
  category: ActivityCategory;
  subMetrics: SubMetricsState;
  data: EnrichedActivityRow[];
  chartWindow: StatsChartWindow;
};

export default function ActivityCategoryChart({
  category,
  subMetrics,
  data,
  chartWindow,
}: ActivityCategoryChartProps) {
  const { t } = useI18n();
  const { theme } = useTheme();

  const { chartData, options } = useMemo(() => {
    const colors = readChartColors(theme);
    const datasets: ChartData['datasets'] = [];
    const bucketX = (row: EnrichedActivityRow) => activityBucketCenterMs(row);

    let needsRightAxis = false;
    let showLeftAxis = false;

    const leftAxis = () => {
      showLeftAxis = true;
      return 'y' as const;
    };
    const rightAxis = () => {
      needsRightAxis = true;
      return 'y1' as const;
    };
    const pickAxis = (useRight: boolean) => (useRight ? rightAxis() : leftAxis());

    switch (category) {
      case 'query': {
        const q = subMetrics.query;
        if (q.count) {
          datasets.push({
            type: 'bar',
            label: t('stats.chart.queryCount'),
            data: data.map((row) => ({ x: bucketX(row), y: row.query_count })),
            backgroundColor: 'rgba(16, 185, 129, 0.85)',
            borderRadius: 2,
            yAxisID: leftAxis(),
            order: 2,
          });
        }
        if (q.latency) {
          const useRight = q.count;
          const axis = pickAxis(useRight);
          datasets.push({
            type: 'bar',
            label: t('stats.chart.latencyBand'),
            data: data.map((row) => ({
              x: bucketX(row),
              y: [row.query_min, row.query_max] as [number, number],
            })) as never,
            backgroundColor: 'rgba(245, 158, 11, 0.22)',
            borderColor: 'transparent',
            borderSkipped: false,
            yAxisID: axis,
            order: 3,
          });
          datasets.push({
            type: 'line',
            label: t('stats.chart.avgLatency'),
            data: data.map((row) => ({ x: bucketX(row), y: row.query_avg })),
            borderColor: '#f59e0b',
            backgroundColor: '#f59e0b',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            yAxisID: axis,
            order: 1,
          });
        }
        break;
      }
      case 'ingestion': {
        const ing = subMetrics.ingestion;
        if (ing.count) {
          datasets.push({
            type: 'bar',
            label: t('stats.chart.ingestCount'),
            data: data.map((row) => ({ x: bucketX(row), y: row.ingest_count })),
            backgroundColor: 'rgba(59, 130, 246, 0.85)',
            borderRadius: 2,
            yAxisID: leftAxis(),
            order: 2,
          });
        }
        if (ing.size) {
          datasets.push({
            type: 'line',
            label: t('stats.chart.avgBodySize'),
            data: data.map((row) => ({ x: bucketX(row), y: row.ingest_size_avg })),
            borderColor: '#60a5fa',
            backgroundColor: '#60a5fa',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            yAxisID: pickAxis(ing.count),
            order: 1,
          });
        }
        break;
      }
      case 'insertion': {
        const ins = subMetrics.insertion;
        if (ins.count) {
          datasets.push({
            type: 'bar',
            label: t('stats.chart.rowsInserted'),
            data: data.map((row) => ({ x: bucketX(row), y: row.insert_total_count })),
            backgroundColor: 'rgba(20, 184, 166, 0.85)',
            borderRadius: 2,
            yAxisID: leftAxis(),
            order: 2,
          });
        }
        if (ins.cost) {
          datasets.push({
            type: 'line',
            label: t('stats.chart.insertCost'),
            data: data.map((row) => ({ x: bucketX(row), y: row.insert_total_cost })),
            borderColor: '#2dd4bf',
            backgroundColor: '#2dd4bf',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            yAxisID: pickAxis(ins.count),
            order: 1,
          });
        }
        break;
      }
      case 'connections': {
        const conn = subMetrics.connections;
        if (conn.http) {
          datasets.push({
            type: 'line',
            label: t('stats.chart.httpConn'),
            data: data.map((row) => ({ x: bucketX(row), y: row.http_conn_count })),
            borderColor: '#ec4899',
            backgroundColor: '#ec4899',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            yAxisID: leftAxis(),
            order: 1,
          });
        }
        if (conn.sse) {
          datasets.push({
            type: 'line',
            label: t('stats.chart.sseSessions'),
            data: data.map((row) => ({ x: bucketX(row), y: row.sse_session_count })),
            borderColor: '#8b5cf6',
            backgroundColor: '#8b5cf6',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            yAxisID: leftAxis(),
            order: 2,
          });
        }
        break;
      }
    }

    const chartData: ChartData = { datasets };

    const options: ChartOptions<'bar'> = {
      ...chartLayout,
      interaction: { mode: 'index', intersect: false },
      plugins: baseChartPlugins(colors),
      scales: activityScaleOptions({
        colors,
        window: chartWindow,
        needsRightAxis,
        showLeftAxis,
      }),
    };

    return { chartData, options };
  }, [category, subMetrics, data, chartWindow, t, theme]);

  return <Chart type="bar" data={chartData as ChartData<'bar'>} options={options} />;
}
