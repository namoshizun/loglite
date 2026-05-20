import { useState } from 'react';
import { Copy, Check } from 'lucide-react';

interface JsonViewerProps {
  data: any;
  title?: string;
}

export default function JsonViewer({ data, title }: JsonViewerProps) {
  const [copied, setCopied] = useState(false);

  const formattedJson = typeof data === 'string'
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

  // Basic syntax highlighter for JSON
  const highlightJson = (json: string) => {
    if (!json) return '';
    
    // Escaping HTML characters
    const escaped = json
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');

    // Regex to match JSON tokens
    const tokenRegex = /("(\\u[a-zA-Z0-9]{4}|\\[^u]|[^\\"])*"(\s*:)?|\b(true|false|null)\b|-?\d+(?:\.\d*)?(?:[eE][+-]?\d+)?)/g;

    return escaped.replace(tokenRegex, (match) => {
      let cls = 'text-amber-400'; // default string
      if (/^"/.test(match)) {
        if (/:$/.test(match)) {
          cls = 'text-blue-400 font-semibold'; // key
        } else {
          cls = 'text-green-400'; // string value
        }
      } else if (/true|false/.test(match)) {
        cls = 'text-purple-400'; // boolean
      } else if (/null/.test(match)) {
        cls = 'text-zinc-500 italic'; // null
      } else {
        cls = 'text-orange-400'; // number
      }
      return `<span class="${cls}">${match}</span>`;
    });
  };

  return (
    <div className="bg-zinc-950 border border-zinc-800 rounded-lg overflow-hidden flex flex-col font-mono text-xs w-full">
      {/* JSON Viewer Header */}
      <div className="bg-zinc-900 px-4 py-2 border-b border-zinc-800 flex items-center justify-between">
        <span className="text-zinc-400 font-semibold">{title || 'Data Payload'}</span>
        <button
          onClick={handleCopy}
          className="text-zinc-400 hover:text-zinc-200 p-1.5 rounded bg-zinc-950 hover:bg-zinc-850 border border-zinc-800 transition-colors flex items-center gap-1.5"
          title="Copy payload"
        >
          {copied ? (
            <>
              <Check size={12} className="text-green-500" />
              <span className="text-[10px] text-green-500 font-bold">Copied!</span>
            </>
          ) : (
            <>
              <Copy size={12} />
              <span className="text-[10px]">Copy</span>
            </>
          )}
        </button>
      </div>

      {/* JSON Code Area */}
      <div className="p-4 overflow-x-auto max-h-[350px] text-left leading-relaxed">
        <pre 
          className="whitespace-pre-wrap break-all"
          dangerouslySetInnerHTML={{ __html: highlightJson(formattedJson) }}
        />
      </div>
    </div>
  );
}
