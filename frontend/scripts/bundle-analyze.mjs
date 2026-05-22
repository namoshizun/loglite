/**
 * Production build + rollup-plugin-visualizer report + package breakdown.
 * Invoked by ../analyze-bundle.sh
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { build } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import { visualizer } from 'rollup-plugin-visualizer';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(__dirname, '..');
const outDir = 'dist-analyze';
const statsFile = 'bundle-stats.html';

function pkgFromModuleId(id) {
  const s = String(id).replace(/^\0/, '');
  const idx = s.indexOf('node_modules/');
  if (idx === -1) {
    if (s.includes('/src/') || s.includes('\\src\\')) return 'app';
    if (s.includes('rolldown') || s.includes('vite/')) return 'runtime';
    return 'other';
  }
  const rest = s.slice(idx + 'node_modules/'.length);
  if (rest.startsWith('@')) {
    const [scope, name] = rest.split('/');
    return `${scope}/${name}`;
  }
  return rest.split(/[/\\]/)[0];
}

function aggregateByPackage(data) {
  const byPkg = new Map();
  let totalRendered = 0;
  let totalGzip = 0;

  for (const meta of Object.values(data.nodeMetas ?? {})) {
    const parts = meta.moduleParts ?? {};
    for (const partId of Object.values(parts)) {
      const part = data.nodeParts?.[partId];
      if (!part?.renderedLength) continue;
      const pkg = pkgFromModuleId(meta.id);
      const cur = byPkg.get(pkg) ?? { rendered: 0, gzip: 0 };
      cur.rendered += part.renderedLength;
      cur.gzip += part.gzipLength ?? 0;
      byPkg.set(pkg, cur);
      totalRendered += part.renderedLength;
      totalGzip += part.gzipLength ?? 0;
    }
  }

  return { byPkg, totalRendered, totalGzip };
}

function formatKb(bytes) {
  return `${(bytes / 1024).toFixed(1)}`;
}

function printChunkSizes(distPath) {
  const assetsDir = path.join(distPath, 'assets');
  if (!fs.existsSync(assetsDir)) return;

  console.log('\nOutput chunks (minified):\n');
  const files = fs
    .readdirSync(assetsDir)
    .filter((f) => f.endsWith('.js') || f.endsWith('.css'))
    .map((f) => {
      const stat = fs.statSync(path.join(assetsDir, f));
      return { name: f, bytes: stat.size };
    })
    .sort((a, b) => b.bytes - a.bytes);

  for (const { name, bytes } of files) {
    console.log(`  ${formatKb(bytes).padStart(8)} KB  ${name}`);
  }
}

function printPackageTable(byPkg, totalRendered, totalGzip) {
  const sorted = [...byPkg.entries()].sort((a, b) => b[1].rendered - a[1].rendered);

  console.log('\nEstimated share by package (module graph, may exceed single-chunk size):\n');
  console.log('   %      raw KB   gzip KB   package');
  console.log('  ' + '-'.repeat(52));

  const topN = 20;
  for (const [pkg, s] of sorted.slice(0, topN)) {
    const pct = totalRendered ? (100 * s.rendered) / totalRendered : 0;
    console.log(
      `  ${pct.toFixed(1).padStart(5)}%  ${formatKb(s.rendered).padStart(8)}  ${formatKb(s.gzip).padStart(8)}  ${pkg}`,
    );
  }

  if (sorted.length > topN) {
    const rest = sorted.slice(topN);
    const restRendered = rest.reduce((n, [, s]) => n + s.rendered, 0);
    const restGzip = rest.reduce((n, [, s]) => n + s.gzip, 0);
    const pct = totalRendered ? (100 * restRendered) / totalRendered : 0;
    console.log(
      `  ${pct.toFixed(1).padStart(5)}%  ${formatKb(restRendered).padStart(8)}  ${formatKb(restGzip).padStart(8)}  (${rest.length} other packages)`,
    );
  }

  console.log('  ' + '-'.repeat(52));
  console.log(
    `          ${formatKb(totalRendered).padStart(8)}  ${formatKb(totalGzip).padStart(8)}  (totals in graph)`,
  );
}

async function main() {
  process.chdir(root);

  console.log('Building with rollup-plugin-visualizer...\n');

  await build({
    plugins: [
      react(),
      tailwindcss(),
      visualizer({
        filename: statsFile,
        gzipSize: true,
        brotliSize: false,
        open: false,
      }),
    ],
    build: {
      outDir,
      emptyOutDir: true,
      sourcemap: false,
    },
  });

  const statsPath = path.join(root, statsFile);
  const html = fs.readFileSync(statsPath, 'utf8');
  const match = html.match(/const data = (\{[\s\S]*?\});\s*\n/);
  if (!match) {
    console.error('Could not parse visualizer data from', statsPath);
    process.exit(1);
  }

  const data = JSON.parse(match[1]);
  const { byPkg, totalRendered, totalGzip } = aggregateByPackage(data);
  printChunkSizes(path.join(root, outDir));
  printPackageTable(byPkg, totalRendered, totalGzip);

  console.log(`\nTreemap report: ${statsPath}`);
  console.log(`Build output:   ${path.join(root, outDir)}/`);
  console.log(
    '\nTip: lazy-load heavy tabs (StatsDashboard / HistoricalQuery) to shrink the main chunk.\n',
  );
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
