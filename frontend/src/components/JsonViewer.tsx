import { useState } from 'react';
import { Copy, Check } from 'lucide-react';
import { useTheme } from '../theme';

interface JsonViewerProps {
  data: any;
  title?: string;
}

function highlightJson(json: string, theme: 'dark' | 'light') {
  if (!json) return '';

  const escaped = json.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  const tokenRegex =
    /("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+-]?\d+)?)/g;

  const light = theme === 'light';
  const stringCls = light ? 'text-green-700' : 'text-green-400';
  const keyCls = light ? 'text-blue-700 font-semibold' : 'text-blue-400 font-semibold';
  const boolCls = light ? 'text-purple-700' : 'text-purple-400';
  const nullCls = light ? 'text-zinc-500 italic' : 'text-zinc-500 italic';
  const numCls = light ? 'text-orange-700' : 'text-orange-400';
  const defaultCls = light ? 'text-amber-800' : 'text-amber-400';

  return escaped.replace(tokenRegex, (match) => {
    let cls = defaultCls;
    if (/^"/.test(match)) {
      cls = /:$/.test(match) ? keyCls : stringCls;
    } else if (/true|false/.test(match)) {
      cls = boolCls;
    } else if (/null/.test(match)) {
      cls = nullCls;
    } else {
      cls = numCls;
    }
    return `<span class="${cls}">${match}</span>`;
  });
}

export default function JsonViewer({ data, title }: JsonViewerProps) {
  const { theme } = useTheme();
  const [copied, setCopied] = useState(false);

  const formattedJson =
    typeof data === 'string'
      ? (() => {
          try {
            return JSON.stringify(JSON.parse(data), null, 2);
          } catch {
            return data;
          }
        })()
      : JSON.stringify(data, null, 2);

  const handleCopy = () => {
    navigator.clipboard.writeText(formattedJson);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="bg-background border border-border rounded-lg overflow-hidden flex flex-col font-mono text-xs w-full">
      <div className="bg-muted px-4 py-2 border-b border-border flex items-center justify-between">
        <span className="text-muted-foreground font-semibold">{title || 'Data Payload'}</span>
        <button
          type="button"
          onClick={handleCopy}
          className="text-muted-foreground hover:text-foreground p-1.5 rounded bg-background hover:bg-muted border border-border transition-colors flex items-center gap-1.5"
          title="Copy payload"
        >
          {copied ? (
            <>
              <Check size={12} className="text-green-500" />
              <span className="text-[10px] text-green-600 font-bold">Copied!</span>
            </>
          ) : (
            <>
              <Copy size={12} />
              <span className="text-[10px]">Copy</span>
            </>
          )}
        </button>
      </div>

      <div className="p-4 overflow-x-auto max-h-[350px] text-left leading-relaxed text-foreground">
        <pre
          className="whitespace-pre-wrap break-all"
          dangerouslySetInnerHTML={{ __html: highlightJson(formattedJson, theme) }}
        />
      </div>
    </div>
  );
}
