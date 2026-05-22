import type { ChartOptions } from 'chart.js';
import { formatChartTime, formatChartTooltipLabel } from './constants';

export type ChartColors = {
  grid: string;
  axis: string;
  tooltipBg: string;
  tooltipBorder: string;
  tooltipFg: string;
};

export function readChartColors(_theme?: string): ChartColors {
  void _theme;
  const style = getComputedStyle(document.documentElement);
  const pick = (name: string, fallback: string) => style.getPropertyValue(name).trim() || fallback;

  return {
    grid: pick('--chart-grid', '#27272a'),
    axis: pick('--chart-axis', '#71717a'),
    tooltipBg: pick('--chart-tooltip-bg', '#18181b'),
    tooltipBorder: pick('--chart-tooltip-border', '#27272a'),
    tooltipFg: pick('--chart-tooltip-fg', '#fafafa'),
  };
}

type ScaleOptionsParams = {
  colors: ChartColors;
  labels: string[];
  needsRightAxis: boolean;
  showLeftAxis: boolean;
};

export function activityScaleOptions({
  colors,
  labels,
  needsRightAxis,
  showLeftAxis,
}: ScaleOptionsParams): ChartOptions<'bar'>['scales'] {
  const tickColor = colors.axis;
  return {
    x: {
      ticks: {
        color: tickColor,
        font: { size: 11 },
        maxRotation: 0,
        autoSkip: true,
        maxTicksLimit: 8,
        callback: (_value, index) => formatChartTime(labels[index] ?? ''),
      },
      grid: { color: colors.grid },
    },
    y: {
      type: 'linear',
      position: 'left',
      display: showLeftAxis,
      ticks: { color: tickColor, font: { size: 11 } },
      grid: { color: colors.grid },
    },
    y1: {
      type: 'linear',
      position: 'right',
      display: needsRightAxis,
      ticks: { color: tickColor, font: { size: 11 } },
      grid: { drawOnChartArea: false },
    },
  };
}

export function databaseScaleOptions(
  colors: ChartColors,
  labels: string[],
): ChartOptions<'line'>['scales'] {
  const tickColor = colors.axis;
  return {
    x: {
      ticks: {
        color: tickColor,
        font: { size: 10 },
        maxRotation: 0,
        autoSkip: true,
        maxTicksLimit: 6,
        callback: (_value, index) => formatChartTime(labels[index] ?? ''),
      },
      grid: { color: colors.grid },
    },
    y: {
      ticks: { color: tickColor, font: { size: 10 } },
      grid: { color: colors.grid },
    },
  };
}

export function baseChartPlugins(colors: ChartColors): ChartOptions['plugins'] {
  return {
    legend: {
      position: 'top',
      labels: { color: colors.axis, boxWidth: 12, font: { size: 11 } },
    },
    tooltip: {
      backgroundColor: colors.tooltipBg,
      borderColor: colors.tooltipBorder,
      borderWidth: 1,
      titleColor: colors.tooltipFg,
      bodyColor: colors.tooltipFg,
      callbacks: {
        title: (items) => {
          const label = items[0]?.label;
          return label ? formatChartTooltipLabel(label) : '';
        },
      },
    },
  };
}

export const chartLayout = {
  responsive: true,
  maintainAspectRatio: false,
} as const;
